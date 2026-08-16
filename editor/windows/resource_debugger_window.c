/**
 * @file resource_debugger_window.c
 * @brief Resource Debugger window (APX-374): repository introspection on the
 * v2 shell.
 *
 * Port of main-branch Skore::ResourceDebuggerWindow (migration manifest
 * §3.8): an on-demand Center window (workspace mask 0 — never auto-opened)
 * with Types / Instance tabs and back-history navigation. The v2 repository
 * API has no public type enumeration, so the Types tab lists the builtin
 * payload type ids (resource_asset_builtins.h + component types) that
 * `find_type` resolves plus the distinct types of live resources found by
 * walking the dense RID range 1..resource_count; instance lists per type
 * come from the same walk. Generic field values are read through the
 * repository field descriptors + getters, so the Instance tab shows real
 * data for any attached repository (scene / project / test).
 *
 * Public entry points go through the `sk_editor_resource_debugger_ops_t`
 * table registered with add_impl (window-table-pattern.md §7); the shell's
 * Window/Resource Debugger menu routes through it.
 */

#include "resource_debugger_window.h"

#include "allocator.h"
#include "resource_asset_builtins.h"
#include "resource_component_types.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_RD_STATE_TYPE_ID SK_TYPE_ID("sk.editor.resource_debugger.state", 0x2b4d6f8a0c2e4f6bULL, 0x7a9c0e2f4b6d8f0aULL)

/* Sentinel for "no type selected" (no RD_NO_SELECTION in the shared headers). */
#define RD_NO_SELECTION ((u32) - 1)

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct rd_type_t {
	const sk_resource_type_t* type;
	char name[SK_EDITOR_RD_LABEL_CAP];
	u32 instance_count;
	sk_rid_t instances[SK_EDITOR_RD_MAX_INSTANCES];
} rd_type_t;

typedef struct rd_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	sk_repository_t* repository;	 /* borrowed; attached via set_repository */
	const sk_repository_api_t* repo; /* cached table */

	rd_type_t types[SK_EDITOR_RD_MAX_TYPES];
	u32 type_count;
	u32 selected_type; /* RD_NO_SELECTION = none */
	sk_rid_t selected_instance;
	sk_rid_t history[SK_EDITOR_RD_MAX_HISTORY];
	u32 history_count;
	char search[SK_EDITOR_RD_LABEL_CAP];
	i32 tab; /* SK_EDITOR_RD_TAB_TYPES / _INSTANCE */
	u64 refresh_version;

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t root;
	sk_ui_node_t type_list;
	sk_ui_node_t instance_list;
	sk_ui_node_t fields_table;
	sk_ui_node_t meta_table;
	sk_ui_node_t search_input;
	sk_ui_item_t list_items[SK_EDITOR_RD_MAX_TYPES + SK_EDITOR_RD_MAX_INSTANCES];
	sk_ui_item_array_t list_arr;
	i32 last_built_tab;
	u64 last_built_version;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
} rd_state_t;

typedef struct rd_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} rd_class_state_t;

static void rd_init(sk_editor_window_t* window);
static void rd_draw(sk_editor_window_t* window, i32* open);
static void rd_destroy(sk_editor_window_t* window);

static sk_editor_window_t rd_window = {
	.title = "Resource Debugger",
	.dock_id = "sk.editor_window.resource_debugger",
	.dock_position = SK_EDITOR_DOCK_FILL,
	.order = 50,
	.workspace_mask = 0u, /* on-demand: opened explicitly, never by dockspace init */
	.init = rd_init,
	.draw = rd_draw,
	.render = NULL,
	.destroy = rd_destroy,
};

static rd_class_state_t* rd_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (rd_class_state_t*)app_api->get_api(app_context, SK_EDITOR_RD_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Repository introspection (ui-independent)                         */
/* ------------------------------------------------------------------ */

/* Fill @p out with the builtin payload type ids the repository may have
 * registered (asset payloads + component payloads). Runtime assignment:
 * SK_TYPE_ID expands to a compound literal, which is not a constant
 * expression in file-scope/static initializers (same constraint as the
 * APX-330 scaffolding). */
static void rd_known_type_ids(sk_type_id_t* out, u32 cap, u32* out_count) {
	(void)cap;
	out[0] = (sk_type_id_t){SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_LO, SK_ANIMATION_CLIP_RESOURCE_TYPE_ID_HI};
	out[1] = (sk_type_id_t){SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_LO, SK_ANIMATION_CONTROLLER_RESOURCE_TYPE_ID_HI};
	out[2] = (sk_type_id_t){SK_AUDIO_RESOURCE_TYPE_ID_LO, SK_AUDIO_RESOURCE_TYPE_ID_HI};
	out[3] = (sk_type_id_t){SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_LO, SK_CSHARP_SCRIPT_RESOURCE_TYPE_ID_HI};
	out[4] = (sk_type_id_t){SK_DCC_ASSET_TYPE_ID_LO, SK_DCC_ASSET_TYPE_ID_HI};
	out[5] = (sk_type_id_t){SK_ENTITY_RESOURCE_TYPE_ID_LO, SK_ENTITY_RESOURCE_TYPE_ID_HI};
	out[6] = (sk_type_id_t){SK_FONT_RESOURCE_TYPE_ID_LO, SK_FONT_RESOURCE_TYPE_ID_HI};
	out[7] = (sk_type_id_t){SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_LO, SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_HI};
	out[8] = (sk_type_id_t){SK_MESH_RESOURCE_TYPE_ID_LO, SK_MESH_RESOURCE_TYPE_ID_HI};
	out[9] = (sk_type_id_t){SK_UI_DOCUMENT_RESOURCE_TYPE_ID_LO, SK_UI_DOCUMENT_RESOURCE_TYPE_ID_HI};
	out[10] = (sk_type_id_t){SK_UI_STYLE_RESOURCE_TYPE_ID_LO, SK_UI_STYLE_RESOURCE_TYPE_ID_HI};
	out[11] = (sk_type_id_t){SK_SCENE_RESOURCE_TYPE_ID_LO, SK_SCENE_RESOURCE_TYPE_ID_HI};
	out[12] = (sk_type_id_t){SK_SHADER_RESOURCE_TYPE_ID_LO, SK_SHADER_RESOURCE_TYPE_ID_HI};
	out[13] = (sk_type_id_t){SK_TEXTURE_RESOURCE_TYPE_ID_LO, SK_TEXTURE_RESOURCE_TYPE_ID_HI};
	out[14] = (sk_type_id_t){SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_LO, SK_TEXTURE_IMPORT_SETTINGS_TYPE_ID_HI};
	out[15] = (sk_type_id_t){SK_FBX_IMPORT_SETTINGS_TYPE_ID_LO, SK_FBX_IMPORT_SETTINGS_TYPE_ID_HI};
	out[16] = (sk_type_id_t){SK_GLTF_IMPORT_SETTINGS_TYPE_ID_LO, SK_GLTF_IMPORT_SETTINGS_TYPE_ID_HI};
	out[17] = (sk_type_id_t){SK_OBJ_IMPORT_SETTINGS_TYPE_ID_LO, SK_OBJ_IMPORT_SETTINGS_TYPE_ID_HI};
	out[18] = (sk_type_id_t){SK_TRANSFORM_COMPONENT_TYPE_ID_LO, SK_TRANSFORM_COMPONENT_TYPE_ID_HI};
	out[19] = (sk_type_id_t){SK_CAMERA_COMPONENT_TYPE_ID_LO, SK_CAMERA_COMPONENT_TYPE_ID_HI};
	out[20] = (sk_type_id_t){SK_LIGHT_COMPONENT_TYPE_ID_LO, SK_LIGHT_COMPONENT_TYPE_ID_HI};
	out[21] = (sk_type_id_t){SK_MESH_RENDERER_COMPONENT_TYPE_ID_LO, SK_MESH_RENDERER_COMPONENT_TYPE_ID_HI};
	out[22] = (sk_type_id_t){SK_STATIC_TAG_COMPONENT_TYPE_ID_LO, SK_STATIC_TAG_COMPONENT_TYPE_ID_HI};
	*out_count = 23u;
}

static rd_type_t* rd_find_type_by_id(rd_state_t* state, sk_type_id_t type_id) {
	u32 i;
	for (i = 0u; i < state->type_count; ++i) {
		if (state->types[i].type != NULL && SK_TYPE_ID_EQ(state->repo->type_id(state->types[i].type), type_id)) {
			return &state->types[i];
		}
	}
	return NULL;
}

/* Rebuild the type table: known registered type ids + distinct types of live
 * resources (RIDs are sequential 1..resource_count). */
static void rd_refresh(rd_state_t* state) {
	const sk_repository_api_t* repo = state->repo;
	u64 total;
	u64 rid;
	sk_type_id_t known[SK_EDITOR_RD_MAX_TYPES];
	u32 known_count;
	u32 i;

	state->type_count = 0u;
	if (state->repository == NULL || repo == NULL) {
		state->selected_type = RD_NO_SELECTION;
		return;
	}
	rd_known_type_ids(known, SK_EDITOR_RD_MAX_TYPES, &known_count);
	for (i = 0u; i < known_count; ++i) {
		const sk_resource_type_t* type = repo->find_type(state->repository, known[i]);
		if (type == NULL || rd_find_type_by_id(state, known[i]) != NULL) {
			continue;
		}
		if (state->type_count < SK_EDITOR_RD_MAX_TYPES) {
			rd_type_t* t = &state->types[state->type_count++];
			t->type = type;
			t->instance_count = 0u;
			(void)snprintf(t->name, sizeof(t->name), "%s", repo->type_name(type));
		}
	}
	/* Walk the dense RID range for live instances + any extra types. */
	total = repo->resource_count(state->repository);
	for (rid = 1u; rid <= total; ++rid) {
		sk_rid_t r = {rid};
		const sk_resource_type_t* type;
		rd_type_t* t;
		if (repo->has_resource(state->repository, r) == 0) {
			continue;
		}
		type = repo->resource_type(state->repository, r);
		if (type == NULL) {
			continue;
		}
		t = rd_find_type_by_id(state, repo->type_id(type));
		if (t == NULL) {
			if (state->type_count >= SK_EDITOR_RD_MAX_TYPES) {
				continue;
			}
			t = &state->types[state->type_count++];
			t->type = type;
			t->instance_count = 0u;
			(void)snprintf(t->name, sizeof(t->name), "%s", repo->type_name(type));
		}
		if (t->instance_count < SK_EDITOR_RD_MAX_INSTANCES) {
			t->instances[t->instance_count++] = r;
		}
	}
	if (state->selected_type >= state->type_count) {
		state->selected_type = 0u;
	}
	state->refresh_version += 1u;
}

/* Type name without a registered repository (display-only fallback). */
static const_chr_t rd_type_name_of(const sk_resource_type_t* type, const sk_repository_api_t* repo) {
	return type != NULL ? repo->type_name(type) : "?";
}

/* ------------------------------------------------------------------ */
/*  Field value formatting (ui-independent)                           */
/* ------------------------------------------------------------------ */

static const_chr_t rd_field_type_name(sk_resource_field_type_t type) {
	switch (type) {
	case SK_RESOURCE_FIELD_TYPE_NONE:
		return "None";
	case SK_RESOURCE_FIELD_TYPE_BOOL:
		return "Bool";
	case SK_RESOURCE_FIELD_TYPE_INT:
		return "Int";
	case SK_RESOURCE_FIELD_TYPE_UINT:
		return "UInt";
	case SK_RESOURCE_FIELD_TYPE_FLOAT:
		return "Float";
	case SK_RESOURCE_FIELD_TYPE_STRING:
		return "String";
	case SK_RESOURCE_FIELD_TYPE_VEC2:
		return "Vec2";
	case SK_RESOURCE_FIELD_TYPE_VEC3:
		return "Vec3";
	case SK_RESOURCE_FIELD_TYPE_VEC4:
		return "Vec4";
	case SK_RESOURCE_FIELD_TYPE_QUAT:
		return "Quat";
	case SK_RESOURCE_FIELD_TYPE_MAT4:
		return "Mat4";
	case SK_RESOURCE_FIELD_TYPE_COLOR:
		return "Color";
	case SK_RESOURCE_FIELD_TYPE_ENUM:
		return "Enum";
	case SK_RESOURCE_FIELD_TYPE_BLOB:
		return "Blob";
	case SK_RESOURCE_FIELD_TYPE_REFERENCE:
		return "Reference";
	case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
		return "ReferenceArray";
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT:
		return "SubObject";
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST:
		return "SubObjectList";
	case SK_RESOURCE_FIELD_TYPE_BUFFER:
		return "Buffer";
	case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
		return "TypeId";
	case SK_RESOURCE_FIELD_TYPE_MAX:
	default:
		return "Unknown";
	}
}

static void rd_format_rid(char* out, u32 cap, sk_rid_t rid) {
	(void)snprintf(out, cap, "RID:%llu", rid.id);
}

/* Format the value of field @p field on @p view. @p link receives a
 * navigable RID for reference/subobject/array fields (0 otherwise). */
static void rd_format_field_value(const sk_repository_api_t* repo, sk_resource_object_t view, const sk_resource_field_t* field, char* out, u32 cap, sk_rid_t* link) {
	u32 index = field->index;
	if (link != NULL) {
		*link = SK_RID_ZERO;
	}
	switch (field->type) {
	case SK_RESOURCE_FIELD_TYPE_NONE:
	case SK_RESOURCE_FIELD_TYPE_MAX:
		(void)snprintf(out, cap, "%s", "(?)");
		break;
	case SK_RESOURCE_FIELD_TYPE_BOOL:
		(void)snprintf(out, cap, "%s", repo->get_bool(view, index) != 0 ? "true" : "false");
		break;
	case SK_RESOURCE_FIELD_TYPE_INT:
		(void)snprintf(out, cap, "%lld", repo->get_int(view, index));
		break;
	case SK_RESOURCE_FIELD_TYPE_UINT:
		(void)snprintf(out, cap, "%llu", repo->get_uint(view, index));
		break;
	case SK_RESOURCE_FIELD_TYPE_FLOAT:
		(void)snprintf(out, cap, "%.4f", repo->get_float(view, index));
		break;
	case SK_RESOURCE_FIELD_TYPE_STRING: {
		const_chr_t s = repo->get_string(view, index);
		(void)snprintf(out, cap, "%s", s != NULL ? s : "(null)");
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_VEC2: {
		sk_vec2_t v = repo->get_vec2(view, index);
		(void)snprintf(out, cap, "(%.3f, %.3f)", (double)v.x, (double)v.y);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_VEC3: {
		sk_vec3_t v = repo->get_vec3(view, index);
		(void)snprintf(out, cap, "(%.3f, %.3f, %.3f)", (double)v.x, (double)v.y, (double)v.z);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_VEC4: {
		sk_vec4_t v = repo->get_vec4(view, index);
		(void)snprintf(out, cap, "(%.3f, %.3f, %.3f, %.3f)", (double)v.x, (double)v.y, (double)v.z, (double)v.w);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_QUAT: {
		sk_quat_t q = repo->get_quat(view, index);
		(void)snprintf(out, cap, "(%.3f, %.3f, %.3f, %.3f)", (double)q.x, (double)q.y, (double)q.z, (double)q.w);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_MAT4:
		(void)snprintf(out, cap, "%s", "[mat4]");
		break;
	case SK_RESOURCE_FIELD_TYPE_COLOR: {
		sk_color_t c = repo->get_color(view, index);
		(void)snprintf(out, cap, "(%.2f, %.2f, %.2f, %.2f)", (double)c.r, (double)c.g, (double)c.b, (double)c.a);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_ENUM:
		(void)snprintf(out, cap, "%llu", repo->get_enum(view, index));
		break;
	case SK_RESOURCE_FIELD_TYPE_BLOB: {
		u32 size = 0u;
		(void)repo->get_blob(view, index, &size);
		(void)snprintf(out, cap, "[%u bytes]", size);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_BUFFER: {
		u32 size = 0u;
		(void)repo->get_buffer(view, index, &size);
		(void)snprintf(out, cap, "[%u bytes]", size);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_TYPE_ID: {
		sk_type_id_t tid = repo->get_type_id(view, index);
		(void)snprintf(out, cap, "0x%016llx%016llx", tid.lo, tid.hi);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_REFERENCE: {
		sk_rid_t ref = repo->get_reference(view, index);
		if (ref.id != 0u) {
			rd_format_rid(out, cap, ref);
			if (link != NULL) {
				*link = ref;
			}
		} else {
			(void)snprintf(out, cap, "%s", "(null)");
		}
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
		sk_rid_t sub = repo->get_subobject(view, index);
		if (sub.id != 0u) {
			rd_format_rid(out, cap, sub);
			if (link != NULL) {
				*link = sub;
			}
		} else {
			(void)snprintf(out, cap, "%s", "(null)");
		}
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
		u32 count = 0u;
		(void)repo->get_reference_array(view, index, &count);
		(void)snprintf(out, cap, "[%u refs]", count);
		if (count > 0u && link != NULL) {
			const sk_rid_t* items = repo->get_reference_array(view, index, &count);
			if (items != NULL) {
				*link = items[0];
			}
		}
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
		u32 count = 0u;
		(void)repo->get_subobject_list(view, index, &count);
		(void)snprintf(out, cap, "[%u sub-objects]", count);
		if (count > 0u && link != NULL) {
			const sk_rid_t* items = repo->get_subobject_list(view, index, &count);
			if (items != NULL) {
				*link = items[0];
			}
		}
		break;
	}
	default:
		(void)snprintf(out, cap, "%s", "(?)");
		break;
	}
}

/* ------------------------------------------------------------------ */
/*  Widget callbacks                                                   */
/* ------------------------------------------------------------------ */

static void rd_on_type_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user) {
	rd_state_t* state = (rd_state_t*)user;
	(void)ctx;
	(void)host;
	if (state != NULL && item_id > 0u && item_id <= (u64)state->type_count) {
		state->selected_type = (u32)(item_id - 1u);
	}
}

static void rd_on_instance_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user) {
	rd_state_t* state = (rd_state_t*)user;
	(void)ctx;
	(void)host;
	if (state != NULL && state->selected_type < state->type_count && item_id > 0u && item_id <= (u64)state->types[state->selected_type].instance_count) {
		rd_type_t* t = &state->types[state->selected_type];
		sk_rid_t rid = t->instances[(u32)(item_id - 1u)];
		if (state->selected_instance.id != 0u && state->history_count < SK_EDITOR_RD_MAX_HISTORY) {
			state->history[state->history_count++] = state->selected_instance;
		}
		state->selected_instance = rid;
		state->tab = SK_EDITOR_RD_TAB_INSTANCE;
	}
}

static void rd_on_search_change(sk_ui_context_t* ctx, sk_ui_node_t node, const_chr_t text, void_ptr_t user) {
	rd_state_t* state = (rd_state_t*)user;
	(void)ctx;
	(void)node;
	if (state != NULL) {
		(void)snprintf(state->search, sizeof(state->search), "%s", text != NULL ? text : "");
	}
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                     */
/* ------------------------------------------------------------------ */

static void rd_apply_panel_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 8.0f;
	p.layout.padding.top = 8.0f;
	p.layout.padding.right = 8.0f;
	p.layout.padding.bottom = 8.0f;
	p.layout.row_gap = 6.0f;
	p.background_color = sk_ui_rgba(0.09f, 0.10f, 0.13f, 0.96f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void rd_apply_info_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_PADDING | SK_UI_SP_COLUMN_GAP;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	p.layout.column_gap = 8.0f;
	p.layout.padding.left = 4.0f;
	p.layout.padding.top = 2.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 2.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void rd_apply_info_label_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	p.color = sk_ui_rgba(0.75f, 0.77f, 0.85f, 1.0f);
	p.font_size = 12.0f;
	p.layout.width = sk_ui_pt(140.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void rd_build_type_list(rd_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	u32 n = 0u;
	if (ui == NULL) {
		return;
	}
	for (i = 0u; i < state->type_count; ++i) {
		if (state->search[0] != '\0' && strstr(state->types[i].name, state->search) == NULL) {
			continue;
		}
		sk_ui_item_set(&state->list_items[n], (u64)i + 1u, 0u, state->types[i].name, 0u);
		n++;
	}
	state->list_arr.items = n > 0u ? state->list_items : NULL;
	state->list_arr.count = n;
	state->list_arr.revision = (u32)(state->refresh_version & 0xFFFFFFFFu);
	if (sk_ui_node_is_valid(state->type_list)) {
		(void)ui->item_bind_set_array(ctx, state->type_list, &state->list_arr);
		(void)ui->item_bind_sync(ctx, state->type_list);
		if (state->selected_type < state->type_count) {
			(void)ui->item_bind_set_selected(ctx, state->type_list, (u64)state->selected_type + 1u, 1);
		}
	}
}

static void rd_build_instance_list(rd_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	rd_type_t* t;
	u32 i;
	if (ui == NULL || state->selected_type >= state->type_count) {
		return;
	}
	t = &state->types[state->selected_type];
	for (i = 0u; i < t->instance_count; ++i) {
		char label[64];
		(void)snprintf(label, sizeof(label), "RID:%llu", t->instances[i].id);
		sk_ui_item_set(&state->list_items[SK_EDITOR_RD_MAX_TYPES + i], (u64)i + 1u, 0u, label, 0u);
	}
	state->list_arr.items = t->instance_count > 0u ? &state->list_items[SK_EDITOR_RD_MAX_TYPES] : NULL;
	state->list_arr.count = t->instance_count;
	if (sk_ui_node_is_valid(state->instance_list)) {
		(void)ui->item_bind_set_array(ctx, state->instance_list, &state->list_arr);
		(void)ui->item_bind_sync(ctx, state->instance_list);
	}
}

/* Rebuild the Types tab details under @p content (destroyed + rebuilt by the
 * caller on selection change). */
static void rd_build_types_tab(rd_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t body;
	sk_ui_node_t left;
	sk_ui_node_t right;
	sk_ui_node_t left_scroll;
	sk_ui_node_t right_scroll;
	sk_ui_node_t fields;
	sk_ui_style_props_t p;
	rd_type_t* t;
	char buf[SK_EDITOR_RD_LABEL_CAP];
	u32 i;

	/* Search + type list (left). */
	state->search_input = ui->widget_search_input(ctx, content, state->search, "rd.search");
	(void)ui->text_input_set_on_change(ctx, state->search_input, rd_on_search_change, state);

	body = ui->widget_view(ctx, content, "rd.body");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_COLUMN_GAP;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.column_gap = 6.0f;
	(void)ui->node_set_inline_style(ctx, body, &p);

	left = ui->widget_view(ctx, body, "rd.left");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(240.0f);
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, left, &p);
	left_scroll = ui->widget_scroll_view(ctx, left, "rd.left.scroll");
	/* Unique clay id for the scroll content (two scroll views share this
	 * window's layout surface; NULL-id scroll_content nodes would collide). */
	(void)ui->node_set_id(ctx, ui->scroll_view_content(ctx, left_scroll), "rd.left.scroll.content");
	state->type_list = ui->widget_list(ctx, left_scroll, &state->list_arr, "rd.typelist");
	(void)ui->item_bind_set_on_activate(ctx, state->type_list, rd_on_type_activate, state);
	rd_build_type_list(state);

	/* Right: type details. */
	right = ui->widget_view(ctx, body, "rd.right");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, right, &p);
	right_scroll = ui->widget_scroll_view(ctx, right, "rd.right.scroll");
	(void)ui->node_set_id(ctx, ui->scroll_view_content(ctx, right_scroll), "rd.right.scroll.content");

	if (state->selected_type >= state->type_count) {
		(void)ui->widget_text_disabled(ctx, right_scroll, "Select a resource type from the list.", "rd.none");
		return;
	}
	t = &state->types[state->selected_type];

	/* Type info table. */
	(void)ui->widget_text_colored(ctx, right_scroll, "Type Info", sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
	rd_apply_info_row_style(ui, ctx, right_scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, right_scroll, "Full Name", "rd.type.fullname.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		(void)ui->widget_label(ctx, right_scroll, rd_type_name_of(t->type, state->repo), NULL);
	}
	rd_apply_info_row_style(ui, ctx, right_scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, right_scroll, "Fields", "rd.type.fields.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		(void)snprintf(buf, sizeof(buf), "%u", state->repo->type_field_count(t->type));
		(void)ui->widget_label(ctx, right_scroll, buf, NULL);
	}
	rd_apply_info_row_style(ui, ctx, right_scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, right_scroll, "Instances", "rd.type.instances.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		(void)snprintf(buf, sizeof(buf), "%u", t->instance_count);
		(void)ui->widget_label(ctx, right_scroll, buf, NULL);
	}

	/* Fields table. */
	(void)ui->widget_text_colored(ctx, right_scroll, "Fields", sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
	fields = ui->widget_table(ctx, right_scroll, "rd.fields.table", 5, SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS, 0.0f, 0.0f);
	if (sk_ui_node_is_valid(fields)) {
		sk_ui_node_t cell;
		(void)ui->table_setup_column(ctx, fields, "Index", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 50.0f);
		(void)ui->table_setup_column(ctx, fields, "Name", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 0.0f);
		(void)ui->table_setup_column(ctx, fields, "Type", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 120.0f);
		(void)ui->table_setup_column(ctx, fields, "Sub-Type", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 0.0f);
		(void)ui->table_setup_column(ctx, fields, "Size", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 60.0f);
		(void)ui->table_headers_row(ctx, fields);
		for (i = 0u; i < state->repo->type_field_count(t->type); ++i) {
			const sk_resource_field_t* f = state->repo->type_field_at(t->type, i);
			if (f == NULL) {
				continue;
			}
			(void)ui->table_next_row(ctx, fields, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f);
			(void)ui->table_next_column(ctx, fields);
			cell = ui->table_current_cell(ctx, fields);
			(void)snprintf(buf, sizeof(buf), "%u", f->index);
			(void)ui->widget_label(ctx, cell, buf, NULL);
			(void)ui->table_next_column(ctx, fields);
			cell = ui->table_current_cell(ctx, fields);
			(void)ui->widget_label(ctx, cell, f->name, NULL);
			(void)ui->table_next_column(ctx, fields);
			cell = ui->table_current_cell(ctx, fields);
			(void)ui->widget_label(ctx, cell, rd_field_type_name(f->type), NULL);
			(void)ui->table_next_column(ctx, fields);
			cell = ui->table_current_cell(ctx, fields);
			(void)snprintf(buf, sizeof(buf), "%u", f->size);
			(void)ui->widget_label(ctx, cell, buf, NULL);
		}
		(void)ui->table_end(ctx, fields);
	}

	/* Instances table. */
	(void)ui->widget_text_colored(ctx, right_scroll, "Instances", sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
	state->instance_list = ui->widget_list(ctx, right_scroll, &state->list_arr, "rd.instancelist");
	(void)ui->item_bind_set_on_activate(ctx, state->instance_list, rd_on_instance_activate, state);
	rd_build_instance_list(state);
}

/* Build the Instance tab under @p content. */
static void rd_build_instance_tab(rd_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	const sk_resource_type_t* type;
	sk_resource_object_t view;
	sk_ui_node_t scroll;
	sk_ui_node_t values;
	char buf[SK_EDITOR_RD_LABEL_CAP];
	u32 i;
	sk_rid_t parent;
	sk_rid_t prototype;

	if (state->selected_instance.id == 0u || state->repository == NULL) {
		(void)ui->widget_text_disabled(ctx, content, "No instance selected. Select one from the Types tab.", "rd.instance.none");
		return;
	}
	type = state->repo->resource_type(state->repository, state->selected_instance);
	if (type == NULL) {
		(void)ui->widget_text_disabled(ctx, content, "Instance type not found.", "rd.instance.notype");
		return;
	}

	scroll = ui->widget_scroll_view(ctx, content, "rd.instance.scroll");
	(void)ui->node_set_id(ctx, ui->scroll_view_content(ctx, scroll), "rd.instance.scroll.content");

	/* Back button (C++ ICON_FA_ARROW_LEFT " Back"); navigation is driven via
	 * the ops table (and tests) until the widget callback is wired. */
	if (state->history_count > 0u) {
		sk_ui_node_t back = ui->widget_button(ctx, scroll, "< Back", "rd.back");
		(void)back;
	}

	/* Resource metadata table. */
	(void)ui->widget_text_colored(ctx, scroll, "Resource Info", sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "RID", "rd.instance.rid.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		rd_format_rid(buf, sizeof(buf), state->selected_instance);
		(void)ui->widget_label(ctx, scroll, buf, NULL);
	}
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "Type", "rd.instance.type.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		(void)ui->widget_label(ctx, scroll, state->repo->type_name(type), NULL);
	}
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		sk_uuid_t uuid = state->repo->resource_uuid(state->repository, state->selected_instance);
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "UUID", "rd.instance.uuid.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		if (uuid.lo != 0ull || uuid.hi != 0ull) {
			(void)snprintf(buf, sizeof(buf), "%016llx-%016llx", uuid.lo, uuid.hi);
			(void)ui->widget_label(ctx, scroll, buf, NULL);
		} else {
			(void)ui->widget_text_disabled(ctx, scroll, "-", NULL);
		}
	}
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		const_chr_t path = state->repo->get_path(state->repository, state->selected_instance);
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "Path", "rd.instance.path.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		(void)ui->widget_label(ctx, scroll, path != NULL ? path : "-", NULL);
	}
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "Version", "rd.instance.version.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		(void)snprintf(buf, sizeof(buf), "%llu", state->repo->get_version(state->repository, state->selected_instance));
		(void)ui->widget_label(ctx, scroll, buf, NULL);
	}
	parent = state->repo->get_parent(state->repository, state->selected_instance);
	prototype = state->repo->get_prototype(state->repository, state->selected_instance);
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "Parent", "rd.instance.parent.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		if (parent.id != 0u) {
			rd_format_rid(buf, sizeof(buf), parent);
			(void)ui->widget_label(ctx, scroll, buf, NULL);
		} else {
			(void)ui->widget_text_disabled(ctx, scroll, "-", NULL);
		}
	}
	rd_apply_info_row_style(ui, ctx, scroll);
	{
		sk_ui_node_t lbl = ui->widget_label(ctx, scroll, "Prototype", "rd.instance.prototype.label");
		rd_apply_info_label_style(ui, ctx, lbl);
		if (prototype.id != 0u) {
			rd_format_rid(buf, sizeof(buf), prototype);
			(void)ui->widget_label(ctx, scroll, buf, NULL);
		} else {
			(void)ui->widget_text_disabled(ctx, scroll, "-", NULL);
		}
	}

	/* Values table. */
	(void)ui->widget_text_colored(ctx, scroll, "Values", sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
	view = state->repo->read(state->repository, state->selected_instance);
	values = ui->widget_table(ctx, scroll, "rd.values.table", 3, SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS, 0.0f, 0.0f);
	if (sk_ui_node_is_valid(values) && SK_RESOURCE_OBJECT_IS_VALID(view)) {
		sk_ui_node_t cell;
		u32 field_count = state->repo->type_field_count(type);
		(void)ui->table_setup_column(ctx, values, "Field", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 170.0f);
		(void)ui->table_setup_column(ctx, values, "Type", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 120.0f);
		(void)ui->table_setup_column(ctx, values, "Value", SK_UI_TABLE_COLUMN_FLAG_WIDTH_STRETCH, 0.0f);
		(void)ui->table_headers_row(ctx, values);
		for (i = 0u; i < field_count; ++i) {
			const sk_resource_field_t* f = state->repo->type_field_at(type, i);
			sk_rid_t link = SK_RID_ZERO;
			if (f == NULL) {
				continue;
			}
			rd_format_field_value(state->repo, view, f, buf, sizeof(buf), &link);
			(void)ui->table_next_row(ctx, values, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f);
			(void)ui->table_next_column(ctx, values);
			cell = ui->table_current_cell(ctx, values);
			(void)ui->widget_label(ctx, cell, f->name, NULL);
			(void)ui->table_next_column(ctx, values);
			cell = ui->table_current_cell(ctx, values);
			(void)ui->widget_label(ctx, cell, rd_field_type_name(f->type), NULL);
			(void)ui->table_next_column(ctx, values);
			cell = ui->table_current_cell(ctx, values);
			(void)ui->widget_label(ctx, cell, buf, NULL);
		}
		(void)ui->table_end(ctx, values);
	}
}

/* Build the active tab content. */
static void rd_build_selected_tab(rd_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t content;
	if (ui == NULL || !sk_ui_node_is_valid(state->root)) {
		return;
	}
	content = ui->find_by_id(ctx, "rd.content");
	if (sk_ui_node_is_valid(content) && ui->node_alive(ctx, content)) {
		(void)ui->node_destroy(ctx, content);
	}
	content = ui->widget_view(ctx, state->root, "rd.content");
	rd_apply_panel_style(ui, ctx, content);
	state->type_list = SK_UI_NODE_INVALID;
	state->instance_list = SK_UI_NODE_INVALID;
	if (state->tab == SK_EDITOR_RD_TAB_TYPES) {
		rd_build_types_tab(state, content);
	} else {
		rd_build_instance_tab(state, content);
	}
}

static void rd_build_ui(rd_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "rd.content.root");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	rd_apply_panel_style(ui, ctx, state->root);

	{
		sk_ui_node_t tab_bar = ui->widget_tab_bar(ctx, state->root, "rd.tabbar");
		(void)ui->tab_bar_bind_selected(ctx, tab_bar, &state->tab);
		(void)ui->widget_tab(ctx, tab_bar, "Types", "rd.tab.types");
		(void)ui->widget_tab(ctx, tab_bar, "Instance", "rd.tab.instance");
	}

	state->type_list = SK_UI_NODE_INVALID;
	state->instance_list = SK_UI_NODE_INVALID;
	state->last_built_tab = -1;
	state->last_built_version = 0u;
	rd_build_selected_tab(state);
	state->last_built_tab = state->tab;
	state->last_built_version = state->refresh_version;
}

static i32 rd_sync(rd_state_t* state) {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	if (state == NULL || state->ui == NULL) {
		return -1;
	}
	ui = state->ui;
	ctx = state->ui_ctx;
	/* Rebuild tab content when the tab or the repo version changed. */
	if (state->tab != state->last_built_tab || state->refresh_version != state->last_built_version) {
		rd_build_selected_tab(state);
		state->last_built_tab = state->tab;
		state->last_built_version = state->refresh_version;
	}
	/* Search input sync. */
	if (sk_ui_node_is_valid(state->search_input)) {
		const_chr_t t = ui->text_input_get_text(ctx, state->search_input);
		if (t != NULL && strcmp(t, state->search) != 0) {
			(void)snprintf(state->search, sizeof(state->search), "%s", t);
			rd_build_type_list(state);
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void rd_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	rd_class_state_t* cls = (rd_class_state_t*)window->user_data;
	rd_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (rd_state_t*)alloc->alloc(alloc->instance, sizeof(rd_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->selected_type = RD_NO_SELECTION;
	state->tab = SK_EDITOR_RD_TAB_TYPES;
	window->user_data = state;
}

static void rd_draw(sk_editor_window_t* window, i32* open) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_ui_node_t chrome;
	sk_ui_node_t content;
	sk_ui_node_t root;
	*open = 1;
	if (state == NULL) {
		*open = 0;
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (ctx == NULL) {
		return;
	}
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		return;
	}
	chrome = ui->find_by_id(ctx, window->dock_id);
	if (!sk_ui_node_is_valid(chrome)) {
		return;
	}
	content = ui->editor_window_content(ctx, chrome);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	root = ui->find_by_id(ctx, "rd.content.root");
	if (!sk_ui_node_is_valid(root)) {
		rd_build_ui(state, ui, ctx, content);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	(void)rd_sync(state);
}

static void rd_destroy(sk_editor_window_t* window) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ctx != NULL && ui != NULL && sk_ui_node_is_valid(state->root) && ui->node_alive(ctx, state->root)) {
		(void)ui->node_destroy(ctx, state->root);
	}
	state->root = SK_UI_NODE_INVALID;
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

/* ------------------------------------------------------------------ */
/*  Ops table (add_impl; never direct symbols)                        */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* rd_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER);
}

static sk_editor_window_t* rd_ops_inspect_resource(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_rid_t rid) {
	sk_editor_window_t* window = rd_open(app_context, app_api);
	rd_state_t* state = window != NULL ? (rd_state_t*)window->user_data : NULL;
	if (state != NULL && rid.id != 0u) {
		if (state->selected_instance.id != 0u && state->history_count < SK_EDITOR_RD_MAX_HISTORY) {
			state->history[state->history_count++] = state->selected_instance;
		}
		state->selected_instance = rid;
		state->tab = SK_EDITOR_RD_TAB_INSTANCE;
	}
	return window;
}

static void rd_ops_navigate_to_instance(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && rid.id != 0u) {
		if (state->selected_instance.id != 0u && state->history_count < SK_EDITOR_RD_MAX_HISTORY) {
			state->history[state->history_count++] = state->selected_instance;
		}
		state->selected_instance = rid;
		state->tab = SK_EDITOR_RD_TAB_INSTANCE;
	}
}

static void rd_ops_back(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && state->history_count > 0u) {
		state->selected_instance = state->history[--state->history_count];
		state->tab = SK_EDITOR_RD_TAB_INSTANCE;
	}
}

static i32 rd_ops_can_go_back(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL && state->history_count > 0u ? 1 : 0;
}

static void rd_ops_set_repository(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_repository_t* repository) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->repository = repository;
		state->repo = repository != NULL ? app_api->repository_api(app_context) : NULL;
		rd_refresh(state);
	}
}

static sk_repository_t* rd_ops_get_repository(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL ? state->repository : NULL;
}

static void rd_ops_refresh(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		rd_refresh(state);
	}
}

static i32 rd_ops_get_tab(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL ? state->tab : SK_EDITOR_RD_TAB_TYPES;
}

static void rd_ops_set_tab(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 tab) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && (tab == SK_EDITOR_RD_TAB_TYPES || tab == SK_EDITOR_RD_TAB_INSTANCE)) {
		state->tab = tab;
	}
}

static sk_rid_t rd_ops_get_selected_instance(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL ? state->selected_instance : SK_RID_ZERO;
}

static u32 rd_ops_get_selected_type_index(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL ? state->selected_type : RD_NO_SELECTION;
}

static void rd_ops_set_selected_type_index(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && index < state->type_count) {
		state->selected_type = index;
	}
}

static u32 rd_ops_get_type_count(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL ? state->type_count : 0u;
}

static const_chr_t rd_ops_type_name_at(const sk_editor_window_t* window, u32 index) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	if (state == NULL || index >= state->type_count) {
		return NULL;
	}
	return state->types[index].name;
}

static u32 rd_ops_type_field_count(const sk_editor_window_t* window, u32 index) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	if (state == NULL || index >= state->type_count || state->repo == NULL) {
		return 0u;
	}
	return state->repo->type_field_count(state->types[index].type);
}

static const sk_resource_field_t* rd_ops_type_field_at(const sk_editor_window_t* window, u32 index, u32 field) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	if (state == NULL || index >= state->type_count || state->repo == NULL) {
		return NULL;
	}
	return state->repo->type_field_at(state->types[index].type, field);
}

static u32 rd_ops_type_instance_count(const sk_editor_window_t* window, u32 index) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	if (state == NULL || index >= state->type_count) {
		return 0u;
	}
	return state->types[index].instance_count;
}

static sk_rid_t rd_ops_type_instance_at(const sk_editor_window_t* window, u32 index, u32 inst) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	if (state == NULL || index >= state->type_count || inst >= state->types[index].instance_count) {
		return SK_RID_ZERO;
	}
	return state->types[index].instances[inst];
}

static u32 rd_ops_instance_field_count(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	const sk_resource_type_t* type;
	if (state == NULL || state->repo == NULL || state->selected_instance.id == 0u) {
		return 0u;
	}
	type = state->repo->resource_type(state->repository, state->selected_instance);
	return type != NULL ? state->repo->type_field_count(type) : 0u;
}

static const sk_resource_field_t* rd_ops_instance_field_at(const sk_editor_window_t* window, u32 field) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	const sk_resource_type_t* type;
	if (state == NULL || state->repo == NULL || state->selected_instance.id == 0u) {
		return NULL;
	}
	type = state->repo->resource_type(state->repository, state->selected_instance);
	return type != NULL ? state->repo->type_field_at(type, field) : NULL;
}

static i32 rd_ops_instance_field_value(const sk_editor_window_t* window, u32 field, sk_editor_rd_field_value_t* out) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	const sk_resource_type_t* type;
	const sk_resource_field_t* f;
	sk_resource_object_t view;
	if (out == NULL) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	if (state == NULL || state->repo == NULL || state->selected_instance.id == 0u) {
		return -1;
	}
	type = state->repo->resource_type(state->repository, state->selected_instance);
	if (type == NULL) {
		return -1;
	}
	f = state->repo->type_field_at(type, field);
	if (f == NULL) {
		return -1;
	}
	view = state->repo->read(state->repository, state->selected_instance);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return -1;
	}
	rd_format_field_value(state->repo, view, f, out->text, sizeof(out->text), &out->link);
	return 0;
}

static void rd_ops_instance_info(const sk_editor_window_t* window, char* type_name, u32 type_name_cap, char* uuid, u32 uuid_cap, char* path, u32 path_cap, u64* version,
								 sk_rid_t* parent, sk_rid_t* prototype) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	const sk_resource_type_t* type;
	if (state == NULL || state->repo == NULL || state->selected_instance.id == 0u) {
		return;
	}
	type = state->repo->resource_type(state->repository, state->selected_instance);
	if (type_name != NULL && type_name_cap > 0u) {
		(void)snprintf(type_name, type_name_cap, "%s", type != NULL ? state->repo->type_name(type) : "?");
	}
	if (uuid != NULL && uuid_cap > 0u) {
		sk_uuid_t u = state->repo->resource_uuid(state->repository, state->selected_instance);
		if (u.lo != 0ull || u.hi != 0ull) {
			(void)snprintf(uuid, uuid_cap, "%016llx-%016llx", u.lo, u.hi);
		} else {
			(void)snprintf(uuid, uuid_cap, "%s", "-");
		}
	}
	if (path != NULL && path_cap > 0u) {
		const_chr_t p = state->repo->get_path(state->repository, state->selected_instance);
		(void)snprintf(path, path_cap, "%s", p != NULL ? p : "-");
	}
	if (version != NULL) {
		*version = state->repo->get_version(state->repository, state->selected_instance);
	}
	if (parent != NULL) {
		*parent = state->repo->get_parent(state->repository, state->selected_instance);
	}
	if (prototype != NULL) {
		*prototype = state->repo->get_prototype(state->repository, state->selected_instance);
	}
}

static const_chr_t rd_ops_get_search(const sk_editor_window_t* window) {
	const rd_state_t* state = (const rd_state_t*)window->user_data;
	return state != NULL ? state->search : "";
}

static void rd_ops_set_search(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t search) {
	rd_state_t* state = (rd_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		(void)snprintf(state->search, sizeof(state->search), "%s", search != NULL ? search : "");
		rd_build_type_list(state);
	}
}

static const sk_editor_resource_debugger_ops_t rd_ops = {
	rd_open,
	rd_ops_inspect_resource,
	rd_ops_navigate_to_instance,
	rd_ops_back,
	rd_ops_can_go_back,
	rd_ops_set_repository,
	rd_ops_get_repository,
	rd_ops_refresh,
	rd_ops_get_tab,
	rd_ops_set_tab,
	rd_ops_get_selected_instance,
	rd_ops_get_selected_type_index,
	rd_ops_set_selected_type_index,
	rd_ops_get_type_count,
	rd_ops_type_name_at,
	rd_ops_type_field_count,
	rd_ops_type_field_at,
	rd_ops_type_instance_count,
	rd_ops_type_instance_at,
	rd_ops_instance_field_count,
	rd_ops_instance_field_at,
	rd_ops_instance_field_value,
	rd_ops_instance_info,
	rd_ops_get_search,
	rd_ops_set_search,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_resource_debugger_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	rd_class_state_t* cls = rd_class(app_context, app_api);
	if (cls != NULL) {
		rd_window.user_data = cls;
		return;
	}
	cls = (rd_class_state_t*)alloc->alloc(alloc->instance, sizeof(rd_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_RD_STATE_TYPE_ID, cls);

	rd_window.type_id = SK_EDITOR_WINDOW_RESOURCE_DEBUGGER;
	rd_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &rd_window);
	app_api->add_impl(app_context, SK_EDITOR_RESOURCE_DEBUGGER_OPS_TYPE_ID, &rd_ops);
}

void sk_editor_resource_debugger_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	rd_class_state_t* cls = rd_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &rd_window);
	app_api->remove_impl(app_context, SK_EDITOR_RESOURCE_DEBUGGER_OPS_TYPE_ID, &rd_ops);
	app_api->set_api(app_context, SK_EDITOR_RD_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (rd_window.user_data == cls) {
		rd_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

typedef struct rd_test_repo_t {
	sk_repository_t* repo;
	const sk_repository_api_t* api;
	sk_rid_t scene;
	sk_rid_t player;
	sk_rid_t camera;
} rd_test_repo_t;

static void rd_test_repo_build(rd_test_repo_t* out) {
	const sk_repository_api_t* repo = sk_test_repository_table();
	const sk_resource_type_t* scene_type;
	const sk_resource_type_t* entity_type;
	const sk_resource_type_t* cam_type;
	sk_resource_object_t view;
	sk_rid_t roots[2];
	sk_rid_t comps[1];
	memset(out, 0, sizeof(*out));
	out->api = repo;
	out->repo = repo->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(out->repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(out->repo, repo));

	scene_type = repo->find_type(out->repo, SK_SCENE_RESOURCE_TYPE_ID);
	entity_type = repo->find_type(out->repo, SK_ENTITY_RESOURCE_TYPE_ID);
	cam_type = repo->find_type(out->repo, SK_CAMERA_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(scene_type);
	TEST_ASSERT_NOT_NULL(entity_type);
	TEST_ASSERT_NOT_NULL(cam_type);

	out->scene = repo->create_resource(out->repo, scene_type, SK_UUID_ZERO, NULL);
	out->player = repo->create_resource(out->repo, entity_type, SK_UUID_ZERO, NULL);
	out->camera = repo->create_resource(out->repo, entity_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(out->scene.id != 0u);
	TEST_ASSERT_TRUE(out->player.id != 0u);
	TEST_ASSERT_TRUE(out->camera.id != 0u);

	view = repo->write(out->repo, out->scene);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, repo->set_string(view, SK_SCENE_RESOURCE_FIELD_NAME, "Main"));
	roots[0] = out->player;
	roots[1] = out->camera;
	TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(view, SK_SCENE_RESOURCE_FIELD_ROOTS, roots, 2u));
	repo->commit(view, NULL);

	view = repo->write(out->repo, out->player);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, repo->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, "Player"));
	{
		sk_rid_t cam_component = repo->create_resource(out->repo, cam_type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(cam_component.id != 0u);
		comps[0] = cam_component;
		TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, comps, 1u));
	}
	repo->commit(view, NULL);

	view = repo->write(out->repo, out->camera);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, repo->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, "Camera"));
	repo->commit(view, NULL);
}

static void rd_test_repo_destroy(rd_test_repo_t* s) {
	sk_test_repository_table()->destroy(s->repo);
	memset(s, 0, sizeof(*s));
}

/* Ops-table path (no ui): types tab lists known + live types, instance lists
 * come from the RID walk, instance fields format real values, and
 * navigate/back keep history. */
SK_TEST(editor_resource_debugger_window_ops_repo_introspection) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_resource_debugger_ops_t* ops;
	sk_editor_window_t* window;
	rd_test_repo_t r;
	sk_editor_rd_field_value_t fv;
	char type_name[64];
	char uuid[64];
	char path[64];
	u64 version = 0u;
	sk_rid_t parent = SK_RID_ZERO;
	sk_rid_t prototype = SK_RID_ZERO;
	u32 i;
	i32 found_scene = 0;
	i32 found_entity = 0;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_resource_debugger_ops(app, boot.api));
	sk_editor_resource_debugger_register(app, boot.api);
	sk_editor_resource_debugger_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_RESOURCE_DEBUGGER_OPS_TYPE_ID));

	ops = sk_editor_resource_debugger_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Resource Debugger", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER));
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_FILL, window->dock_position);
	TEST_ASSERT_EQUAL_UINT(0u, window->workspace_mask); /* on-demand */

	/* No repository yet: empty type list. */
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_type_count(window));

	/* Attach a repository with a scene + entities + a component. */
	rd_test_repo_build(&r);
	ops->set_repository(app, boot.api, window, r.repo);
	TEST_ASSERT_EQUAL_PTR(r.repo, ops->get_repository(window));

	/* Known builtin types (no instances) + live types (scene/entity/camera
	 * component) are all listed. */
	TEST_ASSERT_TRUE(ops->get_type_count(window) >= 5u);
	for (i = 0u; i < ops->get_type_count(window); ++i) {
		const_chr_t name = ops->type_name_at(window, i);
		if (name != NULL && strcmp(name, "SceneResource") == 0) {
			found_scene = 1;
			TEST_ASSERT_EQUAL_UINT(1u, ops->type_instance_count(window, i));
			TEST_ASSERT_TRUE(ops->type_instance_at(window, i, 0u).id == r.scene.id);
		}
		if (name != NULL && strcmp(name, "EntityResource") == 0) {
			found_entity = 1;
			TEST_ASSERT_EQUAL_UINT(2u, ops->type_instance_count(window, i));
		}
	}
	TEST_ASSERT_EQUAL_INT(1, found_scene);
	TEST_ASSERT_EQUAL_INT(1, found_entity);

	/* Instance tab: navigate to the scene, read metadata + fields. */
	ops->navigate_to_instance(app, boot.api, window, r.scene);
	TEST_ASSERT_EQUAL_UINT((u32)SK_EDITOR_RD_TAB_INSTANCE, (u32)ops->get_tab(window));
	TEST_ASSERT_TRUE(ops->get_selected_instance(window).id == r.scene.id);
	ops->instance_info(window, type_name, sizeof(type_name), uuid, sizeof(uuid), path, sizeof(path), &version, &parent, &prototype);
	TEST_ASSERT_EQUAL_STRING("SceneResource", type_name);
	TEST_ASSERT_TRUE(version >= 1u);

	/* Scene field 0 = Name "Main"; field 1 = Roots subobject list. */
	TEST_ASSERT_TRUE(ops->instance_field_count(window) >= 2u);
	TEST_ASSERT_EQUAL_INT(0, ops->instance_field_value(window, 0u, &fv));
	TEST_ASSERT_EQUAL_STRING("Main", fv.text);
	TEST_ASSERT_TRUE(fv.link.id == 0u);
	TEST_ASSERT_EQUAL_INT(0, ops->instance_field_value(window, 1u, &fv));
	TEST_ASSERT_EQUAL_STRING("[2 sub-objects]", fv.text);
	TEST_ASSERT_TRUE(fv.link.id != 0u); /* first root navigable */

	/* Navigate deeper: entity fields show name + components. */
	ops->navigate_to_instance(app, boot.api, window, r.player);
	TEST_ASSERT_EQUAL_INT(0, ops->instance_field_value(window, 0u, &fv));
	TEST_ASSERT_EQUAL_STRING("Player", fv.text);
	TEST_ASSERT_EQUAL_INT(0, ops->instance_field_value(window, 1u, &fv));
	TEST_ASSERT_EQUAL_STRING("[1 sub-objects]", fv.text);

	/* Back returns to the scene. */
	TEST_ASSERT_EQUAL_INT(1, ops->can_go_back(window));
	ops->back(app, boot.api, window);
	TEST_ASSERT_TRUE(ops->get_selected_instance(window).id == r.scene.id);

	/* inspect_resource opens + navigates (ProjectBrowser/EntityTree entry).
	 * The fresh instance has no repository yet — re-attach it. */
	editor->window_close(app, boot.api, window);
	window = ops->inspect_resource(app, boot.api, r.camera);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_TRUE(ops->get_selected_instance(window).id == r.camera.id);
	ops->set_repository(app, boot.api, window, r.repo);
	TEST_ASSERT_EQUAL_INT(0, ops->instance_field_value(window, 0u, &fv));
	TEST_ASSERT_EQUAL_STRING("Camera", fv.text);

	/* Search filters the type list (ops level). */
	ops->set_search(app, boot.api, window, "scene");
	TEST_ASSERT_EQUAL_STRING("scene", ops->get_search(window));

	rd_test_repo_destroy(&r);
	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER));
	TEST_ASSERT_NOT_NULL(sk_editor_resource_debugger_ops(app, boot.api));

	sk_editor_resource_debugger_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_resource_debugger_ops(app, boot.api));
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
