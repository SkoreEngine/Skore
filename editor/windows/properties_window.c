/**
 * @file properties_window.c
 * @brief Properties / Inspector window (APX-374): selection-driven inspector
 * on the v2 shell.
 *
 * Port of main-branch Skore::PropertiesWindow (migration manifest §3.7): a
 * RightBottom / All-workspaces inspector bound to the notify.h selection
 * observers (ENTITY_SELECTION/DESELECTION, ENTITY_DEBUG_SELECTION/
 * DESELECTION, ASSET_SELECTION, RESOURCE_SELECTION, MATERIAL_NODE_SELECTION)
 * filtered by workspace id. The in-scope surface (entity header + component
 * list with add/remove/reset/move, asset name+UUID, generic resource
 * fields) is backed by the attached repository + scene (`set_scene`); the
 * out-of-scope preview pane, material-graph node editing and layer data are
 * MOCK (session state / recorded flags), per the manifest.
 *
 * Public entry points go through the `sk_editor_properties_ops_t` table
 * registered with add_impl (window-table-pattern.md §7); the shell's
 * Window/Properties menu routes through it.
 */

#include "properties_window.h"

#include "allocator.h"
#include "resource_asset_builtins.h"
#include "resource_component_types.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_PROPERTIES_STATE_TYPE_ID SK_TYPE_ID("sk.editor.properties.state", 0x9c0e2f4b6d8f0a2cULL, 0x5a7b9d0f2e4c6a8bULL)

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct properties_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* Selection (C++ PropertiesWindow members). */
	sk_editor_properties_kind_t kind;
	sk_rid_t selected_rid;
	u32 workspace_id; /* C++ workspace->GetId() filter (v2: workspace type id) */

	/* Scene/repository for entity + component reads (C++ sceneEditor). */
	sk_repository_t* repository; /* borrowed */
	const sk_repository_api_t* repo;
	sk_rid_t scene_rid;

	/* Mock session state (v2 payload gaps). */
	u32 layer_mock[8]; /* entity id low bits -> layer (MOCK; max 8 tracked) */
	sk_rid_t layer_ids[8];
	u32 layer_count;
	u32 reset_component_index; /* MOCK Reset flag */
	char asset_rename[96];	   /* MOCK rename request */
	i32 apply_import_pending;
	i32 reimport_pending;

	u32 selected_component_index;

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t root;
	sk_ui_node_t content;
	sk_ui_node_t add_component_menu;
	u32 last_synced_revision;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;

	/* notify.h observers (add_impl while the window is open). */
	sk_editor_on_entity_selection_t obs_entity_sel;
	sk_editor_on_entity_deselection_t obs_entity_desel;
	sk_editor_on_entity_debug_selection_t obs_debug_sel;
	sk_editor_on_entity_debug_deselection_t obs_debug_desel;
	sk_editor_on_asset_selection_t obs_asset_sel;
	sk_editor_on_resource_selection_t obs_resource_sel;
	sk_editor_on_material_node_selection_t obs_material_sel;
} properties_state_t;

typedef struct properties_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} properties_class_state_t;

static void properties_init(sk_editor_window_t* window);
static void properties_draw(sk_editor_window_t* window, i32* open);
static void properties_destroy(sk_editor_window_t* window);

static sk_editor_window_t properties_window = {
	.title = "Properties",
	.dock_id = "sk.editor_window.properties",
	.dock_position = SK_EDITOR_DOCK_RIGHT_BOTTOM,
	.order = 0,
	.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	.init = properties_init,
	.draw = properties_draw,
	.render = NULL,
	.destroy = properties_destroy,
};

static properties_class_state_t* properties_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (properties_class_state_t*)app_api->get_api(app_context, SK_EDITOR_PROPERTIES_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Selection observers (C++ Event::Bind in ctor, filtered by ws id)  */
/* ------------------------------------------------------------------ */

static void properties_clear_selection_internal(properties_state_t* state) {
	state->kind = SK_EDITOR_PROPERTIES_NONE;
	state->selected_rid = SK_RID_ZERO;
	state->selected_component_index = 0xFFFFFFFFu;
	state->apply_import_pending = 0;
	state->reimport_pending = 0;
}

static void props_on_entity_selection(void* user, u32 workspace_id, sk_rid_t rid) {
	properties_state_t* state = (properties_state_t*)user;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	if (rid.id == 0u && state->kind != SK_EDITOR_PROPERTIES_ENTITY) {
		return;
	}
	properties_clear_selection_internal(state);
	state->kind = SK_EDITOR_PROPERTIES_ENTITY;
	state->selected_rid = rid;
}

static void props_on_entity_deselection(void* user, u32 workspace_id, sk_rid_t rid) {
	properties_state_t* state = (properties_state_t*)user;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	if (rid.id == 0u && state->kind != SK_EDITOR_PROPERTIES_ENTITY) {
		return;
	}
	if (SK_RID_EQ(state->selected_rid, rid) && state->kind == SK_EDITOR_PROPERTIES_ENTITY) {
		properties_clear_selection_internal(state);
	}
}

static void props_on_debug_selection(void* user, u32 workspace_id, void* entity) {
	properties_state_t* state = (properties_state_t*)user;
	(void)entity;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	properties_clear_selection_internal(state);
	state->kind = SK_EDITOR_PROPERTIES_DEBUG_ENTITY;
}

static void props_on_debug_deselection(void* user, u32 workspace_id, void* entity) {
	properties_state_t* state = (properties_state_t*)user;
	(void)entity;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	if (state->kind == SK_EDITOR_PROPERTIES_DEBUG_ENTITY) {
		properties_clear_selection_internal(state);
	}
}

static void props_on_asset_selection(void* user, u32 workspace_id, sk_rid_t rid) {
	properties_state_t* state = (properties_state_t*)user;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	properties_clear_selection_internal(state);
	state->kind = SK_EDITOR_PROPERTIES_ASSET;
	state->selected_rid = rid;
}

static void props_on_resource_selection(void* user, u32 workspace_id, sk_rid_t rid) {
	properties_state_t* state = (properties_state_t*)user;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	properties_clear_selection_internal(state);
	state->kind = SK_EDITOR_PROPERTIES_RESOURCE;
	state->selected_rid = rid;
}

static void props_on_material_selection(void* user, u32 workspace_id, sk_rid_t rid) {
	properties_state_t* state = (properties_state_t*)user;
	if (state == NULL || state->workspace_id != workspace_id) {
		return;
	}
	if (rid.id == 0u && state->kind != SK_EDITOR_PROPERTIES_MATERIAL_NODE) {
		return;
	}
	properties_clear_selection_internal(state);
	state->kind = SK_EDITOR_PROPERTIES_MATERIAL_NODE;
	state->selected_rid = rid;
}

/* ------------------------------------------------------------------ */
/*  Entity / component helpers (ui-independent)                       */
/* ------------------------------------------------------------------ */

static sk_resource_object_t props_entity_view(const properties_state_t* state, sk_rid_t entity) {
	if (state->repository == NULL || state->repo == NULL || entity.id == 0u) {
		return (sk_resource_object_t){0u};
	}
	return state->repo->read(state->repository, entity);
}

static const_chr_t props_entity_name(const properties_state_t* state, sk_rid_t entity) {
	sk_resource_object_t view = props_entity_view(state, entity);
	if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
		const_chr_t name = state->repo->get_string(view, SK_ENTITY_RESOURCE_FIELD_NAME);
		if (name != NULL && name[0] != '\0') {
			return name;
		}
	}
	return "Entity";
}

/* Components sub-object list of @p entity (may be empty). */
static const sk_rid_t* props_components(const properties_state_t* state, sk_rid_t entity, u32* out_count) {
	sk_resource_object_t view = props_entity_view(state, entity);
	if (out_count != NULL) {
		*out_count = 0u;
	}
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return NULL;
	}
	return state->repo->get_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, out_count);
}

static const_chr_t props_component_name(const properties_state_t* state, sk_rid_t component) {
	const sk_resource_type_t* type;
	if (state->repo == NULL || state->repository == NULL || component.id == 0u) {
		return "Component";
	}
	type = state->repo->resource_type(state->repository, component);
	if (type == NULL) {
		return "Component";
	}
	return state->repo->type_name(type);
}

/* Component display name: strip the "sk." prefix and "_resource" suffix. */
static void props_component_label(const properties_state_t* state, sk_rid_t component, char* out, u32 cap) {
	const_chr_t name = props_component_name(state, component);
	const_chr_t p = name;
	if (strncmp(p, "sk.", 3u) == 0) {
		p += 3u;
	}
	(void)snprintf(out, cap, "%s", p);
}

/* Add a component of a registered type id to the selected entity. */
static sk_rid_t props_add_component_internal(properties_state_t* state, sk_type_id_t type_id) {
	const sk_resource_type_t* type;
	sk_rid_t entity;
	sk_rid_t comp;
	sk_resource_object_t view;
	const sk_rid_t* list;
	u32 count = 0u;
	u32 i;
	if (state->repository == NULL || state->repo == NULL || state->kind != SK_EDITOR_PROPERTIES_ENTITY) {
		return SK_RID_ZERO;
	}
	type = state->repo->find_type(state->repository, type_id);
	if (type == NULL) {
		return SK_RID_ZERO;
	}
	entity = state->selected_rid;
	list = props_components(state, entity, &count);
	for (i = 0u; i < count; ++i) {
		if (state->repo->resource_type(state->repository, list[i]) == type) {
			return SK_RID_ZERO; /* already has this component kind */
		}
	}
	comp = state->repo->create_resource(state->repository, type, SK_UUID_ZERO, NULL);
	if (comp.id == 0u) {
		return SK_RID_ZERO;
	}
	view = state->repo->write(state->repository, entity);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		(void)state->repo->destroy_resource(state->repository, comp, NULL);
		return SK_RID_ZERO;
	}
	(void)state->repo->add_to_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, comp);
	state->repo->commit(view, NULL);
	return comp;
}

/* ------------------------------------------------------------------ */
/*  Widget styles                                                     */
/* ------------------------------------------------------------------ */

static void props_apply_panel_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
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
	p.layout.row_gap = 4.0f;
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 0.96f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void props_apply_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 8.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void props_apply_label_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	p.color = sk_ui_rgba(0.75f, 0.77f, 0.85f, 1.0f);
	p.font_size = 12.0f;
	p.layout.width = sk_ui_pt(90.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void props_apply_header_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR;
	p.color = sk_ui_rgba(0.85f, 0.87f, 0.93f, 1.0f);
	p.font_size = 13.0f;
	p.layout.padding.left = 4.0f;
	p.layout.padding.top = 3.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 3.0f;
	p.background_color = sk_ui_rgba(0.14f, 0.15f, 0.18f, 1.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static const_chr_t props_field_value_text(const properties_state_t* state, sk_rid_t rid, const sk_resource_field_t* f, char* out, u32 cap);

/* ------------------------------------------------------------------ */
/*  Draw sections                                                     */
/* ------------------------------------------------------------------ */

static void props_draw_info_row(properties_state_t* state, sk_ui_node_t content, const_chr_t label, const_chr_t value) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t row = ui->widget_view(ctx, content, NULL);
	sk_ui_node_t lbl;
	props_apply_row_style(ui, ctx, row);
	lbl = ui->widget_label(ctx, row, label, NULL);
	props_apply_label_style(ui, ctx, lbl);
	(void)ui->widget_label(ctx, row, value, NULL);
}

static void props_draw_entity(properties_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_rid_t entity = state->selected_rid;
	sk_resource_object_t view;
	char buf[160];
	u32 i;
	u32 comp_count = 0u;
	const sk_rid_t* comps;

	(void)ui->widget_label(ctx, content, "Entity", "props.entity.header");
	props_apply_header_style(ui, ctx, ui->find_by_id(ctx, "props.entity.header"));

	view = props_entity_view(state, entity);
	props_draw_info_row(state, content, "Name", props_entity_name(state, entity));
	if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
		sk_uuid_t uuid = state->repo->resource_uuid(state->repository, entity);
		if (uuid.lo != 0ull || uuid.hi != 0ull) {
			(void)snprintf(buf, sizeof(buf), "%016llx-%016llx", uuid.lo, uuid.hi);
		} else {
			(void)snprintf(buf, sizeof(buf), "%s", "-");
		}
		props_draw_info_row(state, content, "UUID", buf);
	}
	{
		u32 layer = 0u;
		for (i = 0u; i < state->layer_count; ++i) {
			if (SK_RID_EQ(state->layer_ids[i], entity)) {
				layer = state->layer_mock[i];
				break;
			}
		}
		(void)snprintf(buf, sizeof(buf), "Layer %u (MOCK)", layer);
		props_draw_info_row(state, content, "Layer", buf);
	}

	/* Components. */
	comps = props_components(state, entity, &comp_count);
	(void)ui->widget_text_colored(ctx, content, "Components", sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
	for (i = 0u; i < comp_count && i < SK_EDITOR_PROPERTIES_MAX_COMPONENTS; ++i) {
		char label[96];
		char id[48];
		sk_ui_node_t row = ui->widget_view(ctx, content, NULL);
		props_component_label(state, comps[i], label, sizeof(label));
		props_apply_row_style(ui, ctx, row);
		(void)snprintf(id, sizeof(id), "props.component.%u", i);
		(void)ui->widget_label(ctx, row, label, id);
		(void)ui->widget_text_disabled(ctx, row, state->repo->type_name(state->repo->resource_type(state->repository, comps[i])), NULL);
	}
	if (comp_count == 0u) {
		(void)ui->widget_text_disabled(ctx, content, "No components.", "props.components.empty");
	}
	/* Add Component (MOCK popup driven via the ops table + context menu). */
	(void)ui->widget_button(ctx, content, "Add Component", "props.add_component");
}

static void props_draw_asset(properties_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_rid_t asset = state->selected_rid;
	const sk_resource_type_t* type;
	char buf[160];
	u32 i;

	(void)ui->widget_label(ctx, content, "Asset", "props.asset.header");
	props_apply_header_style(ui, ctx, ui->find_by_id(ctx, "props.asset.header"));

	props_draw_info_row(state, content, "Name", state->asset_rename[0] != '\0' ? state->asset_rename : "(asset)");
	if (state->repository != NULL && state->repo != NULL && asset.id != 0u) {
		sk_uuid_t uuid = state->repo->resource_uuid(state->repository, asset);
		if (uuid.lo != 0ull || uuid.hi != 0ull) {
			(void)snprintf(buf, sizeof(buf), "%016llx-%016llx", uuid.lo, uuid.hi);
			props_draw_info_row(state, content, "UUID", buf);
		}
	}
	{
		char ridbuf[48];
		(void)snprintf(ridbuf, sizeof(ridbuf), "%llu", asset.id);
		props_draw_info_row(state, content, "RID", ridbuf);
	}

	/* Generic resource fields (real read through the attached repo). */
	type = (state->repo != NULL && state->repository != NULL) ? state->repo->resource_type(state->repository, asset) : NULL;
	if (type != NULL) {
		(void)ui->widget_text_colored(ctx, content, state->repo->type_name(type), sk_ui_rgba(0.80f, 0.82f, 0.90f, 1.0f), NULL);
		(void)ui->widget_text_disabled(ctx, content, "Import settings / reimport are MOCK until the v2 asset cook pipeline exists.", "props.asset.import.mock");
		for (i = 0u; i < state->repo->type_field_count(type) && i < 12u; ++i) {
			const sk_resource_field_t* f = state->repo->type_field_at(type, i);
			char value[160];
			if (f == NULL) {
				continue;
			}
			props_draw_info_row(state, content, f->name, props_field_value_text(state, asset, f, value, sizeof(value)));
		}
	}
}

/* Format one generic field value (asset/resource) into @p out. */
static const_chr_t props_field_value_text(const properties_state_t* state, sk_rid_t rid, const sk_resource_field_t* f, char* out, u32 cap) {
	sk_resource_object_t view;
	const_chr_t s;
	if (state->repo == NULL || state->repository == NULL || rid.id == 0u || f == NULL) {
		(void)snprintf(out, cap, "%s", "-");
		return out;
	}
	view = state->repo->read(state->repository, rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		(void)snprintf(out, cap, "%s", "-");
		return out;
	}
	switch ((i32)f->type) {
	case SK_RESOURCE_FIELD_TYPE_BOOL:
		(void)snprintf(out, cap, "%s", state->repo->get_bool(view, f->index) != 0 ? "true" : "false");
		break;
	case SK_RESOURCE_FIELD_TYPE_INT:
		(void)snprintf(out, cap, "%lld", state->repo->get_int(view, f->index));
		break;
	case SK_RESOURCE_FIELD_TYPE_UINT:
		(void)snprintf(out, cap, "%llu", state->repo->get_uint(view, f->index));
		break;
	case SK_RESOURCE_FIELD_TYPE_FLOAT:
		(void)snprintf(out, cap, "%.4f", state->repo->get_float(view, f->index));
		break;
	case SK_RESOURCE_FIELD_TYPE_STRING:
		s = state->repo->get_string(view, f->index);
		(void)snprintf(out, cap, "%s", s != NULL ? s : "(null)");
		break;
	case SK_RESOURCE_FIELD_TYPE_REFERENCE: {
		sk_rid_t ref = state->repo->get_reference(view, f->index);
		(void)snprintf(out, cap, "RID:%llu", ref.id);
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
		sk_rid_t sub = state->repo->get_subobject(view, f->index);
		(void)snprintf(out, cap, "RID:%llu", sub.id);
		break;
	}
	default: {
		u32 count = 0u;
		if (f->type == SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) {
			(void)state->repo->get_reference_array(view, f->index, &count);
			(void)snprintf(out, cap, "[%u refs]", count);
		} else if (f->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
			(void)state->repo->get_subobject_list(view, f->index, &count);
			(void)snprintf(out, cap, "[%u sub-objects]", count);
		} else {
			(void)snprintf(out, cap, "%s", "(?)");
		}
		break;
	}
	}
	return out;
}

static void props_draw_resource(properties_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	const sk_resource_type_t* type;
	char buf[160];
	u32 i;

	(void)ui->widget_label(ctx, content, "Resource", "props.resource.header");
	props_apply_header_style(ui, ctx, ui->find_by_id(ctx, "props.resource.header"));

	(void)snprintf(buf, sizeof(buf), "%llu", state->selected_rid.id);
	props_draw_info_row(state, content, "RID", buf);

	type = (state->repo != NULL && state->repository != NULL) ? state->repo->resource_type(state->repository, state->selected_rid) : NULL;
	if (type != NULL) {
		props_draw_info_row(state, content, "Type", state->repo->type_name(type));
		for (i = 0u; i < state->repo->type_field_count(type) && i < 12u; ++i) {
			const sk_resource_field_t* f = state->repo->type_field_at(type, i);
			char value[160];
			if (f == NULL) {
				continue;
			}
			props_draw_info_row(state, content, f->name, props_field_value_text(state, state->selected_rid, f, value, sizeof(value)));
		}
	} else {
		(void)ui->widget_text_disabled(ctx, content, "Attach a repository (set_scene) to inspect resource fields.", "props.resource.norepo");
	}
}

static void props_draw_material_node(properties_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	char buf[64];
	(void)ui->widget_label(ctx, content, "Material Node", "props.material.header");
	props_apply_header_style(ui, ctx, ui->find_by_id(ctx, "props.material.header"));
	(void)snprintf(buf, sizeof(buf), "RID:%llu", state->selected_rid.id);
	props_draw_info_row(state, content, "Node", buf);
	(void)ui->widget_text_disabled(ctx, content, "Material graph node editing is out of scope (graph editor not ported).", "props.material.mock");
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                     */
/* ------------------------------------------------------------------ */

static void props_rebuild_content(properties_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t content;
	if (ui == NULL || !sk_ui_node_is_valid(state->content)) {
		return;
	}
	/* The host node is stable (props.content.host); destroy its previous
	 * content subtree (node_destroy is recursive) and rebuild under it. */
	content = ui->find_by_id(ctx, "props.content");
	if (sk_ui_node_is_valid(content) && ui->node_alive(ctx, content)) {
		(void)ui->node_destroy(ctx, content);
	}
	content = ui->widget_view(ctx, state->content, "props.content");
	props_apply_panel_style(ui, ctx, content);

	switch (state->kind) {
	case SK_EDITOR_PROPERTIES_ENTITY:
		props_draw_entity(state, content);
		break;
	case SK_EDITOR_PROPERTIES_ASSET:
		props_draw_asset(state, content);
		break;
	case SK_EDITOR_PROPERTIES_RESOURCE:
		props_draw_resource(state, content);
		break;
	case SK_EDITOR_PROPERTIES_MATERIAL_NODE:
		props_draw_material_node(state, content);
		break;
	case SK_EDITOR_PROPERTIES_DEBUG_ENTITY:
		(void)ui->widget_text_disabled(ctx, content, "Runtime (debug) entity selected — no live scene in v2 (MOCK).", "props.debug.mock");
		break;
	case SK_EDITOR_PROPERTIES_NONE:
	default:
		(void)ui->widget_text_centered(ctx, content, "Select something...", "props.empty");
		break;
	}
}

static void props_build_ui(properties_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	state->ui = ui;
	state->ui_ctx = ctx;
	state->root = ui->widget_view(ctx, parent, "props.content.root");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	props_apply_panel_style(ui, ctx, state->root);
	/* Stable content host; per-selection subtrees rebuild under it. */
	state->content = ui->widget_view(ctx, state->root, "props.content.host");
	props_rebuild_content(state);
}

static i32 props_sync(properties_state_t* state) {
	u32 revision;
	if (state == NULL || state->ui == NULL) {
		return -1;
	}
	revision = (u32)state->kind + (u32)(state->selected_rid.id & 0xFFFFFFFFu) + state->selected_component_index;
	if (revision != state->last_synced_revision) {
		props_rebuild_content(state);
		state->last_synced_revision = revision;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void properties_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	properties_class_state_t* cls = (properties_class_state_t*)window->user_data;
	properties_state_t* state;
	const sk_app_api_t* api;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (properties_state_t*)alloc->alloc(alloc->instance, sizeof(properties_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->selected_component_index = 0xFFFFFFFFu;
	state->workspace_id = SK_EDITOR_WORKSPACE_SCENE;
	{
		sk_editor_workspace_t* ws = sk_editor_workspace_active(cls->app_context, cls->app_api);
		if (ws != NULL) {
			state->workspace_id = sk_editor_workspace_type_id(ws);
		}
	}

	/* C++ ctor Event::Bind set — subscribe to the notify.h observers. */
	api = cls->app_api;
	state->obs_entity_sel.order = 10;
	state->obs_entity_sel.user = state;
	state->obs_entity_sel.on_entity_selection = props_on_entity_selection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_ENTITY_SELECTION, &state->obs_entity_sel);

	state->obs_entity_desel.order = 10;
	state->obs_entity_desel.user = state;
	state->obs_entity_desel.on_entity_deselection = props_on_entity_deselection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_ENTITY_DESELECTION, &state->obs_entity_desel);

	state->obs_debug_sel.order = 10;
	state->obs_debug_sel.user = state;
	state->obs_debug_sel.on_entity_debug_selection = props_on_debug_selection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_ENTITY_DEBUG_SELECTION, &state->obs_debug_sel);

	state->obs_debug_desel.order = 10;
	state->obs_debug_desel.user = state;
	state->obs_debug_desel.on_entity_debug_deselection = props_on_debug_deselection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_ENTITY_DEBUG_DESELECTION, &state->obs_debug_desel);

	state->obs_asset_sel.order = 10;
	state->obs_asset_sel.user = state;
	state->obs_asset_sel.on_asset_selection = props_on_asset_selection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_ASSET_SELECTION, &state->obs_asset_sel);

	state->obs_resource_sel.order = 10;
	state->obs_resource_sel.user = state;
	state->obs_resource_sel.on_resource_selection = props_on_resource_selection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_RESOURCE_SELECTION, &state->obs_resource_sel);

	state->obs_material_sel.order = 10;
	state->obs_material_sel.user = state;
	state->obs_material_sel.on_material_node_selection = props_on_material_selection;
	api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_MATERIAL_NODE_SELECTION, &state->obs_material_sel);

	window->user_data = state;
}

static void properties_draw(sk_editor_window_t* window, i32* open) {
	properties_state_t* state = (properties_state_t*)window->user_data;
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
	root = ui->find_by_id(ctx, "props.content.root");
	if (!sk_ui_node_is_valid(root)) {
		props_build_ui(state, ui, ctx, content);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	(void)props_sync(state);
}

static void properties_destroy(sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_app_api_t* api;
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	/* C++ dtor Event::Unbind set — drop the observers. */
	api = state->app_api;
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_ENTITY_SELECTION, &state->obs_entity_sel);
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_ENTITY_DESELECTION, &state->obs_entity_desel);
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_ENTITY_DEBUG_SELECTION, &state->obs_debug_sel);
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_ENTITY_DEBUG_DESELECTION, &state->obs_debug_desel);
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_ASSET_SELECTION, &state->obs_asset_sel);
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_RESOURCE_SELECTION, &state->obs_resource_sel);
	api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_MATERIAL_NODE_SELECTION, &state->obs_material_sel);

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

static sk_editor_window_t* properties_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_PROPERTIES);
}

static void props_ops_set_scene(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_repository_t* repository, sk_rid_t scene_rid) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->repository = repository;
		state->repo = repository != NULL ? app_api->repository_api(app_context) : NULL;
		state->scene_rid = scene_rid;
	}
}

static sk_editor_properties_kind_t props_ops_get_kind(const sk_editor_window_t* window) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	return state != NULL ? state->kind : SK_EDITOR_PROPERTIES_NONE;
}

static sk_rid_t props_ops_get_selected_rid(const sk_editor_window_t* window) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	return state != NULL ? state->selected_rid : SK_RID_ZERO;
}

static void props_ops_clear_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		properties_clear_selection_internal(state);
	}
}

static const_chr_t props_ops_get_entity_name(const sk_editor_window_t* window, sk_rid_t entity) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	return state != NULL ? props_entity_name(state, entity) : "";
}

static void props_ops_rename_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t entity, const_chr_t name) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	sk_resource_object_t view;
	(void)app_context;
	(void)app_api;
	if (state == NULL || state->repo == NULL || state->repository == NULL || entity.id == 0u || name == NULL) {
		return;
	}
	view = state->repo->write(state->repository, entity);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return;
	}
	(void)state->repo->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, name);
	state->repo->commit(view, NULL);
}

static i32 props_ops_get_entity_uuid(const sk_editor_window_t* window, sk_rid_t entity, char* out, u32 cap) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	sk_uuid_t uuid;
	if (out == NULL || cap == 0u || state == NULL || state->repo == NULL || state->repository == NULL || entity.id == 0u) {
		return -1;
	}
	uuid = state->repo->resource_uuid(state->repository, entity);
	if (uuid.lo != 0ull || uuid.hi != 0ull) {
		(void)snprintf(out, cap, "%016llx-%016llx", uuid.lo, uuid.hi);
	} else {
		(void)snprintf(out, cap, "%s", "-");
	}
	return 0;
}

static u32 props_ops_get_entity_layer(const sk_editor_window_t* window, sk_rid_t entity) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	u32 i;
	if (state == NULL) {
		return 0u;
	}
	for (i = 0u; i < state->layer_count; ++i) {
		if (SK_RID_EQ(state->layer_ids[i], entity)) {
			return state->layer_mock[i];
		}
	}
	return 0u;
}

static void props_ops_set_entity_layer(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t entity, u32 layer) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	u32 i;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return;
	}
	for (i = 0u; i < state->layer_count; ++i) {
		if (SK_RID_EQ(state->layer_ids[i], entity)) {
			state->layer_mock[i] = layer;
			return;
		}
	}
	if (state->layer_count < (u32)(sizeof(state->layer_ids) / sizeof(state->layer_ids[0]))) {
		state->layer_ids[state->layer_count] = entity;
		state->layer_mock[state->layer_count] = layer;
		state->layer_count += 1u;
	}
}

static u32 props_ops_get_component_count(const sk_editor_window_t* window) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	u32 count = 0u;
	if (state == NULL || state->kind != SK_EDITOR_PROPERTIES_ENTITY) {
		return 0u;
	}
	(void)props_components(state, state->selected_rid, &count);
	return count;
}

static const_chr_t props_ops_component_name_at(const sk_editor_window_t* window, u32 index) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	static char label[96];
	const sk_rid_t* list;
	u32 count = 0u;
	if (state == NULL || state->kind != SK_EDITOR_PROPERTIES_ENTITY) {
		return NULL;
	}
	list = props_components(state, state->selected_rid, &count);
	if (index >= count) {
		return NULL;
	}
	props_component_label(state, list[index], label, sizeof(label));
	return label;
}

static sk_rid_t props_ops_component_rid_at(const sk_editor_window_t* window, u32 index) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	const sk_rid_t* list;
	u32 count = 0u;
	if (state == NULL || state->kind != SK_EDITOR_PROPERTIES_ENTITY) {
		return SK_RID_ZERO;
	}
	list = props_components(state, state->selected_rid, &count);
	if (index >= count) {
		return SK_RID_ZERO;
	}
	return list[index];
}

static sk_rid_t props_ops_add_component(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t type_name) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	sk_type_id_t builtins[5];
	u32 i;
	(void)app_context;
	(void)app_api;
	if (state == NULL || type_name == NULL) {
		return SK_RID_ZERO;
	}
	/* Runtime assignment: SK_TYPE_ID expands to a compound literal, which is
	 * not a constant expression for static initializers (same constraint as
	 * the APX-330 scaffolding). */
	builtins[0] = (sk_type_id_t){SK_TRANSFORM_COMPONENT_TYPE_ID_LO, SK_TRANSFORM_COMPONENT_TYPE_ID_HI};
	builtins[1] = (sk_type_id_t){SK_CAMERA_COMPONENT_TYPE_ID_LO, SK_CAMERA_COMPONENT_TYPE_ID_HI};
	builtins[2] = (sk_type_id_t){SK_LIGHT_COMPONENT_TYPE_ID_LO, SK_LIGHT_COMPONENT_TYPE_ID_HI};
	builtins[3] = (sk_type_id_t){SK_MESH_RENDERER_COMPONENT_TYPE_ID_LO, SK_MESH_RENDERER_COMPONENT_TYPE_ID_HI};
	builtins[4] = (sk_type_id_t){SK_STATIC_TAG_COMPONENT_TYPE_ID_LO, SK_STATIC_TAG_COMPONENT_TYPE_ID_HI};
	for (i = 0u; i < (u32)(sizeof(builtins) / sizeof(builtins[0])); ++i) {
		const sk_resource_type_t* t = state->repo != NULL ? state->repo->find_type(state->repository, builtins[i]) : NULL;
		if (t != NULL && strcmp(state->repo->type_name(t), type_name) == 0) {
			return props_add_component_internal(state, builtins[i]);
		}
	}
	return SK_RID_ZERO;
}

static void props_ops_remove_component(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	const sk_rid_t* list;
	u32 count = 0u;
	sk_resource_object_t view;
	(void)app_context;
	(void)app_api;
	if (state == NULL || state->kind != SK_EDITOR_PROPERTIES_ENTITY || state->repo == NULL || state->repository == NULL) {
		return;
	}
	list = props_components(state, state->selected_rid, &count);
	if (index >= count) {
		return;
	}
	view = state->repo->write(state->repository, state->selected_rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return;
	}
	(void)state->repo->remove_from_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, list[index]);
	state->repo->commit(view, NULL);
	(void)state->repo->destroy_resource(state->repository, list[index], NULL);
}

static void props_ops_move_component(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index, i32 delta) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	const sk_rid_t* list;
	u32 count = 0u;
	sk_rid_t items[SK_EDITOR_PROPERTIES_MAX_COMPONENTS];
	sk_resource_object_t view;
	u32 i;
	i32 target;
	(void)app_context;
	(void)app_api;
	if (state == NULL || state->kind != SK_EDITOR_PROPERTIES_ENTITY || state->repo == NULL || state->repository == NULL) {
		return;
	}
	list = props_components(state, state->selected_rid, &count);
	if (index >= count || count == 0u || count > SK_EDITOR_PROPERTIES_MAX_COMPONENTS) {
		return;
	}
	target = (i32)index + delta;
	if (target < 0 || target >= (i32)count) {
		return;
	}
	for (i = 0u; i < count; ++i) {
		items[i] = list[i];
	}
	{
		sk_rid_t tmp = items[index];
		items[index] = items[(u32)target];
		items[(u32)target] = tmp;
	}
	view = state->repo->write(state->repository, state->selected_rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return;
	}
	(void)state->repo->set_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, items, count);
	state->repo->commit(view, NULL);
	if (state->selected_component_index == index) {
		state->selected_component_index = (u32)target;
	} else if (state->selected_component_index == (u32)target) {
		state->selected_component_index = index;
	}
}

static void props_ops_reset_component(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->reset_component_index = index; /* MOCK: records the request */
	}
}

static u32 props_ops_consume_reset_component(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	u32 idx;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return 0xFFFFFFFFu;
	}
	idx = state->reset_component_index;
	state->reset_component_index = 0xFFFFFFFFu;
	return idx;
}

static u32 props_ops_get_selected_component_index(const sk_editor_window_t* window) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	return state != NULL ? state->selected_component_index : 0xFFFFFFFFu;
}

static void props_ops_set_selected_component_index(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->selected_component_index = index;
	}
}

static const_chr_t props_ops_get_asset_name(const sk_editor_window_t* window) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	return state != NULL && state->asset_rename[0] != '\0' ? state->asset_rename : "(asset)";
}

static i32 props_ops_get_asset_uuid(const sk_editor_window_t* window, char* out, u32 cap) {
	return props_ops_get_entity_uuid(window, ((const properties_state_t*)window->user_data) != NULL ? ((const properties_state_t*)window->user_data)->selected_rid : SK_RID_ZERO,
									 out, cap);
}

static void props_ops_rename_asset(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t name) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && name != NULL) {
		(void)snprintf(state->asset_rename, sizeof(state->asset_rename), "%s", name); /* MOCK */
	}
}

static i32 props_ops_has_import_settings(const sk_editor_window_t* window) {
	/* MOCK: no v2 asset cook/import-settings pipeline yet — always false so
	 * the in-scope generic fields are what the UI shows. */
	(void)window;
	return 0;
}

static void props_ops_apply_import_settings(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->apply_import_pending = 1; /* MOCK */
	}
}

static void props_ops_reimport_asset(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->reimport_pending = 1; /* MOCK */
	}
}

static i32 props_ops_consume_apply_import(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	i32 v;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return 0;
	}
	v = state->apply_import_pending;
	state->apply_import_pending = 0;
	return v;
}

static i32 props_ops_consume_reimport(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	properties_state_t* state = (properties_state_t*)window->user_data;
	i32 v;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return 0;
	}
	v = state->reimport_pending;
	state->reimport_pending = 0;
	return v;
}

static u32 props_ops_get_field_count(const sk_editor_window_t* window) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	const sk_resource_type_t* type;
	sk_rid_t rid;
	if (state == NULL || state->repo == NULL || state->repository == NULL) {
		return 0u;
	}
	if (state->kind != SK_EDITOR_PROPERTIES_ASSET && state->kind != SK_EDITOR_PROPERTIES_RESOURCE) {
		return 0u;
	}
	rid = state->selected_rid;
	type = state->repo->resource_type(state->repository, rid);
	return type != NULL ? state->repo->type_field_count(type) : 0u;
}

static const sk_resource_field_t* props_ops_get_field_at(const sk_editor_window_t* window, u32 index) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	const sk_resource_type_t* type;
	sk_rid_t rid;
	if (state == NULL || state->repo == NULL || state->repository == NULL) {
		return NULL;
	}
	if (state->kind != SK_EDITOR_PROPERTIES_ASSET && state->kind != SK_EDITOR_PROPERTIES_RESOURCE) {
		return NULL;
	}
	rid = state->selected_rid;
	type = state->repo->resource_type(state->repository, rid);
	return type != NULL ? state->repo->type_field_at(type, index) : NULL;
}

static i32 props_ops_get_field_value(const sk_editor_window_t* window, u32 index, char* out, u32 cap) {
	const properties_state_t* state = (const properties_state_t*)window->user_data;
	const sk_resource_type_t* type;
	const sk_resource_field_t* f;
	if (out == NULL || cap == 0u || state == NULL || state->repo == NULL || state->repository == NULL) {
		return -1;
	}
	if (state->kind != SK_EDITOR_PROPERTIES_ASSET && state->kind != SK_EDITOR_PROPERTIES_RESOURCE) {
		return -1;
	}
	type = state->repo->resource_type(state->repository, state->selected_rid);
	if (type == NULL) {
		return -1;
	}
	f = state->repo->type_field_at(type, index);
	if (f == NULL) {
		return -1;
	}
	props_field_value_text(state, state->selected_rid, f, out, cap);
	return 0;
}

static const sk_editor_properties_ops_t properties_ops = {
	properties_open,
	props_ops_set_scene,
	props_ops_get_kind,
	props_ops_get_selected_rid,
	props_ops_clear_selection,
	props_ops_get_entity_name,
	props_ops_rename_entity,
	props_ops_get_entity_uuid,
	props_ops_get_entity_layer,
	props_ops_set_entity_layer,
	props_ops_get_component_count,
	props_ops_component_name_at,
	props_ops_component_rid_at,
	props_ops_add_component,
	props_ops_remove_component,
	props_ops_move_component,
	props_ops_reset_component,
	props_ops_consume_reset_component,
	props_ops_get_selected_component_index,
	props_ops_set_selected_component_index,
	props_ops_get_asset_name,
	props_ops_get_asset_uuid,
	props_ops_rename_asset,
	props_ops_has_import_settings,
	props_ops_apply_import_settings,
	props_ops_reimport_asset,
	props_ops_consume_apply_import,
	props_ops_consume_reimport,
	props_ops_get_field_count,
	props_ops_get_field_at,
	props_ops_get_field_value,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_properties_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	properties_class_state_t* cls = properties_class(app_context, app_api);
	if (cls != NULL) {
		properties_window.user_data = cls;
		return;
	}
	cls = (properties_class_state_t*)alloc->alloc(alloc->instance, sizeof(properties_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_PROPERTIES_STATE_TYPE_ID, cls);

	properties_window.type_id = SK_EDITOR_WINDOW_PROPERTIES;
	properties_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &properties_window);
	app_api->add_impl(app_context, SK_EDITOR_PROPERTIES_OPS_TYPE_ID, &properties_ops);
}

void sk_editor_properties_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	properties_class_state_t* cls = properties_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &properties_window);
	app_api->remove_impl(app_context, SK_EDITOR_PROPERTIES_OPS_TYPE_ID, &properties_ops);
	app_api->set_api(app_context, SK_EDITOR_PROPERTIES_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (properties_window.user_data == cls) {
		properties_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

/* Ops-table path (no ui): selection flows through the notify.h observers
 * (filtered by workspace id), entity name/layer + component add/remove/move
 * mutate the attached repository, and the generic field reads format real
 * values. */
SK_TEST(editor_properties_window_ops_selection_and_components) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_properties_ops_t* ops;
	sk_editor_window_t* window;
	const sk_repository_api_t* repo = sk_test_repository_table();
	const sk_resource_type_t* entity_type;
	const sk_resource_type_t* scene_type;
	const sk_resource_type_t* cam_type;
	const sk_resource_type_t* light_type;
	sk_repository_t* repository;
	sk_rid_t scene;
	sk_rid_t player;
	sk_resource_object_t view;
	sk_rid_t roots[1];
	char buf[160];

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	sk_editor_properties_register(app, boot.api);
	sk_editor_properties_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_PROPERTIES_OPS_TYPE_ID));

	ops = sk_editor_properties_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Properties", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PROPERTIES));
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_RIGHT_BOTTOM, window->dock_position);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_GRAPH));

	/* Nothing selected yet. */
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_NONE, (int)ops->get_kind(window));

	/* Build a scene with one entity + one camera component. */
	repository = repo->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repository);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repository, repo));
	scene_type = repo->find_type(repository, SK_SCENE_RESOURCE_TYPE_ID);
	entity_type = repo->find_type(repository, SK_ENTITY_RESOURCE_TYPE_ID);
	cam_type = repo->find_type(repository, SK_CAMERA_COMPONENT_TYPE_ID);
	light_type = repo->find_type(repository, SK_LIGHT_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(scene_type);
	TEST_ASSERT_NOT_NULL(entity_type);
	TEST_ASSERT_NOT_NULL(cam_type);
	scene = repo->create_resource(repository, scene_type, SK_UUID_ZERO, NULL);
	player = repo->create_resource(repository, entity_type, SK_UUID_ZERO, NULL);
	view = repo->write(repository, player);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, repo->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, "Player"));
	{
		sk_rid_t comp = repo->create_resource(repository, cam_type, SK_UUID_ZERO, NULL);
		sk_rid_t comps[1];
		comps[0] = comp;
		TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, comps, 1u));
	}
	repo->commit(view, NULL);
	view = repo->write(repository, scene);
	roots[0] = player;
	TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(view, SK_SCENE_RESOURCE_FIELD_ROOTS, roots, 1u));
	repo->commit(view, NULL);
	ops->set_scene(app, boot.api, window, repository, scene);

	/* Entity selection flows through the notify observer (Scene workspace). */
	sk_editor_notify_entity_selection(app, boot.api, SK_EDITOR_WORKSPACE_SCENE, player);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_ENTITY, (int)ops->get_kind(window));
	TEST_ASSERT_TRUE(ops->get_selected_rid(window).id == player.id);
	TEST_ASSERT_EQUAL_STRING("Player", ops->get_entity_name(window, player));
	TEST_ASSERT_EQUAL_INT(0, ops->get_entity_uuid(window, player, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_STRING("-", buf);

	/* Entity rename is a real repo write. */
	ops->rename_entity(app, boot.api, window, player, "Player Renamed");
	TEST_ASSERT_EQUAL_STRING("Player Renamed", ops->get_entity_name(window, player));

	/* Component surface: 1 camera component, name via repo type name. */
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_component_count(window));
	TEST_ASSERT_EQUAL_STRING("CameraResource", ops->component_name_at(window, 0u));
	TEST_ASSERT_TRUE(ops->component_rid_at(window, 0u).id != 0u);

	/* Add a light component (registered builtin type) + list reorder. */
	{
		sk_rid_t comp = ops->add_component(app, boot.api, window, repo->type_name(light_type));
		TEST_ASSERT_TRUE(comp.id != 0u);
	}
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_component_count(window));
	ops->move_component(app, boot.api, window, 1u, -1);
	TEST_ASSERT_EQUAL_STRING("LightResource", ops->component_name_at(window, 0u));
	ops->move_component(app, boot.api, window, 0u, 1);
	TEST_ASSERT_EQUAL_STRING("CameraResource", ops->component_name_at(window, 0u));

	/* Reset records a MOCK flag; remove is a real list + destroy op. */
	ops->set_selected_component_index(app, boot.api, window, 0u);
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_selected_component_index(window));
	ops->reset_component(app, boot.api, window, 0u);
	TEST_ASSERT_EQUAL_UINT(0u, ops->consume_reset_component(app, boot.api, window));
	TEST_ASSERT_EQUAL_UINT(0xFFFFFFFFu, ops->consume_reset_component(app, boot.api, window));
	{
		sk_rid_t removed = ops->component_rid_at(window, 0u);
		ops->remove_component(app, boot.api, window, 0u);
		TEST_ASSERT_EQUAL_UINT(1u, ops->get_component_count(window));
		TEST_ASSERT_FALSE(removed.id == ops->component_rid_at(window, 0u).id);
	}

	/* Layer is MOCK session state. */
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_entity_layer(window, player));
	ops->set_entity_layer(app, boot.api, window, player, 2u);
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_entity_layer(window, player));

	/* Deselection clears (notify observer). */
	sk_editor_notify_entity_deselection(app, boot.api, SK_EDITOR_WORKSPACE_SCENE, player);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_NONE, (int)ops->get_kind(window));

	/* Asset selection: kind + generic field read. */
	sk_editor_notify_asset_selection(app, boot.api, SK_EDITOR_WORKSPACE_SCENE, player);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_ASSET, (int)ops->get_kind(window));
	TEST_ASSERT_TRUE(ops->get_field_count(window) >= 3u);
	TEST_ASSERT_EQUAL_INT(0, ops->get_field_value(window, 0u, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_STRING("Player Renamed", buf);
	TEST_ASSERT_EQUAL_STRING("(asset)", ops->get_asset_name(window));
	ops->rename_asset(app, boot.api, window, "MyTex.png");
	TEST_ASSERT_EQUAL_STRING("MyTex.png", ops->get_asset_name(window));
	TEST_ASSERT_EQUAL_INT(0, ops->consume_reimport(app, boot.api, window));
	ops->reimport_asset(app, boot.api, window);
	TEST_ASSERT_EQUAL_INT(1, ops->consume_reimport(app, boot.api, window));

	/* Resource selection kind. */
	sk_editor_notify_resource_selection(app, boot.api, SK_EDITOR_WORKSPACE_SCENE, scene);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_RESOURCE, (int)ops->get_kind(window));

	/* Workspace filter: a different workspace id is ignored. */
	sk_editor_notify_entity_selection(app, boot.api, SK_EDITOR_WORKSPACE_GRAPH, player);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_RESOURCE, (int)ops->get_kind(window));

	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PROPERTIES));
	TEST_ASSERT_NOT_NULL(sk_editor_properties_ops(app, boot.api));

	repo->destroy(repository);
	sk_editor_properties_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_properties_ops(app, boot.api));
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
