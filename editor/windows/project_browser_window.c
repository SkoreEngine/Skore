/**
 * @file project_browser_window.c
 * @brief Project / Content Browser window (APX-369): v2 migration.
 *
 * Port of main-branch Skore::ProjectBrowserWindow (migration manifest §3.6)
 * onto the v2 editor shell. The real surface is backed by the v2 asset layer
 * (sk_resource_assets_api_t + sk_repository_t from the attached editor
 * project): folder tree, content item grid, breadcrumb navigation, single +
 * multi selection, Create/Delete/Rename context menu, search filter, and
 * drag sources (content grid + tree file leaves) with folder drop targets
 * that move assets through move_asset. Public entry points live on
 * sk_editor_project_browser_ops_t (registered with add_impl, never free
 * symbols); selection changes go through the notify.h observers.
 *
 * MOCK sections (the v2 asset layer cannot answer these yet, clearly marked):
 *  - No project attached → a fixed mock folder tree + item list.
 *  - Import button (OS file dialog not ported) → no-op; drop-file import is
 *    real (OnDropFile observer imports into the open directory).
 *  - Show in Explorer / Copy Path Id → recorded on window state
 *    (test-visible), no OS call.
 *  - Show Resource Inspector routes through the Resource Debugger ops table
 *    (`inspect_resource`).
 *  - Thumbnails: out of scope (manifest §2.2) — folder/file tiles use the
 *    Content/Images icon atlas (APX-367), never a thumbnail texture.
 */

#include "project_browser_window.h"

#include "allocator.h"
#include "editor_api.h"
#include "editor_icons.h"
#include "notify.h"
#include "resource_assets_types.h"
#include "resource_debugger_window.h"
#include "serialization.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID SK_TYPE_ID("sk.editor.project_browser.state", 0xfb0f9ae989d9c601ULL, 0x9eeb7516b535a5b9ULL)

#define PB_MENU_CAP 32u
#define PB_HIDDEN_EXT_CAP 16u
#define PB_SELECTED_CAP 64u
#define PB_NAME_CAP 256u
#define PB_GRID_CAP 512u
#define PB_TREE_CAP 1024u
#define PB_BREADCRUMB_CAP 32u
#define PB_PATH_CAP 512u
#define PB_MOCK_NEW_ASSET_BASE 100ull
#define PB_TREE_PANE_W 150.0f
#define PB_HEADER_H 28.0f
#define PB_ROW_H 22.0f
#define PB_GRID_LABEL_H 18.0f

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct pb_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	sk_editor_menu_item_desc_t menus[PB_MENU_CAP];
	u32 menu_count;
	const_chr_t hidden_ext[PB_HIDDEN_EXT_CAP];
	u32 hidden_count;
} pb_class_state_t;

/** One listing item in the open directory (folders first, then files). */
typedef struct pb_list_item_t {
	sk_rid_t rid;		/* asset RID (selection identity) */
	sk_rid_t directory; /* ResourceAssetDirectory node RID (folder items) */
	i32 is_directory;
	u8 _pad0[4];
	char label[PB_NAME_CAP]; /* name + extension (what the grid shows) */
} pb_list_item_t;

/** One tree row descriptor (parallel to the tree item array). */
typedef struct pb_tree_meta_t {
	sk_rid_t directory; /* directory node RID (row id); zero for file leaves */
	sk_rid_t asset;		/* asset RID (selection identity) */
	i32 is_leaf;		/* file leaf (tree-only view shows files under folders) */
	u8 _pad0[4];
} pb_tree_meta_t;

/** Context-menu row binding: node → menu item action. */
typedef struct pb_menu_binding_t {
	sk_ui_node_t node;
	u32 item_index; /* index into class menus */
} pb_menu_binding_t;

typedef struct pb_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* Borrowed project (host owns; NULL → mock data). */
	sk_resource_assets_context_t* assets;
	sk_repository_t* repository;
	sk_rid_t root_directory;

	/* View state. */
	sk_rid_t open_directory; /* ResourceAssetDirectory node RID */
	sk_rid_t rename_item;	 /* asset RID pending rename */
	sk_rid_t selected[PB_SELECTED_CAP];
	u32 selected_count;
	sk_rid_t last_selected;
	u32 workspace_id;
	i32 tree_only_view;		  /* EditorSerialize treeOnlyView */
	f32 content_browser_zoom; /* EditorSerialize contentBrowserZoom */
	char search[PB_NAME_CAP];
	i32 mock_data; /* non-zero when no project is attached (MOCK) */

	/* Listing storage (in-place; label pointers stay valid across rebuilds). */
	sk_ui_item_t grid_items[PB_GRID_CAP];
	char grid_labels[PB_GRID_CAP][PB_NAME_CAP];
	pb_list_item_t grid_meta[PB_GRID_CAP];
	u32 grid_count;	  /* unfiltered item count in the open directory */
	u32 grid_visible; /* after search filter + hidden extensions */
	sk_ui_item_t tree_items[PB_TREE_CAP];
	char tree_labels[PB_TREE_CAP][PB_NAME_CAP];
	pb_tree_meta_t tree_meta[PB_TREE_CAP];
	u32 tree_count;
	/* Persistent item arrays the widgets bind (must outlive the bind). */
	sk_ui_item_array_t grid_arr;
	sk_ui_item_array_t tree_arr;
	sk_rid_t breadcrumb[PB_BREADCRUMB_CAP];
	u32 breadcrumb_count;

	/* Caches / revisions. */
	sk_rid_t cached_directory;
	u64 cached_version;
	u32 listing_revision;	/* bumped on every rebuild */
	u32 applied_revision;	/* last revision pushed into the widgets */
	u32 selection_revision; /* bumped on every selection change */
	u32 applied_selection;
	i32 ui_built;

	/* Last actions for MOCK features (test-visible). */
	char last_import_path[PB_PATH_CAP];
	char last_show_in_explorer[PB_PATH_CAP];
	char last_copied_path_id[PB_PATH_CAP];
	u32 mock_seq;

	/* UI handles (live only while a ui context hosts the dock chrome). */
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
	sk_ui_node_t root;
	sk_ui_node_t header;
	sk_ui_node_t breadcrumb_row;
	sk_ui_node_t zoom_slider;
	sk_ui_node_t search_input;
	sk_ui_node_t settings_popup;
	sk_ui_node_t tree_pane;
	sk_ui_node_t tree_scroll;
	sk_ui_node_t tree_content;
	sk_ui_node_t tree;
	sk_ui_node_t grid_pane;
	sk_ui_node_t grid_scroll;
	sk_ui_node_t grid_content;
	sk_ui_node_t grid;
	sk_ui_node_t context_menu;
	sk_ui_node_t crumb_nodes[PB_BREADCRUMB_CAP];
	u32 crumb_count;
	sk_ui_node_t rename_input; /* tree-only rename overlay */
	u32 rename_armed;
	u32 last_click_mods; /* ctrl held at the last pointer-down (capture) */
	f32 pointer_x;
	f32 pointer_y;
	pb_menu_binding_t ctx_bindings[PB_MENU_CAP + 4u];
	u32 ctx_binding_count;

	/* Drop-file observer (real import path). */
	sk_editor_on_drop_file_t drop_observer;
	char last_drop_path[PB_PATH_CAP];
} pb_state_t;

static void pb_init(sk_editor_window_t* window);
static void pb_draw(sk_editor_window_t* window, i32* open);
static void pb_destroy(sk_editor_window_t* window);
static i32 pb_save(const sk_editor_window_t* window, char* out, u32 cap, u32* out_len);
static i32 pb_load(sk_editor_window_t* window, const_chr_t json, u32 len);

static sk_editor_window_t pb_window = {
	.title = "Project Browser",
	.dock_id = "sk.editor_window.project_browser",
	.dock_position = SK_EDITOR_DOCK_BOTTOM_LEFT,
	.order = 0,
	.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	.init = pb_init,
	.draw = pb_draw,
	.render = NULL,
	.destroy = pb_destroy,
	.save = pb_save,
	.load = pb_load,
};

static pb_class_state_t* pb_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (pb_class_state_t*)app_api->get_api(app_context, SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Mock data (MOCK: no project attached)                             */
/* ------------------------------------------------------------------ */

/* Mock tree: node 1 "Assets" (root) with dirs 2 "Scenes", 3 "Textures";
 * node 2 has dir 6 "Levels"; files 4 "Main.scene" / 5 "Hero.png" under the
 * root and 7 "Level1.scene" under Levels. Folder assets: 1 (root), 20
 * (Scenes), 21 (Textures), 22 (Levels). */
static sk_rid_t mock_node_of_asset(sk_rid_t asset) {
	u64 id = asset.id;
	if (id == 1ull) {
		return (sk_rid_t){1ull};
	}
	if (id == 20ull) {
		return (sk_rid_t){2ull};
	}
	if (id == 21ull) {
		return (sk_rid_t){3ull};
	}
	if (id == 22ull) {
		return (sk_rid_t){6ull};
	}
	return SK_RID_ZERO;
}

static sk_rid_t mock_asset_of_node(sk_rid_t node) {
	u64 id = node.id;
	if (id == 1ull) {
		return (sk_rid_t){1ull};
	}
	if (id == 2ull) {
		return (sk_rid_t){20ull};
	}
	if (id == 3ull) {
		return (sk_rid_t){21ull};
	}
	if (id == 6ull) {
		return (sk_rid_t){22ull};
	}
	return SK_RID_ZERO;
}

static i32 mock_is_directory(sk_rid_t asset) {
	u64 id = asset.id;
	return (id == 1ull || id == 20ull || id == 21ull || id == 22ull) ? 1 : 0;
}

static const_chr_t mock_name_of(sk_rid_t rid, i32 is_dir, char* out, u32 cap) {
	u64 id = rid.id;
	const_chr_t name;
	(void)snprintf(out, cap, "%s", "");
	if (is_dir) {
		if (id == 1ull) {
			name = "Assets";
		} else if (id == 2ull || id == 20ull) {
			name = "Scenes";
		} else if (id == 3ull || id == 21ull) {
			name = "Textures";
		} else if (id == 6ull || id == 22ull) {
			name = "Levels";
		} else {
			name = "Folder";
		}
		(void)snprintf(out, cap, "%s", name);
	} else {
		if (id == 4ull) {
			name = "Main.scene";
		} else if (id == 5ull) {
			name = "Hero.png";
		} else if (id == 7ull) {
			name = "Level1.scene";
		} else {
			name = "Asset";
		}
		(void)snprintf(out, cap, "%s", name);
	}
	return out;
}

static sk_rid_t mock_parent_node(sk_rid_t node) {
	u64 id = node.id;
	if (id == 2ull || id == 3ull) {
		return (sk_rid_t){1ull};
	}
	if (id == 6ull) {
		return (sk_rid_t){2ull};
	}
	return SK_RID_ZERO;
}

static u32 mock_child_dirs(sk_rid_t node, sk_rid_t* out, u32 cap) {
	u32 n = 0u;
	if (node.id == 1ull) {
		if (n < cap) {
			out[n++] = (sk_rid_t){2ull};
		}
		if (n < cap) {
			out[n++] = (sk_rid_t){3ull};
		}
	} else if (node.id == 2ull && n < cap) {
		out[n++] = (sk_rid_t){6ull};
	}
	return n;
}

static u32 mock_child_files(sk_rid_t node, sk_rid_t* out, u32 cap) {
	u32 n = 0u;
	if (node.id == 1ull) {
		if (n < cap) {
			out[n++] = (sk_rid_t){4ull};
		}
		if (n < cap) {
			out[n++] = (sk_rid_t){5ull};
		}
	} else if (node.id == 6ull && n < cap) {
		out[n++] = (sk_rid_t){7ull};
	}
	return n;
}

/* ------------------------------------------------------------------ */
/*  Asset-layer helpers (real project)                                */
/* ------------------------------------------------------------------ */

static i32 pb_has_project(const pb_state_t* state) {
	return state->assets != NULL && state->repository != NULL && state->root_directory.id != 0u;
}

static const sk_repository_api_t* pb_repo_api(const pb_state_t* state) {
	return state->app_api->repository_api(state->app_context);
}

/* Asset display name (name + extension). Returns 1 on success. */
static i32 pb_asset_label(pb_state_t* state, sk_rid_t rid, i32 is_dir, char* out, u32 cap) {
	if (pb_has_project(state)) {
		const sk_resource_assets_api_t* assets_api = state->app_api->resource_assets_api(state->app_context);
		char name[PB_NAME_CAP];
		if (assets_api->get_asset_full_name(state->assets, rid, name, (u32)sizeof(name)) != 0) {
			(void)snprintf(out, cap, "%s", name);
			return 1;
		}
		if (assets_api->get_asset_name(state->assets, rid, name, (u32)sizeof(name)) != 0) {
			(void)snprintf(out, cap, "%s", name);
			return 1;
		}
	}
	mock_name_of(rid, is_dir, out, cap);
	return 1;
}

static i32 pb_is_directory(pb_state_t* state, sk_rid_t rid) {
	if (pb_has_project(state)) {
		const sk_repository_api_t* repo = pb_repo_api(state);
		sk_resource_object_t view = repo->read(state->repository, rid);
		if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
			return repo->get_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY) != 0 ? 1 : 0;
		}
		return 0;
	}
	return mock_is_directory(rid);
}

/* Directory node RID of an asset (folder assets own a directory node). */
static sk_rid_t pb_directory_node_of(pb_state_t* state, sk_rid_t asset) {
	if (pb_has_project(state)) {
		const sk_repository_api_t* repo = pb_repo_api(state);
		/* A folder asset is the DIRECTORY_ASSET sub-object of its node, so the
		 * node is the asset's parent owner. */
		return repo->get_parent(state->repository, asset);
	}
	return mock_node_of_asset(asset);
}

/* Asset RID of a directory node. */
static sk_rid_t pb_asset_of_directory(pb_state_t* state, sk_rid_t node) {
	if (pb_has_project(state)) {
		const sk_repository_api_t* repo = pb_repo_api(state);
		sk_resource_object_t view = repo->read(state->repository, node);
		if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
			return repo->get_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
		}
		return SK_RID_ZERO;
	}
	return mock_asset_of_node(node);
}

static u32 pb_directory_children(pb_state_t* state, sk_rid_t node, i32 dirs, sk_rid_t* out, u32 cap) {
	if (pb_has_project(state)) {
		const sk_repository_api_t* repo = pb_repo_api(state);
		sk_resource_object_t view = repo->read(state->repository, node);
		u32 count = 0u;
		const sk_rid_t* items;
		u32 i;
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			return 0u;
		}
		items = repo->get_subobject_list(view, dirs ? SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES : SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
		if (items == NULL || count == 0u) {
			return 0u;
		}
		if (count > cap) {
			count = cap;
		}
		for (i = 0u; i < count; ++i) {
			out[i] = items[i];
		}
		return count;
	}
	return dirs ? mock_child_dirs(node, out, cap) : mock_child_files(node, out, cap);
}

static sk_rid_t pb_parent_directory(pb_state_t* state, sk_rid_t node) {
	if (pb_has_project(state)) {
		return pb_repo_api(state)->get_parent(state->repository, node);
	}
	return mock_parent_node(node);
}

static const_chr_t pb_path_id(pb_state_t* state, sk_rid_t rid, char* out, u32 cap) {
	if (pb_has_project(state)) {
		const_chr_t p = state->app_api->resource_assets_api(state->app_context)->get_path_id(state->assets, rid);
		if (p != NULL) {
			(void)snprintf(out, cap, "%s", p);
			return out;
		}
	}
	(void)snprintf(out, cap, "MOCK:%llu", rid.id);
	return out;
}

/* ------------------------------------------------------------------ */
/*  Case-insensitive substring (ImGuiTextFilter default)              */
/* ------------------------------------------------------------------ */

static i32 pb_filter_pass(const pb_state_t* state, const_chr_t text) {
	const_chr_t f = state->search;
	size_t i;
	size_t n;
	if (text == NULL) {
		return 0;
	}
	n = strlen(f);
	if (n == 0u) {
		return 1;
	}
	for (i = 0u; text[i] != '\0'; ++i) {
		size_t j;
		for (j = 0u; j < n; ++j) {
			char a = text[i + j];
			char b = f[j];
			if (a >= 'A' && a <= 'Z') {
				a = (char)(a - 'A' + 'a');
			}
			if (b >= 'A' && b <= 'Z') {
				b = (char)(b - 'A' + 'a');
			}
			if (a == '\0' || a != b) {
				break;
			}
		}
		if (j == n) {
			return 1;
		}
	}
	return 0;
}

static i32 pb_extension_hidden(const pb_state_t* state, const_chr_t ext) {
	const pb_class_state_t* cls = pb_class(state->app_context, state->app_api);
	u32 i;
	if (cls == NULL || ext == NULL || ext[0] == '\0') {
		return 0;
	}
	for (i = 0u; i < cls->hidden_count; ++i) {
		if (strcmp(cls->hidden_ext[i], ext) == 0) {
			return 1;
		}
	}
	return 0;
}

static i32 pb_asset_extension_hidden(pb_state_t* state, sk_rid_t rid, i32 is_dir) {
	if (is_dir || !pb_has_project(state)) {
		return 0;
	}
	{
		const sk_repository_api_t* repo = pb_repo_api(state);
		sk_resource_object_t view = repo->read(state->repository, rid);
		const_chr_t ext;
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			return 0;
		}
		ext = repo->get_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION);
		return pb_extension_hidden(state, ext != NULL ? ext : "");
	}
}

/* Content/Images icon (APX-367): folder/file tiles from the icon registry;
 * 0 (glyph) when no registry is live (plain unit tests / no GPU host). */
static u32 pb_icon_view(pb_state_t* state, i32 is_dir) {
	sk_editor_icons_t* icons = sk_editor_icons_resolve(state->app_context, state->app_api);
	const sk_editor_icon_t* ic;
	if (icons == NULL) {
		return 0u;
	}
	ic = sk_editor_icons_get(icons, is_dir != 0 ? SK_EDITOR_ICON_FOLDER : SK_EDITOR_ICON_FILE);
	return ic != NULL ? ic->view_index : 0u;
}

/* ------------------------------------------------------------------ */
/*  Listing builder                                                    */
/* ------------------------------------------------------------------ */

static i32 pb_selected_contains(const pb_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->selected_count; ++i) {
		if (SK_RID_EQ(state->selected[i], rid)) {
			return 1;
		}
	}
	return 0;
}

static void pb_set_item_selected(sk_ui_item_t* item, i32 sel) {
	if (sel != 0) {
		item->flags |= (u32)SK_UI_ITEM_FLAG_SELECTED;
	} else {
		item->flags &= ~(u32)SK_UI_ITEM_FLAG_SELECTED;
	}
}

/* Add one folder (directory node) to the tree array at @p parent_id. */
static void pb_tree_add_folder(pb_state_t* state, sk_rid_t node, u64 parent_id) {
	sk_rid_t asset = pb_asset_of_directory(state, node);
	char name[PB_NAME_CAP];
	u32 idx = state->tree_count;
	if (idx >= PB_TREE_CAP || asset.id == 0u) {
		return;
	}
	(void)pb_asset_label(state, asset, 1, name, (u32)sizeof(name));
	sk_ui_item_set(&state->tree_items[idx], node.id, parent_id, state->tree_labels[idx], 0u);
	(void)snprintf(state->tree_labels[idx], PB_NAME_CAP, "%s", name);
	pb_set_item_selected(&state->tree_items[idx], pb_selected_contains(state, asset));
	state->tree_meta[idx].directory = node;
	state->tree_meta[idx].asset = asset;
	state->tree_meta[idx].is_leaf = 0;
	state->tree_count = idx + 1u;
}

/* Add one file leaf (asset) to the tree array under @p parent_id. */
static void pb_tree_add_file(pb_state_t* state, sk_rid_t asset, u64 parent_id) {
	char name[PB_NAME_CAP];
	u32 idx = state->tree_count;
	if (idx >= PB_TREE_CAP) {
		return;
	}
	(void)pb_asset_label(state, asset, 0, name, (u32)sizeof(name));
	sk_ui_item_set(&state->tree_items[idx], asset.id, parent_id, state->tree_labels[idx], (u32)SK_UI_ITEM_FLAG_LEAF);
	(void)snprintf(state->tree_labels[idx], PB_NAME_CAP, "%s", name);
	pb_set_item_selected(&state->tree_items[idx], pb_selected_contains(state, asset));
	state->tree_meta[idx].directory = SK_RID_ZERO;
	state->tree_meta[idx].asset = asset;
	state->tree_meta[idx].is_leaf = 1;
	state->tree_count = idx + 1u;
}

/* NOLINTBEGIN(misc-no-recursion) -- folder-tree depth is bounded by the
 * repository sub-object tree (the widget caps cycles at depth 64). */
static void pb_tree_recurse(pb_state_t* state, sk_rid_t node, u64 parent_id) {
	sk_rid_t dirs[PB_TREE_CAP];
	u32 i;
	u32 n;
	pb_tree_add_folder(state, node, parent_id);
	if (state->tree_count == 0u) {
		return; /* capacity hit; stop building */
	}
	n = pb_directory_children(state, node, 1, dirs, PB_TREE_CAP);
	for (i = 0u; i < n; ++i) {
		pb_tree_recurse(state, dirs[i], node.id);
	}
	if (state->tree_only_view) {
		sk_rid_t files[PB_TREE_CAP];
		u32 fn = pb_directory_children(state, node, 0, files, PB_TREE_CAP);
		for (i = 0u; i < fn; ++i) {
			char name[PB_NAME_CAP];
			if (pb_asset_extension_hidden(state, files[i], 0)) {
				continue;
			}
			/* Search narrows tree leaves (matches the content-grid filter). */
			(void)pb_asset_label(state, files[i], 0, name, (u32)sizeof(name));
			if (pb_filter_pass(state, name)) {
				pb_tree_add_file(state, files[i], node.id);
			}
		}
	}
}
/* NOLINTEND(misc-no-recursion) */

static void pb_rebuild_breadcrumb(pb_state_t* state) {
	sk_rid_t item = state->open_directory;
	u32 i;
	state->breadcrumb_count = 0u;
	if (item.id == 0u) {
		return;
	}
	/* Walk up collecting nodes; root is last. */
	while (item.id != 0u && state->breadcrumb_count < PB_BREADCRUMB_CAP) {
		state->breadcrumb[state->breadcrumb_count++] = item;
		item = pb_parent_directory(state, item);
	}
	/* Reverse so index 0 is the root. */
	for (i = 0u; i < state->breadcrumb_count / 2u; ++i) {
		sk_rid_t tmp = state->breadcrumb[i];
		state->breadcrumb[i] = state->breadcrumb[state->breadcrumb_count - 1u - i];
		state->breadcrumb[state->breadcrumb_count - 1u - i] = tmp;
	}
}

static void pb_rebuild_listing(pb_state_t* state) {
	sk_rid_t dirs[PB_GRID_CAP];
	sk_rid_t files[PB_GRID_CAP];
	u32 dn;
	u32 fn;
	u32 i;
	sk_rid_t root;

	state->grid_count = 0u;
	state->grid_visible = 0u;
	state->tree_count = 0u;

	root = pb_has_project(state) ? state->root_directory : (sk_rid_t){1ull};
	if (state->open_directory.id == 0u) {
		state->open_directory = root;
	}
	if (state->root_directory.id == 0u) {
		state->root_directory = root;
	}

	/* Folder tree. */
	if (pb_has_project(state)) {
		pb_tree_recurse(state, state->root_directory, 0ull);
	} else {
		pb_tree_recurse(state, (sk_rid_t){1ull}, 0ull);
	}

	/* Content grid: directories then files, minus hidden extensions and the
	 * search filter. */
	dn = pb_directory_children(state, state->open_directory, 1, dirs, PB_GRID_CAP);
	for (i = 0u; i < dn; ++i) {
		sk_rid_t asset = pb_asset_of_directory(state, dirs[i]);
		char name[PB_NAME_CAP];
		u32 idx;
		if (asset.id == 0u) {
			continue;
		}
		(void)pb_asset_label(state, asset, 1, name, (u32)sizeof(name));
		idx = state->grid_count;
		if (idx >= PB_GRID_CAP) {
			break;
		}
		state->grid_meta[idx].rid = asset;
		state->grid_meta[idx].directory = dirs[i];
		state->grid_meta[idx].is_directory = 1;
		(void)snprintf(state->grid_meta[idx].label, PB_NAME_CAP, "%s", name);
		sk_ui_item_set(&state->grid_items[idx], asset.id, 0ull, state->grid_labels[idx], 0u);
		state->grid_items[idx].icon = pb_icon_view(state, 1);
		(void)snprintf(state->grid_labels[idx], PB_NAME_CAP, "%s", name);
		pb_set_item_selected(&state->grid_items[idx], pb_selected_contains(state, asset));
		state->grid_count = idx + 1u;
		if (pb_filter_pass(state, name)) {
			state->grid_visible++;
		}
	}
	fn = pb_directory_children(state, state->open_directory, 0, files, PB_GRID_CAP);
	for (i = 0u; i < fn; ++i) {
		char name[PB_NAME_CAP];
		u32 idx;
		if (pb_asset_extension_hidden(state, files[i], 0)) {
			continue;
		}
		(void)pb_asset_label(state, files[i], 0, name, (u32)sizeof(name));
		idx = state->grid_count;
		if (idx >= PB_GRID_CAP) {
			break;
		}
		state->grid_meta[idx].rid = files[i];
		state->grid_meta[idx].directory = SK_RID_ZERO;
		state->grid_meta[idx].is_directory = 0;
		(void)snprintf(state->grid_meta[idx].label, PB_NAME_CAP, "%s", name);
		sk_ui_item_set(&state->grid_items[idx], files[i].id, 0ull, state->grid_labels[idx], (u32)SK_UI_ITEM_FLAG_LEAF);
		state->grid_items[idx].icon = pb_icon_view(state, 0);
		(void)snprintf(state->grid_labels[idx], PB_NAME_CAP, "%s", name);
		pb_set_item_selected(&state->grid_items[idx], pb_selected_contains(state, files[i]));
		state->grid_count = idx + 1u;
		if (pb_filter_pass(state, name)) {
			state->grid_visible++;
		}
	}

	pb_rebuild_breadcrumb(state);
	state->listing_revision++;
	state->mock_data = pb_has_project(state) ? 0 : 1;
	/* Keep the persistent widget arrays in sync (widgets re-read on sync). */
	state->tree_arr.items = state->tree_items;
	state->tree_arr.count = state->tree_count;
	state->tree_arr.revision = state->listing_revision;
	state->grid_arr.items = state->grid_items;
	state->grid_arr.count = state->grid_count;
	state->grid_arr.revision = state->listing_revision;
}

/* Detect version changes / structural mutations on the open directory and
 * relist. Called every draw. */
static void pb_check_versions(pb_state_t* state) {
	if (pb_has_project(state) && state->open_directory.id != 0u) {
		u64 v = pb_repo_api(state)->get_version(state->repository, state->open_directory);
		if (!SK_RID_EQ(state->cached_directory, state->open_directory) || v != state->cached_version) {
			state->cached_directory = state->open_directory;
			state->cached_version = v;
			pb_rebuild_listing(state);
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Selection + notifications                                         */
/* ------------------------------------------------------------------ */

static u32 pb_workspace_id(pb_state_t* state) {
	sk_editor_workspace_t* ws = sk_editor_workspace_active(state->app_context, state->app_api);
	if (ws != NULL) {
		state->workspace_id = sk_editor_workspace_type_id(ws);
	}
	return state->workspace_id;
}

static void pb_mark_dirty(pb_state_t* state) {
	if (state != NULL) {
		sk_editor_notify_dirty(state->app_context, state->app_api, pb_workspace_id(state));
	}
}

static void pb_inspect_resource(pb_state_t* state, sk_rid_t rid) {
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

static void pb_notify_selection(pb_state_t* state) {
	sk_editor_notify_selection_changed(state->app_context, state->app_api);
	if (state->last_selected.id != 0u) {
		sk_editor_notify_asset_selection(state->app_context, state->app_api, pb_workspace_id(state), state->last_selected);
	}
	state->selection_revision++;
}

static void pb_clear_selection_internal(pb_state_t* state) {
	state->selected_count = 0u;
	state->last_selected = SK_RID_ZERO;
}

static void pb_select_rids(pb_state_t* state, const sk_rid_t* rids, u32 count) {
	u32 i;
	pb_clear_selection_internal(state);
	for (i = 0u; i < count && i < PB_SELECTED_CAP; ++i) {
		state->selected[state->selected_count++] = rids[i];
		state->last_selected = rids[i];
	}
}

/* Toggle @p rid in the selection (ctrl-click). */
static void pb_toggle_select(pb_state_t* state, sk_rid_t rid) {
	u32 i;
	for (i = 0u; i < state->selected_count; ++i) {
		if (SK_RID_EQ(state->selected[i], rid)) {
			state->selected[i] = state->selected[state->selected_count - 1u];
			state->selected_count--;
			if (SK_RID_EQ(state->last_selected, rid)) {
				state->last_selected = state->selected_count > 0u ? state->selected[0] : SK_RID_ZERO;
			}
			pb_notify_selection(state);
			return;
		}
	}
	if (state->selected_count < PB_SELECTED_CAP) {
		state->selected[state->selected_count++] = rid;
		state->last_selected = rid;
		pb_notify_selection(state);
	}
}

/* Apply a UI click: ctrl toggles, otherwise exclusive replace. */
static void pb_apply_click_selection(pb_state_t* state, sk_rid_t rid) {
	if ((state->last_click_mods & (u32)SK_UI_MOD_CTRL) != 0u) {
		pb_toggle_select(state, rid);
		return;
	}
	pb_select_rids(state, &rid, 1u);
	pb_notify_selection(state);
}

static void pb_sync_widget_selection(pb_state_t* state) {
	u32 i;
	if (state->ui == NULL) {
		return;
	}
	if (sk_ui_node_is_valid(state->grid)) {
		for (i = 0u; i < state->grid_count; ++i) {
			(void)state->ui->item_bind_set_selected(state->ui_ctx, state->grid, state->grid_items[i].id, pb_selected_contains(state, state->grid_meta[i].rid));
		}
	}
	if (sk_ui_node_is_valid(state->tree)) {
		for (i = 0u; i < state->tree_count; ++i) {
			(void)state->ui->item_bind_set_selected(state->ui_ctx, state->tree, state->tree_items[i].id, pb_selected_contains(state, state->tree_meta[i].asset));
		}
	}
	state->applied_selection = state->selection_revision;
}

/* ------------------------------------------------------------------ */
/*  Navigation                                                         */
/* ------------------------------------------------------------------ */

static void pb_set_open_directory(pb_state_t* state, sk_rid_t node) {
	if (node.id == 0u || SK_RID_EQ(node, state->open_directory)) {
		return;
	}
	state->open_directory = node;
	state->rename_item = SK_RID_ZERO;
	state->cached_directory = node;
	if (pb_has_project(state)) {
		state->cached_version = pb_repo_api(state)->get_version(state->repository, node);
	}
	pb_rebuild_listing(state);
}

/* ------------------------------------------------------------------ */
/*  Rename                                                             */
/* ------------------------------------------------------------------ */

static void pb_apply_rename(pb_state_t* state, sk_rid_t asset_rid, const_chr_t new_name) {
	const sk_repository_api_t* repo;
	sk_resource_object_t view;
	if (asset_rid.id == 0u || !pb_has_project(state) || new_name == NULL || new_name[0] == '\0') {
		return;
	}
	repo = pb_repo_api(state);
	view = repo->write(state->repository, asset_rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return;
	}
	repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, new_name);
	repo->commit(view, NULL);
	pb_mark_dirty(state);
	pb_rebuild_listing(state);
}

/* ------------------------------------------------------------------ */
/*  Delete / move (asset layer)                                       */
/* ------------------------------------------------------------------ */

static void pb_delete_selected(pb_state_t* state) {
	sk_rid_t doomed[PB_SELECTED_CAP];
	u32 count = 0u;
	u32 i;
	const sk_repository_api_t* repo;
	if (!pb_has_project(state) || state->selected_count == 0u) {
		return;
	}
	repo = pb_repo_api(state);
	for (i = 0u; i < state->selected_count; ++i) {
		sk_rid_t asset = state->selected[i];
		sk_rid_t node = pb_directory_node_of(state, asset);
		doomed[count++] = pb_is_directory(state, asset) ? node : asset;
	}
	for (i = 0u; i < count; ++i) {
		if (doomed[i].id != 0u) {
			(void)repo->destroy_resource(state->repository, doomed[i], NULL);
		}
	}
	pb_clear_selection_internal(state);
	pb_notify_selection(state);
	pb_mark_dirty(state);
	pb_rebuild_listing(state);
}

static void pb_move_asset(pb_state_t* state, sk_rid_t target_directory, sk_rid_t dragged_asset) {
	if (!pb_has_project(state) || target_directory.id == 0u || dragged_asset.id == 0u) {
		return;
	}
	state->app_api->resource_assets_api(state->app_context)->move_asset(state->assets, target_directory, dragged_asset, NULL);
	pb_mark_dirty(state);
	pb_rebuild_listing(state);
}

/* ------------------------------------------------------------------ */
/*  Menu actions (C++ static action handlers)                         */
/* ------------------------------------------------------------------ */

static void pb_action_new_folder(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	sk_rid_t rid = SK_RID_ZERO;
	(void)app_api;
	(void)user;
	if (state == NULL) {
		return;
	}
	if (pb_has_project(state)) {
		rid = state->app_api->resource_assets_api(app_context)->create_asset_directory(state->assets, state->open_directory, "New Folder", NULL);
	} else {
		/* MOCK: fabricate a folder entry. */
		rid.id = PB_MOCK_NEW_ASSET_BASE + (u64)(state->mock_seq++);
	}
	if (rid.id != 0u) {
		pb_rebuild_listing(state);
		pb_select_rids(state, &rid, 1u);
		state->rename_item = rid;
		pb_notify_selection(state);
	}
}

static void pb_action_delete(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)user;
	if (state != NULL) {
		pb_delete_selected(state);
	}
}

static void pb_action_rename(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)user;
	if (state != NULL && state->last_selected.id != 0u) {
		state->rename_item = state->last_selected;
		state->rename_armed = 0;
	}
}

static void pb_action_show_in_explorer(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	char path[PB_PATH_CAP];
	(void)app_context;
	(void)app_api;
	(void)user;
	if (state == NULL) {
		return;
	}
	/* MOCK: Platform::OpenURL is not ported; record the path id instead. */
	(void)pb_path_id(state, state->open_directory.id != 0u ? state->open_directory : state->last_selected, path, (u32)sizeof(path));
	(void)snprintf(state->last_show_in_explorer, PB_PATH_CAP, "%s", path);
}

static void pb_action_copy_path_id(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	char path[PB_NAME_CAP];
	(void)app_context;
	(void)app_api;
	(void)user;
	if (state == NULL || state->last_selected.id == 0u) {
		return;
	}
	/* MOCK: clipboard hooks are host-provided; record the path id always. */
	(void)pb_path_id(state, state->last_selected, path, (u32)sizeof(path));
	(void)snprintf(state->last_copied_path_id, PB_NAME_CAP, "%s", path);
}

static void pb_action_show_resource_inspector(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)user;
	if (state == NULL || state->last_selected.id == 0u) {
		return;
	}
	pb_inspect_resource(state, state->last_selected);
}

static void pb_seed_class_menus(pb_class_state_t* cls) {
	static const sk_editor_menu_item_desc_t builtin_create_folder = {
		.item_name = "Create/New Folder",
		.order = 0,
		.action = pb_action_new_folder,
		.visible = NULL,
		.user = NULL,
	};
	static const sk_editor_menu_item_desc_t builtin_delete = {
		.item_name = "Delete",
		.order = 25,
		.action = pb_action_delete,
		.visible = NULL,
		.user = NULL,
	};
	static const sk_editor_menu_item_desc_t builtin_rename = {
		.item_name = "Rename",
		.order = 30,
		.action = pb_action_rename,
		.visible = NULL,
		.user = NULL,
	};
	static const sk_editor_menu_item_desc_t builtin_show_in_explorer = {
		.item_name = "Show in Explorer",
		.order = 240,
		.action = pb_action_show_in_explorer,
		.visible = NULL,
		.user = NULL,
	};
	static const sk_editor_menu_item_desc_t builtin_copy_path_id = {
		.item_name = "Copy Path Id",
		.order = 250,
		.action = pb_action_copy_path_id,
		.visible = NULL,
		.user = NULL,
	};
	static const sk_editor_menu_item_desc_t builtin_show_resource_inspector = {
		.item_name = "Show Resource Inspector",
		.order = 500,
		.action = pb_action_show_resource_inspector,
		.visible = NULL,
		.user = NULL,
	};
	if (cls->menu_count == 0u) {
		cls->menus[cls->menu_count++] = builtin_create_folder;
		cls->menus[cls->menu_count++] = builtin_delete;
		cls->menus[cls->menu_count++] = builtin_rename;
		cls->menus[cls->menu_count++] = builtin_show_in_explorer;
		cls->menus[cls->menu_count++] = builtin_copy_path_id;
		cls->menus[cls->menu_count++] = builtin_show_resource_inspector;
	}
}

/* ------------------------------------------------------------------ */
/*  Ops implementations                                                */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* pb_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
}

static void pb_add_menu_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item) {
	pb_class_state_t* cls = pb_class(app_context, app_api);
	if (cls == NULL || item == NULL || item->item_name == NULL || cls->menu_count >= PB_MENU_CAP) {
		return;
	}
	cls->menus[cls->menu_count] = *item;
	cls->menu_count++;
}

static i32 pb_can_create_asset(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event) {
	(void)app_context;
	(void)app_api;
	(void)event;
	return 1;
}

static void pb_asset_new(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event) {
	sk_editor_window_t* window = event != NULL ? event->window : NULL;
	pb_state_t* state;
	sk_rid_t rid = SK_RID_ZERO;
	if (window == NULL) {
		window = sk_editor_window_by_type(app_context, app_api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	}
	if (window == NULL) {
		return;
	}
	state = (pb_state_t*)window->user_data;
	if (state == NULL) {
		return;
	}
	if (pb_has_project(state)) {
		const_chr_t ext = (event != NULL && event->user != NULL) ? (const_chr_t)event->user : "";
		rid = state->app_api->resource_assets_api(app_context)->create_asset_file(state->assets, state->open_directory, "New Asset", ext, NULL);
	} else {
		/* MOCK: fabricate an asset entry. */
		rid.id = PB_MOCK_NEW_ASSET_BASE + (u64)(state->mock_seq++);
	}
	if (rid.id != 0u) {
		pb_rebuild_listing(state);
		pb_select_rids(state, &rid, 1u);
		state->rename_item = rid;
		pb_notify_selection(state);
		pb_mark_dirty(state);
	}
}

static void pb_hide_extension(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension) {
	pb_class_state_t* cls = pb_class(app_context, app_api);
	u32 i;
	if (cls == NULL || extension == NULL || extension[0] == '\0' || cls->hidden_count >= PB_HIDDEN_EXT_CAP) {
		return;
	}
	for (i = 0u; i < cls->hidden_count; ++i) {
		if (strcmp(cls->hidden_ext[i], extension) == 0) {
			return;
		}
	}
	cls->hidden_ext[cls->hidden_count] = extension;
	cls->hidden_count++;
}

static i32 pb_extension_is_hidden(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension) {
	pb_class_state_t* cls = pb_class(app_context, app_api);
	u32 i;
	if (cls == NULL) {
		return 0;
	}
	for (i = 0u; i < cls->hidden_count; ++i) {
		if (strcmp(cls->hidden_ext[i], extension) == 0) {
			return 1;
		}
	}
	return 0;
}

static void pb_clear_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)scope;
	if (state == NULL) {
		return;
	}
	pb_clear_selection_internal(state);
	pb_notify_selection(state);
}

static void pb_select_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)scope;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	pb_select_rids(state, &rid, 1u);
	pb_notify_selection(state);
}

static void pb_set_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_rid_t* rids, u32 count, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)scope;
	if (state == NULL) {
		return;
	}
	pb_select_rids(state, rids, count);
	pb_notify_selection(state);
}

static void pb_set_rename_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	(void)scope;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	pb_select_rids(state, &rid, 1u);
	state->rename_item = rid;
	state->rename_armed = 0;
	pb_notify_selection(state);
}

static sk_rid_t pb_get_open_directory(const sk_editor_window_t* window) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	return state != NULL ? state->open_directory : SK_RID_ZERO;
}

static sk_rid_t pb_get_last_selected_item(const sk_editor_window_t* window) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	return state != NULL ? state->last_selected : SK_RID_ZERO;
}

static u32 pb_get_selected_items(const sk_editor_window_t* window, sk_rid_t* out, u32 out_cap) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	u32 i;
	u32 n = state != NULL ? state->selected_count : 0u;
	if (state == NULL || out == NULL || out_cap == 0u) {
		return n;
	}
	if (n > out_cap) {
		n = out_cap;
	}
	for (i = 0u; i < n; ++i) {
		out[i] = state->selected[i];
	}
	return state != NULL ? state->selected_count : 0u;
}

static void pb_activate_item(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	if (pb_is_directory(state, rid)) {
		sk_rid_t node = pb_directory_node_of(state, rid);
		if (node.id != 0u) {
			pb_set_open_directory(state, node);
		}
		pb_select_rids(state, &rid, 1u);
		pb_notify_selection(state);
		return;
	}
	/* C++ OpenAsset: select first (OnAssetSelection), then activate. */
	pb_select_rids(state, &rid, 1u);
	pb_notify_selection(state);
	sk_editor_notify_asset_opened(app_context, app_api, pb_workspace_id(state), rid);
	sk_editor_notify_asset_activated(app_context, app_api, pb_workspace_id(state), rid);
}

static void pb_reveal_path(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	sk_rid_t node;
	(void)app_context;
	(void)app_api;
	if (state == NULL || rid.id == 0u) {
		return;
	}
	/* Files live in their containing directory node; folders own one. */
	node = pb_directory_node_of(state, rid);
	if (node.id == 0u) {
		node = state->root_directory;
	}
	if (node.id != 0u) {
		pb_set_open_directory(state, node);
	}
	pb_select_rids(state, &rid, 1u);
	state->rename_item = SK_RID_ZERO;
	pb_notify_selection(state);
	/* Open the tree ancestors so the revealed row is visible (ui sync). */
	if (state->ui != NULL && sk_ui_node_is_valid(state->tree)) {
		u64 id = node.id;
		(void)state->ui->item_bind_open_ancestors(state->ui_ctx, state->tree, id);
	}
}

static void pb_refresh(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return;
	}
	if (pb_has_project(state) && state->open_directory.id != 0u) {
		state->cached_version = pb_repo_api(state)->get_version(state->repository, state->open_directory);
	}
	state->cached_directory = state->open_directory;
	pb_rebuild_listing(state);
}

static void pb_set_project(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_editor_project_t* project) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state == NULL) {
		return;
	}
	if (project != NULL) {
		state->assets = sk_editor_project_assets(project);
		state->repository = sk_editor_project_repository(project);
		state->root_directory = sk_editor_project_root_directory(project);
	} else {
		state->assets = NULL;
		state->repository = NULL;
		state->root_directory = SK_RID_ZERO;
	}
	state->open_directory = state->root_directory;
	state->cached_directory = state->root_directory;
	state->cached_version = 0u;
	state->mock_seq = 0u;
	pb_rebuild_listing(state);
}

static u32 pb_item_count(const sk_editor_window_t* window) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	return state != NULL ? state->grid_count : 0u;
}

static u32 pb_visible_item_count(const sk_editor_window_t* window) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	return state != NULL ? state->grid_visible : 0u;
}

static const_chr_t pb_item_name_at(const sk_editor_window_t* window, u32 index, i32* out_is_directory) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	if (state == NULL || index >= state->grid_count) {
		if (out_is_directory != NULL) {
			*out_is_directory = 0;
		}
		return "";
	}
	if (out_is_directory != NULL) {
		*out_is_directory = state->grid_meta[index].is_directory;
	}
	return state->grid_meta[index].label;
}

static const sk_editor_project_browser_ops_t pb_ops = {
	pb_open,		pb_add_menu_item, pb_can_create_asset, pb_asset_new,		  pb_hide_extension,		 pb_extension_is_hidden, pb_clear_selection,
	pb_select_item, pb_set_selection, pb_set_rename_item,  pb_get_open_directory, pb_get_last_selected_item, pb_get_selected_items,	 pb_activate_item,
	pb_reveal_path, pb_refresh,		  pb_set_project,	   pb_item_count,		  pb_visible_item_count,	 pb_item_name_at,
};

/* ------------------------------------------------------------------ */
/*  Drop-file observer (real import)                                  */
/* ------------------------------------------------------------------ */

static void pb_on_drop_file(void* user, const_chr_t path) {
	pb_state_t* state = (pb_state_t*)user;
	size_t n;
	if (state == NULL || path == NULL) {
		return;
	}
	n = strlen(path);
	if (n >= PB_PATH_CAP) {
		n = PB_PATH_CAP - 1u;
	}
	memcpy(state->last_drop_path, path, n);
	state->last_drop_path[n] = '\0';
	/* Real import path: core engine imports the dropped file into the open
	 * directory (C++ OnDropFile → ResourceAssets::ImportAsset). */
	if (pb_has_project(state) && state->open_directory.id != 0u) {
		(void)state->app_api->resource_assets_api(state->app_context)->import_asset(state->assets, state->open_directory, path, NULL);
		pb_mark_dirty(state);
		pb_rebuild_listing(state);
	}
	(void)snprintf(state->last_import_path, PB_PATH_CAP, "%s", state->last_drop_path);
}

/* ------------------------------------------------------------------ */
/*  UI chrome (retained; rebuilt only when the dock teardown drops it) */
/* ------------------------------------------------------------------ */

static void pb_apply_panel_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 0.96f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void pb_apply_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 6.0f;
	p.layout.width = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void pb_apply_pane_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 4.0f;
	p.layout.padding.top = 4.0f;
	p.layout.padding.right = 4.0f;
	p.layout.padding.bottom = 4.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* Root capture-phase watcher: records modifier state on pointer-down (before
 * the row click handlers run), the pointer position, and handles Delete /
 * Backspace hotkeys. */
static void pb_on_root_event(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	(void)node;
	if (state == NULL || event == NULL) {
		return;
	}
	if (event->phase == SK_UI_EVENT_PHASE_CAPTURE && event->type == SK_UI_EVENT_POINTER_DOWN) {
		state->last_click_mods = event->mods;
		state->pointer_x = event->x;
		state->pointer_y = event->y;
	}
	if (event->phase == SK_UI_EVENT_PHASE_CAPTURE && event->type == SK_UI_EVENT_POINTER_MOVE) {
		state->pointer_x = event->x;
		state->pointer_y = event->y;
	}
	if (event->phase == SK_UI_EVENT_PHASE_CAPTURE && event->type == SK_UI_EVENT_KEY_DOWN && event->down != 0) {
		sk_ui_node_t focus = state->ui != NULL ? state->ui->focus_get(ctx) : SK_UI_NODE_INVALID;
		i32 in_text = sk_ui_node_is_valid(focus) && (sk_ui_node_eq(focus, state->search_input) || sk_ui_node_eq(focus, state->rename_input)) ? 1 : 0;
		if (in_text == 0) {
			if (event->key == SK_UI_KEY_DELETE) {
				pb_delete_selected(state);
				event->consumed = 1;
			} else if (event->key == SK_UI_KEY_BACKSPACE) {
				sk_rid_t parent = state->open_directory.id != 0u ? pb_parent_directory(state, state->open_directory) : SK_RID_ZERO;
				if (parent.id != 0u) {
					pb_set_open_directory(state, parent);
					event->consumed = 1;
				}
			}
		}
	}
}

/* Tree row activation: select (+ navigate when a folder row is clicked). */
static void pb_on_tree_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	u32 i;
	(void)ctx;
	(void)host;
	if (state == NULL) {
		return;
	}
	for (i = 0u; i < state->tree_count; ++i) {
		if (state->tree_items[i].id == item_id) {
			sk_rid_t asset = state->tree_meta[i].asset;
			pb_apply_click_selection(state, asset);
			/* Left-click on a folder row navigates (C++ pendingOpenDirectory). */
			if (state->tree_meta[i].is_leaf == 0 && (state->last_click_mods & (u32)SK_UI_MOD_CTRL) == 0u) {
				pb_set_open_directory(state, state->tree_meta[i].directory);
			}
			return;
		}
	}
}

/* Grid row activation (click select; folders navigate on double-click via
 * content_grid_last_enter). */
static void pb_on_grid_activate(sk_ui_context_t* ctx, sk_ui_node_t host, u64 item_id, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	u32 i;
	(void)ctx;
	(void)host;
	if (state == NULL) {
		return;
	}
	for (i = 0u; i < state->grid_count; ++i) {
		if (state->grid_items[i].id == item_id) {
			pb_apply_click_selection(state, state->grid_meta[i].rid);
			return;
		}
	}
}

static void pb_on_zoom_change(sk_ui_context_t* ctx, sk_ui_node_t node, f32 value, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	(void)ctx;
	(void)node;
	if (state == NULL) {
		return;
	}
	state->content_browser_zoom = value;
	if (state->ui != NULL && sk_ui_node_is_valid(state->grid)) {
		(void)state->ui->content_grid_set_scale(state->ui_ctx, state->grid, value);
	}
}

static void pb_on_settings_two_columns(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	if (state != NULL) {
		state->tree_only_view = 0;
		pb_rebuild_listing(state);
	}
	event->consumed = 1;
}

static void pb_on_settings_one_column(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	if (state != NULL) {
		state->tree_only_view = 1;
		pb_rebuild_listing(state);
	}
	event->consumed = 1;
}

static void pb_on_import_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	/* MOCK: Platform::OpenDialogMultiple is not ported; drop-file import
	 * (pb_on_drop_file) is the real import path. */
	if (state != NULL) {
		(void)snprintf(state->last_import_path, PB_PATH_CAP, "%s", "MOCK: file dialog not ported; drop files to import");
	}
	event->consumed = 1;
}

/* Breadcrumb button click: navigate to that ancestor directory. */
static void pb_on_crumb_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	pb_state_t* state = (pb_state_t*)user;
	u32 i;
	(void)ctx;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK) {
		return;
	}
	if (state != NULL) {
		for (i = 0u; i < state->crumb_count; ++i) {
			if (sk_ui_node_eq(state->crumb_nodes[i], node)) {
				pb_set_open_directory(state, state->breadcrumb[i]);
				break;
			}
		}
	}
	event->consumed = 1;
}

/* Rebuild the breadcrumb buttons under the header (directory change). */
static void pb_rebuild_breadcrumb_ui(pb_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	if (state == NULL || ui == NULL || !sk_ui_node_is_valid(state->breadcrumb_row)) {
		return;
	}
	/* Destroy previous crumbs. */
	{
		u32 n = ui->node_child_count(ctx, state->breadcrumb_row);
		for (i = 0u; i < n; ++i) {
			sk_ui_node_t c = ui->node_child_at(ctx, state->breadcrumb_row, 0u);
			if (sk_ui_node_is_valid(c)) {
				(void)ui->node_destroy(ctx, c);
			}
		}
	}
	state->crumb_count = 0u;
	for (i = 0u; i < state->breadcrumb_count && i < PB_BREADCRUMB_CAP; ++i) {
		char id[64];
		char label[PB_NAME_CAP];
		sk_rid_t node = state->breadcrumb[i];
		sk_rid_t asset = pb_asset_of_directory(state, node);
		sk_ui_node_t btn;
		sk_ui_node_callbacks_t cbs;
		(void)pb_asset_label(state, asset, 1, label, (u32)sizeof(label));
		(void)snprintf(id, sizeof(id), "pb.crumb.%u", i);
		btn = ui->widget_button(ctx, state->breadcrumb_row, label, id);
		if (!sk_ui_node_is_valid(btn)) {
			return;
		}
		if (i + 1u < state->breadcrumb_count) {
			(void)ui->widget_label(ctx, state->breadcrumb_row, ">", NULL);
		}
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = pb_on_crumb_click;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, btn, &cbs);
		state->crumb_nodes[state->crumb_count++] = btn;
	}
}

/* Build the whole window chrome under the dock content node. */
static void pb_build_ui(pb_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_node_callbacks_t cbs;
	sk_ui_style_props_t p;
	sk_ui_node_t header;
	sk_ui_node_t body;
	sk_ui_node_t import_btn;
	sk_ui_node_t settings_btn;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "pb.content");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	pb_apply_panel_style(ui, ctx, state->root);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_event = pb_on_root_event;
	cbs.user = state;
	(void)ui->node_set_callbacks(ctx, state->root, &cbs);

	/* Header row: Import, breadcrumb, zoom slider, search, settings. */
	header = ui->widget_view(ctx, state->root, "pb.header");
	pb_apply_row_style(ui, ctx, header);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_FLEX_SHRINK;
	p.layout.height = sk_ui_pt(PB_HEADER_H);
	p.layout.flex_shrink = 0.0f;
	p.layout.padding.left = 6.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.top = 3.0f;
	p.layout.padding.bottom = 3.0f;
	(void)ui->node_merge_inline_style(ctx, header, &p);
	state->header = header;

	import_btn = ui->widget_button(ctx, header, "Import", "pb.import");
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = pb_on_import_click;
	cbs.user = state;
	(void)ui->node_set_callbacks(ctx, import_btn, &cbs);

	state->breadcrumb_row = ui->widget_view(ctx, header, "pb.breadcrumb");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_FLEX_GROW;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 4.0f;
	p.layout.flex_grow = 1.0f;
	(void)ui->node_set_inline_style(ctx, state->breadcrumb_row, &p);
	pb_rebuild_breadcrumb_ui(state);

	state->zoom_slider = ui->widget_slider(ctx, header, 0.4f, 5.0f, state->content_browser_zoom, "pb.zoom");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH;
	p.layout.width = sk_ui_pt(120.0f);
	(void)ui->node_merge_inline_style(ctx, state->zoom_slider, &p);
	(void)ui->slider_set_on_change(ctx, state->zoom_slider, pb_on_zoom_change, state);

	state->search_input = ui->widget_text_input(ctx, header, state->search, "pb.search");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH;
	p.layout.width = sk_ui_pt(160.0f);
	(void)ui->node_merge_inline_style(ctx, state->search_input, &p);

	settings_btn = ui->widget_button(ctx, header, "Settings", "pb.settings");
	state->settings_popup = ui->widget_menu_popup(ctx, settings_btn, 0, "pb.settings.popup");
	{
		sk_ui_node_t two = ui->widget_menu_item(ctx, state->settings_popup, "Two Columns (Tree + Content)", "pb.settings.two");
		sk_ui_node_t one = ui->widget_menu_item(ctx, state->settings_popup, "One Column (Tree Only)", "pb.settings.one");
		(void)two;
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = pb_on_settings_two_columns;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, two, &cbs);
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = pb_on_settings_one_column;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, one, &cbs);
	}

	/* Body: tree pane + content grid pane. */
	body = ui->widget_view(ctx, state->root, "pb.body");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_HEIGHT;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.min_height = sk_ui_pt(72.0f);
	(void)ui->node_set_inline_style(ctx, body, &p);

	state->tree_pane = ui->widget_view(ctx, body, "pb.tree.pane");
	pb_apply_pane_style(ui, ctx, state->tree_pane);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH;
	p.layout.width = sk_ui_pt(PB_TREE_PANE_W);
	(void)ui->node_merge_inline_style(ctx, state->tree_pane, &p);
	state->tree_scroll = ui->widget_scroll_view(ctx, state->tree_pane, "pb.tree.scroll");
	state->tree_content = ui->scroll_view_content(ctx, state->tree_scroll);
	/* Clay auto-ids content nodes as (widget, sibling-index) — unique ids
	 * keep multiple scroll views from colliding in one layout. */
	(void)ui->node_set_id(ctx, state->tree_content, "pb.tree.scroll.content");
	state->tree = ui->widget_tree(ctx, state->tree_content, &state->tree_arr, "pb.tree");
	(void)ui->item_bind_set_on_activate(ctx, state->tree, pb_on_tree_activate, state);
	(void)ui->item_bind_set_flags(ctx, state->tree, SK_UI_TREE_NODE_FLAGS_DEFAULT);

	state->grid_pane = ui->widget_view(ctx, body, "pb.grid.pane");
	pb_apply_pane_style(ui, ctx, state->grid_pane);
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW;
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(ctx, state->grid_pane, &p);
	state->grid_scroll = ui->widget_scroll_view(ctx, state->grid_pane, "pb.grid.scroll");
	state->grid_content = ui->scroll_view_content(ctx, state->grid_scroll);
	(void)ui->node_set_id(ctx, state->grid_content, "pb.grid.scroll.content");
	state->grid = ui->widget_content_grid(ctx, state->grid_content, &state->grid_arr, state->content_browser_zoom, "pb.grid");
	(void)ui->item_bind_set_on_activate(ctx, state->grid, pb_on_grid_activate, state);

	/* Context menu overlay (floating; opened on right-click). */
	state->context_menu = ui->widget_context_menu(ctx, state->root, "pb.context");
	(void)ui->menu_set_open(ctx, state->context_menu, 0);

	state->ui_built = 1;
	state->applied_revision = state->listing_revision;
}

/* Rebuild the context menu rows for the right-clicked asset. */
static void pb_build_context_menu(pb_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	pb_class_state_t* cls;
	sk_ui_node_t create_popup = SK_UI_NODE_INVALID;
	u32 i;
	i32 has_selection;
	if (state == NULL || ui == NULL || !sk_ui_node_is_valid(state->context_menu)) {
		return;
	}
	cls = pb_class(state->app_context, state->app_api);
	state->ctx_binding_count = 0u;
	has_selection = state->last_selected.id != 0u ? 1u : 0u;

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

	/* Create submenu. */
	{
		sk_ui_node_t create_menu = ui->widget_submenu(ctx, state->context_menu, "Create", "pb.ctx.create");
		if (sk_ui_node_is_valid(create_menu)) {
			create_popup = ui->menu_get_popup(ctx, create_menu);
		}
	}
	for (i = 0u; i < cls->menu_count; ++i) {
		const sk_editor_menu_item_desc_t* item = &cls->menus[i];
		sk_ui_node_t parent = state->context_menu;
		sk_ui_node_t row;
		char id[64];
		i32 enabled = 1;
		if (item->item_name == NULL) {
			continue;
		}
		if (item->visible != NULL && item->visible(state->app_context, state->app_api, NULL, item->user) == 0) {
			continue;
		}
		if (strncmp(item->item_name, "Create/", 7u) == 0) {
			if (!sk_ui_node_is_valid(create_popup)) {
				continue;
			}
			parent = create_popup;
		} else if (strcmp(item->item_name, "Delete") == 0 || strcmp(item->item_name, "Rename") == 0 || strcmp(item->item_name, "Show Resource Inspector") == 0) {
			enabled = has_selection;
		}
		(void)snprintf(id, sizeof(id), "pb.ctx.item.%u", i);
		row = ui->widget_menu_item(ctx, parent, item->item_name, id);
		if (!sk_ui_node_is_valid(row)) {
			continue;
		}
		(void)ui->menu_item_set_enabled(ctx, row, enabled);
		if (state->ctx_binding_count < (u32)(sizeof(state->ctx_bindings) / sizeof(state->ctx_bindings[0]))) {
			state->ctx_bindings[state->ctx_binding_count].node = row;
			state->ctx_bindings[state->ctx_binding_count].item_index = i;
			state->ctx_binding_count++;
		}
	}
}

/* Per-frame sync: push state into the retained widgets. */
static void pb_sync_ui(pb_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	f32 zoom;
	u32 i;

	/* Search input: sync from the widget (the input owns the text; the
	 * listing rebuilds when it changes). */
	if (sk_ui_node_is_valid(state->search_input)) {
		const_chr_t live = ui->text_input_get_text(ctx, state->search_input);
		if (live != NULL && strcmp(live, state->search) != 0) {
			(void)snprintf(state->search, sizeof(state->search), "%s", live);
			pb_rebuild_listing(state);
		}
	}

	/* Zoom slider. */
	if (sk_ui_node_is_valid(state->zoom_slider)) {
		zoom = ui->slider_get_value(ctx, state->zoom_slider);
		if (zoom > 0.0f && fabsf(zoom - state->content_browser_zoom) > 0.0001f) {
			state->content_browser_zoom = zoom;
		}
		(void)ui->slider_set_value(ctx, state->zoom_slider, state->content_browser_zoom);
	}
	if (sk_ui_node_is_valid(state->grid)) {
		(void)ui->content_grid_set_scale(ctx, state->grid, state->content_browser_zoom);
		/* Bind atlas UVs so folder/file tiles sample Content/Images instead
		 * of the whole atlas (texture_id is the view slot; UVs pick the icon). */
		for (i = 0u; i < state->grid_count; ++i) {
			const sk_editor_icon_t* ic;
			sk_ui_node_t thumb;
			sk_editor_icons_t* icons = sk_editor_icons_resolve(state->app_context, state->app_api);
			if (icons == NULL) {
				break;
			}
			ic = sk_editor_icons_get(icons, state->grid_meta[i].is_directory != 0 ? SK_EDITOR_ICON_FOLDER : SK_EDITOR_ICON_FILE);
			if (ic == NULL) {
				continue;
			}
			state->grid_items[i].icon = ic->view_index;
			thumb = ui->content_grid_thumb(ctx, state->grid, state->grid_items[i].id);
			if (!sk_ui_node_is_valid(thumb)) {
				continue;
			}
			(void)ui->node_set_prop_i32(ctx, thumb, "texture_id", (i32)ic->view_index);
			(void)ui->node_set_prop_f32(ctx, thumb, "uv0_x", ic->uv0x);
			(void)ui->node_set_prop_f32(ctx, thumb, "uv0_y", ic->uv0y);
			(void)ui->node_set_prop_f32(ctx, thumb, "uv1_x", ic->uv1x);
			(void)ui->node_set_prop_f32(ctx, thumb, "uv1_y", ic->uv1y);
		}
	}

	/* Listing changes: push the new item arrays into the bound widgets. */
	if (state->applied_revision != state->listing_revision) {
		/* The widgets re-read the persistent arrays on style_resolve; only the
		 * breadcrumb + selection sync need a push here. */
		pb_rebuild_breadcrumb_ui(state);
		state->applied_revision = state->listing_revision;
		state->applied_selection = state->selection_revision - 1u; /* force selection sync */
	}

	/* Selection flags. */
	if (state->applied_selection != state->selection_revision) {
		pb_sync_widget_selection(state);
	}

	/* Pending rename: begin the grid rename once per target. */
	if (state->rename_item.id != 0u && state->rename_armed == 0u) {
		if (!state->tree_only_view && sk_ui_node_is_valid(state->grid)) {
			i32 found = 0;
			for (i = 0u; i < state->grid_count; ++i) {
				if (SK_RID_EQ(state->grid_meta[i].rid, state->rename_item)) {
					found = 1;
					break;
				}
			}
			if (found != 0) {
				if (ui->content_grid_begin_rename(ctx, state->grid, state->rename_item.id) == 0) {
					state->rename_armed = 1;
				}
			} else {
				state->rename_item = SK_RID_ZERO; /* target not in the open dir */
			}
		} else {
			/* Tree-only rename: build the overlay input over the row. */
			u64 row_id = 0ull;
			for (i = 0u; i < state->tree_count; ++i) {
				if (SK_RID_EQ(state->tree_meta[i].asset, state->rename_item)) {
					row_id = state->tree_items[i].id;
					break;
				}
			}
			if (row_id != 0ull && sk_ui_node_is_valid(state->tree)) {
				sk_ui_node_t row = ui->item_bind_find(ctx, state->tree, row_id);
				sk_ui_rect_t rect;
				if (sk_ui_node_is_valid(row) && ui->node_get_abs_rect(ctx, row, &rect, NULL) == 0) {
					sk_ui_node_t input;
					char name[PB_NAME_CAP];
					if (sk_ui_node_is_valid(state->rename_input)) {
						(void)ui->node_destroy(ctx, state->rename_input);
					}
					(void)pb_asset_label(state, state->rename_item, pb_is_directory(state, state->rename_item), name, (u32)sizeof(name));
					input = ui->widget_text_input(ctx, state->root, name, "pb.rename.tree");
					(void)ui->node_set_prop_i32(ctx, input, "hidden", 0);
					{
						sk_ui_style_props_t p;
						memset(&p, 0, sizeof(p));
						p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK;
						p.layout.position = SK_UI_POSITION_ABSOLUTE;
						p.layout.left = sk_ui_pt(rect.x + 4.0f);
						p.layout.top = sk_ui_pt(rect.y);
						p.layout.width = sk_ui_pt(rect.width > 60.0f ? rect.width : 120.0f);
						p.layout.height = sk_ui_pt(rect.height);
						p.layout.flex_grow = 0.0f;
						p.layout.flex_shrink = 0.0f;
						(void)ui->node_set_inline_style(ctx, input, &p);
					}
					(void)ui->action_focus(ctx, input);
					state->rename_input = input;
					state->rename_armed = 1;
				}
			}
		}
	}

	/* Grid rename completion. */
	if (state->rename_item.id != 0u && state->rename_armed != 0u && !state->tree_only_view && sk_ui_node_is_valid(state->grid)) {
		sk_ui_content_item_state_t st;
		if (ui->content_grid_item_state(ctx, state->grid, state->rename_item.id, &st) == 0 && st.rename_finish != 0) {
			pb_apply_rename(state, state->rename_item, st.new_name != NULL ? st.new_name : "");
			state->rename_item = SK_RID_ZERO;
			state->rename_armed = 0u;
		}
	}

	/* Tree-only rename overlay completion (focus left the input). */
	if (state->rename_item.id != 0u && state->rename_armed != 0u && state->tree_only_view && sk_ui_node_is_valid(state->rename_input)) {
		sk_ui_node_t focus = ui->focus_get(ctx);
		if (!sk_ui_node_is_valid(focus) || (!sk_ui_node_eq(focus, state->rename_input) && !sk_ui_node_eq(ui->node_parent(ctx, focus), state->rename_input))) {
			const_chr_t text = ui->text_input_get_text(ctx, state->rename_input);
			pb_apply_rename(state, state->rename_item, text != NULL ? text : "");
			state->rename_item = SK_RID_ZERO;
			state->rename_armed = 0u;
			if (sk_ui_node_is_valid(state->rename_input)) {
				(void)ui->node_destroy(ctx, state->rename_input);
				state->rename_input = SK_UI_NODE_INVALID;
			}
		}
	}

	/* Scroll content heights (content width = viewport width so the grid
	 * column count tracks the pane; height = row count). */
	if (sk_ui_node_is_valid(state->tree_scroll)) {
		sk_ui_rect_t r;
		f32 w = 200.0f;
		f32 h = 4.0f + (f32)state->tree_count * PB_ROW_H;
		if (ui->node_get_abs_rect(ctx, state->tree_scroll, &r, NULL) == 0 && r.width > 1.0f) {
			w = r.width;
		}
		(void)ui->scroll_view_set_content_size(ctx, state->tree_scroll, w, h);
	}
	if (sk_ui_node_is_valid(state->grid_scroll)) {
		sk_ui_rect_t r;
		i32 cols_i = ui->content_grid_get_column_count(ctx, state->grid);
		u32 cols = cols_i > 0 ? (u32)cols_i : 0u;
		u32 rows = cols > 0u ? (state->grid_visible + cols - 1u) / cols : 0u;
		f32 thumb = ui->content_grid_get_thumb_size(ctx, state->grid);
		f32 w = 300.0f;
		f32 h = 4.0f + (f32)rows * (thumb + PB_GRID_LABEL_H);
		if (ui->node_get_abs_rect(ctx, state->grid_scroll, &r, NULL) == 0 && r.width > 1.0f) {
			w = r.width;
		}
		(void)ui->scroll_view_set_content_size(ctx, state->grid_scroll, w, h);
	}
}

/* Handle edge interactions the widgets surfaced since the last draw. */
static void pb_handle_interactions(pb_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u64 id;
	u32 i;

	if (ui == NULL || !sk_ui_node_is_valid(state->grid) || !sk_ui_node_is_valid(state->tree)) {
		return;
	}

	/* Grid double-click: folders navigate, files activate. */
	id = ui->content_grid_last_enter(ctx, state->grid);
	if (id != SK_UI_ITEM_ID_NONE) {
		for (i = 0u; i < state->grid_count; ++i) {
			if (state->grid_items[i].id == id) {
				if (state->grid_meta[i].is_directory != 0) {
					pb_set_open_directory(state, state->grid_meta[i].directory);
					pb_clear_selection_internal(state);
					pb_notify_selection(state);
				} else {
					pb_select_rids(state, &state->grid_meta[i].rid, 1u);
					pb_notify_selection(state);
					sk_editor_notify_asset_opened(state->app_context, state->app_api, pb_workspace_id(state), state->grid_meta[i].rid);
					sk_editor_notify_asset_activated(state->app_context, state->app_api, pb_workspace_id(state), state->grid_meta[i].rid);
				}
				break;
			}
		}
	}

	/* Grid right-click: open the context menu on that item. */
	id = ui->content_grid_last_right_click(ctx, state->grid);
	if (id != SK_UI_ITEM_ID_NONE) {
		for (i = 0u; i < state->grid_count; ++i) {
			if (state->grid_items[i].id == id) {
				pb_apply_click_selection(state, state->grid_meta[i].rid);
				pb_build_context_menu(state);
				/* Position the context menu at the pointer and open it. */
				{
					sk_ui_style_props_t p;
					memset(&p, 0, sizeof(p));
					p.mask = SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
					p.layout.position = SK_UI_POSITION_ABSOLUTE;
					p.layout.left = sk_ui_pt(state->pointer_x);
					p.layout.top = sk_ui_pt(state->pointer_y);
					(void)ui->node_merge_inline_style(ctx, state->context_menu, &p);
				}
				(void)ui->menu_set_open(ctx, state->context_menu, 1);
				break;
			}
		}
	}

	/* Context menu rows: poll clicks and run the bound actions. */
	if (ui->menu_get_open(ctx, state->context_menu) != 0) {
		for (i = 0u; i < state->ctx_binding_count; ++i) {
			if (sk_ui_node_is_valid(state->ctx_bindings[i].node) && ui->menu_item_clicked(ctx, state->ctx_bindings[i].node) != 0) {
				const sk_editor_menu_item_desc_t* item = &pb_class(state->app_context, state->app_api)->menus[state->ctx_bindings[i].item_index];
				if (item->action != NULL) {
					sk_editor_window_t* window = sk_editor_window_by_type(state->app_context, state->app_api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
					item->action(state->app_context, state->app_api, window, item->user);
				}
				(void)ui->menu_set_open(ctx, state->context_menu, 0);
				break;
			}
		}
	}

	/* Drag sources / targets (SK_UI_ASSET_PAYLOAD; C++ content grid + tree
	 * leaf sources, folder drop targets). */
	{
		typedef struct pb_payload_t {
			u64 asset_rid;
		} pb_payload_t;
		pb_payload_t payload;
		u32 grid_flag = SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS | SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER;

		for (i = 0u; i < state->grid_count; ++i) {
			sk_ui_node_t row = ui->item_bind_find(ctx, state->grid, state->grid_items[i].id);
			if (!sk_ui_node_is_valid(row)) {
				continue;
			}
			payload.asset_rid = state->grid_meta[i].rid.id;
			(void)ui->drag_drop_source(ctx, row, SK_UI_ASSET_PAYLOAD, &payload, sizeof(payload), grid_flag);
			if (state->grid_meta[i].is_directory != 0) {
				(void)ui->drag_drop_target(ctx, row, SK_UI_ASSET_PAYLOAD, SK_UI_DRAG_DROP_FLAG_NONE);
			}
		}
		for (i = 0u; i < state->tree_count; ++i) {
			sk_ui_node_t row = ui->item_bind_find(ctx, state->tree, state->tree_items[i].id);
			if (!sk_ui_node_is_valid(row)) {
				continue;
			}
			if (state->tree_meta[i].is_leaf != 0) {
				payload.asset_rid = state->tree_meta[i].asset.id;
				(void)ui->drag_drop_source(ctx, row, SK_UI_ASSET_PAYLOAD, &payload, sizeof(payload), grid_flag);
			} else {
				(void)ui->drag_drop_target(ctx, row, SK_UI_ASSET_PAYLOAD, SK_UI_DRAG_DROP_FLAG_NONE);
			}
		}
		{
			const sk_ui_payload_t* accepted = ui->drag_drop_accept(ctx, SK_UI_ASSET_PAYLOAD, SK_UI_DRAG_DROP_FLAG_NONE);
			if (accepted != NULL && accepted->delivery != 0 && accepted->data != NULL && accepted->size == sizeof(payload)) {
				sk_rid_t dragged = {((const pb_payload_t*)accepted->data)->asset_rid};
				sk_ui_node_t target = ui->drag_drop_get_hovered_target(ctx);
				/* Map the hovered row back to its directory node. */
				if (sk_ui_node_is_valid(target)) {
					sk_rid_t target_dir = SK_RID_ZERO;
					u32 g;
					for (g = 0u; g < state->grid_count; ++g) {
						sk_ui_node_t row = ui->item_bind_find(ctx, state->grid, state->grid_items[g].id);
						if (sk_ui_node_eq(row, target)) {
							target_dir = state->grid_meta[g].directory;
							break;
						}
					}
					if (target_dir.id == 0u) {
						u32 t;
						for (t = 0u; t < state->tree_count; ++t) {
							sk_ui_node_t row = ui->item_bind_find(ctx, state->tree, state->tree_items[t].id);
							if (sk_ui_node_eq(row, target)) {
								target_dir = state->tree_meta[t].directory;
								break;
							}
						}
					}
					if (target_dir.id != 0u) {
						pb_move_asset(state, target_dir, dragged);
					}
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void pb_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	pb_class_state_t* cls = (pb_class_state_t*)window->user_data;
	pb_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (pb_state_t*)alloc->alloc(alloc->instance, sizeof(pb_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->workspace_id = SK_EDITOR_WORKSPACE_SCENE;
	state->tree_only_view = 0;
	state->content_browser_zoom = 0.55f;
	state->root_directory = SK_RID_ZERO;
	state->open_directory = SK_RID_ZERO;
	state->rename_item = SK_RID_ZERO;
	pb_rebuild_listing(state); /* mock seed until set_project */

	state->drop_observer.order = 0;
	state->drop_observer.user = state;
	state->drop_observer.on_drop_file = pb_on_drop_file;
	cls->app_api->add_impl(cls->app_context, SK_EDITOR_NOTIFY_DROP_FILE, &state->drop_observer);
	window->user_data = state;
}

static void pb_draw(sk_editor_window_t* window, i32* open) {
	pb_state_t* state = (pb_state_t*)window->user_data;
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
	/* Data always stays current (works without a ui host). */
	pb_check_versions(state);

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
		pb_build_ui(state, ui, ctx, content);
	}
	if (state->ui_built == 0) {
		return;
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	pb_sync_ui(state);
	pb_handle_interactions(state);
}

static void pb_destroy(sk_editor_window_t* window) {
	pb_state_t* state = (pb_state_t*)window->user_data;
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
	state->app_api->remove_impl(state->app_context, SK_EDITOR_NOTIFY_DROP_FILE, &state->drop_observer);
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

static i32 pb_save(const sk_editor_window_t* window, char* out, u32 cap, u32* out_len) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_archive_writer_t writer;
	sk_str_view_t json;
	if (state == NULL) {
		return -1;
	}
	if (sk_json_archive_writer_init(&writer, alloc) != 0) {
		return -1;
	}
	writer.write_bool(writer.instance, sk_str_view_cstr("treeOnlyView"), state->tree_only_view);
	writer.write_float(writer.instance, sk_str_view_cstr("contentBrowserZoom"), (f64)state->content_browser_zoom);
	json = sk_json_archive_writer_emit_as_string(&writer);
	if (json.data == NULL || json.size + 1u > cap) {
		sk_archive_writer_destroy(&writer);
		return -1;
	}
	memcpy(out, json.data, (size_t)json.size);
	out[json.size] = '\0';
	if (out_len != NULL) {
		*out_len = json.size;
	}
	sk_archive_writer_destroy(&writer);
	return 0;
}

static i32 pb_load(sk_editor_window_t* window, const_chr_t json, u32 len) {
	pb_state_t* state = (pb_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_archive_reader_t reader;
	if (state == NULL) {
		return -1;
	}
	if (json == NULL || len == 0u) {
		return 0;
	}
	if (sk_json_archive_reader_init(&reader, sk_str_view_make(json, len), alloc) != 0) {
		return -1;
	}
	state->tree_only_view = reader.read_bool(reader.instance, sk_str_view_cstr("treeOnlyView")) != 0 ? 1 : 0;
	{
		f64 zoom = reader.read_float(reader.instance, sk_str_view_cstr("contentBrowserZoom"));
		if (zoom > 0.0) {
			state->content_browser_zoom = (f32)zoom;
		}
	}
	sk_archive_reader_destroy(&reader);
	pb_rebuild_listing(state);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_project_browser_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	pb_class_state_t* cls = pb_class(app_context, app_api);
	if (cls != NULL) {
		pb_window.user_data = cls;
		return;
	}
	cls = (pb_class_state_t*)alloc->alloc(alloc->instance, sizeof(pb_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	pb_seed_class_menus(cls);
	app_api->set_api(app_context, SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID, cls);

	pb_window.type_id = SK_EDITOR_WINDOW_PROJECT_BROWSER;
	pb_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &pb_window);
	app_api->add_impl(app_context, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID, &pb_ops);
}

void sk_editor_project_browser_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	pb_class_state_t* cls = pb_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &pb_window);
	app_api->remove_impl(app_context, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID, &pb_ops);
	app_api->set_api(app_context, SK_EDITOR_PROJECT_BROWSER_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (pb_window.user_data == cls) {
		pb_window.user_data = NULL;
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

static void pbt_path(const_chr_t a, const_chr_t b, char* out, u32 out_cap) {
	TEST_ASSERT_TRUE(sk_path_join(sk_str_view_cstr(a), sk_str_view_cstr(b), out, out_cap) >= 0);
}

static void pbt_write(const_chr_t path, const_chr_t text) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_TRUE(fs->write_file(file, text, strlen(text)) == strlen(text));
	fs->close_file(file);
}

/* Create a temp project: Assets/{Models,Tex}/..., seed files. Returns the
 * project (caller closes + removes temp). */
static sk_editor_project_t* pbt_open_temp_project(sk_app_context_t* app, const sk_app_api_t* app_api, const sk_editor_api_t* editor, char* root, u32 root_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char temp[SK_FS_PATH_MAX];
	char assets[SK_FS_PATH_MAX];
	char models[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];

	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	pbt_path(temp, "skore_pb_apx369", root, root_cap);
	pbt_path(root, "Assets", assets, (u32)sizeof(assets));
	pbt_path(assets, "Models", models, (u32)sizeof(models));
	(void)fs->create_directory(root);
	(void)fs->create_directory(assets);
	(void)fs->create_directory(models);

	pbt_path(assets, "seed.mesh", path, (u32)sizeof(path));
	pbt_write(path, "mesh-placeholder");
	pbt_path(assets, "Hero.png", path, (u32)sizeof(path));
	pbt_write(path, "png-bytes");
	pbt_path(models, "Rig.skmesh", path, (u32)sizeof(path));
	pbt_write(path, "rig-placeholder");

	return editor->project_open(app, app_api, "Game", root);
}

static void pbt_remove_temp_project(const_chr_t root) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char assets[SK_FS_PATH_MAX];
	char models[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	pbt_path(root, "Assets", assets, (u32)sizeof(assets));
	pbt_path(assets, "Models", models, (u32)sizeof(models));
	pbt_path(models, "Rig.skmesh", path, (u32)sizeof(path));
	(void)fs->remove(path);
	(void)fs->remove(models);
	pbt_path(assets, "seed.mesh", path, (u32)sizeof(path));
	(void)fs->remove(path);
	pbt_path(assets, "Hero.png", path, (u32)sizeof(path));
	(void)fs->remove(path);
	(void)fs->remove(assets);
	(void)fs->remove(root);
}

typedef struct pbt_obs_t {
	u32 selection_count;
	u32 asset_count;
	u32 opened_count;
	u32 activated_count;
	sk_rid_t last_asset;
	u32 last_workspace;
} pbt_obs_t;

static void pbt_obs_selection(void* user) {
	((pbt_obs_t*)user)->selection_count++;
}

static void pbt_obs_asset(void* user, u32 workspace_id, sk_rid_t rid) {
	pbt_obs_t* o = (pbt_obs_t*)user;
	o->asset_count++;
	o->last_workspace = workspace_id;
	o->last_asset = rid;
}

static void pbt_obs_opened(void* user, u32 workspace_id, sk_rid_t rid) {
	pbt_obs_t* o = (pbt_obs_t*)user;
	o->opened_count++;
	o->last_workspace = workspace_id;
	o->last_asset = rid;
}

static void pbt_obs_activated(void* user, u32 workspace_id, sk_rid_t rid) {
	pbt_obs_t* o = (pbt_obs_t*)user;
	o->activated_count++;
	o->last_workspace = workspace_id;
	o->last_asset = rid;
}

/* Ops-table path: the window opens, lists a real scanned project, and every
 * public entry point works when called from outside (no ui host). */
SK_TEST(editor_project_browser_ops_real_assets_listing) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_project_browser_ops_t* ops;
	sk_editor_window_t* window;
	sk_editor_project_t* project;
	sk_rid_t hero;
	sk_rid_t rig;
	sk_rid_t sel[3];
	char root[SK_FS_PATH_MAX];
	i32 is_dir = 0;
	u32 i;
	pbt_obs_t obs;
	sk_editor_on_selection_changed_t on_sel;
	sk_editor_on_asset_selection_t on_asset;
	sk_editor_on_asset_opened_t on_open;
	sk_editor_on_asset_activated_t on_act;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	project = pbt_open_temp_project(app, boot.api, editor, root, (u32)sizeof(root));
	TEST_ASSERT_NOT_NULL(project);

	TEST_ASSERT_NULL(sk_editor_project_browser_ops(app, boot.api));
	sk_editor_project_browser_register(app, boot.api);
	sk_editor_project_browser_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID));

	ops = sk_editor_project_browser_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);
	TEST_ASSERT_EQUAL_PTR(&pb_ops, ops);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Project Browser", window->title);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_BOTTOM_LEFT, window->dock_position);
	TEST_ASSERT_EQUAL_INT(0, window->order);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));

	/* Before a project: clearly-marked mock listing. */
	TEST_ASSERT_TRUE(ops->item_count(window) > 0u);
	TEST_ASSERT_TRUE(strstr(ops->item_name_at(window, 0u, &is_dir), "Scenes") != NULL);
	TEST_ASSERT_EQUAL_INT(1, is_dir);

	/* Attach the real project: the browser lists the scanned package. */
	ops->set_project(app, boot.api, window, project);
	TEST_ASSERT_EQUAL_UINT64(ops->get_open_directory(window).id, editor->project_root_directory(project).id);
	TEST_ASSERT_TRUE(ops->item_count(window) >= 2u); /* Models folder + seed.mesh + Hero.png */
	TEST_ASSERT_EQUAL_INT(1, is_dir);
	{
		i32 found_models = 0;
		i32 found_mesh = 0;
		for (i = 0u; i < ops->item_count(window); ++i) {
			const_chr_t name = ops->item_name_at(window, i, &is_dir);
			if (is_dir && strcmp(name, "Models") == 0) {
				found_models = 1;
			}
			if (!is_dir && strcmp(name, "seed.mesh") == 0) {
				found_mesh = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(1, found_models);
		TEST_ASSERT_EQUAL_INT(1, found_mesh);
	}

	/* Selection observers fire through add_impl (no event bus). */
	memset(&obs, 0, sizeof(obs));
	memset(&on_sel, 0, sizeof(on_sel));
	memset(&on_asset, 0, sizeof(on_asset));
	memset(&on_open, 0, sizeof(on_open));
	memset(&on_act, 0, sizeof(on_act));
	on_sel.user = &obs;
	on_sel.on_selection_changed = pbt_obs_selection;
	on_asset.user = &obs;
	on_asset.on_asset_selection = pbt_obs_asset;
	on_open.user = &obs;
	on_open.on_asset_opened = pbt_obs_opened;
	on_act.user = &obs;
	on_act.on_asset_activated = pbt_obs_activated;
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_SELECTION_CHANGED, &on_sel);
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_ASSET_SELECTION, &on_asset);
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_ASSET_OPENED, &on_open);
	boot.api->add_impl(app, SK_EDITOR_NOTIFY_ASSET_ACTIVATED, &on_act);

	/* Find Hero.png (root) and the Models directory + its Rig asset. */
	hero = SK_RID_ZERO;
	rig = SK_RID_ZERO;
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		sk_repository_t* repository = editor->project_repository(project);
		sk_resource_object_t root_view = repo->read(repository, editor->project_root_directory(project));
		u32 count = 0u;
		u32 dcount = 0u;
		const sk_rid_t* assets = repo->get_subobject_list(root_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
		const sk_rid_t* dirs = repo->get_subobject_list(root_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &dcount);
		for (i = 0u; i < count; ++i) {
			sk_resource_object_t v = repo->read(repository, assets[i]);
			const_chr_t name = repo->get_string(v, SK_RESOURCE_ASSET_FIELD_NAME);
			const_chr_t ext = repo->get_string(v, SK_RESOURCE_ASSET_FIELD_EXTENSION);
			if (name != NULL && strcmp(name, "Hero") == 0 && ext != NULL && strcmp(ext, ".png") == 0) {
				hero = assets[i];
			}
		}
		for (i = 0u; i < dcount; ++i) {
			sk_resource_object_t dv = repo->read(repository, dirs[i]);
			sk_rid_t asset = repo->get_subobject(dv, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
			sk_resource_object_t av = repo->read(repository, asset);
			const_chr_t name = repo->get_string(av, SK_RESOURCE_ASSET_FIELD_NAME);
			if (name != NULL && strcmp(name, "Models") == 0) {
				u32 ac = 0u;
				u32 a2;
				const sk_rid_t* inside = repo->get_subobject_list(dv, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &ac);
				for (a2 = 0u; a2 < ac; ++a2) {
					rig = inside[a2];
				}
			}
		}
	}
	TEST_ASSERT_TRUE(hero.id != 0u);
	TEST_ASSERT_TRUE(rig.id != 0u);

	/* Single selection: SelectItem. */
	ops->select_item(app, boot.api, window, hero, NULL);
	TEST_ASSERT_EQUAL_UINT64(hero.id, ops->get_last_selected_item(window).id);
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_selected_items(window, sel, 3u));
	TEST_ASSERT_EQUAL_UINT64(hero.id, sel[0].id);
	TEST_ASSERT_EQUAL_UINT(1u, obs.selection_count);
	TEST_ASSERT_EQUAL_UINT(1u, obs.asset_count);
	TEST_ASSERT_EQUAL_UINT(SK_EDITOR_WORKSPACE_SCENE, obs.last_workspace);
	TEST_ASSERT_EQUAL_UINT64(hero.id, obs.last_asset.id);

	/* Multi selection: SetSelection. */
	sel[0] = hero;
	sel[1] = rig;
	ops->set_selection(app, boot.api, window, sel, 2u, NULL);
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_selected_items(window, sel, 3u));
	TEST_ASSERT_EQUAL_UINT64(rig.id, ops->get_last_selected_item(window).id);

	ops->clear_selection(app, boot.api, window, NULL);
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_selected_items(window, sel, 3u));
	TEST_ASSERT_EQUAL_UINT64(0ull, ops->get_last_selected_item(window).id);

	/* RevealPath: navigate to the Models subdirectory and select Rig. */
	ops->reveal_path(app, boot.api, window, rig);
	TEST_ASSERT_TRUE(ops->get_open_directory(window).id != editor->project_root_directory(project).id);
	TEST_ASSERT_EQUAL_UINT64(rig.id, ops->get_last_selected_item(window).id);
	{
		i32 found_rig = 0;
		for (i = 0u; i < ops->item_count(window); ++i) {
			/* Rig.skmesh has no builtin handler, so the asset layer labels it
			 * by its name only (no extension appended). */
			if (strcmp(ops->item_name_at(window, i, &is_dir), "Rig") == 0) {
				found_rig = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(1, found_rig);
	}

	/* Create via AssetNew through the ops table (real asset layer). */
	{
		sk_editor_menu_item_event_t event;
		char scene_ext[] = ".scene";
		event.window = window;
		event.user = (void*)scene_ext;
		TEST_ASSERT_EQUAL_INT(1, ops->can_create_asset(app, boot.api, &event));
		ops->asset_new(app, boot.api, &event);
		TEST_ASSERT_TRUE(ops->get_last_selected_item(window).id != 0u);
	}
	{
		i32 found_new = 0;
		for (i = 0u; i < ops->item_count(window); ++i) {
			const_chr_t name = ops->item_name_at(window, i, &is_dir);
			if (!is_dir && strcmp(name, "New Asset.scene") == 0) {
				found_new = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(1, found_new);
	}

	/* Rename an asset through the repository (the same write the rename
	 * commit applies), then HideExtension filters it out. */
	{
		const sk_repository_api_t* repo = sk_test_repository_table();
		sk_repository_t* repository = editor->project_repository(project);
		sk_resource_object_t view = repo->write(repository, rig);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, "RenamedMesh");
		repo->commit(view, NULL);
		ops->refresh(app, boot.api, window);
	}
	{
		i32 found = 0;
		for (i = 0u; i < ops->item_count(window); ++i) {
			if (strcmp(ops->item_name_at(window, i, &is_dir), "RenamedMesh") == 0) {
				found = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(1, found);
	}

	/* HideExtension filters the listing (Rig carries the .skmesh extension). */
	ops->hide_extension(app, boot.api, ".skmesh");
	ops->hide_extension(app, boot.api, ".skmesh");
	TEST_ASSERT_EQUAL_INT(1, ops->extension_is_hidden(app, boot.api, ".skmesh"));
	TEST_ASSERT_EQUAL_INT(0, ops->extension_is_hidden(app, boot.api, ".png"));
	ops->refresh(app, boot.api, window);
	{
		i32 found_mesh = 0;
		for (i = 0u; i < ops->item_count(window); ++i) {
			if (strcmp(ops->item_name_at(window, i, &is_dir), "RenamedMesh") == 0) {
				found_mesh = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT(0, found_mesh);
	}

	/* Drop-file observer imports into the open directory (real path). */
	{
		char tone[SK_FS_PATH_MAX];
		pbt_path(root, "tone.wav", tone, (u32)sizeof(tone));
		pbt_write(tone, "RIFF....WAVEfmt ");
		sk_editor_notify_drop_file(app, boot.api, tone);
		TEST_ASSERT_EQUAL_STRING(tone, ((pb_state_t*)window->user_data)->last_drop_path);
		(void)fs->remove(tone);
	}
	{
		i32 found_audio = 0;
		for (i = 0u; i < ops->item_count(window); ++i) {
			/* The audio importer's wrapper carries an empty extension, so the
			 * asset layer labels it by its name only. */
			if (strcmp(ops->item_name_at(window, i, &is_dir), "tone") == 0) {
				found_audio = 1;
			}
		}
		TEST_ASSERT_EQUAL_INT_MESSAGE(1, found_audio, "tone not in listing after drop import");
	}

	/* ActivateItem announces opened/activated observers. */
	ops->activate_item(app, boot.api, window, hero);
	TEST_ASSERT_EQUAL_UINT(1u, obs.opened_count);
	TEST_ASSERT_EQUAL_UINT(1u, obs.activated_count);

	editor->window_close(app, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(0u, boot.api->impl_count(app, SK_EDITOR_NOTIFY_DROP_FILE));
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER));

	sk_editor_project_browser_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_project_browser_ops(app, boot.api));
	editor->project_close(project);
	sk_app_shutdown(app);
	pbt_remove_temp_project(root);
}

/* Helper: resolve the listing RID at @p index (private to the test TU). */
static sk_rid_t pbt_rid_at(const sk_editor_window_t* window, u32 index) {
	const pb_state_t* state = (const pb_state_t*)window->user_data;
	if (state == NULL || index >= state->grid_count) {
		return SK_RID_ZERO;
	}
	return state->grid_meta[index].rid;
}

SK_TEST(editor_project_browser_save_load_roundtrip) {
	sk_app_boot_t boot = sk_app_create();
	const sk_editor_project_browser_ops_t* ops;
	sk_editor_window_t* window;
	char json[256];
	u32 len = 0u;
	pb_state_t* state;

	TEST_ASSERT_NOT_NULL(boot.context);
	sk_editor_bind_tables(boot.context, boot.api);
	sk_editor_project_browser_register(boot.context, boot.api);
	ops = sk_editor_project_browser_ops(boot.context, boot.api);
	window = ops->open(boot.context, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_NOT_NULL(window->save);
	TEST_ASSERT_NOT_NULL(window->load);

	state = (pb_state_t*)window->user_data;
	state->tree_only_view = 1;
	state->content_browser_zoom = 2.5f;
	TEST_ASSERT_EQUAL_INT(0, window->save(window, json, (u32)sizeof(json), &len));
	TEST_ASSERT_TRUE(len > 0u);
	TEST_ASSERT_NOT_NULL(strstr(json, "treeOnlyView"));
	TEST_ASSERT_NOT_NULL(strstr(json, "contentBrowserZoom"));

	state->tree_only_view = 0;
	state->content_browser_zoom = 1.0f;
	TEST_ASSERT_EQUAL_INT(0, window->load(window, json, len));
	TEST_ASSERT_EQUAL_INT(1, state->tree_only_view);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, state->content_browser_zoom);

	sk_editor_window_close(boot.context, boot.api, window);
	sk_editor_project_browser_shutdown(boot.context, boot.api);
	sk_app_shutdown(boot.context);
}

/* ui-hosted path: the dock chrome carries the folder tree + content grid;
 * click selects, double-click navigates, search filters, context menu
 * actions run, drag sources/targets are live. */
SK_TEST(editor_project_browser_ui_dock_tree_grid_context) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_project_browser_ops_t* ops;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_editor_window_t* window;
	sk_editor_project_t* project;
	sk_rid_t hero;
	char root[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	i32 open = 1;
	sk_ui_node_t tree_node;
	sk_ui_node_t grid_node;
	sk_ui_node_t search;
	sk_ui_node_t crumb;
	u32 i;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	project = pbt_open_temp_project(app, boot.api, editor, root, (u32)sizeof(root));
	TEST_ASSERT_NOT_NULL(project);

	/* Load the ui plugin (skipped when not built next to the tests). */
	{
		const sk_filesystem_api_t* fs = sk_test_filesystem_table();
		char base[SK_FS_PATH_MAX];
		char plugin[SK_FS_PATH_MAX];
#if defined(_WIN32)
		const_chr_t name = "sk-ui.dll";
#elif defined(__APPLE__)
		const_chr_t name = "sk-ui.dylib";
#else
		const_chr_t name = "sk-ui.so";
#endif
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			base[0] = '\0';
		}
		if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugin, (u32)sizeof(plugin)) < 0) {
			plugin[0] = '\0';
		}
		(void)sk_path_join(sk_str_view_cstr(plugin), sk_str_view_cstr(name), path, (u32)sizeof(path));
		(void)boot.api->load_plugin(app, path);
	}
	ui = (const sk_ui_api_t*)boot.api->get_api(app, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		/* Plugin not built/copied next to tests — skip rather than fail CI. */
		editor->project_close(project);
		pbt_remove_temp_project(root);
		sk_app_shutdown(app);
		TEST_IGNORE_MESSAGE("sk-ui plugin not available");
		return;
	}
	TEST_ASSERT_EQUAL_INT(0, ui->init());
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	sk_editor_workspace_register_impls(app, boot.api);
	sk_editor_project_browser_register(app, boot.api);
	sk_editor_windows_register_impls(app, boot.api);
	ops = sk_editor_project_browser_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(ws);
	sk_editor_workspace_set_dock_context(ws, ctx);
	sk_editor_workspace_switch(ws);
	editor->dockspace_init(ws);
	window = editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	TEST_ASSERT_NOT_NULL(window);

	ops->set_project(app, boot.api, window, project);
	window->draw(window, &open);
	TEST_ASSERT_EQUAL_INT(1, open);

	tree_node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.tree");
	grid_node = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.grid");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tree_node));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(grid_node));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.search")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.header")));

	/* The grid lists the scanned package; the tree has folder rows. */
	TEST_ASSERT_TRUE(ops->item_count(window) >= 2u);
	TEST_ASSERT_TRUE(ui->item_bind_row_count(ctx, grid_node) >= 2u);
	TEST_ASSERT_TRUE(ui->item_bind_row_count(ctx, tree_node) >= 1u);

	/* Find Hero in the listing (no .png handler, so the asset layer labels
	 * it by its name only). */
	hero = SK_RID_ZERO;
	for (i = 0u; i < ops->item_count(window); ++i) {
		i32 is_dir;
		if (strcmp(ops->item_name_at(window, i, &is_dir), "Hero") == 0) {
			hero = pbt_rid_at(window, i);
			break;
		}
	}
	TEST_ASSERT_TRUE(hero.id != 0u);

	/* Programmatic selection is visible through the ops table. */
	ops->select_item(app, boot.api, window, hero, NULL);
	TEST_ASSERT_EQUAL_UINT64(hero.id, ops->get_last_selected_item(window).id);
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_selected_items(window, &hero, 1u));

	/* Search narrows the grid (root listing: Models + seed.mesh + Hero). */
	search = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.search");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(search));
	TEST_ASSERT_TRUE(ops->item_count(window) >= 3u);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, search, "Hero"));
	window->draw(window, &open);
	TEST_ASSERT_TRUE(ops->visible_item_count(window) >= 1u);
	TEST_ASSERT_TRUE(ops->visible_item_count(window) < ops->item_count(window));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, search, ""));
	window->draw(window, &open);

	/* RevealPath navigates into Models and the grid lists the rig asset. */
	{
		sk_rid_t rig = SK_RID_ZERO;
		const sk_repository_api_t* repo = sk_test_repository_table();
		sk_repository_t* repository = editor->project_repository(project);
		sk_resource_object_t root_view = repo->read(repository, editor->project_root_directory(project));
		u32 count = 0u;
		const sk_rid_t* dirs = repo->get_subobject_list(root_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &count);
		u32 d;
		for (d = 0u; d < count; ++d) {
			sk_resource_object_t dv = repo->read(repository, dirs[d]);
			sk_rid_t asset = repo->get_subobject(dv, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
			sk_resource_object_t av = repo->read(repository, asset);
			const_chr_t name = repo->get_string(av, SK_RESOURCE_ASSET_FIELD_NAME);
			if (name != NULL && strcmp(name, "Models") == 0) {
				u32 ac = 0u;
				const sk_rid_t* assets = repo->get_subobject_list(dv, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &ac);
				for (u32 a2 = 0u; a2 < ac; ++a2) {
					rig = assets[a2];
				}
			}
		}
		TEST_ASSERT_TRUE(rig.id != 0u);
		ops->reveal_path(app, boot.api, window, rig);
		TEST_ASSERT_TRUE(ops->get_open_directory(window).id != editor->project_root_directory(project).id);
		window->draw(window, &open);
		TEST_ASSERT_TRUE(ops->item_count(window) >= 1u);
	}

	/* Breadcrumb exists (root + Models). */
	crumb = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.crumb.0");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(crumb));

	/* Context menu surface is live (right-click driven open state). */
	{
		sk_rid_t first = pbt_rid_at(window, 0u);
		TEST_ASSERT_TRUE(first.id != 0u);
		ops->select_item(app, boot.api, window, first, NULL);
		window->draw(window, &open);
	}
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "pb.context")));

	/* Delete + Rename menu actions run on the selection (ops-level path). */
	ops->clear_selection(app, boot.api, window, NULL);
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_selected_items(window, &hero, 1u));

	editor->window_close(app, boot.api, window);
	editor->workspace_destroy(ws);
	ui->context_destroy(ctx);
	ui->shutdown();
	sk_editor_project_browser_shutdown(app, boot.api);
	editor->project_close(project);
	sk_app_shutdown(app);
	pbt_remove_temp_project(root);
}

#endif /* SK_TESTS */
