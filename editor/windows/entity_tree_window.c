/**
 * @file entity_tree_window.c
 * @brief Scene hierarchy / Entity Tree window (APX-370): v2 migration.
 *
 * Port of main-branch Skore::EntityTreeWindow (migration manifest §3.3) onto
 * the v2 editor shell. The window lists one scene (or the clearly-marked MOCK
 * scene when none is attached) as a hierarchical entity tree backed by the v2
 * entity layer (sk.scene_resource Roots -> sk.entity_resource Children /
 * Name / Components payloads, resource_asset_builtins.h):
 *
 *  - widget_tree item binding gives expand/collapse arrows, row selection
 *    state, and per-row identity that survives relabel/reparent/rebuild.
 *  - Row activation selects (ctrl toggles via the window root capture);
 *    clicking the empty tree area clears. Selection changes publish the
 *    notify.h observers (SELECTION_CHANGED + ENTITY_SELECTION /
 *    ENTITY_DESELECTION) so the inspector can react.
 *  - F2 / the Rename menu arms an in-row rename overlay; the commit applies
 *    set_string on the entity Name field and publishes ENTITY_RENAMED.
 *  - The toolbar "+" / right-click context menu carries Create Entity /
 *    Create Entity From Asset / Rename / Duplicate / Delete / Show Scene
 *    Entity / Show Resource Inspector (plus the dead prototype-override
 *    rows, mirroring the C++ where CheckIsOverride always returns false).
 *    Create = create_resource + add to the parent Children (or scene Roots),
 *    Duplicate = repository clone (deep), Delete = destroy_resource (the
 *    repository detaches from the parent and cascades sub-objects); each
 *    publishes its notify observer.
 *  - Every non-root row is a drag source and every row a drop target
 *    (SK_UI_ENTITY_PAYLOAD); dropping reparents the dragged entity under the
 *    hovered row (the scene root row = scene Roots), with a cycle guard.
 *    Publishes ENTITY_REPARENTED. The C++ between-row reorder bands
 *    (DrawMovePayload / MoveSelectedBefore) are not ported: reorder is
 *    outside the APX-370 surface (create/delete/reparent).
 *
 * MOCK sections (the v2 entity layer cannot answer these yet, clearly
 * marked):
 *  - Active (visible) + Locked toggles: v2 EntityResource has no
 *    Deactivated / Locked fields, so both live in session-only window state
 *    (per-rid et_toggle_t), never written to the payload.
 *  - Create Entity From Asset: the C++ opens a resource-selection popup; v2
 *    has no scene-asset picker yet, so the action records a test-visible
 *    flag instead.
 *  - "Show Scene Entity" (debug toggle): v2 has no live scene/entity runtime
 *    hierarchy, so the toggle only flips the session flag (the tree stays on
 *    the RID path).
 *  - Show Resource Inspector routes through the Resource Debugger ops table
 *    (`inspect_resource`) — never a direct symbol.
 *  - Double-click / second click of the already-selected row frames the
 *    entity through the Scene View ops table (`view_entity`).
 *
 * Public entry points follow the APX-365 pattern (window-table-pattern.md):
 * the window publishes one process-lifetime `sk_editor_entity_tree_ops_t`
 * table registered with `app_api->add_impl` under
 * SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID — never exported as free symbols.
 */

#include "entity_tree_window.h"

#include "allocator.h"
#include "editor_api.h"
#include "editor_shell.h"
#include "notify.h"
#include "resource_asset_builtins.h"
#include "properties_window.h"
#include "resource_debugger_window.h"
#include "scene_view_window.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SK_EDITOR_ENTITY_TREE_STATE_TYPE_ID SK_TYPE_ID("sk.editor.entity_tree.state", 0x7c3555ce2b06dbf3ULL, 0xb029aa13618a5f8fULL)

#define ET_NAME_CAP 256u
#define ET_TREE_CAP 1024u
#define ET_SELECTED_CAP 64u
#define ET_TOGGLE_CAP 512u
#define ET_MENU_CAP 32u
#define ET_MOCK_CAP 64u
#define ET_CHILD_CHUNK 64u
#define ET_ROW_H 22.0f
#define ET_TOOLBAR_H 28.0f

/* Mock scene entity ids: high 32 bits spell "MOCK" so real repository RIDs
 * (dense page indexes, small) never collide with them. */
#define ET_MOCK_BASE 0x4d4f434b00000000ull

/* Platform key code for F2 (platform_window.h: SK_KEY_F1 + 1). The UI layer
 * passes host-defined key codes through unchanged. */
#define ET_KEY_F2 1101

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct et_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	sk_editor_menu_item_desc_t menus[ET_MENU_CAP];
	u32 menu_count;
} et_class_state_t;

/* One mock scene entity. parent 0 = scene root. */
typedef struct et_mock_entity_t {
	u64 id;
	u64 parent;
	i32 alive;
	u8 _pad0[4];
	char name[ET_NAME_CAP];
} et_mock_entity_t;

/* Session-only active/locked state (MOCK; not written to the payload). */
typedef struct et_toggle_t {
	u64 id;
	i32 active;
	i32 locked;
	u8 _pad0[4];
} et_toggle_t;

typedef struct et_menu_binding_t {
	sk_ui_node_t node;
	u32 item_index;
} et_menu_binding_t;

typedef struct et_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* Borrowed scene (host owns; NULL → mock scene). */
	sk_repository_t* repository;
	sk_rid_t scene_rid;
	i32 mock_scene;

	/* Selection. */
	sk_rid_t selected[ET_SELECTED_CAP];
	u32 selected_count;
	sk_rid_t last_selected;
	u32 workspace_id;

	/* Rename. */
	sk_rid_t rename_entity;
	i32 rename_armed;

	/* Search filter (C++ searchEntity). */
	char search[ET_NAME_CAP];

	/* Mock scene model (seeded lazily; supports the full ops surface). */
	et_mock_entity_t mock[ET_MOCK_CAP];
	u32 mock_count;
	u64 mock_seq;

	/* Active/locked session state (MOCK). */
	et_toggle_t toggles[ET_TOGGLE_CAP];
	u32 toggle_count;

	/* Tree item storage (labels live in tree_names; item labels point into it). */
	sk_ui_item_t tree_items[ET_TREE_CAP];
	char tree_names[ET_TREE_CAP][ET_NAME_CAP];
	sk_rid_t tree_rids[ET_TREE_CAP];
	u32 tree_count;
	sk_ui_item_array_t tree_arr;
	u32 listing_revision;
	u32 applied_revision;
	u32 selection_revision;
	u32 applied_selection;

	/* Reveal target (open ancestors + select after create/duplicate). */
	sk_rid_t reveal_rid;

	/* MOCK recorders (test-visible). */
	i32 last_create_from_asset;

	/* Session flags (C++ members). */
	i32 show_scene_entity;
	i32 read_only;

	/* UI handles (live only while a ui context hosts the dock chrome). */
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
	sk_ui_node_t root;
	sk_ui_node_t toolbar;
	sk_ui_node_t add_btn;
	sk_ui_node_t search_input;
	sk_ui_node_t tree_scroll;
	sk_ui_node_t tree_content;
	sk_ui_node_t tree;
	sk_ui_node_t context_menu;
	sk_ui_node_t rename_input;
	u32 ui_built;

	/* Per-frame interaction scratch (set by input callbacks, consumed in
	 * et_handle_interactions, then cleared). */
	u64 clicked_entity_id;
	u32 left_down;
	u32 right_down;
	u32 last_click_mods;
	f32 pointer_x;
	f32 pointer_y;

	et_menu_binding_t ctx_bindings[ET_MENU_CAP + 4u];
	u32 ctx_binding_count;
} et_state_t;

static sk_rid_t et_root_of(const et_state_t* state);
static void et_on_tree_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user);
static void et_build_context_menu(et_state_t* state);
static void et_open_ancestors(et_state_t* state, u64 item_id);

static void et_init(sk_editor_window_t* window);
static void et_draw(sk_editor_window_t* window, i32* open);
static void et_destroy(sk_editor_window_t* window);

static sk_editor_window_t et_window = {
	.title = "Entity Tree",
	.dock_id = "sk.editor_window.entity_tree",
	.dock_position = SK_EDITOR_DOCK_RIGHT_TOP,
	.order = 0,
	.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	.init = et_init,
	.draw = et_draw,
	.render = NULL,
	.destroy = et_destroy,
};

static et_class_state_t* et_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (et_class_state_t*)app_api->get_api(app_context, SK_EDITOR_ENTITY_TREE_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Mock scene model                                                   */
/* ------------------------------------------------------------------ */

/* Mock scene: root "Demo Scene" with Main Camera / Directional Light /
 * Player (locked) roots and a Character Mesh under Player (inactive). */
static void et_mock_seed(et_state_t* state) {
	static const struct {
		const_chr_t name;
		u64 parent; /* 0 = scene root */
		i32 active;
		i32 locked;
	} seed[] = {
		{"Demo Scene", 0ull, 1, 0}, {"Main Camera", 0ull, 1, 0}, {"Directional Light", 0ull, 1, 0}, {"Player", 0ull, 1, 1}, {"Character Mesh", ET_MOCK_BASE | 4ull, 0, 0},
	};
	u32 i;
	state->mock_count = 0u;
	state->mock_seq = 0u;
	for (i = 0u; i < (u32)(sizeof(seed) / sizeof(seed[0])) && state->mock_count < ET_MOCK_CAP; ++i) {
		et_mock_entity_t* m = &state->mock[state->mock_count];
		m->id = ET_MOCK_BASE | ((u64)i + 1ull);
		m->parent = seed[i].parent;
		m->alive = 1;
		(void)snprintf(m->name, sizeof(m->name), "%s", seed[i].name);
		if (seed[i].active == 0 || seed[i].locked != 0) {
			if (state->toggle_count < ET_TOGGLE_CAP) {
				state->toggles[state->toggle_count].id = m->id;
				state->toggles[state->toggle_count].active = seed[i].active;
				state->toggles[state->toggle_count].locked = seed[i].locked;
				state->toggle_count++;
			}
		}
		state->mock_count++;
		state->mock_seq = (u64)i + 1ull;
	}
}

static i32 et_mock_index(et_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->mock_count; ++i) {
		if (state->mock[i].id == rid.id) {
			return (i32)i;
		}
	}
	return -1;
}

static u32 et_mock_children(et_state_t* state, sk_rid_t rid, sk_rid_t* out, u32 cap) {
	u32 i;
	u32 n = 0u;
	u64 root_id = ET_MOCK_BASE | 1ull;
	for (i = 0u; i < state->mock_count && n < cap; ++i) {
		u64 p = state->mock[i].parent;
		/* parent 0 = scene root in the mock model; never a child of itself. */
		if (state->mock[i].id != rid.id && (p == rid.id || (p == 0ull && rid.id == root_id))) {
			out[n++] = (sk_rid_t){state->mock[i].id};
		}
	}
	return n;
}

static sk_rid_t et_mock_parent(et_state_t* state, sk_rid_t rid) {
	i32 idx = et_mock_index(state, rid);
	if (idx < 0 || state->mock[idx].parent == 0ull) {
		return SK_RID_ZERO;
	}
	return (sk_rid_t){state->mock[idx].parent};
}

static void et_mock_remove(et_state_t* state, sk_rid_t rid) {
	i32 idx = et_mock_index(state, rid);
	u32 i;
	if (idx < 0) {
		return;
	}
	for (i = (u32)idx; i + 1u < state->mock_count; ++i) {
		state->mock[i] = state->mock[i + 1u];
	}
	state->mock_count--;
}

static sk_rid_t et_mock_add(et_state_t* state, u64 parent, const_chr_t name) {
	et_mock_entity_t* m;
	if (state->mock_count >= ET_MOCK_CAP) {
		return SK_RID_ZERO;
	}
	state->mock_seq++;
	m = &state->mock[state->mock_count++];
	m->id = ET_MOCK_BASE | state->mock_seq;
	m->parent = parent;
	m->alive = 1;
	(void)snprintf(m->name, sizeof(m->name), "%s", name != NULL ? name : "New Entity");
	return (sk_rid_t){m->id};
}

static sk_rid_t et_mock_create(et_state_t* state, sk_rid_t parent) {
	u64 p = (parent.id == 0u || SK_RID_EQ(parent, et_root_of(state))) ? 0ull : parent.id;
	return et_mock_add(state, p, "New Entity");
}

// NOLINTBEGIN(misc-no-recursion) -- entity trees recurse through Children.
static void et_mock_clone_into(et_state_t* state, u32 src_idx, u64 new_parent) {
	sk_rid_t children[ET_CHILD_CHUNK];
	char namebuf[ET_NAME_CAP];
	u32 n;
	u32 i;
	sk_rid_t child_rid;
	sk_rid_t created;
	if (src_idx >= state->mock_count) {
		return;
	}
	(void)snprintf(namebuf, sizeof(namebuf), "%s", state->mock[src_idx].name);
	created = et_mock_add(state, new_parent, namebuf);
	if (created.id == 0u) {
		return;
	}
	n = et_mock_children(state, (sk_rid_t){state->mock[src_idx].id}, children, ET_CHILD_CHUNK);
	for (i = 0u; i < n; ++i) {
		child_rid = children[i];
		et_mock_clone_into(state, (u32)et_mock_index(state, child_rid), created.id);
	}
}
// NOLINTEND(misc-no-recursion)

static sk_rid_t et_mock_duplicate(et_state_t* state, sk_rid_t rid) {
	i32 idx = et_mock_index(state, rid);
	sk_rid_t parent;
	if (idx < 0) {
		return SK_RID_ZERO;
	}
	parent = et_mock_parent(state, rid);
	et_mock_clone_into(state, (u32)idx, parent.id);
	return (sk_rid_t){ET_MOCK_BASE | state->mock_seq};
}

static void et_mock_delete(et_state_t* state, sk_rid_t rid) {
	sk_rid_t doomed[ET_TREE_CAP];
	u32 count = 0u;
	u32 i;
	sk_rid_t chunk[ET_CHILD_CHUNK];
	sk_rid_t stack[ET_TREE_CAP];
	u32 sp = 0u;

	stack[sp++] = rid;
	while (sp > 0u && count < ET_TREE_CAP) {
		sk_rid_t cur = stack[--sp];
		u32 n = et_mock_children(state, cur, chunk, ET_CHILD_CHUNK);
		u32 c;
		doomed[count++] = cur;
		for (c = 0u; c < n && sp < ET_TREE_CAP; ++c) {
			stack[sp++] = chunk[c];
		}
	}
	for (i = 0u; i < count; ++i) {
		et_mock_remove(state, doomed[i]);
	}
}

static void et_mock_reparent(et_state_t* state, sk_rid_t entity, sk_rid_t new_parent) {
	i32 idx = et_mock_index(state, entity);
	u64 p;
	u64 guard;
	i32 cycle = 0;
	if (idx < 0 || SK_RID_EQ(entity, et_root_of(state))) {
		return;
	}
	p = (new_parent.id == 0u || SK_RID_EQ(new_parent, et_root_of(state))) ? 0ull : new_parent.id;
	if (p == state->mock[idx].id) {
		return; /* drop on self */
	}
	/* Cycle guard: new_parent must not be the entity or one of its descendants. */
	guard = p;
	while (guard != 0ull) {
		i32 gidx = et_mock_index(state, (sk_rid_t){guard});
		if (gidx < 0) {
			break;
		}
		if (state->mock[gidx].id == state->mock[idx].id) {
			cycle = 1;
			break;
		}
		guard = state->mock[gidx].parent;
	}
	if (cycle != 0) {
		return;
	}
	state->mock[idx].parent = p;
	sk_editor_notify_entity_reparented(state->app_context, state->app_api, state->workspace_id, entity, (sk_rid_t){p});
}

/* ------------------------------------------------------------------ */
/*  Real entity-layer helpers                                          */
/* ------------------------------------------------------------------ */

static i32 et_has_project(const et_state_t* state) {
	return state->mock_scene == 0 && state->repository != NULL && state->scene_rid.id != 0u;
}

static const sk_repository_api_t* et_repo_api(const et_state_t* state) {
	return state->app_api->repository_api(state->app_context);
}

/* Children field of @p rid: scene payload → Roots, entity payload → Children. */
static u32 et_children_field(et_state_t* state, sk_rid_t rid) {
	const sk_resource_type_t* type = et_repo_api(state)->resource_type(state->repository, rid);
	if (type != NULL && SK_TYPE_ID_EQ(et_repo_api(state)->type_id(type), SK_SCENE_RESOURCE_TYPE_ID)) {
		return (u32)SK_SCENE_RESOURCE_FIELD_ROOTS;
	}
	return (u32)SK_ENTITY_RESOURCE_FIELD_CHILDREN;
}

static sk_rid_t et_root_of(const et_state_t* state) {
	if (state->mock_scene != 0) {
		return (sk_rid_t){ET_MOCK_BASE | 1ull};
	}
	return state->scene_rid;
}

static u32 et_children_of(et_state_t* state, sk_rid_t rid, sk_rid_t* out, u32 cap) {
	const sk_repository_api_t* repo;
	sk_resource_object_t view;
	u32 count = 0u;
	u32 n;
	u32 i;
	if (state->mock_scene != 0) {
		return et_mock_children(state, rid, out, cap);
	}
	if (!et_has_project(state)) {
		return 0u;
	}
	repo = et_repo_api(state);
	view = repo->read(state->repository, rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return 0u;
	}
	{
		const sk_rid_t* items = repo->get_subobject_list(view, et_children_field(state, rid), &count);
		n = count < cap ? count : cap;
		for (i = 0u; i < n; ++i) {
			out[i] = items[i];
		}
	}
	return n;
}

static const_chr_t et_name_of(et_state_t* state, sk_rid_t rid, char* out, u32 cap) {
	const sk_repository_api_t* repo;
	sk_resource_object_t view;
	const_chr_t name;
	if (state->mock_scene != 0) {
		i32 idx = et_mock_index(state, rid);
		if (idx >= 0) {
			return state->mock[idx].name;
		}
		(void)snprintf(out, cap, "%s", "Entity");
		return out;
	}
	repo = et_repo_api(state);
	view = repo->read(state->repository, rid);
	name = SK_RESOURCE_OBJECT_IS_VALID(view) ? repo->get_string(view, SK_ENTITY_RESOURCE_FIELD_NAME) : NULL;
	(void)snprintf(out, cap, "%s", name != NULL && name[0] != '\0' ? name : "Entity");
	return out;
}

static i32 et_has_children(et_state_t* state, sk_rid_t rid) {
	sk_rid_t probe[1];
	return et_children_of(state, rid, probe, 1u) > 0u ? 1 : 0;
}

static i32 et_ascii_lower(i32 c) {
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Case-insensitive substring match (ImGuiTextFilter subset). */
static i32 et_filter_pass(const_chr_t text, const_chr_t filter) {
	u32 n = filter != NULL ? (u32)strlen(filter) : 0u;
	const_chr_t t;
	if (n == 0u) {
		return 1;
	}
	if (text == NULL) {
		return 0;
	}
	for (t = text; *t != '\0'; ++t) {
		u32 i;
		i32 match = 1;
		for (i = 0u; i < n; ++i) {
			if (et_ascii_lower((unsigned char)t[i]) != et_ascii_lower((unsigned char)filter[i])) {
				match = 0;
				break;
			}
		}
		if (match != 0) {
			return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Tree rebuild                                                       */
/* ------------------------------------------------------------------ */

// NOLINTBEGIN(misc-no-recursion) -- entity trees recurse through Children.
static void et_tree_add_hier(et_state_t* state, sk_rid_t rid, u64 parent_id, u32 depth) {
	sk_rid_t chunk[ET_CHILD_CHUNK];
	char namebuf[ET_NAME_CAP];
	const_chr_t name;
	sk_ui_item_t* item;
	u32 n;
	u32 i;
	(void)depth;
	if (state->tree_count >= ET_TREE_CAP) {
		return;
	}
	name = et_name_of(state, rid, namebuf, (u32)sizeof(namebuf));
	item = &state->tree_items[state->tree_count];
	sk_ui_item_set(item, rid.id, parent_id, name, 0u);
	if (!et_has_children(state, rid)) {
		item->flags |= (u32)SK_UI_ITEM_FLAG_LEAF;
	}
	if (SK_RID_EQ(rid, et_root_of(state))) {
		item->flags |= (u32)SK_UI_ITEM_FLAG_OPEN; /* root starts expanded (SetNextItemOpen Once) */
	}
	(void)snprintf(state->tree_names[state->tree_count], ET_NAME_CAP, "%s", name);
	state->tree_rids[state->tree_count] = rid;
	state->tree_count++;

	n = et_children_of(state, rid, chunk, ET_CHILD_CHUNK);
	for (i = 0u; i < n; ++i) {
		et_tree_add_hier(state, chunk[i], rid.id, depth + 1u);
	}
}
// NOLINTEND(misc-no-recursion)

/* Filtered mode: flat list of matching entities as leaves (C++ filter mode
 * renders every row as ImGuiTreeLeaf). Non-matching parents are skipped but
 * their descendants stay reachable. */
// NOLINTBEGIN(misc-no-recursion)
static void et_tree_add_filtered(et_state_t* state, sk_rid_t rid, u64 parent_id) {
	sk_rid_t chunk[ET_CHILD_CHUNK];
	char namebuf[ET_NAME_CAP];
	const_chr_t name;
	i32 matches;
	u64 my_parent;
	u32 n;
	u32 i;
	name = et_name_of(state, rid, namebuf, (u32)sizeof(namebuf));
	matches = SK_RID_EQ(rid, et_root_of(state)) || et_filter_pass(name, state->search) != 0;
	my_parent = matches ? parent_id : 0ull;
	if (matches && state->tree_count < ET_TREE_CAP) {
		sk_ui_item_t* item = &state->tree_items[state->tree_count];
		sk_ui_item_set(item, rid.id, my_parent, name, (u32)SK_UI_ITEM_FLAG_LEAF);
		(void)snprintf(state->tree_names[state->tree_count], ET_NAME_CAP, "%s", name);
		state->tree_rids[state->tree_count] = rid;
		state->tree_count++;
	}
	n = et_children_of(state, rid, chunk, ET_CHILD_CHUNK);
	for (i = 0u; i < n; ++i) {
		et_tree_add_filtered(state, chunk[i], my_parent);
	}
}
// NOLINTEND(misc-no-recursion)

static void et_rebuild_tree(et_state_t* state) {
	if (state->mock_scene != 0 && state->mock_count == 0u) {
		et_mock_seed(state);
	}
	state->tree_count = 0u;
	if (state->search[0] != '\0') {
		et_tree_add_filtered(state, et_root_of(state), 0ull);
	} else {
		et_tree_add_hier(state, et_root_of(state), 0ull, 0u);
	}
	state->tree_arr.items = state->tree_items;
	state->tree_arr.count = state->tree_count;
	state->tree_arr.revision = state->listing_revision;
	state->listing_revision++;
}

/* ------------------------------------------------------------------ */
/*  Selection + observers                                              */
/* ------------------------------------------------------------------ */

static void et_mark_dirty(et_state_t* state) {
	if (state != NULL) {
		sk_editor_notify_dirty(state->app_context, state->app_api, state->workspace_id);
	}
}

/* C++ EntityTreeWindow::ShowResourceInspector → ResourceDebuggerWindow::InspectResource. */
static void et_inspect_resource(et_state_t* state, sk_rid_t rid) {
	const sk_editor_resource_debugger_ops_t* rd;
	sk_editor_window_t* dbg;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	rd = sk_editor_resource_debugger_ops(state->app_context, state->app_api);
	if (rd == NULL || rd->inspect_resource == NULL) {
		return;
	}
	dbg = rd->inspect_resource(state->app_context, state->app_api, rid);
	if (dbg != NULL && rd->set_repository != NULL && state->repository != NULL) {
		rd->set_repository(state->app_context, state->app_api, dbg, state->repository);
	}
}

/* C++ EntityTreeWindow double-click → SceneViewWindow::ViewEntity. */
static void et_frame_in_scene_view(et_state_t* state, sk_rid_t rid) {
	const sk_editor_scene_view_ops_t* sv;
	sk_editor_window_t* win;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	sv = sk_editor_scene_view_ops(state->app_context, state->app_api);
	if (sv == NULL) {
		return;
	}
	win = sv->open != NULL ? sv->open(state->app_context, state->app_api) : NULL;
	if (win != NULL && sv->view_entity != NULL) {
		sv->view_entity(state->app_context, state->app_api, win, rid);
	}
}

static i32 et_selected_contains(const et_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->selected_count; ++i) {
		if (SK_RID_EQ(state->selected[i], rid)) {
			return 1;
		}
	}
	return 0;
}

static void et_clear_selection_internal(et_state_t* state) {
	state->selected_count = 0u;
	state->last_selected = SK_RID_ZERO;
}

static void et_deselect_all_notify(et_state_t* state, sk_rid_t except) {
	u32 i;
	for (i = 0u; i < state->selected_count; ++i) {
		if (state->selected[i].id != 0u && !SK_RID_EQ(state->selected[i], except)) {
			sk_editor_notify_entity_deselection(state->app_context, state->app_api, state->workspace_id, state->selected[i]);
		}
	}
}

static void et_select_exclusive(et_state_t* state, sk_rid_t rid) {
	if (state->selected_count == 1u && SK_RID_EQ(state->selected[0], rid)) {
		/* Already the only selection: still refresh the widget revision. */
		state->selection_revision++;
		return;
	}
	et_deselect_all_notify(state, rid);
	et_clear_selection_internal(state);
	state->selected[state->selected_count++] = rid;
	state->last_selected = rid;
	sk_editor_notify_entity_selection(state->app_context, state->app_api, state->workspace_id, rid);
	sk_editor_notify_selection_changed(state->app_context, state->app_api);
	state->selection_revision++;
}

static void et_toggle_select(et_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->selected_count; ++i) {
		if (SK_RID_EQ(state->selected[i], rid)) {
			state->selected[i] = state->selected[state->selected_count - 1u];
			state->selected_count--;
			if (SK_RID_EQ(state->last_selected, rid)) {
				state->last_selected = state->selected_count > 0u ? state->selected[0] : SK_RID_ZERO;
			}
			sk_editor_notify_entity_deselection(state->app_context, state->app_api, state->workspace_id, rid);
			sk_editor_notify_selection_changed(state->app_context, state->app_api);
			state->selection_revision++;
			return;
		}
	}
	if (state->selected_count < ET_SELECTED_CAP) {
		state->selected[state->selected_count++] = rid;
		state->last_selected = rid;
		sk_editor_notify_entity_selection(state->app_context, state->app_api, state->workspace_id, rid);
		sk_editor_notify_selection_changed(state->app_context, state->app_api);
		state->selection_revision++;
	}
}

/* Drop @p rid (and descendants) from the selection after a delete. */
static void et_deselect_subtree(et_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->selected_count;) {
		if (SK_RID_EQ(state->selected[i], rid)) {
			state->selected[i] = state->selected[state->selected_count - 1u];
			state->selected_count--;
		} else {
			i++;
		}
	}
	if (SK_RID_EQ(state->last_selected, rid)) {
		state->last_selected = state->selected_count > 0u ? state->selected[0] : SK_RID_ZERO;
	}
	state->selection_revision++;
}

/* ------------------------------------------------------------------ */
/*  Structure ops (real repository)                                    */
/* ------------------------------------------------------------------ */

// NOLINTBEGIN(misc-no-recursion) -- entity trees recurse through Children.
static void et_collect_subtree(et_state_t* state, sk_rid_t rid, sk_rid_t* out, u32 cap, u32* count) {
	sk_rid_t chunk[ET_CHILD_CHUNK];
	u32 n;
	u32 i;
	if (*count >= cap) {
		return;
	}
	out[(*count)++] = rid;
	n = et_children_of(state, rid, chunk, ET_CHILD_CHUNK);
	for (i = 0u; i < n; ++i) {
		et_collect_subtree(state, chunk[i], out, cap, count);
	}
}
// NOLINTEND(misc-no-recursion)

static sk_rid_t et_create_entity_internal(et_state_t* state, sk_rid_t parent, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo;
	const sk_resource_type_t* type;
	sk_rid_t rid;
	sk_rid_t target;
	sk_resource_object_t view;
	if (state->mock_scene != 0) {
		rid = et_mock_create(state, parent);
		if (rid.id != 0u) {
			sk_editor_notify_entity_created(state->app_context, state->app_api, state->workspace_id, rid);
			et_mark_dirty(state);
			et_select_exclusive(state, rid);
			et_rebuild_tree(state);
			state->reveal_rid = rid;
		}
		return rid;
	}
	if (!et_has_project(state)) {
		return SK_RID_ZERO;
	}
	repo = et_repo_api(state);
	type = repo->find_type(state->repository, SK_ENTITY_RESOURCE_TYPE_ID);
	if (type == NULL) {
		return SK_RID_ZERO;
	}
	rid = repo->create_resource(state->repository, type, SK_UUID_ZERO, scope);
	if (rid.id == 0u) {
		return SK_RID_ZERO;
	}
	view = repo->write(state->repository, rid);
	if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
		repo->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, "New Entity");
		repo->commit(view, scope);
	}
	target = (parent.id == 0u || SK_RID_EQ(parent, et_root_of(state))) ? et_root_of(state) : parent;
	view = repo->write(state->repository, target);
	if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
		repo->add_to_subobject_list(view, et_children_field(state, target), rid);
		repo->commit(view, scope);
	}
	sk_editor_notify_entity_created(state->app_context, state->app_api, state->workspace_id, rid);
	et_mark_dirty(state);
	et_select_exclusive(state, rid);
	et_rebuild_tree(state);
	state->reveal_rid = rid;
	return rid;
}

static void et_rename_entity_internal(et_state_t* state, sk_rid_t rid, const_chr_t new_name, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo;
	sk_resource_object_t view;
	const_chr_t name = new_name != NULL ? new_name : "";
	if (rid.id == 0u) {
		return;
	}
	if (state->mock_scene != 0) {
		i32 idx = et_mock_index(state, rid);
		if (idx < 0) {
			return;
		}
		(void)snprintf(state->mock[idx].name, sizeof(state->mock[idx].name), "%s", name);
		sk_editor_notify_entity_renamed(state->app_context, state->app_api, state->workspace_id, rid, state->mock[idx].name);
		et_mark_dirty(state);
		et_rebuild_tree(state);
		return;
	}
	if (!et_has_project(state)) {
		return;
	}
	repo = et_repo_api(state);
	view = repo->write(state->repository, rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return;
	}
	repo->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, name);
	repo->commit(view, scope);
	sk_editor_notify_entity_renamed(state->app_context, state->app_api, state->workspace_id, rid, name);
	et_mark_dirty(state);
	et_rebuild_tree(state);
}

static sk_rid_t et_duplicate_entity_internal(et_state_t* state, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo;
	sk_rid_t clone;
	sk_rid_t target;
	sk_resource_object_t view;
	if (rid.id == 0u || SK_RID_EQ(rid, et_root_of(state))) {
		return SK_RID_ZERO;
	}
	if (state->mock_scene != 0) {
		clone = et_mock_duplicate(state, rid);
		if (clone.id != 0u) {
			sk_editor_notify_entity_created(state->app_context, state->app_api, state->workspace_id, clone);
			et_mark_dirty(state);
			et_select_exclusive(state, clone);
			et_rebuild_tree(state);
			state->reveal_rid = clone;
		}
		return clone;
	}
	if (!et_has_project(state)) {
		return SK_RID_ZERO;
	}
	repo = et_repo_api(state);
	clone = repo->clone(state->repository, rid, SK_UUID_ZERO, scope);
	if (clone.id == 0u) {
		return SK_RID_ZERO;
	}
	target = et_repo_api(state)->get_parent(state->repository, rid);
	if (target.id == 0u) {
		target = et_root_of(state);
	}
	view = repo->write(state->repository, target);
	if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
		repo->add_to_subobject_list(view, et_children_field(state, target), clone);
		repo->commit(view, scope);
	}
	sk_editor_notify_entity_created(state->app_context, state->app_api, state->workspace_id, clone);
	et_mark_dirty(state);
	et_select_exclusive(state, clone);
	et_rebuild_tree(state);
	state->reveal_rid = clone;
	return clone;
}

static void et_delete_entity_internal(et_state_t* state, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	sk_rid_t doomed[ET_TREE_CAP];
	u32 count = 0u;
	u32 i;
	if (rid.id == 0u || SK_RID_EQ(rid, et_root_of(state))) {
		return; /* the scene root cannot be deleted from the tree */
	}
	et_collect_subtree(state, rid, doomed, ET_TREE_CAP, &count);
	if (state->mock_scene != 0) {
		et_mock_delete(state, rid);
	} else {
		if (!et_has_project(state)) {
			return;
		}
		(void)et_repo_api(state)->destroy_resource(state->repository, rid, scope); /* cascades into children */
	}
	for (i = 0u; i < count; ++i) {
		sk_editor_notify_entity_deleted(state->app_context, state->app_api, state->workspace_id, doomed[i]);
		et_deselect_subtree(state, doomed[i]);
	}
	if (count > 0u) {
		et_mark_dirty(state);
	}
	/* Drop deleted ids from the session toggle state. */
	for (i = 0u; i < count; ++i) {
		u32 t;
		for (t = 0u; t < state->toggle_count;) {
			if (state->toggles[t].id == doomed[i].id) {
				state->toggles[t] = state->toggles[state->toggle_count - 1u];
				state->toggle_count--;
			} else {
				t++;
			}
		}
	}
	et_rebuild_tree(state);
}

static void et_reparent_entity_internal(et_state_t* state, sk_rid_t entity, sk_rid_t new_parent, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo;
	sk_rid_t target;
	sk_rid_t old_parent;
	sk_resource_object_t view;
	if (entity.id == 0u || SK_RID_EQ(entity, et_root_of(state))) {
		return;
	}
	if (state->mock_scene != 0) {
		et_mock_reparent(state, entity, new_parent);
		et_mark_dirty(state);
		et_rebuild_tree(state);
		return;
	}
	if (!et_has_project(state)) {
		return;
	}
	repo = et_repo_api(state);
	target = (new_parent.id == 0u || SK_RID_EQ(new_parent, et_root_of(state))) ? et_root_of(state) : new_parent;
	if (SK_RID_EQ(target, entity)) {
		return; /* drop on self */
	}
	/* Cycle guard: entity must not be an ancestor of the new parent. */
	if (repo->is_parent_of(state->repository, entity, target) != 0) {
		return;
	}
	old_parent = repo->get_parent(state->repository, entity);
	if (SK_RID_EQ(old_parent, target)) {
		et_rebuild_tree(state);
		return; /* already there (e.g. root entity dropped on the scene root) */
	}
	if (old_parent.id != 0u) {
		view = repo->write(state->repository, old_parent);
		if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
			repo->remove_from_subobject_list(view, et_children_field(state, old_parent), entity);
			repo->commit(view, scope);
		}
	}
	view = repo->write(state->repository, target);
	if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
		repo->add_to_subobject_list(view, et_children_field(state, target), entity);
		repo->commit(view, scope);
	}
	sk_editor_notify_entity_reparented(state->app_context, state->app_api, state->workspace_id, entity, target);
	et_mark_dirty(state);
	et_rebuild_tree(state);
}

/* ------------------------------------------------------------------ */
/*  Active / locked session state (MOCK)                              */
/* ------------------------------------------------------------------ */

static et_toggle_t* et_toggle_find_mut(et_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->toggle_count; ++i) {
		if (state->toggles[i].id == rid.id) {
			return &state->toggles[i];
		}
	}
	return NULL;
}

/* Read-only toggle lookup (default active=1 / locked=0). */
static i32 et_toggle_lookup(const et_state_t* state, sk_rid_t rid, i32* out_active, i32* out_locked) {
	u32 i;
	for (i = 0u; i < state->toggle_count; ++i) {
		if (state->toggles[i].id == rid.id) {
			*out_active = state->toggles[i].active;
			*out_locked = state->toggles[i].locked;
			return 1;
		}
	}
	return 0;
}

static i32 et_is_active(const et_state_t* state, sk_rid_t rid) {
	i32 a;
	i32 l;
	return et_toggle_lookup(state, rid, &a, &l) != 0 ? a : 1;
}

static i32 et_is_locked(const et_state_t* state, sk_rid_t rid) {
	i32 a;
	i32 l;
	return et_toggle_lookup(state, rid, &a, &l) != 0 ? l : 0;
}

static void et_set_active(et_state_t* state, sk_rid_t rid, i32 active) {
	et_toggle_t* t = et_toggle_find_mut(state, rid);
	if (t == NULL) {
		if (state->toggle_count >= ET_TOGGLE_CAP) {
			return;
		}
		t = &state->toggles[state->toggle_count++];
		t->id = rid.id;
		t->active = et_is_active(state, rid);
		t->locked = et_is_locked(state, rid);
	}
	t->active = active != 0 ? 1 : 0;
}

static void et_set_locked(et_state_t* state, sk_rid_t rid, i32 locked) {
	et_toggle_t* t = et_toggle_find_mut(state, rid);
	if (t == NULL) {
		if (state->toggle_count >= ET_TOGGLE_CAP) {
			return;
		}
		t = &state->toggles[state->toggle_count++];
		t->id = rid.id;
		t->active = et_is_active(state, rid);
		t->locked = et_is_locked(state, rid);
	}
	t->locked = locked != 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Context menu (seed + checks + actions)                            */
/* ------------------------------------------------------------------ */

static i32 et_actionable_selection(const et_state_t* state) {
	u32 i;
	sk_rid_t root = et_root_of(state);
	if (state->selected_count == 0u) {
		return 0;
	}
	for (i = 0u; i < state->selected_count; ++i) {
		if (!SK_RID_EQ(state->selected[i], root)) {
			return 1;
		}
	}
	return 0;
}

static et_state_t* et_state_of(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	(void)app_context;
	(void)app_api;
	if (window == NULL) {
		return NULL;
	}
	return (et_state_t*)window->user_data;
}

/* C++ CheckSelectedEntity: rename/duplicate/delete enabled unless only the
 * scene root is selected. */
static i32 et_check_selected(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state == NULL) {
		sk_editor_window_t* w = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_ENTITY_TREE);
		state = w != NULL ? (et_state_t*)w->user_data : NULL;
	}
	if (state == NULL || state->read_only != 0) {
		return 0;
	}
	return et_actionable_selection(state);
}

/* C++ CheckEntityActions: entity create entries visible. */
static i32 et_check_entity_actions(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state == NULL) {
		sk_editor_window_t* w = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_ENTITY_TREE);
		state = w != NULL ? (et_state_t*)w->user_data : NULL;
	}
	return state != NULL && state->read_only == 0 ? 1 : 0;
}

/* C++ CheckIsOverride: always false (prototype overrides not ported). */
static i32 et_check_never(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	(void)app_context;
	(void)app_api;
	(void)window;
	(void)user;
	return 0;
}

static sk_rid_t et_menu_parent(et_state_t* state) {
	if (state->selected_count > 0u && !SK_RID_EQ(state->selected[0], et_root_of(state))) {
		return state->selected[0];
	}
	return et_root_of(state);
}

static void et_action_create_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state != NULL) {
		(void)et_create_entity_internal(state, et_menu_parent(state), NULL);
	}
}

static void et_action_create_from_asset(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	/* MOCK: C++ opens a resource-selection popup (openAssetSelectionPopup);
	 * v2 has no scene-asset picker yet, so the action records a flag. */
	if (state != NULL) {
		state->last_create_from_asset = 1;
	}
}

static void et_action_rename(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state != NULL && state->last_selected.id != 0u) {
		state->rename_entity = state->last_selected;
		state->rename_armed = 0;
	}
}

static void et_action_duplicate(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state != NULL) {
		(void)et_duplicate_entity_internal(state, state->last_selected, NULL);
	}
}

static void et_action_delete(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	u32 i;
	(void)user;
	if (state == NULL) {
		return;
	}
	/* Delete the selection (snapshot first: the loop mutates it). */
	for (i = 0u; i < state->selected_count;) {
		sk_rid_t rid = state->selected[i];
		if (!SK_RID_EQ(rid, et_root_of(state))) {
			et_delete_entity_internal(state, rid, NULL);
		} else {
			i++;
		}
	}
}

static void et_action_show_scene_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state != NULL) {
		/* MOCK: v2 has no live scene/entity runtime hierarchy; the toggle only
		 * flips the session flag (C++ ShowSceneEntity). */
		state->show_scene_entity = !state->show_scene_entity;
	}
}

static void et_action_show_resource_inspector(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)user;
	if (state != NULL && state->selected_count > 0u) {
		et_inspect_resource(state, state->selected[0]);
	}
}

static void et_action_remove_override(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	(void)app_context;
	(void)app_api;
	(void)window;
	(void)user; /* dead in C++ (CheckIsOverride always false) */
}

static void et_action_add_back_instance(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	(void)app_context;
	(void)app_api;
	(void)window;
	(void)user; /* prototype removals not ported */
}

static void et_seed_class_menus(et_class_state_t* cls) {
	/* Order matches C++ RegisterType priorities (-100..1000). */
	cls->menus[cls->menu_count].item_name = "Revert Instance Overrides";
	cls->menus[cls->menu_count].order = -100;
	cls->menus[cls->menu_count].action = et_action_remove_override;
	cls->menus[cls->menu_count].visible = et_check_never;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Add Back This Instance";
	cls->menus[cls->menu_count].order = -95;
	cls->menus[cls->menu_count].action = et_action_add_back_instance;
	cls->menus[cls->menu_count].visible = et_check_never;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Create Entity";
	cls->menus[cls->menu_count].order = 0;
	cls->menus[cls->menu_count].action = et_action_create_entity;
	cls->menus[cls->menu_count].visible = et_check_entity_actions;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Create Entity From Asset";
	cls->menus[cls->menu_count].order = 15;
	cls->menus[cls->menu_count].action = et_action_create_from_asset;
	cls->menus[cls->menu_count].visible = et_check_entity_actions;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Rename";
	cls->menus[cls->menu_count].order = 200;
	cls->menus[cls->menu_count].action = et_action_rename;
	cls->menus[cls->menu_count].visible = et_check_selected;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Duplicate";
	cls->menus[cls->menu_count].order = 210;
	cls->menus[cls->menu_count].action = et_action_duplicate;
	cls->menus[cls->menu_count].visible = et_check_selected;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Delete";
	cls->menus[cls->menu_count].order = 220;
	cls->menus[cls->menu_count].action = et_action_delete;
	cls->menus[cls->menu_count].visible = et_check_selected;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Show Resource Inspector";
	cls->menus[cls->menu_count].order = 500;
	cls->menus[cls->menu_count].action = et_action_show_resource_inspector;
	cls->menu_count++;

	cls->menus[cls->menu_count].item_name = "Show Scene Entity";
	cls->menus[cls->menu_count].order = 1000;
	cls->menus[cls->menu_count].action = et_action_show_scene_entity;
	cls->menu_count++;
}

/* ------------------------------------------------------------------ */
/*  Ops table                                                          */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* et_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_ENTITY_TREE);
}

static void et_add_menu_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item) {
	et_class_state_t* cls = et_class(app_context, app_api);
	if (cls == NULL || item == NULL || item->item_name == NULL) {
		return;
	}
	if (cls->menu_count < ET_MENU_CAP) {
		cls->menus[cls->menu_count] = *item;
		cls->menu_count++;
	}
}

static void et_set_scene(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_repository_t* repository, sk_rid_t scene_rid) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return;
	}
	state->repository = repository;
	state->scene_rid = scene_rid;
	state->mock_scene = (repository == NULL || scene_rid.id == 0u) ? 1 : 0;
	state->rename_entity = SK_RID_ZERO;
	state->rename_armed = 0;
	et_clear_selection_internal(state);
	state->selection_revision++;
	et_rebuild_tree(state);
}

static void et_clear_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)scope;
	if (state == NULL) {
		return;
	}
	et_deselect_all_notify(state, SK_RID_ZERO);
	et_clear_selection_internal(state);
	sk_editor_notify_selection_changed(app_context, app_api);
	state->selection_revision++;
}

static void et_select_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, i32 clear_others, sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)scope;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	if (clear_others != 0) {
		et_select_exclusive(state, rid);
	} else {
		et_toggle_select(state, rid);
	}
}

static u32 et_get_selected(const sk_editor_window_t* window, sk_rid_t* out, u32 out_cap) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	u32 n;
	if (state == NULL) {
		return 0u;
	}
	n = state->selected_count < out_cap ? state->selected_count : out_cap;
	if (out != NULL) {
		u32 i;
		for (i = 0u; i < n; ++i) {
			out[i] = state->selected[i];
		}
	}
	return state->selected_count;
}

static sk_rid_t et_get_last_selected(const sk_editor_window_t* window) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? state->last_selected : SK_RID_ZERO;
}

static void et_set_rename_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)scope;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	et_select_exclusive(state, rid);
	state->rename_entity = rid;
	state->rename_armed = 0;
}

static void et_rename_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, const_chr_t new_name) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	if (state != NULL) {
		et_rename_entity_internal(state, rid, new_name, NULL);
	}
}

static sk_rid_t et_create_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t parent, sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	if (state == NULL) {
		return SK_RID_ZERO;
	}
	return et_create_entity_internal(state, parent, scope);
}

static void et_create_entity_from_asset(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->last_create_from_asset = 1; /* MOCK popup */
	}
}

static void et_duplicate_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	if (state != NULL) {
		(void)et_duplicate_entity_internal(state, state->last_selected, scope);
	}
}

static void et_delete_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	u32 i;
	if (state == NULL) {
		return;
	}
	for (i = 0u; i < state->selected_count;) {
		sk_rid_t rid = state->selected[i];
		if (!SK_RID_EQ(rid, et_root_of(state))) {
			et_delete_entity_internal(state, rid, scope);
		} else {
			i++;
		}
	}
}

static void et_reparent_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t entity, sk_rid_t new_parent,
							   sk_undo_redo_scope_t* scope) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	if (state != NULL) {
		et_reparent_entity_internal(state, entity, new_parent, scope);
	}
}

static void et_set_entity_active(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, i32 active) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		et_set_active(state, rid, active);
	}
}

static void et_set_entity_locked(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, i32 locked) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		et_set_locked(state, rid, locked);
	}
}

static i32 et_is_entity_active(const sk_editor_window_t* window, sk_rid_t rid) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? et_is_active(state, rid) : 1;
}

static i32 et_is_entity_locked(const sk_editor_window_t* window, sk_rid_t rid) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? et_is_locked(state, rid) : 0;
}

static sk_rid_t et_get_root(const sk_editor_window_t* window) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? et_root_of(state) : SK_RID_ZERO;
}

static u32 et_entity_count(const sk_editor_window_t* window) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? state->tree_count : 0u;
}

static const_chr_t et_entity_name_at(const sk_editor_window_t* window, u32 index) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	if (state == NULL || index >= state->tree_count) {
		return "";
	}
	return state->tree_names[index];
}

static sk_rid_t et_entity_rid_at(const sk_editor_window_t* window, u32 index) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	if (state == NULL || index >= state->tree_count) {
		return SK_RID_ZERO;
	}
	return state->tree_rids[index];
}

static i32 et_get_show_scene_entity(const sk_editor_window_t* window) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? state->show_scene_entity : 0;
}

static void et_set_show_scene_entity(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 on) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->show_scene_entity = on != 0 ? 1 : 0;
	}
}

static i32 et_is_read_only(const sk_editor_window_t* window) {
	const et_state_t* state = (const et_state_t*)window->user_data;
	return state != NULL ? state->read_only : 0;
}

static void et_set_read_only(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 ro) {
	et_state_t* state = et_state_of(app_context, app_api, window);
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->read_only = ro != 0 ? 1 : 0;
	}
}

static const sk_editor_entity_tree_ops_t et_ops = {
	et_open,
	et_add_menu_item,
	et_set_scene,
	et_clear_selection,
	et_select_entity,
	et_get_selected,
	et_get_last_selected,
	et_set_rename_entity,
	et_rename_entity,
	et_create_entity,
	et_create_entity_from_asset,
	et_duplicate_entity,
	et_delete_entity,
	et_reparent_entity,
	et_set_entity_active,
	et_set_entity_locked,
	et_is_entity_active,
	et_is_entity_locked,
	et_get_root,
	et_entity_count,
	et_entity_name_at,
	et_entity_rid_at,
	et_get_show_scene_entity,
	et_set_show_scene_entity,
	et_is_read_only,
	et_set_read_only,
};

/* ------------------------------------------------------------------ */
/*  Widget styles                                                      */
/* ------------------------------------------------------------------ */

static void et_apply_panel_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_WIDTH |
			 SK_UI_SP_BORDER_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 6.0f;
	p.layout.padding.top = 6.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.bottom = 6.0f;
	p.layout.row_gap = 6.0f;
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 0.96f);
	p.border_color = sk_ui_rgba(0.25f, 0.28f, 0.34f, 1.0f);
	p.layout.border.left = 1.0f;
	p.layout.border.top = 1.0f;
	p.layout.border.right = 1.0f;
	p.layout.border.bottom = 1.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void et_apply_toolbar_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 6.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt(ET_TOOLBAR_H);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void et_apply_scroll_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.min_height = sk_ui_pt(40.0f);
	p.background_color = sk_ui_rgba(0.06f, 0.07f, 0.09f, 1.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* ------------------------------------------------------------------ */
/*  Row chrome (visible/lock toggles, MOCK state)                     */
/* ------------------------------------------------------------------ */

static void et_on_vis_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	et_state_t* state = (et_state_t*)user;
	u64 rid_id = 0ull;
	const_chr_t id;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	id = state->ui->node_get_id(ctx, node);
	if (id != NULL) {
		const_chr_t tag = strstr(id, ".vis.");
		if (tag != NULL) {
			rid_id = strtoull(tag + 5, NULL, 10);
		}
	}
	if (rid_id != 0ull) {
		et_set_active(state, (sk_rid_t){rid_id}, !et_is_active(state, (sk_rid_t){rid_id}));
	}
	event->consumed = 1;
}

static void et_on_lock_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	et_state_t* state = (et_state_t*)user;
	u64 rid_id = 0ull;
	const_chr_t id;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	id = state->ui->node_get_id(ctx, node);
	if (id != NULL) {
		const_chr_t tag = strstr(id, ".lock.");
		if (tag != NULL) {
			rid_id = strtoull(tag + 6, NULL, 10);
		}
	}
	if (rid_id != 0ull) {
		et_set_locked(state, (sk_rid_t){rid_id}, !et_is_locked(state, (sk_rid_t){rid_id}));
	}
	event->consumed = 1;
}

/* Ensure the row's label stretches so the vis/lock buttons sit at the right
 * edge (flow layout; no absolute positioning needed). */
static void et_row_label_stretch(et_state_t* state, sk_ui_node_t row) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	u32 n = ui->node_child_count(ctx, row);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t c = ui->node_child_at(ctx, row, i);
		sk_ui_prop_value_t pv;
		if (ui->node_get_prop(ctx, c, "widget", &pv) == 0 && pv.type == SK_UI_PROP_STR && pv.data.str_value != NULL && strcmp(pv.data.str_value, "label") == 0) {
			sk_ui_style_props_t p;
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
			p.layout.flex_grow = 1.0f;
			p.layout.flex_shrink = 1.0f;
			(void)ui->node_merge_inline_style(ctx, c, &p);
			return;
		}
	}
}

static void et_sync_row_toggles(et_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	for (i = 0u; i < state->tree_count; ++i) {
		sk_ui_node_t row;
		char idbuf[80];
		sk_ui_node_t vis;
		sk_ui_node_t lock;
		sk_ui_style_props_t p;
		i32 active;
		i32 locked;
		if (!sk_ui_node_is_valid(state->tree)) {
			return;
		}
		row = ui->item_bind_find(ctx, state->tree, state->tree_items[i].id);
		if (!sk_ui_node_is_valid(row)) {
			continue;
		}
		et_row_label_stretch(state, row);
		active = et_is_active(state, state->tree_rids[i]);
		locked = et_is_locked(state, state->tree_rids[i]);

		(void)snprintf(idbuf, sizeof(idbuf), "et.row.vis.%llu", state->tree_rids[i].id);
		vis = ui->query_by_test_id(ctx, row, idbuf);
		if (!sk_ui_node_is_valid(vis)) {
			sk_ui_node_callbacks_t cbs;
			vis = ui->widget_small_button(ctx, row, "V", idbuf);
			memset(&cbs, 0, sizeof(cbs));
			cbs.on_click = et_on_vis_click;
			cbs.user = state;
			(void)ui->node_set_callbacks(ctx, vis, &cbs);
		}
		(void)ui->button_set_size(ctx, vis, 16.0f, 16.0f);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_COLOR;
		p.color = active != 0 ? sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f) : sk_ui_rgba(0.45f, 0.45f, 0.47f, 1.0f);
		(void)ui->node_merge_inline_style(ctx, vis, &p);

		(void)snprintf(idbuf, sizeof(idbuf), "et.row.lock.%llu", state->tree_rids[i].id);
		lock = ui->query_by_test_id(ctx, row, idbuf);
		if (!sk_ui_node_is_valid(lock)) {
			sk_ui_node_callbacks_t cbs;
			lock = ui->widget_small_button(ctx, row, "L", idbuf);
			memset(&cbs, 0, sizeof(cbs));
			cbs.on_click = et_on_lock_click;
			cbs.user = state;
			(void)ui->node_set_callbacks(ctx, lock, &cbs);
		}
		(void)ui->button_set_size(ctx, lock, 16.0f, 16.0f);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_COLOR;
		p.color = locked != 0 ? sk_ui_rgba(0.92f, 0.93f, 0.95f, 1.0f) : sk_ui_rgba(0.45f, 0.45f, 0.47f, 1.0f);
		(void)ui->node_merge_inline_style(ctx, lock, &p);
	}
}

/* ------------------------------------------------------------------ */
/*  Input callbacks (root capture)                                     */
/* ------------------------------------------------------------------ */

static sk_rid_t et_row_under_pointer(et_state_t* state, f32 x, f32 y) {
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	u32 i;
	if (state == NULL || state->ui == NULL || !sk_ui_node_is_valid(state->tree)) {
		return SK_RID_ZERO;
	}
	ui = state->ui;
	ctx = state->ui_ctx;
	for (i = 0u; i < state->tree_count; ++i) {
		sk_ui_rect_t r;
		sk_ui_node_t row = ui->item_bind_find(ctx, state->tree, state->tree_items[i].id);
		if (!sk_ui_node_is_valid(row)) {
			continue;
		}
		if (ui->node_get_abs_rect(ctx, row, &r, NULL) != 0) {
			continue;
		}
		if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
			return state->tree_rids[i];
		}
	}
	return SK_RID_ZERO;
}

static void et_on_root_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	et_state_t* state = (et_state_t*)user;
	(void)node;
	if (state == NULL || event == NULL || event->phase != SK_UI_EVENT_PHASE_CAPTURE) {
		return;
	}
	if (event->type == SK_UI_EVENT_POINTER_DOWN) {
		state->last_click_mods = event->mods;
		state->pointer_x = event->x;
		state->pointer_y = event->y;
		if (event->button == SK_UI_POINTER_BUTTON_LEFT) {
			state->left_down = 1;
			if (sk_ui_node_is_valid(state->tree) && et_row_under_pointer(state, event->x, event->y).id != 0u) {
				/* Keep hotkeys routed to the tree while interacting with rows. */
				(void)state->ui->action_focus(ctx, state->tree);
			}
		} else if (event->button == SK_UI_POINTER_BUTTON_RIGHT) {
			state->right_down = 1;
		}
	} else if (event->type == SK_UI_EVENT_POINTER_MOVE) {
		state->pointer_x = event->x;
		state->pointer_y = event->y;
	} else if (event->type == SK_UI_EVENT_KEY_DOWN) {
		if (event->down == 0 || state->read_only != 0) {
			return;
		}
		{
			sk_ui_node_t focus = state->ui != NULL ? state->ui->focus_get(ctx) : SK_UI_NODE_INVALID;
			i32 in_text = sk_ui_node_is_valid(focus) && (sk_ui_node_eq(focus, state->search_input) || sk_ui_node_eq(focus, state->rename_input)) ? 1 : 0;
			if (in_text != 0) {
				return;
			}
			if (event->key == SK_UI_KEY_DELETE) {
				u32 i;
				for (i = 0u; i < state->selected_count;) {
					sk_rid_t rid = state->selected[i];
					if (!SK_RID_EQ(rid, et_root_of(state))) {
						et_delete_entity_internal(state, rid, NULL);
					} else {
						i++;
					}
				}
				event->consumed = 1;
			} else if (event->key == ET_KEY_F2) {
				if (state->last_selected.id != 0u) {
					state->rename_entity = state->last_selected;
					state->rename_armed = 0;
					event->consumed = 1;
				}
			} else if ((event->mods & (u32)SK_UI_MOD_CTRL) != 0u && event->key == 'D') {
				(void)et_duplicate_entity_internal(state, state->last_selected, NULL);
				event->consumed = 1;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/*  UI build                                                           */
/* ------------------------------------------------------------------ */

static void et_on_add_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	et_state_t* state = (et_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	et_build_context_menu(state);
	if (sk_ui_node_is_valid(state->context_menu)) {
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
		p.layout.position = SK_UI_POSITION_ABSOLUTE;
		p.layout.left = sk_ui_pt(state->pointer_x > 0.0f ? state->pointer_x : 8.0f);
		p.layout.top = sk_ui_pt(state->pointer_y > 0.0f ? state->pointer_y : 8.0f);
		(void)state->ui->node_merge_inline_style(ctx, state->context_menu, &p);
		(void)state->ui->menu_set_open(ctx, state->context_menu, 1);
	}
	event->consumed = 1;
}

static void et_build_ui(et_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_node_callbacks_t cbs;
	sk_ui_style_props_t p;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "et.content");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	et_apply_panel_style(ui, ctx, state->root);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = et_on_root_event;
	cbs.user = state;
	(void)ui->node_set_callbacks(ctx, state->root, &cbs);

	/* Toolbar: + (create menu) + search filter. */
	state->toolbar = ui->widget_view(ctx, state->root, "et.toolbar");
	et_apply_toolbar_style(ui, ctx, state->toolbar);
	state->add_btn = ui->widget_button(ctx, state->toolbar, "+", "et.add");
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = et_on_add_click;
	cbs.user = state;
	(void)ui->node_set_callbacks(ctx, state->add_btn, &cbs);
	state->search_input = ui->widget_text_input_with_hint(ctx, state->toolbar, state->search, "Search entities", "et.search");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING;
	p.layout.flex_grow = 1.0f;
	/* APX-384: the 22pt forced height with the class's 6pt top/bottom padding
	 * left only ~8px of content for 14px text (line ~16.3px), so the hint's
	 * descenders were sliced by the input's bottom edge. Fill the toolbar row
	 * and slim the vertical padding so hint + typed text render fully. */
	p.layout.height = sk_ui_pt(ET_TOOLBAR_H);
	p.layout.padding.left = 6.0f;
	p.layout.padding.top = 3.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.bottom = 3.0f;
	(void)ui->node_merge_inline_style(ctx, state->search_input, &p);

	/* Tree region. */
	state->tree_scroll = ui->widget_scroll_view(ctx, state->root, "et.scroll");
	et_apply_scroll_style(ui, ctx, state->tree_scroll);
	state->tree_content = ui->scroll_view_content(ctx, state->tree_scroll);
	(void)ui->node_set_id(ctx, state->tree_content, "et.scroll.content");
	state->tree = ui->widget_tree(ctx, state->tree_content, &state->tree_arr, "et.tree");
	(void)ui->item_bind_set_flags(ctx, state->tree, SK_UI_TREE_NODE_FLAGS_DEFAULT | SK_UI_TREE_NODE_FLAG_ALLOW_OVERLAP);
	(void)ui->item_bind_set_on_activate(ctx, state->tree, et_on_tree_activate, state);
	(void)ui->node_set_focusable(ctx, state->tree, 1);

	/* Context menu overlay (floating; opened on + / right-click). */
	state->context_menu = ui->widget_context_menu(ctx, state->root, "et.context");
	(void)ui->menu_set_open(ctx, state->context_menu, 0);

	state->ui_built = 1;
	state->applied_revision = state->listing_revision - 1u; /* force first push */
}

/* Rebuild the context menu rows from the class menu list. */
static void et_build_context_menu(et_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	et_class_state_t* cls;
	sk_ui_node_t create_popup = SK_UI_NODE_INVALID;
	u32 i;
	if (state == NULL || ui == NULL || !sk_ui_node_is_valid(state->context_menu)) {
		return;
	}
	cls = et_class(state->app_context, state->app_api);
	state->ctx_binding_count = 0u;

	/* Clear previous rows. */
	{
		u32 n = ui->node_child_count(ctx, state->context_menu);
		for (i = 0u; i < n; ++i) {
			sk_ui_node_t c = ui->node_child_at(ctx, state->context_menu, 0u);
			if (sk_ui_node_is_valid(c)) {
				(void)ui->node_destroy(ctx, c);
			}
		}
	}
	for (i = 0u; i < cls->menu_count; ++i) {
		const sk_editor_menu_item_desc_t* item = &cls->menus[i];
		sk_ui_node_t parent = state->context_menu;
		sk_ui_node_t row;
		char id[64];
		if (item->item_name == NULL) {
			continue;
		}
		if (item->visible != NULL && item->visible(state->app_context, state->app_api, NULL, item->user) == 0) {
			continue;
		}
		if (strncmp(item->item_name, "Create/", 7u) == 0) {
			if (!sk_ui_node_is_valid(create_popup)) {
				sk_ui_node_t create_menu = ui->widget_submenu(ctx, state->context_menu, "Create", "et.ctx.create");
				if (!sk_ui_node_is_valid(create_menu)) {
					continue;
				}
				create_popup = ui->menu_get_popup(ctx, create_menu);
			}
			parent = create_popup;
		}
		(void)snprintf(id, sizeof(id), "et.ctx.item.%u", i);
		row = ui->widget_menu_item(ctx, parent, item->item_name, id);
		if (!sk_ui_node_is_valid(row)) {
			continue;
		}
		if (state->ctx_binding_count < (u32)(sizeof(state->ctx_bindings) / sizeof(state->ctx_bindings[0]))) {
			state->ctx_bindings[state->ctx_binding_count].node = row;
			state->ctx_bindings[state->ctx_binding_count].item_index = i;
			state->ctx_binding_count++;
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Per-frame sync + interactions                                      */
/* ------------------------------------------------------------------ */

static void et_sync_ui(et_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;

	/* Search input: sync from the widget (the input owns the text). */
	if (sk_ui_node_is_valid(state->search_input)) {
		const_chr_t live = ui->text_input_get_text(ctx, state->search_input);
		if (live != NULL && strcmp(live, state->search) != 0) {
			(void)snprintf(state->search, sizeof(state->search), "%s", live);
			et_rebuild_tree(state);
		}
	}

	/* Tree revision: push the (possibly rebuilt) item array. */
	if (state->applied_revision != state->listing_revision) {
		if (sk_ui_node_is_valid(state->tree)) {
			(void)ui->item_bind_sync(ctx, state->tree);
		}
		state->applied_revision = state->listing_revision;
		state->applied_selection = state->selection_revision - 1u; /* force selection push */
	}

	/* Reveal target: open ancestors of a newly created/duplicated entity. */
	if (state->reveal_rid.id != 0u && sk_ui_node_is_valid(state->tree)) {
		et_open_ancestors(state, state->reveal_rid.id);
		state->reveal_rid = SK_RID_ZERO;
	}

	/* Selection flags into the widget. */
	if (state->applied_selection != state->selection_revision) {
		if (sk_ui_node_is_valid(state->tree)) {
			for (i = 0u; i < state->tree_count; ++i) {
				(void)ui->item_bind_set_selected(ctx, state->tree, state->tree_items[i].id, et_selected_contains(state, state->tree_rids[i]));
			}
		}
		state->applied_selection = state->selection_revision;
	}

	/* Pending rename: open the row's ancestors, then build the overlay input
	 * over the row once a laid-out rect exists. */
	if (state->rename_entity.id != 0u && state->rename_armed == 0u) {
		sk_ui_node_t row;
		sk_ui_rect_t r;
		sk_ui_rect_t root_rect;
		char namebuf[ET_NAME_CAP];
		const_chr_t name;
		u64 row_id = state->rename_entity.id;
		if (sk_ui_node_is_valid(state->tree)) {
			et_open_ancestors(state, row_id);
		}
		row = ui->item_bind_find(ctx, state->tree, row_id);
		if (sk_ui_node_is_valid(row) && ui->node_get_abs_rect(ctx, row, &r, NULL) == 0 && r.width > 0.5f && r.height > 0.5f &&
			ui->node_get_abs_rect(ctx, state->root, &root_rect, NULL) == 0) {
			sk_ui_node_t input;
			sk_ui_style_props_t p;
			name = et_name_of(state, state->rename_entity, namebuf, (u32)sizeof(namebuf));
			if (sk_ui_node_is_valid(state->rename_input)) {
				(void)ui->node_destroy(ctx, state->rename_input);
			}
			input = ui->widget_text_input(ctx, state->root, name, "et.rename");
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
			p.layout.position = SK_UI_POSITION_ABSOLUTE;
			p.layout.left = sk_ui_pt(r.x - root_rect.x + 4.0f);
			p.layout.top = sk_ui_pt(r.y - root_rect.y);
			p.layout.width = sk_ui_pt(r.width > 60.0f ? r.width : 120.0f);
			p.layout.height = sk_ui_pt(r.height);
			p.layout.flex_grow = 0.0f;
			p.layout.flex_shrink = 0.0f;
			(void)ui->node_set_inline_style(ctx, input, &p);
			(void)ui->action_focus(ctx, input);
			state->rename_input = input;
			state->rename_armed = 1;
		}
	}

	/* Rename completion: commit when focus left the input. */
	if (state->rename_entity.id != 0u && state->rename_armed != 0u && sk_ui_node_is_valid(state->rename_input)) {
		sk_ui_node_t focus = ui->focus_get(ctx);
		if (!sk_ui_node_is_valid(focus) || (!sk_ui_node_eq(focus, state->rename_input) && !sk_ui_node_eq(ui->node_parent(ctx, focus), state->rename_input))) {
			const_chr_t text = ui->text_input_get_text(ctx, state->rename_input);
			et_rename_entity_internal(state, state->rename_entity, text != NULL ? text : "", NULL);
			state->rename_entity = SK_RID_ZERO;
			state->rename_armed = 0;
			if (sk_ui_node_is_valid(state->rename_input)) {
				(void)ui->node_destroy(ctx, state->rename_input);
				state->rename_input = SK_UI_NODE_INVALID;
			}
		}
	}

	/* Row chrome + scroll content height. */
	et_sync_row_toggles(state);
	if (sk_ui_node_is_valid(state->tree_scroll)) {
		sk_ui_rect_t r;
		f32 w = 200.0f;
		f32 h = 4.0f + (f32)state->tree_count * ET_ROW_H;
		if (ui->node_get_abs_rect(ctx, state->tree_scroll, &r, NULL) == 0 && r.width > 1.0f) {
			w = r.width;
		}
		(void)ui->scroll_view_set_content_size(ctx, state->tree_scroll, w, h);
	}
}

/* Open every ancestor of @p item_id. The selection sync pushes
 * item_bind_set_selected for every row, which seeds those ids, so the
 * ONCE-based item_bind_open_ancestors would skip them — use ALWAYS instead. */
static void et_open_ancestors(et_state_t* state, u64 item_id) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 guard = 0u;
	u64 pid = item_id;
	while (pid != 0ull && guard < 64u) {
		u32 i;
		u64 next = 0ull;
		for (i = 0u; i < state->tree_count; ++i) {
			if (state->tree_items[i].id == pid) {
				next = state->tree_items[i].parent_id;
				break;
			}
		}
		if (next == 0ull) {
			break;
		}
		(void)ui->item_bind_set_open(ctx, state->tree, next, 1);
		pid = next;
		guard += 1u;
	}
}

/* Apply a UI row click: ctrl toggles, otherwise exclusive replace. */
static void et_on_tree_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user) {
	et_state_t* state = (et_state_t*)user;
	u32 i;
	(void)ctx;
	(void)host;
	if (state == NULL) {
		return;
	}
	state->clicked_entity_id = item_id;
	for (i = 0u; i < state->tree_count; ++i) {
		if (state->tree_items[i].id == item_id) {
			sk_rid_t rid = state->tree_rids[i];
			if ((state->last_click_mods & (u32)SK_UI_MOD_CTRL) != 0u) {
				et_toggle_select(state, rid);
			} else if (state->selected_count == 1u && SK_RID_EQ(state->selected[0], rid)) {
				/* Second click of the already-selected row (C++ double-click
				 * ViewEntity): frame through the Scene View ops table. */
				et_frame_in_scene_view(state, rid);
			} else {
				et_select_exclusive(state, rid);
			}
			return;
		}
	}
}

/* Handle edge interactions the widgets surfaced since the last draw. */
static void et_handle_interactions(et_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	sk_rid_t hit;
	sk_ui_rect_t content_rect;

	if (ui == NULL || !sk_ui_node_is_valid(state->tree)) {
		return;
	}

	/* Right-click: select the row under the pointer (C++ records
	 * entitySelection on right-click too) and open the context menu. */
	if (state->right_down != 0u) {
		hit = et_row_under_pointer(state, state->pointer_x, state->pointer_y);
		if (hit.id != 0u) {
			et_select_exclusive(state, hit);
		}
		et_build_context_menu(state);
		if (sk_ui_node_is_valid(state->context_menu)) {
			sk_ui_style_props_t p;
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
			p.layout.position = SK_UI_POSITION_ABSOLUTE;
			p.layout.left = sk_ui_pt(state->pointer_x);
			p.layout.top = sk_ui_pt(state->pointer_y);
			(void)ui->node_merge_inline_style(ctx, state->context_menu, &p);
			(void)ui->menu_set_open(ctx, state->context_menu, 1);
		}
		state->right_down = 0u;
	}

	/* Left-click on empty tree area clears the selection (no ctrl). */
	if (state->left_down != 0u && state->clicked_entity_id == 0u) {
		hit = et_row_under_pointer(state, state->pointer_x, state->pointer_y);
		if (hit.id == 0u && ui->node_get_abs_rect(ctx, state->tree_content, &content_rect, NULL) == 0 && state->pointer_x >= content_rect.x &&
			state->pointer_x <= content_rect.x + content_rect.width && state->pointer_y >= content_rect.y && state->pointer_y <= content_rect.y + content_rect.height &&
			(state->last_click_mods & (u32)SK_UI_MOD_CTRL) == 0u) {
			et_deselect_all_notify(state, SK_RID_ZERO);
			et_clear_selection_internal(state);
			sk_editor_notify_selection_changed(state->app_context, state->app_api);
			state->selection_revision++;
		}
	}
	state->left_down = 0u;
	state->clicked_entity_id = 0u;

	/* Context menu rows: poll clicks and run the bound actions. */
	if (sk_ui_node_is_valid(state->context_menu) && ui->menu_get_open(ctx, state->context_menu) != 0) {
		for (i = 0u; i < state->ctx_binding_count; ++i) {
			if (sk_ui_node_is_valid(state->ctx_bindings[i].node) && ui->menu_item_clicked(ctx, state->ctx_bindings[i].node) != 0) {
				const sk_editor_menu_item_desc_t* item = &et_class(state->app_context, state->app_api)->menus[state->ctx_bindings[i].item_index];
				if (item->action != NULL) {
					sk_editor_window_t* window = sk_editor_window_by_type(state->app_context, state->app_api, SK_EDITOR_WINDOW_ENTITY_TREE);
					item->action(state->app_context, state->app_api, window, item->user);
				}
				(void)ui->menu_set_open(ctx, state->context_menu, 0);
				break;
			}
		}
	}

	/* Drag sources (non-root rows) + drop targets (every row): reparent. */
	{
		typedef struct et_drag_payload_t {
			u64 rid;
		} et_drag_payload_t;
		et_drag_payload_t payload;
		u32 flag = SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS | SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER;
		sk_rid_t root = et_root_of(state);

		for (i = 0u; i < state->tree_count; ++i) {
			sk_ui_node_t row = ui->item_bind_find(ctx, state->tree, state->tree_items[i].id);
			if (!sk_ui_node_is_valid(row)) {
				continue;
			}
			if (!SK_RID_EQ(state->tree_rids[i], root)) {
				payload.rid = state->tree_rids[i].id;
				(void)ui->drag_drop_source(ctx, row, SK_UI_ENTITY_PAYLOAD, &payload, sizeof(payload), flag);
				(void)ui->drag_drop_set_preview(ctx, row, state->tree_names[i]);
			}
			(void)ui->drag_drop_target(ctx, row, SK_UI_ENTITY_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_PREVIEW_TOOLTIP);
		}
		{
			const sk_ui_payload_t* accepted = ui->drag_drop_accept(ctx, SK_UI_ENTITY_PAYLOAD,
																   SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT | SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_PREVIEW_TOOLTIP);
			if (accepted != NULL && accepted->delivery != 0 && accepted->data != NULL && accepted->size == sizeof(payload)) {
				sk_rid_t dragged = {((const et_drag_payload_t*)accepted->data)->rid};
				sk_ui_node_t target = ui->drag_drop_get_hovered_target(ctx);
				if (sk_ui_node_is_valid(target)) {
					u32 t;
					for (t = 0u; t < state->tree_count; ++t) {
						sk_ui_node_t row = ui->item_bind_find(ctx, state->tree, state->tree_items[t].id);
						if (sk_ui_node_eq(row, target)) {
							et_reparent_entity_internal(state, dragged, state->tree_rids[t], NULL);
							break;
						}
					}
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void et_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	et_class_state_t* cls = (et_class_state_t*)window->user_data;
	et_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (et_state_t*)alloc->alloc(alloc->instance, sizeof(et_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->workspace_id = SK_EDITOR_WORKSPACE_SCENE;
	state->mock_scene = 1;	/* mock until set_scene attaches a real scene */
	et_rebuild_tree(state); /* seeds the mock scene */
	window->user_data = state;
}

static void et_draw(sk_editor_window_t* window, i32* open) {
	et_state_t* state = (et_state_t*)window->user_data;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_ui_node_t chrome;
	sk_ui_node_t content;
	*open = 1;
	if (state == NULL) {
		*open = 0;
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (ctx == NULL) {
		return; /* no ui host (plain unit tests keep the ops/logic path only) */
	}
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		return;
	}
	chrome = ui->find_by_id(ctx, window->dock_id);
	if (!sk_ui_node_is_valid(chrome)) {
		return; /* not docked in the active workspace yet */
	}
	content = ui->editor_window_content(ctx, chrome);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	/* Rebuild the chrome when the dock teardown destroyed it (workspace
	 * switch / tab close) — stable ids survive node recycling. */
	if (!sk_ui_node_is_valid(state->root)) {
		et_build_ui(state, ui, ctx, content);
	}
	if (state->ui_built == 0) {
		return;
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	/* Keep hotkeys routed to the tree when nothing else holds focus. */
	if (!sk_ui_node_is_valid(ui->focus_get(ctx)) && sk_ui_node_is_valid(state->tree)) {
		(void)ui->action_focus(ctx, state->tree);
	}
	et_sync_ui(state);
	et_handle_interactions(state);
}

static void et_destroy(sk_editor_window_t* window) {
	et_state_t* state = (et_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	/* Tear down the chrome with the shell's ui context (valid while the
	 * shell owns the frame; dock teardown may have destroyed the nodes
	 * already — node_alive guards that case). */
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
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_entity_tree_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	et_class_state_t* cls = et_class(app_context, app_api);
	if (cls != NULL) {
		et_window.user_data = cls;
		return;
	}
	cls = (et_class_state_t*)alloc->alloc(alloc->instance, sizeof(et_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	et_seed_class_menus(cls);
	app_api->set_api(app_context, SK_EDITOR_ENTITY_TREE_STATE_TYPE_ID, cls);

	et_window.type_id = SK_EDITOR_WINDOW_ENTITY_TREE;
	et_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &et_window);
	app_api->add_impl(app_context, SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID, &et_ops);
}

void sk_editor_entity_tree_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	et_class_state_t* cls = et_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &et_window);
	app_api->remove_impl(app_context, SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID, &et_ops);
	app_api->set_api(app_context, SK_EDITOR_ENTITY_TREE_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (et_window.user_data == cls) {
		et_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "filesystem.h"
#include "path.h"
#include "test.h"
#include "unity.h"

/* Build a small real scene in a fresh repository: scene "Main" with roots
 * Player (child Hand) and Camera. */
typedef struct ett_scene_t {
	sk_repository_t* repo;
	sk_rid_t scene;
	sk_rid_t player;
	sk_rid_t hand;
	sk_rid_t camera;
} ett_scene_t;

static sk_rid_t ett_entity(sk_repository_t* repo, const sk_repository_api_t* api, const_chr_t name) {
	const sk_resource_type_t* type = api->find_type(repo, SK_ENTITY_RESOURCE_TYPE_ID);
	sk_rid_t rid;
	sk_resource_object_t view;
	TEST_ASSERT_NOT_NULL(type);
	rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	view = api->write(repo, rid);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_ENTITY_RESOURCE_FIELD_NAME, name));
	api->commit(view, NULL);
	return rid;
}

static void ett_scene_build(ett_scene_t* out) {
	const sk_repository_api_t* repo = sk_test_repository_table();
	const sk_resource_type_t* scene_type;
	sk_resource_object_t view;
	sk_rid_t roots[2];
	sk_rid_t child[1];
	memset(out, 0, sizeof(*out));
	out->repo = repo->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(out->repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(out->repo, repo));

	scene_type = repo->find_type(out->repo, SK_SCENE_RESOURCE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(scene_type);
	out->scene = repo->create_resource(out->repo, scene_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(out->scene.id != 0u);
	view = repo->write(out->repo, out->scene);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, repo->set_string(view, SK_SCENE_RESOURCE_FIELD_NAME, "Main"));
	repo->commit(view, NULL);

	out->player = ett_entity(out->repo, repo, "Player");
	out->hand = ett_entity(out->repo, repo, "Hand");
	out->camera = ett_entity(out->repo, repo, "Camera");

	child[0] = out->hand;
	view = repo->write(out->repo, out->player);
	TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_CHILDREN, child, 1u));
	repo->commit(view, NULL);

	roots[0] = out->player;
	roots[1] = out->camera;
	view = repo->write(out->repo, out->scene);
	TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(view, SK_SCENE_RESOURCE_FIELD_ROOTS, roots, 2u));
	repo->commit(view, NULL);
}

static void ett_scene_destroy(ett_scene_t* s) {
	sk_test_repository_table()->destroy(s->repo);
	memset(s, 0, sizeof(*s));
}

/* Observer harness (notify.h structs). */
typedef struct ett_obs_t {
	u32 sel_changed;
	u32 entity_sel;
	u32 entity_desel;
	u32 created;
	u32 renamed;
	u32 deleted;
	u32 reparented;
	u32 last_workspace;
	sk_rid_t last_rid;
	sk_rid_t last_parent;
	char last_name[64];
} ett_obs_t;

static void ett_on_sel_changed(void* user) {
	((ett_obs_t*)user)->sel_changed++;
}

static void ett_on_entity_sel(void* user, u32 workspace_id, sk_rid_t rid) {
	ett_obs_t* o = (ett_obs_t*)user;
	o->entity_sel++;
	o->last_workspace = workspace_id;
	o->last_rid = rid;
}

static void ett_on_entity_desel(void* user, u32 workspace_id, sk_rid_t rid) {
	ett_obs_t* o = (ett_obs_t*)user;
	o->entity_desel++;
	o->last_workspace = workspace_id;
	o->last_rid = rid;
}

static void ett_on_created(void* user, u32 workspace_id, sk_rid_t rid) {
	ett_obs_t* o = (ett_obs_t*)user;
	o->created++;
	o->last_workspace = workspace_id;
	o->last_rid = rid;
}

static void ett_on_renamed(void* user, u32 workspace_id, sk_rid_t rid, const_chr_t name) {
	ett_obs_t* o = (ett_obs_t*)user;
	o->renamed++;
	o->last_workspace = workspace_id;
	o->last_rid = rid;
	(void)snprintf(o->last_name, sizeof(o->last_name), "%s", name != NULL ? name : "");
}

static void ett_on_deleted(void* user, u32 workspace_id, sk_rid_t rid) {
	ett_obs_t* o = (ett_obs_t*)user;
	o->deleted++;
	o->last_workspace = workspace_id;
	o->last_rid = rid;
}

static void ett_on_reparented(void* user, u32 workspace_id, sk_rid_t rid, sk_rid_t new_parent) {
	ett_obs_t* o = (ett_obs_t*)user;
	o->reparented++;
	o->last_workspace = workspace_id;
	o->last_rid = rid;
	o->last_parent = new_parent;
}

/* Observer storage is caller-owned (notify.h lifetime rule: never register a
 * stack observer without a matching remove before that frame returns). */
typedef struct ett_obs_set_t {
	sk_editor_on_selection_changed_t sel;
	sk_editor_on_entity_selection_t esel;
	sk_editor_on_entity_deselection_t edesel;
	sk_editor_on_entity_created_t created;
	sk_editor_on_entity_renamed_t renamed;
	sk_editor_on_entity_deleted_t deleted;
	sk_editor_on_entity_reparented_t reparented;
} ett_obs_set_t;

static void ett_register_observers(sk_app_context_t* app, const sk_app_api_t* api, ett_obs_t* obs, ett_obs_set_t* set) {
	memset(obs, 0, sizeof(*obs));
	memset(set, 0, sizeof(*set));

	{
		sk_editor_on_selection_changed_t* sel = &set->sel;
		sk_editor_on_entity_selection_t* esel = &set->esel;
		sk_editor_on_entity_deselection_t* edesel = &set->edesel;
		sk_editor_on_entity_created_t* created = &set->created;
		sk_editor_on_entity_renamed_t* renamed = &set->renamed;
		sk_editor_on_entity_deleted_t* deleted = &set->deleted;
		sk_editor_on_entity_reparented_t* reparented = &set->reparented;

		sel->user = obs;
		sel->on_selection_changed = ett_on_sel_changed;
		esel->user = obs;
		esel->on_entity_selection = ett_on_entity_sel;
		edesel->user = obs;
		edesel->on_entity_deselection = ett_on_entity_desel;
		created->user = obs;
		created->on_entity_created = ett_on_created;
		renamed->user = obs;
		renamed->on_entity_renamed = ett_on_renamed;
		deleted->user = obs;
		deleted->on_entity_deleted = ett_on_deleted;
		reparented->user = obs;
		reparented->on_entity_reparented = ett_on_reparented;
	}

	api->add_impl(app, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &set->sel);
	api->add_impl(app, SK_EDITOR_NOTIFY_ENTITY_SELECTION, &set->esel);
	api->add_impl(app, SK_EDITOR_NOTIFY_ENTITY_DESELECTION, &set->edesel);
	api->add_impl(app, SK_EDITOR_NOTIFY_ENTITY_CREATED, &set->created);
	api->add_impl(app, SK_EDITOR_NOTIFY_ENTITY_RENAMED, &set->renamed);
	api->add_impl(app, SK_EDITOR_NOTIFY_ENTITY_DELETED, &set->deleted);
	api->add_impl(app, SK_EDITOR_NOTIFY_ENTITY_REPARENTED, &set->reparented);
}

/* Ops-table path (no ui): the window opens, shows the mock scene, and every
 * public entry point works when called from outside. */
SK_TEST(editor_entity_tree_ops_mock_scene) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_entity_tree_ops_t* ops;
	sk_editor_window_t* window;
	ett_obs_t obs;
	ett_obs_set_t obs_set;
	sk_rid_t root;
	sk_rid_t player;
	sk_rid_t light;
	sk_rid_t mesh;
	sk_rid_t sel[8];

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_entity_tree_ops(app, boot.api));
	sk_editor_entity_tree_register(app, boot.api);
	sk_editor_entity_tree_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID));

	ops = sk_editor_entity_tree_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);
	TEST_ASSERT_EQUAL_PTR(&et_ops, ops);
	TEST_ASSERT_NOT_NULL(ops->open);
	TEST_ASSERT_NOT_NULL(ops->set_scene);
	TEST_ASSERT_NOT_NULL(ops->create_entity);
	TEST_ASSERT_NOT_NULL(ops->delete_entity);
	TEST_ASSERT_NOT_NULL(ops->reparent_entity);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Entity Tree", window->title);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_RIGHT_TOP, window->dock_position);
	TEST_ASSERT_EQUAL_INT(0, window->order);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_GRAPH));
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_ENTITY_TREE));

	/* Mock scene (no set_scene): Demo Scene root + Main Camera + Directional
	 * Light + Player (locked) + Character Mesh (child of Player, inactive). */
	TEST_ASSERT_TRUE(ops->entity_count(window) >= 5u);
	root = ops->get_root(window);
	TEST_ASSERT_TRUE(root.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Demo Scene", ops->entity_name_at(window, 0u));
	player = ops->entity_rid_at(window, 3u);
	TEST_ASSERT_EQUAL_STRING("Player", ops->entity_name_at(window, 3u));
	light = ops->entity_rid_at(window, 2u);
	TEST_ASSERT_EQUAL_STRING("Directional Light", ops->entity_name_at(window, 2u));
	mesh = ops->entity_rid_at(window, 4u);
	TEST_ASSERT_EQUAL_STRING("Character Mesh", ops->entity_name_at(window, 4u));

	ett_register_observers(app, boot.api, &obs, &obs_set);

	/* Selection publishes selection_changed + entity_selection. */
	ops->select_entity(app, boot.api, window, player, 1, NULL);
	TEST_ASSERT_EQUAL_UINT64(player.id, ops->get_last_selected(window).id);
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_selected_entities(window, sel, 8u));
	TEST_ASSERT_EQUAL_UINT64(player.id, sel[0].id);
	TEST_ASSERT_EQUAL_UINT(1u, obs.sel_changed);
	TEST_ASSERT_EQUAL_UINT(1u, obs.entity_sel);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_WORKSPACE_SCENE, obs.last_workspace);
	TEST_ASSERT_EQUAL_UINT64(player.id, obs.last_rid.id);

	/* Ctrl-toggle path (clear_others == 0). */
	ops->select_entity(app, boot.api, window, mesh, 0, NULL);
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_selected_entities(window, sel, 8u));
	ops->select_entity(app, boot.api, window, mesh, 0, NULL); /* toggle off */
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_selected_entities(window, sel, 8u));

	/* Clear publishes deselection for the dropped selection. */
	ops->clear_selection(app, boot.api, window, NULL);
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_selected_entities(window, sel, 8u));
	TEST_ASSERT_TRUE(obs.entity_desel >= 1u);

	/* Create under Player (mock): count grows, created + selection observers. */
	{
		u32 before = ops->entity_count(window);
		sk_rid_t created = ops->create_entity(app, boot.api, window, player, NULL);
		TEST_ASSERT_TRUE(created.id != 0u);
		TEST_ASSERT_EQUAL_UINT(before + 1u, ops->entity_count(window));
		TEST_ASSERT_EQUAL_UINT64(created.id, ops->get_last_selected(window).id);
		TEST_ASSERT_EQUAL_UINT(1u, obs.created);
		TEST_ASSERT_EQUAL_UINT64(created.id, obs.last_rid.id);
	}

	/* Rename (apply path) publishes entity_renamed with the new name. */
	ops->rename_entity(app, boot.api, window, player, "Hero");
	TEST_ASSERT_EQUAL_STRING("Hero", ops->entity_name_at(window, 3u));
	TEST_ASSERT_EQUAL_UINT(1u, obs.renamed);
	TEST_ASSERT_EQUAL_STRING("Hero", obs.last_name);

	/* set_rename_entity arms the overlay (ops-level: selects the target). */
	ops->set_rename_entity(app, boot.api, window, player, NULL);
	TEST_ASSERT_EQUAL_UINT64(player.id, ops->get_last_selected(window).id);

	/* Reparent: move Directional Light under Player. */
	ops->reparent_entity(app, boot.api, window, light, player, NULL);
	TEST_ASSERT_EQUAL_UINT(1u, obs.reparented);
	TEST_ASSERT_EQUAL_UINT64(light.id, obs.last_rid.id);
	TEST_ASSERT_EQUAL_UINT64(player.id, obs.last_parent.id);

	/* Cycle guard: Player under its own child is rejected (no change). */
	{
		u32 before = ops->entity_count(window);
		ops->reparent_entity(app, boot.api, window, player, mesh, NULL);
		TEST_ASSERT_EQUAL_UINT(before, ops->entity_count(window));
		TEST_ASSERT_EQUAL_UINT(1u, obs.reparented); /* still 1: rejected */
	}

	/* Duplicate deep-copies the Player subtree (+4: Hero + Character Mesh +
	 * New Entity + Directional Light). */
	{
		u32 before = ops->entity_count(window);
		ops->select_entity(app, boot.api, window, player, 1, NULL);
		ops->duplicate_entity(app, boot.api, window, NULL);
		TEST_ASSERT_EQUAL_UINT(before + 4u, ops->entity_count(window));
		TEST_ASSERT_EQUAL_UINT(2u, obs.created); /* create + duplicate */
	}

	/* Delete the Player subtree: count shrinks, deleted observer fires. */
	{
		u32 before = ops->entity_count(window);
		ops->select_entity(app, boot.api, window, player, 1, NULL);
		ops->delete_entity(app, boot.api, window, NULL);
		TEST_ASSERT_TRUE(ops->entity_count(window) < before);
		/* Preorder subtree notify: the parent + every descendant published. */
		TEST_ASSERT_TRUE(obs.deleted >= 4u);
	}

	/* Active/lock toggles are session-only (MOCK), on a live entity. */
	{
		sk_rid_t cam = ops->entity_rid_at(window, 1u); /* Main Camera */
		TEST_ASSERT_EQUAL_INT(1, ops->is_entity_active(window, cam));
		TEST_ASSERT_EQUAL_INT(0, ops->is_entity_locked(window, cam));
		ops->set_entity_active(app, boot.api, window, cam, 0);
		ops->set_entity_locked(app, boot.api, window, cam, 1);
		TEST_ASSERT_EQUAL_INT(0, ops->is_entity_active(window, cam));
		TEST_ASSERT_EQUAL_INT(1, ops->is_entity_locked(window, cam));
	}

	/* Mock recorders. */
	ops->create_entity_from_asset(app, boot.api, window);
	TEST_ASSERT_EQUAL_INT(1, ((et_state_t*)window->user_data)->last_create_from_asset);
	ops->set_show_scene_entity(app, boot.api, window, 1);
	TEST_ASSERT_EQUAL_INT(1, ops->get_show_scene_entity(window));
	ops->set_read_only(app, boot.api, window, 1);
	TEST_ASSERT_EQUAL_INT(1, ops->is_read_only(window));
	ops->set_read_only(app, boot.api, window, 0);

	/* Menu-item extension point (C++ AddMenuItem). */
	{
		sk_editor_menu_item_desc_t item;
		u32 before = et_class(app, boot.api)->menu_count;
		memset(&item, 0, sizeof(item));
		item.item_name = "Create/Custom Entity";
		item.order = 5;
		item.action = et_action_create_entity;
		ops->add_menu_item(app, boot.api, &item);
		TEST_ASSERT_EQUAL_UINT(before + 1u, et_class(app, boot.api)->menu_count);
	}

	/* Detach back to mock / re-attach works. */
	ops->set_scene(app, boot.api, window, NULL, SK_RID_ZERO);
	TEST_ASSERT_EQUAL_STRING("Demo Scene", ops->entity_name_at(window, 0u));
	{
		u32 i;
		for (i = 0u; i < ops->entity_count(window); ++i) {
			TEST_ASSERT_TRUE(ops->entity_rid_at(window, i).id != 0u);
		}
	}

	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_ENTITY_TREE));

	sk_editor_entity_tree_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_entity_tree_ops(app, boot.api));
	sk_app_shutdown(app);
}

/* Real entity layer: a repository scene (sk.scene_resource Roots → entity
 * Children) answers the tree, and create/rename/duplicate/delete/reparent
 * mutate the repository and publish the observers. */
SK_TEST(editor_entity_tree_ops_real_scene) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_entity_tree_ops_t* ops;
	sk_editor_window_t* window;
	ett_scene_t scene;
	ett_obs_t obs;
	ett_obs_set_t obs_set;
	sk_rid_t created;
	sk_rid_t sel[8];

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);
	ett_scene_build(&scene);

	sk_editor_entity_tree_register(app, boot.api);
	ops = sk_editor_entity_tree_ops(app, boot.api);
	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);

	/* Attach the real scene: tree = Main (root), Player, Hand, Camera. */
	ops->set_scene(app, boot.api, window, scene.repo, scene.scene);
	TEST_ASSERT_EQUAL_UINT(4u, ops->entity_count(window));
	TEST_ASSERT_EQUAL_STRING("Main", ops->entity_name_at(window, 0u));
	TEST_ASSERT_EQUAL_STRING("Player", ops->entity_name_at(window, 1u));
	TEST_ASSERT_EQUAL_STRING("Hand", ops->entity_name_at(window, 2u));
	TEST_ASSERT_EQUAL_STRING("Camera", ops->entity_name_at(window, 3u));
	TEST_ASSERT_EQUAL_UINT64(scene.scene.id, ops->get_root(window).id);

	ett_register_observers(app, boot.api, &obs, &obs_set);

	/* Create under Player: real entity_resource in the repository. */
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		sk_resource_object_t view;
		created = ops->create_entity(app, boot.api, window, scene.player, NULL);
		TEST_ASSERT_TRUE(created.id != 0u);
		TEST_ASSERT_EQUAL_UINT(5u, ops->entity_count(window));
		TEST_ASSERT_EQUAL_UINT(1u, obs.created);
		view = repo->read(scene.repo, created);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_STRING("New Entity", repo->get_string(view, SK_ENTITY_RESOURCE_FIELD_NAME));
	}

	/* Rename through the repository + observers. */
	ops->rename_entity(app, boot.api, window, scene.player, "Hero");
	TEST_ASSERT_EQUAL_STRING("Hero", ops->entity_name_at(window, 1u));
	TEST_ASSERT_EQUAL_UINT(1u, obs.renamed);
	TEST_ASSERT_EQUAL_STRING("Hero", obs.last_name);
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		sk_resource_object_t view = repo->read(scene.repo, scene.player);
		TEST_ASSERT_EQUAL_STRING("Hero", repo->get_string(view, SK_ENTITY_RESOURCE_FIELD_NAME));
	}

	/* Duplicate deep-copies (Hero + Hand + New Entity = +3), distinct rid. */
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		u32 before = ops->entity_count(window);
		ops->select_entity(app, boot.api, window, scene.player, 1, NULL);
		ops->duplicate_entity(app, boot.api, window, NULL);
		TEST_ASSERT_EQUAL_UINT(before + 3u, ops->entity_count(window));
		TEST_ASSERT_EQUAL_UINT(2u, obs.created); /* create + duplicate */
		{
			sk_rid_t clone = ops->get_last_selected(window);
			TEST_ASSERT_TRUE(clone.id != scene.player.id);
			TEST_ASSERT_EQUAL_INT(1, repo->has_resource(scene.repo, clone));
		}
	}

	/* Reparent Hand to the scene roots (new_parent 0): published with the
	 * scene as the new parent; the repository parent link moves. */
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		ops->reparent_entity(app, boot.api, window, scene.hand, SK_RID_ZERO, NULL);
		TEST_ASSERT_EQUAL_UINT(1u, obs.reparented);
		TEST_ASSERT_EQUAL_UINT64(scene.hand.id, obs.last_rid.id);
		TEST_ASSERT_EQUAL_UINT64(scene.scene.id, obs.last_parent.id);
		TEST_ASSERT_TRUE(SK_RID_EQ(repo->get_parent(scene.repo, scene.hand), scene.scene));
	}

	/* Cycle guard: Player under its own child (New Entity) is rejected. */
	{
		u32 before = ops->entity_count(window);
		ops->reparent_entity(app, boot.api, window, scene.player, created, NULL);
		TEST_ASSERT_EQUAL_UINT(before, ops->entity_count(window));
		TEST_ASSERT_EQUAL_UINT(1u, obs.reparented); /* still 1: rejected */
	}

	/* Delete: destroys the resource (repository cascade) + observers. */
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		u32 before = ops->entity_count(window);
		ops->select_entity(app, boot.api, window, scene.camera, 1, NULL);
		ops->delete_entity(app, boot.api, window, NULL);
		TEST_ASSERT_EQUAL_UINT(before - 1u, ops->entity_count(window));
		TEST_ASSERT_EQUAL_INT(0, repo->has_resource(scene.repo, scene.camera));
		TEST_ASSERT_EQUAL_UINT(1u, obs.deleted);
		TEST_ASSERT_EQUAL_UINT64(scene.camera.id, obs.last_rid.id);
	}

	/* Selection on real rids. */
	ops->select_entity(app, boot.api, window, scene.hand, 1, NULL);
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_selected_entities(window, sel, 8u));
	TEST_ASSERT_EQUAL_UINT64(scene.hand.id, sel[0].id);

	editor->window_close(app, boot.api, window);
	sk_editor_entity_tree_shutdown(app, boot.api);
	ett_scene_destroy(&scene);
	sk_app_shutdown(app);
}

/* ui-hosted path: the window chrome is attached directly to a ui context
 * (no dock layer — the shell covers docking separately), so the tree rows
 * have real geometry and click/right-click/drag/rename/search are exercised
 * through input dispatch. */
SK_TEST(editor_entity_tree_ui_dock_tree) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_entity_tree_ops_t* ops;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_editor_window_t* window;
	ett_obs_t obs;
	ett_obs_set_t obs_set;
	sk_ui_node_t chrome;
	sk_ui_node_t tree;
	sk_ui_node_t row;
	sk_ui_rect_t r;
	sk_rid_t root;
	sk_rid_t player;
	sk_rid_t light;
	sk_ui_input_event_t ev;
	sk_ui_style_props_t p;
	const_chr_t plugin_name;
	i32 open = 1;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	/* Load the ui plugin (skipped when not built next to the tests). */
	{
		const sk_filesystem_api_t* fs = sk_test_filesystem_table();
		char base[SK_FS_PATH_MAX];
		char plugin[SK_FS_PATH_MAX];
		char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
		plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
		plugin_name = "sk-ui.dylib";
#else
		plugin_name = "sk-ui.so";
#endif
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			base[0] = '\0';
		}
		if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugin, (u32)sizeof(plugin)) < 0) {
			plugin[0] = '\0';
		}
		(void)sk_path_join(sk_str_view_cstr(plugin), sk_str_view_cstr(plugin_name), path, (u32)sizeof(path));
		(void)boot.api->load_plugin(app, path);
	}
	ui = (const sk_ui_api_t*)boot.api->get_api(app, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		sk_app_shutdown(app);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, ui->init());
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	sk_editor_workspace_register_impls(app, boot.api);
	sk_editor_entity_tree_register(app, boot.api);
	ops = sk_editor_entity_tree_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	/* Active workspace hosting the window chrome (no dockspace model). */
	ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(ws);
	sk_editor_workspace_set_dock_context(ws, ctx);
	sk_editor_workspace_switch(ws);

	/* Attach the editor-window chrome directly to the context root and size
	 * it, so the window's own chrome builds inside a laid-out subtree. */
	chrome = ui->widget_editor_window(ctx, ui->context_root(ctx), "Entity Tree", "sk.editor_window.entity_tree");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(chrome));
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
	p.layout.position = SK_UI_POSITION_ABSOLUTE;
	p.layout.left = sk_ui_pt(30.0f);
	p.layout.top = sk_ui_pt(40.0f);
	p.layout.width = sk_ui_pt(340.0f);
	p.layout.height = sk_ui_pt(560.0f);
	p.layout.flex_grow = 0.0f;
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_merge_inline_style(ctx, chrome, &p);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(1, open);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));

	tree = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.tree");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tree));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.add")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.search")));

	/* Mock scene rows are materialized (root + 3 direct children; the
	 * Character Mesh sits under the still-collapsed Player row). */
	root = ops->get_root(window);
	TEST_ASSERT_TRUE(root.id != 0u);
	TEST_ASSERT_TRUE(ui->item_bind_row_count(ctx, tree) >= 4u);
	TEST_ASSERT_TRUE(ui->item_bind_get_open(ctx, tree, root.id)); /* root expanded by default */
	player = (sk_rid_t){ET_MOCK_BASE | 4ull};					  /* Player */
	light = (sk_rid_t){ET_MOCK_BASE | 3ull};					  /* Directional Light */
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, tree, player.id)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->item_bind_find(ctx, tree, light.id)));

	/* Per-row vis/lock toggle buttons exist (child chrome of the rows). */
	{
		char idbuf[80];
		(void)snprintf(idbuf, sizeof(idbuf), "et.row.vis.%llu", player.id);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, idbuf)));
		(void)snprintf(idbuf, sizeof(idbuf), "et.row.lock.%llu", player.id);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, idbuf)));
	}

	ett_register_observers(app, boot.api, &obs, &obs_set);

	/* Row click selects + publishes observers. */
	row = ui->item_bind_find(ctx, tree, player.id);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, row, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 1.0f && r.height > 1.0f);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = r.x + r.width * 0.5f;
	ev.y = r.y + r.height * 0.5f;
	(void)ui->input_dispatch(ctx, &ev);
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	(void)ui->input_dispatch(ctx, &ev);
	ev.down = 0;
	(void)ui->input_dispatch(ctx, &ev);
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_UINT64(player.id, ops->get_last_selected(window).id);
	TEST_ASSERT_EQUAL_UINT(1u, obs.sel_changed);
	TEST_ASSERT_EQUAL_UINT(1u, obs.entity_sel);
	TEST_ASSERT_EQUAL_UINT64(player.id, obs.last_rid.id);

	/* Vis toggle button click flips the session flag without selecting. */
	{
		char idbuf[80];
		sk_ui_node_t vis;
		sk_ui_rect_t vr;
		TEST_ASSERT_EQUAL_INT(1, ops->is_entity_active(window, player));
		(void)snprintf(idbuf, sizeof(idbuf), "et.row.vis.%llu", player.id);
		vis = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, idbuf);
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(vis));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, vis, &vr, NULL));
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = vr.x + vr.width * 0.5f;
		ev.y = vr.y + vr.height * 0.5f;
		(void)ui->input_dispatch(ctx, &ev);
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 1;
		(void)ui->input_dispatch(ctx, &ev);
		ev.down = 0;
		(void)ui->input_dispatch(ctx, &ev);
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_INT(0, ops->is_entity_active(window, player));
		TEST_ASSERT_EQUAL_UINT64(player.id, ops->get_last_selected(window).id); /* selection unchanged */
	}

	/* Right-click on a row selects it and opens the context menu. */
	{
		sk_ui_node_t row2 = ui->item_bind_find(ctx, tree, light.id);
		sk_ui_rect_t rr;
		sk_ui_node_t menu;
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(row2));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, row2, &rr, NULL));
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = rr.x + rr.width * 0.5f;
		ev.y = rr.y + rr.height * 0.5f;
		(void)ui->input_dispatch(ctx, &ev);
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.button = SK_UI_POINTER_BUTTON_RIGHT;
		ev.down = 1;
		(void)ui->input_dispatch(ctx, &ev);
		ev.down = 0;
		(void)ui->input_dispatch(ctx, &ev);
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_UINT64(light.id, ops->get_last_selected(window).id);
		menu = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.context");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(menu));
		TEST_ASSERT_EQUAL_INT(1, ui->menu_get_open(ctx, menu));
	}

	/* Drag Player onto Directional Light: reparent through the payload. */
	{
		sk_ui_node_t src = ui->item_bind_find(ctx, tree, player.id);
		sk_ui_node_t dst = ui->item_bind_find(ctx, tree, light.id);
		sk_ui_rect_t sr;
		sk_ui_rect_t dr;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, src, &sr, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dst, &dr, NULL));
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = sr.x + sr.width * 0.5f;
		ev.y = sr.y + sr.height * 0.5f;
		(void)ui->input_dispatch(ctx, &ev);
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 1;
		(void)ui->input_dispatch(ctx, &ev);
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = sr.x + sr.width * 0.5f + 24.0f;
		ev.y = sr.y + sr.height * 0.5f;
		(void)ui->input_dispatch(ctx, &ev);
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = dr.x + dr.width * 0.5f;
		ev.y = dr.y + dr.height * 0.5f;
		(void)ui->input_dispatch(ctx, &ev);
		TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.down = 0;
		(void)ui->input_dispatch(ctx, &ev);
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_UINT(1u, obs.reparented);
		TEST_ASSERT_EQUAL_UINT64(player.id, obs.last_rid.id);
		TEST_ASSERT_EQUAL_UINT64(light.id, obs.last_parent.id);
	}

	/* Rename overlay: ops->set_rename_entity arms it; draw builds the input
	 * over the row; focus loss commits. */
	{
		sk_ui_node_t rename_input;
		sk_ui_node_t tree2;
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));
		ops->set_rename_entity(app, boot.api, window, player, NULL);
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));
		/* The row was collapsed under Directional Light after the drag;
		 * open_ancestors materializes it, then the overlay input builds. */
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));
		rename_input = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.rename");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(rename_input));
		TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, rename_input, "Hero2"));
		tree2 = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.tree");
		TEST_ASSERT_EQUAL_INT(0, ui->action_focus(ctx, tree2));
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_STRING("Hero2", ops->entity_name_at(window, 3u));
		TEST_ASSERT_EQUAL_UINT(1u, obs.renamed);
		TEST_ASSERT_EQUAL_STRING("Hero2", obs.last_name);
	}

	/* Search filter collapses the tree to matching leaves. */
	{
		sk_ui_node_t search = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "et.search");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(search));
		TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, search, "Camera"));
		window->draw(window, &open);
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 1200.0f, 760.0f));
		TEST_ASSERT_TRUE(ops->entity_count(window) >= 1u);
		TEST_ASSERT_TRUE(ops->entity_count(window) < 6u);
		TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, search, ""));
		window->draw(window, &open);
	}

	editor->window_close(app, boot.api, window);
	editor->workspace_destroy(ws);
	ui->context_destroy(ctx);
	ui->shutdown();
	sk_editor_entity_tree_shutdown(app, boot.api);
	sk_app_shutdown(app);
}

static void ett_on_dirty(void* user, u32 workspace_id) {
	u32* count = (u32*)user;
	(void)workspace_id;
	(*count)++;
}

/* Cross-window: selection / structure / inspect / view-entity / dirty go
 * through add_impl observer structs and per-window ops tables only. */
SK_TEST(editor_windows_cross_notify_and_ops) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_entity_tree_ops_t* et;
	const sk_editor_properties_ops_t* props;
	const sk_editor_scene_view_ops_t* sv;
	const sk_editor_resource_debugger_ops_t* rd;
	sk_editor_window_t* tree;
	sk_editor_window_t* inspector;
	sk_editor_window_t* view;
	sk_editor_window_t* dbg;
	sk_rid_t player;
	sk_rid_t created;
	u32 dirty = 0u;
	sk_editor_on_dirty_t on_dirty;
	et_class_state_t* cls;
	u32 i;
	i32 fired = 0;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	sk_editor_entity_tree_register(app, boot.api);
	sk_editor_properties_register(app, boot.api);
	sk_editor_scene_view_register(app, boot.api);
	sk_editor_resource_debugger_register(app, boot.api);

	et = sk_editor_entity_tree_ops(app, boot.api);
	props = sk_editor_properties_ops(app, boot.api);
	sv = sk_editor_scene_view_ops(app, boot.api);
	rd = sk_editor_resource_debugger_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(et);
	TEST_ASSERT_NOT_NULL(props);
	TEST_ASSERT_NOT_NULL(sv);
	TEST_ASSERT_NOT_NULL(rd);

	tree = et->open(app, boot.api);
	inspector = props->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(tree);
	TEST_ASSERT_NOT_NULL(inspector);

	memset(&on_dirty, 0, sizeof(on_dirty));
	on_dirty.user = &dirty;
	on_dirty.on_dirty = ett_on_dirty;
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_DIRTY, &on_dirty);

	player = et->entity_rid_at(tree, 3u); /* Player in the mock scene */
	TEST_ASSERT_TRUE(player.id != 0u);
	et->select_entity(app, boot.api, tree, player, 1, NULL);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_ENTITY, (int)props->get_kind(inspector));
	TEST_ASSERT_TRUE(props->get_selected_rid(inspector).id == player.id);

	/* Structure: rename refreshes the inspector without changing selection. */
	et->rename_entity(app, boot.api, tree, player, "Hero");
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_ENTITY, (int)props->get_kind(inspector));
	TEST_ASSERT_EQUAL_STRING("Hero", et->entity_name_at(tree, 3u));

	/* Inspect routes through the Resource Debugger ops table. */
	cls = et_class(app, boot.api);
	for (i = 0u; i < cls->menu_count; ++i) {
		if (cls->menus[i].item_name != NULL && strcmp(cls->menus[i].item_name, "Show Resource Inspector") == 0 && cls->menus[i].action != NULL) {
			cls->menus[i].action(app, boot.api, tree, cls->menus[i].user);
			fired = 1;
			break;
		}
	}
	TEST_ASSERT_EQUAL_INT(1, fired);
	dbg = editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER);
	TEST_ASSERT_NOT_NULL(dbg);
	TEST_ASSERT_TRUE(rd->get_selected_instance(dbg).id == player.id);

	/* Second-click / ViewEntity path: ops table, not a direct symbol. */
	et_frame_in_scene_view((et_state_t*)tree->user_data, player);
	view = editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_SCENE_VIEW);
	TEST_ASSERT_NOT_NULL(view);
	TEST_ASSERT_TRUE(sv->get_last_viewed_entity(view).id == player.id);

	/* Create marks dirty; delete of the selected entity clears the inspector. */
	created = et->create_entity(app, boot.api, tree, player, NULL);
	TEST_ASSERT_TRUE(created.id != 0u);
	TEST_ASSERT_TRUE(dirty > 0u);
	et->select_entity(app, boot.api, tree, created, 1, NULL);
	TEST_ASSERT_TRUE(props->get_selected_rid(inspector).id == created.id);
	et->delete_entity(app, boot.api, tree, NULL);
	TEST_ASSERT_EQUAL_INT((int)SK_EDITOR_PROPERTIES_NONE, (int)props->get_kind(inspector));

	/* Close the inspector: observers detach; notify is a no-op on the dead window. */
	editor->window_close(app, boot.api, inspector);
	et->select_entity(app, boot.api, tree, player, 1, NULL);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PROPERTIES));

	boot.api->remove_impl(app, SK_EDITOR_NOTIFY_DIRTY, &on_dirty);
	editor->window_close(app, boot.api, tree);
	if (view != NULL) {
		editor->window_close(app, boot.api, view);
	}
	if (dbg != NULL) {
		editor->window_close(app, boot.api, dbg);
	}
	sk_editor_entity_tree_shutdown(app, boot.api);
	sk_editor_properties_shutdown(app, boot.api);
	sk_editor_scene_view_shutdown(app, boot.api);
	sk_editor_resource_debugger_shutdown(app, boot.api);
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
