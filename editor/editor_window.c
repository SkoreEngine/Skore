/**
 * @file editor_window.c
 * @brief Editor window + workspace registry (APX-329 / APX-330).
 *
 * Scaffolding for the main editor's window/workspace model:
 *
 *  - Window implementations (static `sk_editor_window_t` tables) and
 *    workspace types (static `sk_editor_workspace_type_t` descriptors)
 *    register on the app context via `app_api->add_impl` under two distinct
 *    type ids and are enumerated via `app_api->get_all_impls`. The 14 main
 *    editor windows (titles + docking metadata, empty Draw) register in
 *    main_windows.c.
 *  - Open workspaces and open window instances live in a per-app-context
 *    editor registry stashed on the app registry (no file-scope mutable
 *    state). The registry is created lazily and freed when the last
 *    workspace/window is destroyed, so contexts that create+destroy cleanly
 *    never leak.
 *  - `dockspace_init`/`dockspace_reset` derive a workspace's default dock
 *    layout from the registered window impls whose workspace_mask admits the
 *    workspace type and open one instance of each (deduped by type_id). When
 *    the sk-ui API is registered (editor host / ui-loaded tests) they also
 *    project the layout onto a workspace-owned sk-ui dockspace (main
 *    InitDockSpace splits: center/left/right-top/right-bottom/bottom-left/
 *    bottom-right) and wire the dock chrome: close goes through
 *    dock_tab_close + the dock_set_tab_callback, move/redock and undock stay
 *    live via dock_window_to_node / dock_window_undock, tab activate/reorder
 *    via dock_tab_set_active / dock_tab_reorder.
 */

#include "editor_window.h"

#include "allocator.h"
#include "array.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* Private registry type id: the editor registry object is stashed on the app
 * registry so it lives exactly as long as the app context that owns it. */
#define SK_EDITOR_REGISTRY_TYPE_ID SK_TYPE_ID("sk.editor_registry", 0x5afff7f680b3cb29ULL, 0xd548604f7f3f7392ULL)

/* One default dock slot per docked window type (type + placement metadata for
 * the dockspace renderer). */
typedef struct sk_editor_dock_slot_t {
	sk_type_id_t window_type_id;
	i32 dock_position;
	i32 order;
} sk_editor_dock_slot_t;

typedef struct sk_editor_registry_t {
	SK_ARRAY(sk_editor_workspace_t*) workspaces;
	SK_ARRAY(sk_editor_window_t*) windows;
	sk_editor_workspace_t* active;
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} sk_editor_registry_t;

struct sk_editor_workspace_t {
	sk_editor_registry_t* registry;
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	u32 type_id;
	const_chr_t display_name;
	SK_ARRAY(sk_editor_dock_slot_t) docks;

	/* APX-330: workspace-owned sk-ui dockspace model (NULL until the ui API
	 * is available and dockspace init/reset built the model). APX-366: the
	 * editor shell may bind a shared context with
	 * sk_editor_workspace_set_dock_context; owns_dock_ctx stays 0 then, so
	 * destroying the workspace never destroys the shell's context. */
	const sk_ui_api_t* ui;
	sk_ui_context_t* dock_ctx;
	i32 owns_dock_ctx; /* 1 when dockspace_build created dock_ctx itself */
	sk_ui_dock_node_t dock_root;
	sk_ui_dock_node_t zone_center;
	sk_ui_dock_node_t zone_left;
	sk_ui_dock_node_t zone_right_top;
	sk_ui_dock_node_t zone_right_bottom;
	sk_ui_dock_node_t zone_bottom_left;
	sk_ui_dock_node_t zone_bottom_right;
	i32 initialized; /* dockspace init/reset ran at least once (idempotency) */
	i32 dock_built;	 /* sk-ui dock model currently live on dock_ctx */
	char dock_space_id[64];
};

/* ------------------------------------------------------------------ */
/*  Registry plumbing (per app context, no file-scope statics)        */
/* ------------------------------------------------------------------ */

static sk_editor_registry_t* editor_registry_get(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (sk_editor_registry_t*)app_api->get_api(app_context, SK_EDITOR_REGISTRY_TYPE_ID);
}

static sk_editor_registry_t* editor_registry_ensure(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	if (registry != NULL) {
		return registry;
	}
	registry = (sk_editor_registry_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_registry_t));
	if (registry == NULL) {
		return NULL;
	}
	memset(registry, 0, sizeof(*registry));
	registry->app_context = app_context;
	registry->app_api = app_api;
	sk_array_init(&registry->workspaces, alloc);
	sk_array_init(&registry->windows, alloc);
	app_api->set_api(app_context, SK_EDITOR_REGISTRY_TYPE_ID, (const void_ptr_t)registry);
	return registry;
}

/* Free the registry once it holds no workspaces and no windows. */
static void editor_registry_maybe_free(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_registry_t* registry) {
	const sk_allocator_t* alloc = sk_allocator_default();
	if (registry->workspaces.count != 0u || registry->windows.count != 0u) {
		return;
	}
	sk_array_free(&registry->workspaces);
	sk_array_free(&registry->windows);
	alloc->free(alloc->instance, registry);
	app_api->set_api(app_context, SK_EDITOR_REGISTRY_TYPE_ID, NULL);
}

/* ------------------------------------------------------------------ */
/*  Workspace types                                                   */
/* ------------------------------------------------------------------ */

static const sk_editor_workspace_type_t scene_workspace_type = {
	.id = SK_EDITOR_WORKSPACE_SCENE,
	.display_name = "Scene",
	.order = 0,
};

static const sk_editor_workspace_type_t graph_workspace_type = {
	.id = SK_EDITOR_WORKSPACE_GRAPH,
	.display_name = "Graph",
	.order = 1,
};

static const sk_editor_workspace_type_t animator_workspace_type = {
	.id = SK_EDITOR_WORKSPACE_ANIMATOR,
	.display_name = "Animator",
	.order = 2,
};

static const sk_editor_workspace_type_t material_workspace_type = {
	.id = SK_EDITOR_WORKSPACE_MATERIAL,
	.display_name = "Material",
	.order = 3,
};

static const sk_editor_workspace_type_t* const builtin_workspace_types[] = {
	&scene_workspace_type,
	&graph_workspace_type,
	&animator_workspace_type,
	&material_workspace_type,
};

void sk_editor_workspace_register_impls(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	u32 count = (u32)(sizeof(builtin_workspace_types) / sizeof(builtin_workspace_types[0]));
	for (u32 i = 0u; i < count; ++i) {
		app_api->add_impl(app_context, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID, builtin_workspace_types[i]);
	}
}

static const sk_editor_workspace_type_t* workspace_type_find(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_editor_workspace_type_t* found = NULL;
	const_ptr_t* impls;
	u32 count = app_api->impl_count(app_context, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID);
	if (count == 0u) {
		return NULL;
	}
	impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
	if (impls == NULL) {
		return NULL;
	}
	(void)app_api->get_all_impls(app_context, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID, impls, count);
	for (u32 i = 0u; i < count; ++i) {
		const sk_editor_workspace_type_t* type = (const sk_editor_workspace_type_t*)impls[i];
		if (type->id == workspace_type_id) {
			found = type;
			break;
		}
	}
	alloc->free(alloc->instance, impls);
	return found;
}

/* ------------------------------------------------------------------ */
/*  Workspaces                                                        */
/* ------------------------------------------------------------------ */

sk_editor_workspace_t* sk_editor_workspace_create(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_editor_workspace_type_t* type = workspace_type_find(app_context, app_api, workspace_type_id);
	sk_editor_registry_t* registry;
	sk_editor_workspace_t* workspace;
	if (type == NULL) {
		return NULL;
	}
	registry = editor_registry_ensure(app_context, app_api);
	if (registry == NULL) {
		return NULL;
	}
	workspace = (sk_editor_workspace_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_workspace_t));
	if (workspace == NULL) {
		return NULL;
	}
	memset(workspace, 0, sizeof(*workspace));
	workspace->registry = registry;
	workspace->app_context = app_context;
	workspace->app_api = app_api;
	workspace->type_id = type->id;
	workspace->display_name = type->display_name;
	(void)snprintf(workspace->dock_space_id, sizeof(workspace->dock_space_id), "sk.editor.dock.ws%u", type->id);
	sk_array_init(&workspace->docks, alloc);
	if (sk_array_push(&registry->workspaces, workspace) != 0) {
		sk_array_free(&workspace->docks);
		alloc->free(alloc->instance, workspace);
		editor_registry_maybe_free(app_context, app_api, registry);
		return NULL;
	}
	if (registry->active == NULL) {
		registry->active = workspace;
	}
	return workspace;
}

void sk_editor_workspace_destroy(sk_editor_workspace_t* workspace) {
	sk_editor_registry_t* registry = workspace->registry;
	sk_app_context_t* app_context = workspace->app_context;
	const sk_app_api_t* app_api = workspace->app_api;
	for (u32 i = 0u; i < registry->workspaces.count; ++i) {
		if (registry->workspaces.items[i] == workspace) {
			sk_array_swap_remove(&registry->workspaces, i);
			break;
		}
	}
	if (registry->active == workspace) {
		registry->active = NULL;
	}
	/* Tear down the workspace-owned sk-ui dockspace (chrome, tabs, callback) —
	 * but only when the workspace owns the context (a shell-bound shared
	 * context survives the workspace, see set_dock_context). */
	if (workspace->ui != NULL && workspace->dock_ctx != NULL && workspace->owns_dock_ctx != 0) {
		workspace->ui->context_destroy(workspace->dock_ctx);
	}
	sk_array_free(&workspace->docks);
	sk_allocator_default()->free(sk_allocator_default()->instance, workspace);
	editor_registry_maybe_free(app_context, app_api, registry);
}

void sk_editor_workspace_switch(sk_editor_workspace_t* workspace) {
	workspace->registry->active = workspace;
}

u32 sk_editor_workspace_list(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_workspace_t** out, u32 out_cap) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	if (registry == NULL) {
		return 0u;
	}
	if (out != NULL) {
		u32 n = out_cap < registry->workspaces.count ? out_cap : registry->workspaces.count;
		for (u32 i = 0u; i < n; ++i) {
			out[i] = registry->workspaces.items[i];
		}
	}
	return registry->workspaces.count;
}

sk_editor_workspace_t* sk_editor_workspace_active(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	return registry != NULL ? registry->active : NULL;
}

u32 sk_editor_workspace_type_id(const sk_editor_workspace_t* workspace) {
	return workspace->type_id;
}

const_chr_t sk_editor_workspace_display_name(const sk_editor_workspace_t* workspace) {
	return workspace->display_name;
}

sk_app_context_t* sk_editor_workspace_app_context(const sk_editor_workspace_t* workspace) {
	return workspace->app_context;
}

const sk_app_api_t* sk_editor_workspace_app_api(const sk_editor_workspace_t* workspace) {
	return workspace->app_api;
}

/* APX-330 dock helpers (defined after sk_editor_window_open). */
static void editor_window_dock_on_open(sk_editor_registry_t* registry, sk_editor_window_t* window);
static sk_ui_dock_node_t workspace_zone_for(const sk_editor_workspace_t* workspace, i32 dock_position);

/* ------------------------------------------------------------------ */
/*  Windows                                                           */
/* ------------------------------------------------------------------ */

static const sk_editor_window_t* window_impl_find(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_editor_window_t* found = NULL;
	const_ptr_t* impls;
	u32 count = app_api->impl_count(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID);
	if (count == 0u) {
		return NULL;
	}
	impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
	if (impls == NULL) {
		return NULL;
	}
	(void)app_api->get_all_impls(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, count);
	for (u32 i = 0u; i < count; ++i) {
		const sk_editor_window_t* impl = (const sk_editor_window_t*)impls[i];
		if (SK_TYPE_ID_EQ(impl->type_id, window_type_id)) {
			found = impl;
			break;
		}
	}
	alloc->free(alloc->instance, impls);
	return found;
}

sk_editor_window_t* sk_editor_window_open(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_editor_window_t* impl = window_impl_find(app_context, app_api, window_type_id);
	sk_editor_registry_t* registry;
	sk_editor_window_t* window;
	if (impl == NULL) {
		return NULL;
	}
	/* One open instance per window type: reopen returns the existing one. */
	window = sk_editor_window_by_type(app_context, app_api, window_type_id);
	if (window != NULL) {
		return window;
	}
	registry = editor_registry_ensure(app_context, app_api);
	if (registry == NULL) {
		return NULL;
	}
	window = (sk_editor_window_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_window_t));
	if (window == NULL) {
		return NULL;
	}
	/* Copy the impl table; user_data becomes per-open once init runs. */
	*window = *impl;
	if (sk_array_push(&registry->windows, window) != 0) {
		alloc->free(alloc->instance, window);
		editor_registry_maybe_free(app_context, app_api, registry);
		return NULL;
	}
	if (window->init != NULL) {
		window->init(window);
	}
	/* APX-330: on-demand windows (mask 0) open into the active workspace's
	 * built dock model when the ui plugin is present. */
	editor_window_dock_on_open(registry, window);
	return window;
}

/* Destroy one open window instance (destroy callback + free + registry drop). */
static void editor_window_destroy_instance(sk_editor_registry_t* registry, sk_editor_window_t* window) {
	for (u32 i = 0u; i < registry->windows.count; ++i) {
		if (registry->windows.items[i] == window) {
			if (window->destroy != NULL) {
				window->destroy(window);
			}
			sk_array_swap_remove(&registry->windows, i);
			sk_allocator_default()->free(sk_allocator_default()->instance, window);
			editor_registry_maybe_free(registry->app_context, registry->app_api, registry);
			return;
		}
	}
}

/* First open window instance whose dock id matches @p dock_id, or NULL. */
static sk_editor_window_t* editor_window_by_dock_id(sk_editor_registry_t* registry, const_chr_t dock_id) {
	for (u32 i = 0u; i < registry->windows.count; ++i) {
		sk_editor_window_t* window = registry->windows.items[i];
		if (window->dock_id != NULL && strcmp(window->dock_id, dock_id) == 0) {
			return window;
		}
	}
	return NULL;
}

/* sk-ui dock tab callback (dock_set_tab_callback): chrome-initiated close /
 * undock notifications. Close tears down the editor window instance; the
 * dock model has already removed the tab. user = editor registry. */
static void editor_dock_tab_cb(sk_ui_context_t* ctx, const_chr_t window_id, void_ptr_t user) {
	sk_editor_registry_t* registry = (sk_editor_registry_t*)user;
	sk_editor_window_t* window;
	(void)ctx;
	if (registry == NULL) {
		return;
	}
	window = editor_window_by_dock_id(registry, window_id);
	if (window != NULL) {
		editor_window_destroy_instance(registry, window);
	}
}

/* Dock leaf for a window's default slot in @p workspace's model, or INVALID. */
static sk_ui_dock_node_t workspace_zone_for(const sk_editor_workspace_t* workspace, i32 dock_position) {
	switch (dock_position) {
	case SK_EDITOR_DOCK_LEFT:
		return workspace->zone_left;
	case SK_EDITOR_DOCK_FILL:
		return workspace->zone_center;
	case SK_EDITOR_DOCK_RIGHT_TOP:
		return workspace->zone_right_top;
	case SK_EDITOR_DOCK_RIGHT_BOTTOM:
		return workspace->zone_right_bottom;
	case SK_EDITOR_DOCK_BOTTOM_LEFT:
		return workspace->zone_bottom_left;
	case SK_EDITOR_DOCK_BOTTOM_RIGHT:
		return workspace->zone_bottom_right;
	default:
		return SK_UI_DOCK_NODE_INVALID;
	}
}

/* Create (or reuse) the editor-window chrome node for @p window on the dock
 * host, so dock binds/applies have a widget to reparent. */
static sk_ui_node_t editor_dock_ensure_chrome(sk_editor_workspace_t* workspace, const sk_editor_window_t* window) {
	const sk_ui_api_t* ui = workspace->ui;
	sk_ui_context_t* ctx = workspace->dock_ctx;
	sk_ui_node_t win;
	sk_ui_node_t host;
	win = ui->find_by_id(ctx, window->dock_id);
	if (sk_ui_node_is_valid(win)) {
		return win;
	}
	host = ui->dockspace_host_node(ctx, workspace->dock_root);
	if (!sk_ui_node_is_valid(host)) {
		return SK_UI_NODE_INVALID;
	}
	return ui->widget_editor_window(ctx, host, window->title, window->dock_id);
}

/* Dock a freshly opened (on-demand) window into the active workspace's model. */
static void editor_window_dock_on_open(sk_editor_registry_t* registry, sk_editor_window_t* window) {
	sk_editor_workspace_t* workspace = registry->active;
	sk_ui_dock_node_t zone;
	if (workspace == NULL || workspace->ui == NULL || workspace->dock_ctx == NULL || workspace->dock_built == 0 || window->dock_id == NULL ||
		window->dock_position == SK_EDITOR_DOCK_NONE) {
		return;
	}
	zone = workspace_zone_for(workspace, window->dock_position);
	if (!sk_ui_dock_node_is_valid(zone)) {
		return;
	}
	(void)editor_dock_ensure_chrome(workspace, window);
	(void)workspace->ui->dock_window_to_node(workspace->dock_ctx, window->dock_id, zone, SK_UI_DOCK_DIR_CENTER);
}

void sk_editor_window_close(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	sk_editor_workspace_t* workspace;
	if (registry == NULL) {
		return;
	}
	/* Chrome-close path: close the dock tab (dock_tab_close); its callback
	 * destroys the instance. Avoids double-free when the editor and the dock
	 * chrome both close the same window. Falls back to a direct destroy when
	 * no dock model is live or the window was never docked. */
	workspace = registry->active;
	if (workspace != NULL && workspace->ui != NULL && workspace->dock_ctx != NULL && workspace->dock_built != 0 && window->dock_id != NULL &&
		workspace->ui->dock_tab_close(workspace->dock_ctx, window->dock_id) == 0) {
		return;
	}
	editor_window_destroy_instance(registry, window);
}

sk_editor_window_t* sk_editor_window_by_type(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	if (registry == NULL) {
		return NULL;
	}
	for (u32 i = 0u; i < registry->windows.count; ++i) {
		sk_editor_window_t* window = registry->windows.items[i];
		if (SK_TYPE_ID_EQ(window->type_id, window_type_id)) {
			return window;
		}
	}
	return NULL;
}

sk_editor_window_t* sk_editor_window_by_dock_id(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t dock_id) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	if (registry == NULL || dock_id == NULL) {
		return NULL;
	}
	return editor_window_by_dock_id(registry, dock_id);
}

const sk_editor_window_t* sk_editor_window_impl_by_dock_id(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t dock_id) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_editor_window_t* found = NULL;
	const_ptr_t* impls;
	u32 count;
	if (dock_id == NULL || dock_id[0] == '\0') {
		return NULL;
	}
	count = app_api->impl_count(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID);
	if (count == 0u) {
		return NULL;
	}
	impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
	if (impls == NULL) {
		return NULL;
	}
	(void)app_api->get_all_impls(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, count);
	for (u32 i = 0u; i < count; ++i) {
		const sk_editor_window_t* impl = (const sk_editor_window_t*)impls[i];
		if (impl->dock_id != NULL && strcmp(impl->dock_id, dock_id) == 0) {
			found = impl;
			break;
		}
	}
	alloc->free(alloc->instance, impls);
	return found;
}

u32 sk_editor_window_iterate(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t** out, u32 out_cap) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	if (registry == NULL) {
		return 0u;
	}
	if (out != NULL) {
		u32 n = out_cap < registry->windows.count ? out_cap : registry->windows.count;
		for (u32 i = 0u; i < n; ++i) {
			out[i] = registry->windows.items[i];
		}
	}
	return registry->windows.count;
}

/* ------------------------------------------------------------------ */
/*  Dockspace                                                         */
/* ------------------------------------------------------------------ */

/* main InitDockSpace split ratios (fractions kept by the child toward the
 * split direction; empty zones collapse at apply). */
#define SK_EDITOR_DOCK_BOTTOM_RATIO 0.38f
#define SK_EDITOR_DOCK_BOTTOM_SPLIT_RATIO 0.50f
#define SK_EDITOR_DOCK_LEFT_RATIO 0.22f
#define SK_EDITOR_DOCK_RIGHT_RATIO 0.24f
#define SK_EDITOR_DOCK_RIGHT_SPLIT_RATIO 0.50f
#define SK_EDITOR_LAYOUT_APPLY_CAP 32u

static void dockspace_dock_model_build(sk_editor_workspace_t* workspace);
static void dockspace_zones_invalidate(sk_editor_workspace_t* workspace);

/* Derive the workspace's default dock layout from the registered window
 * impls whose workspace_mask admits the workspace type; open one instance of
 * each docked window (deduped by type). Both init and reset restore this
 * default derivation, then project it onto the workspace's sk-ui dockspace
 * (dock model) when the ui plugin is present. */
static void dockspace_build(sk_editor_workspace_t* workspace) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_app_context_t* app_context = workspace->app_context;
	const sk_app_api_t* app_api = workspace->app_api;
	const_ptr_t* impls;
	u32 count = app_api->impl_count(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID);
	sk_array_free(&workspace->docks);
	sk_array_init(&workspace->docks, alloc);
	if (count != 0u) {
		impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
		if (impls != NULL) {
			(void)app_api->get_all_impls(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, count);
			for (u32 i = 0u; i < count; ++i) {
				const sk_editor_window_t* impl = (const sk_editor_window_t*)impls[i];
				sk_editor_dock_slot_t slot;
				if (!sk_editor_workspace_mask_contains(impl->workspace_mask, workspace->type_id)) {
					continue;
				}
				slot.window_type_id = impl->type_id;
				slot.dock_position = impl->dock_position;
				slot.order = impl->order;
				if (sk_array_push(&workspace->docks, slot) != 0) {
					break;
				}
				(void)sk_editor_window_open(app_context, app_api, impl->type_id);
			}
			alloc->free(alloc->instance, impls);
		}
	}

	/* APX-330: project the default layout onto the workspace's sk-ui
	 * dockspace when the ui plugin is available (editor host / ui-loaded
	 * tests). Without ui the workspace keeps the slot list only. A
	 * shell-bound shared context (APX-366, set_dock_context) is reused;
	 * otherwise each workspace creates (and owns) its own context. */
	workspace->ui = (const sk_ui_api_t*)app_api->get_api(app_context, SK_UI_API_TYPE_ID);
	if (workspace->ui != NULL && workspace->dock_ctx == NULL) {
		workspace->dock_ctx = workspace->ui->context_create(NULL);
		workspace->owns_dock_ctx = 1;
	}
	dockspace_dock_model_build(workspace);
	workspace->initialized = 1;
}

/* Reset all recorded dock zone handles to invalid. */
static void dockspace_zones_invalidate(sk_editor_workspace_t* workspace) {
	workspace->zone_center = SK_UI_DOCK_NODE_INVALID;
	workspace->zone_left = SK_UI_DOCK_NODE_INVALID;
	workspace->zone_right_top = SK_UI_DOCK_NODE_INVALID;
	workspace->zone_right_bottom = SK_UI_DOCK_NODE_INVALID;
	workspace->zone_bottom_left = SK_UI_DOCK_NODE_INVALID;
	workspace->zone_bottom_right = SK_UI_DOCK_NODE_INVALID;
}

/* The dock builder tab-appends in `order` sequence, so the *last*
 * (highest-order) window of a leaf ends up active (last-tab-wins). The C++
 * editor shows the first/lowest-order window in a leaf, so once the default
 * layout is built, activate the lowest-order window of every zone. The
 * workspace->docks array is sorted by (dock_position, order) above, so the
 * first slot of each zone group is that zone's lowest-order window.
 * Restored layouts take the JSON path (dock_layout_load_json) and keep the
 * saved active tab; this build path only runs for default layouts. */
static void dockspace_activate_lowest_order_tabs(sk_editor_workspace_t* workspace) {
	const sk_ui_api_t* ui = workspace->ui;
	sk_ui_context_t* ctx = workspace->dock_ctx;
	u32 i;
	for (i = 0u; i < workspace->docks.count; ++i) {
		const sk_editor_dock_slot_t* slot = &workspace->docks.items[i];
		sk_editor_window_t* window;
		if (i > 0u && workspace->docks.items[i - 1u].dock_position == slot->dock_position) {
			continue; /* not the lowest-order window of this zone */
		}
		window = sk_editor_window_by_type(workspace->app_context, workspace->app_api, slot->window_type_id);
		if (window == NULL || window->dock_id == NULL || ui->dock_window_is_docked(ctx, window->dock_id) != 1) {
			continue;
		}
		(void)ui->dock_tab_set_active(ctx, window->dock_id);
	}
}

/* Build (or rebuild) the workspace's sk-ui dockspace model: the main
 * InitDockSpace split tree (bottom strip bottom-left|bottom-right, left
 * column, center leaf, right column right-top|right-bottom), editor-window
 * chrome for every default window, tabs ordered by `order` per zone, and the
 * tab close callback. Requires workspace->ui + dock_ctx. */
static void dockspace_dock_model_build(sk_editor_workspace_t* workspace) {
	const sk_ui_api_t* ui = workspace->ui;
	sk_ui_context_t* ctx = workspace->dock_ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t top;
	sk_ui_dock_node_t bottom_left;
	sk_ui_dock_node_t bottom_right;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t right_col;
	sk_ui_dock_node_t right_top;
	sk_ui_dock_node_t right_bottom;
	u32 i;

	if (ui == NULL || ctx == NULL) {
		workspace->dock_built = 0;
		return;
	}

	/* Reset (after runtime dock mutations) destroys the previous model first. */
	if (sk_ui_dock_node_is_valid(workspace->dock_root)) {
		(void)ui->dockspace_destroy(ctx, workspace->dock_space_id);
		workspace->dock_root = SK_UI_DOCK_NODE_INVALID;
	}
	dockspace_zones_invalidate(workspace);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, workspace->dock_space_id, SK_UI_DOCKSPACE_KEEP_CENTRAL | SK_UI_DOCKSPACE_AUTO_APPLY);
	if (!sk_ui_dock_node_is_valid(root)) {
		return;
	}
	workspace->dock_root = root;

	if (ui->dock_builder_begin(ctx, root) != 0) {
		return;
	}
	(void)ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_DOWN, SK_EDITOR_DOCK_BOTTOM_RATIO, &bottom, &top);
	(void)ui->dock_builder_split_node(ctx, bottom, SK_UI_DOCK_DIR_LEFT, SK_EDITOR_DOCK_BOTTOM_SPLIT_RATIO, &bottom_left, &bottom_right);
	(void)ui->dock_builder_split_node(ctx, top, SK_UI_DOCK_DIR_LEFT, SK_EDITOR_DOCK_LEFT_RATIO, &left, &rest);
	(void)ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_RIGHT, SK_EDITOR_DOCK_RIGHT_RATIO, &right_col, &center);
	(void)ui->dock_builder_split_node(ctx, right_col, SK_UI_DOCK_DIR_DOWN, SK_EDITOR_DOCK_RIGHT_SPLIT_RATIO, &right_bottom, &right_top);
	workspace->zone_bottom_left = bottom_left;
	workspace->zone_bottom_right = bottom_right;
	workspace->zone_left = left;
	workspace->zone_center = center;
	workspace->zone_right_top = right_top;
	workspace->zone_right_bottom = right_bottom;

	/* Sort default slots by (dock zone, order) so tabs follow `order` within
	 * each zone, then dock each default window into its zone leaf. */
	for (i = 1u; i < workspace->docks.count; ++i) {
		sk_editor_dock_slot_t slot = workspace->docks.items[i];
		i32 j = (i32)i - 1;
		while (j >= 0 && (workspace->docks.items[j].dock_position > slot.dock_position ||
						  (workspace->docks.items[j].dock_position == slot.dock_position && workspace->docks.items[j].order > slot.order))) {
			workspace->docks.items[j + 1] = workspace->docks.items[j];
			j -= 1;
		}
		workspace->docks.items[j + 1] = slot;
	}
	for (i = 0u; i < workspace->docks.count; ++i) {
		const sk_editor_dock_slot_t* slot = &workspace->docks.items[i];
		sk_ui_dock_node_t zone = workspace_zone_for(workspace, slot->dock_position);
		sk_editor_window_t* window = sk_editor_window_by_type(workspace->app_context, workspace->app_api, slot->window_type_id);
		if (!sk_ui_dock_node_is_valid(zone) || window == NULL || window->dock_id == NULL) {
			continue;
		}
		(void)editor_dock_ensure_chrome(workspace, window);
		(void)ui->dock_builder_dock_window(ctx, window->dock_id, zone);
	}
	if (ui->dock_builder_finish(ctx) != 0) {
		return;
	}
	dockspace_activate_lowest_order_tabs(workspace);

	/* Close/undock notifications from the dock chrome (dock_tab_close /
	 * dock_window_undock) land here; the chrome keeps move/redock and tab
	 * activate/reorder live internally (dock_window_to_node /
	 * dock_tab_reorder / dock_tab_set_active). */
	ui->dock_set_tab_callback(ctx, editor_dock_tab_cb, workspace->registry);
	(void)ui->dockspace_end(ctx);
	workspace->dock_built = 1;
}

void sk_editor_dockspace_init(sk_editor_workspace_t* workspace) {
	if (workspace->initialized == 0) {
		dockspace_build(workspace);
	}
}
void sk_editor_dockspace_reset(sk_editor_workspace_t* workspace) {
	/* Close windows that are not part of this workspace's default set
	 * (on-demand mask 0, or a mask that does not admit the type). Then
	 * rebuild the InitDockSpace tree. Windows without a dock_id (test
	 * stubs) stay if their mask matches. */
	sk_editor_window_t* open[SK_EDITOR_LAYOUT_APPLY_CAP];
	u32 n = sk_editor_window_iterate(workspace->app_context, workspace->app_api, open, SK_EDITOR_LAYOUT_APPLY_CAP);
	u32 i;
	if (n > SK_EDITOR_LAYOUT_APPLY_CAP) {
		n = SK_EDITOR_LAYOUT_APPLY_CAP;
	}
	sk_editor_workspace_clear_dockspace(workspace);
	for (i = 0u; i < n; ++i) {
		sk_editor_window_t* window = open[i];
		if (!sk_editor_workspace_mask_contains(window->workspace_mask, workspace->type_id)) {
			sk_editor_window_close(workspace->app_context, workspace->app_api, window);
		}
	}
	dockspace_build(workspace);
}

struct sk_ui_context_t* sk_editor_workspace_dock_context(const sk_editor_workspace_t* workspace) {
	return workspace->dock_ctx;
}

void sk_editor_workspace_set_dock_context(sk_editor_workspace_t* workspace, sk_ui_context_t* ctx) {
	if (workspace == NULL || workspace->initialized != 0) {
		return;
	}
	workspace->dock_ctx = ctx;
	workspace->owns_dock_ctx = 0;
}

void sk_editor_workspace_clear_dockspace(sk_editor_workspace_t* workspace) {
	if (workspace == NULL || workspace->ui == NULL || workspace->dock_ctx == NULL) {
		return;
	}
	if (sk_ui_dock_node_is_valid(workspace->dock_root)) {
		(void)workspace->ui->dockspace_destroy(workspace->dock_ctx, workspace->dock_space_id);
		workspace->dock_root = SK_UI_DOCK_NODE_INVALID;
	}
	dockspace_zones_invalidate(workspace);
	workspace->dock_built = 0;
}

sk_ui_dock_node_t sk_editor_workspace_dock_root(const sk_editor_workspace_t* workspace) {
	return workspace != NULL ? workspace->dock_root : SK_UI_DOCK_NODE_INVALID;
}

i32 sk_editor_workspace_dock_window(sk_editor_workspace_t* workspace, sk_editor_window_t* window) {
	sk_ui_dock_node_t zone;
	if (workspace == NULL || window == NULL || workspace->ui == NULL || workspace->dock_ctx == NULL || workspace->dock_built == 0 || window->dock_id == NULL ||
		window->dock_position == SK_EDITOR_DOCK_NONE) {
		return -1;
	}
	zone = workspace_zone_for(workspace, window->dock_position);
	if (!sk_ui_dock_node_is_valid(zone)) {
		return -1;
	}
	(void)editor_dock_ensure_chrome(workspace, window);
	return workspace->ui->dock_window_to_node(workspace->dock_ctx, window->dock_id, zone, SK_UI_DOCK_DIR_CENTER);
}

static i32 editor_layout_id_wanted(const_chr_t* dock_ids, u32 count, const_chr_t dock_id) {
	u32 i;
	if (dock_id == NULL) {
		return 0;
	}
	for (i = 0u; i < count; ++i) {
		if (dock_ids[i] != NULL && strcmp(dock_ids[i], dock_id) == 0) {
			return 1;
		}
	}
	return 0;
}

i32 sk_editor_workspace_save_dock_json(const sk_editor_workspace_t* workspace, char* out, u32 cap, u32* out_len) {
	if (workspace->ui == NULL || workspace->dock_ctx == NULL || workspace->dock_built == 0) {
		return -1;
	}
	return workspace->ui->dock_layout_save_json(workspace->dock_ctx, workspace->dock_space_id, out, cap, out_len);
}

i32 sk_editor_workspace_apply_layout(sk_editor_workspace_t* workspace, const_chr_t* dock_ids, u32 count, const_chr_t dock_json, u32 dock_json_len) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_app_context_t* app_context = workspace->app_context;
	const sk_app_api_t* app_api = workspace->app_api;
	sk_editor_window_t* open[SK_EDITOR_LAYOUT_APPLY_CAP];
	u32 n;
	u32 i;
	i32 loaded = 0;

	/* Tear the live model first so window_close does not go through the
	 * dock tab callback (and so chrome ids can be reused). */
	sk_editor_workspace_clear_dockspace(workspace);

	n = sk_editor_window_iterate(app_context, app_api, open, SK_EDITOR_LAYOUT_APPLY_CAP);
	if (n > SK_EDITOR_LAYOUT_APPLY_CAP) {
		n = SK_EDITOR_LAYOUT_APPLY_CAP;
	}
	for (i = 0u; i < n; ++i) {
		sk_editor_window_t* window = open[i];
		if (!editor_layout_id_wanted(dock_ids, count, window->dock_id)) {
			sk_editor_window_close(app_context, app_api, window);
		}
	}

	for (i = 0u; i < count; ++i) {
		const sk_editor_window_t* impl = sk_editor_window_impl_by_dock_id(app_context, app_api, dock_ids[i]);
		if (impl == NULL) {
			continue; /* unknown / stale window id */
		}
		(void)sk_editor_window_open(app_context, app_api, impl->type_id);
	}

	workspace->ui = (const sk_ui_api_t*)app_api->get_api(app_context, SK_UI_API_TYPE_ID);
	if (workspace->ui != NULL && workspace->dock_ctx == NULL) {
		workspace->dock_ctx = workspace->ui->context_create(NULL);
		workspace->owns_dock_ctx = 1;
	}

	if (workspace->ui != NULL && workspace->dock_ctx != NULL) {
		const sk_ui_api_t* ui = workspace->ui;
		sk_ui_context_t* ctx = workspace->dock_ctx;

		n = sk_editor_window_iterate(app_context, app_api, open, SK_EDITOR_LAYOUT_APPLY_CAP);
		if (n > SK_EDITOR_LAYOUT_APPLY_CAP) {
			n = SK_EDITOR_LAYOUT_APPLY_CAP;
		}
		for (i = 0u; i < n; ++i) {
			sk_editor_window_t* window = open[i];
			if (window->dock_id == NULL) {
				continue;
			}
			if (!sk_ui_node_is_valid(ui->find_by_id(ctx, window->dock_id))) {
				(void)ui->widget_editor_window(ctx, ui->context_root(ctx), window->title, window->dock_id);
			}
			(void)ui->dock_window_register(ctx, window->dock_id, NULL, NULL);
		}

		if (dock_json != NULL && dock_json_len > 0u) {
			if (ui->dock_layout_load_json(ctx, workspace->dock_space_id, dock_json, dock_json_len) == 0) {
				workspace->dock_root = ui->dockspace_find(ctx, workspace->dock_space_id);
				if (sk_ui_dock_node_is_valid(workspace->dock_root)) {
					ui->dock_set_tab_callback(ctx, editor_dock_tab_cb, workspace->registry);
					workspace->dock_built = 1;
					loaded = 1;
				}
			}
		}

		if (loaded == 0) {
			sk_array_free(&workspace->docks);
			sk_array_init(&workspace->docks, alloc);
			for (i = 0u; i < n; ++i) {
				sk_editor_dock_slot_t slot;
				sk_editor_window_t* window = open[i];
				if (window->dock_id == NULL || window->dock_position == SK_EDITOR_DOCK_NONE) {
					continue;
				}
				slot.window_type_id = window->type_id;
				slot.dock_position = window->dock_position;
				slot.order = window->order;
				if (sk_array_push(&workspace->docks, slot) != 0) {
					break;
				}
			}
			dockspace_dock_model_build(workspace);
		}
	}

	workspace->initialized = 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "filesystem.h"
#include "main_windows.h"
#include "path.h"
#include "platform.h"
#include "test.h"

#define EW_TEST_SCENE_TYPE_ID SK_TYPE_ID("sk.editor_window.test_scene", 0x74649b8b6a5a86d1ULL, 0xe6f5f705460ca798ULL)
#define EW_TEST_GRAPH_TYPE_ID SK_TYPE_ID("sk.editor_window.test_graph", 0x7321506b3a0f1ff0ULL, 0xdb06b30a834da2c8ULL)

/* Callback counters for the fake window impls (SK_TESTS-only scaffolding). */
static u32 ew_test_init_count = 0u;
static u32 ew_test_draw_count = 0u;
static u32 ew_test_destroy_count = 0u;

static void ew_test_scene_init(sk_editor_window_t* window) {
	(void)window;
	ew_test_init_count++;
}

static void ew_test_scene_draw(sk_editor_window_t* window, i32* open) {
	(void)window;
	ew_test_draw_count++;
	/* Exercise the close-request contract: every 3rd draw clears *open. */
	if (ew_test_draw_count % 3u == 0u) {
		*open = 0;
	}
}

static void ew_test_scene_destroy(sk_editor_window_t* window) {
	(void)window;
	ew_test_destroy_count++;
}

static sk_editor_window_t ew_test_scene_window = {
	.title = "Test Scene Window",
	.dock_position = SK_EDITOR_DOCK_FILL,
	.order = 0,
	.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	.init = ew_test_scene_init,
	.draw = ew_test_scene_draw,
	.render = NULL,
	.destroy = ew_test_scene_destroy,
};

static sk_editor_window_t ew_test_graph_window = {
	.title = "Test Graph Window",
	.dock_position = SK_EDITOR_DOCK_RIGHT,
	.order = 1,
	.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_GRAPH),
	.init = ew_test_scene_init,
	.draw = ew_test_scene_draw,
	.render = NULL,
	.destroy = ew_test_scene_destroy,
};

static const sk_editor_workspace_type_t ew_test_qa_workspace_type = {9u, "QA", 8};

SK_TEST(editor_workspace_mask_contains_bits) {
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE), SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE), SK_EDITOR_WORKSPACE_GRAPH));
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(SK_EDITOR_WORKSPACE_ALL, SK_EDITOR_WORKSPACE_MATERIAL));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(0u, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(SK_EDITOR_WORKSPACE_ALL, 0u));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(SK_EDITOR_WORKSPACE_ALL, 9u));
}

SK_TEST(editor_workspace_impls_register_and_count) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const_ptr_t impls[8];
	sk_editor_workspace_t* scene_ws;
	sk_editor_workspace_t* graph_ws;
	sk_editor_workspace_t* list[4];
	u32 n;
	i32 scene_order = -1;
	const_chr_t scene_name = NULL;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	/* Nothing registered yet: no workspace can be created, list/active empty. */
	TEST_ASSERT_EQUAL_UINT(0u, boot.api->impl_count(app, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID));
	TEST_ASSERT_NULL(editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_EQUAL_UINT(0u, editor->workspace_list(app, boot.api, NULL, 0u));
	TEST_ASSERT_NULL(editor->workspace_active(app, boot.api));

	/* Built-ins register as four impls; enumerate via get_all_impls. */
	sk_editor_workspace_register_impls(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(4u, boot.api->impl_count(app, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID));
	n = boot.api->get_all_impls(app, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID, impls, 8u);
	TEST_ASSERT_EQUAL_UINT(4u, n);
	for (u32 i = 0u; i < n; ++i) {
		const sk_editor_workspace_type_t* type = (const sk_editor_workspace_type_t*)impls[i];
		if (type->id == SK_EDITOR_WORKSPACE_SCENE) {
			scene_order = type->order;
			scene_name = type->display_name;
		}
	}
	TEST_ASSERT_EQUAL_INT(0, scene_order);
	TEST_ASSERT_EQUAL_STRING("Scene", scene_name);

	/* Host-registered workspace types join the same impl list. */
	boot.api->add_impl(app, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID, &ew_test_qa_workspace_type);
	TEST_ASSERT_EQUAL_UINT(5u, boot.api->impl_count(app, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID));

	/* create/list/switch/active/destroy through the editor API. */
	scene_ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	TEST_ASSERT_EQUAL_PTR(scene_ws, editor->workspace_active(app, boot.api));

	graph_ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_GRAPH);
	TEST_ASSERT_NOT_NULL(graph_ws);
	TEST_ASSERT_EQUAL_UINT(2u, editor->workspace_list(app, boot.api, list, 4u));
	TEST_ASSERT_EQUAL_PTR(scene_ws, list[0]);
	TEST_ASSERT_EQUAL_PTR(graph_ws, list[1]);
	/* First workspace stays active until switched. */
	TEST_ASSERT_EQUAL_PTR(scene_ws, editor->workspace_active(app, boot.api));

	editor->workspace_switch(graph_ws);
	TEST_ASSERT_EQUAL_PTR(graph_ws, editor->workspace_active(app, boot.api));

	editor->workspace_destroy(scene_ws);
	TEST_ASSERT_EQUAL_UINT(1u, editor->workspace_list(app, boot.api, list, 4u));
	TEST_ASSERT_EQUAL_PTR(graph_ws, list[0]);
	TEST_ASSERT_EQUAL_PTR(graph_ws, editor->workspace_active(app, boot.api));

	editor->workspace_destroy(graph_ws);
	TEST_ASSERT_EQUAL_UINT(0u, editor->workspace_list(app, boot.api, NULL, 0u));
	TEST_ASSERT_NULL(editor->workspace_active(app, boot.api));

	sk_app_shutdown(app);
}

SK_TEST(editor_window_impls_register_open_close) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const_ptr_t impls[4];
	sk_editor_workspace_t* scene_ws;
	sk_editor_window_t* scene_window;
	sk_editor_window_t* graph_window;
	sk_editor_window_t* open_windows[4];
	i32 open_flag;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	sk_editor_workspace_register_impls(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	/* No window impls yet: open fails, iterate empty, by_type misses. */
	TEST_ASSERT_EQUAL_UINT(0u, boot.api->impl_count(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID));
	TEST_ASSERT_NULL(editor->window_open(app, boot.api, EW_TEST_SCENE_TYPE_ID));
	TEST_ASSERT_EQUAL_UINT(0u, editor->window_iterate(app, boot.api, NULL, 0u));
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, EW_TEST_SCENE_TYPE_ID));

	/* Fake window impls register under the window impl type id; count them.
	 * (type_id is set at runtime: file-scope statics cannot use SK_TYPE_ID's
	 * compound literal under -Wpedantic.) */
	ew_test_scene_window.type_id = EW_TEST_SCENE_TYPE_ID;
	ew_test_graph_window.type_id = EW_TEST_GRAPH_TYPE_ID;
	boot.api->add_impl(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &ew_test_scene_window);
	boot.api->add_impl(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &ew_test_graph_window);
	TEST_ASSERT_EQUAL_UINT(2u, boot.api->impl_count(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID));
	TEST_ASSERT_EQUAL_UINT(2u, boot.api->get_all_impls(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, 4u));
	/* render is optional: the stubs have no render stage. */
	TEST_ASSERT_NULL(((const sk_editor_window_t*)impls[0])->render);

	/* Open the stub window: init runs once; one open instance per type. */
	ew_test_init_count = 0u;
	scene_window = editor->window_open(app, boot.api, EW_TEST_SCENE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(scene_window);
	TEST_ASSERT_EQUAL_UINT(1u, ew_test_init_count);
	TEST_ASSERT_EQUAL_PTR(scene_window, editor->window_by_type(app, boot.api, EW_TEST_SCENE_TYPE_ID));
	TEST_ASSERT_EQUAL_STRING("Test Scene Window", scene_window->title);

	/* draw(open*) is plumbed through the open instance table: the stub clears
	 * the open flag on its 3rd draw (window-requested close contract). */
	ew_test_draw_count = 0u;
	open_flag = 1;
	scene_window->draw(scene_window, &open_flag);
	TEST_ASSERT_EQUAL_UINT(1u, ew_test_draw_count);
	TEST_ASSERT_EQUAL_INT(1, open_flag);
	scene_window->draw(scene_window, &open_flag);
	scene_window->draw(scene_window, &open_flag);
	TEST_ASSERT_EQUAL_UINT(3u, ew_test_draw_count);
	TEST_ASSERT_EQUAL_INT(0, open_flag);

	/* Reopening an open type returns the same instance (no double init). */
	TEST_ASSERT_EQUAL_PTR(scene_window, editor->window_open(app, boot.api, EW_TEST_SCENE_TYPE_ID));
	TEST_ASSERT_EQUAL_UINT(1u, ew_test_init_count);

	/* Second window type opens alongside; iterate lists both in open order. */
	graph_window = editor->window_open(app, boot.api, EW_TEST_GRAPH_TYPE_ID);
	TEST_ASSERT_NOT_NULL(graph_window);
	TEST_ASSERT_EQUAL_UINT(2u, editor->window_iterate(app, boot.api, open_windows, 4u));
	TEST_ASSERT_EQUAL_PTR(scene_window, open_windows[0]);
	TEST_ASSERT_EQUAL_PTR(graph_window, open_windows[1]);

	/* Close: destroy callback runs, iterate drops, by_type misses. */
	ew_test_destroy_count = 0u;
	editor->window_close(app, boot.api, scene_window);
	TEST_ASSERT_EQUAL_UINT(1u, ew_test_destroy_count);
	TEST_ASSERT_EQUAL_UINT(1u, editor->window_iterate(app, boot.api, open_windows, 4u));
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, EW_TEST_SCENE_TYPE_ID));

	editor->window_close(app, boot.api, graph_window);
	TEST_ASSERT_EQUAL_UINT(0u, editor->window_iterate(app, boot.api, NULL, 0u));

	/* Dockspace init opens only the windows whose mask admits the type. */
	scene_ws = editor->workspace_create(app, boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	TEST_ASSERT_EQUAL_UINT(1u, editor->window_iterate(app, boot.api, open_windows, 4u));
	TEST_ASSERT_EQUAL_PTR(open_windows[0], editor->window_by_type(app, boot.api, EW_TEST_SCENE_TYPE_ID));
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, EW_TEST_GRAPH_TYPE_ID));

	/* Init is idempotent (dedupes by type); reset restores the default set. */
	editor->dockspace_init(scene_ws);
	TEST_ASSERT_EQUAL_UINT(1u, editor->window_iterate(app, boot.api, open_windows, 4u));
	editor->dockspace_reset(scene_ws);
	TEST_ASSERT_EQUAL_UINT(1u, editor->window_iterate(app, boot.api, open_windows, 4u));

	editor->window_close(app, boot.api, open_windows[0]);
	TEST_ASSERT_EQUAL_UINT(0u, editor->window_iterate(app, boot.api, NULL, 0u));
	editor->workspace_destroy(scene_ws);

	sk_app_shutdown(app);
}

/* ------------------------------------------------------------------ */
/*  APX-330 dock tests (load the sk-ui plugin to get a real dock      */
/*  model; same plugins-dir resolution as tests/main.c)               */
/* ------------------------------------------------------------------ */

typedef struct ew_ui_fixture_t {
	sk_app_boot_t boot;
	const sk_ui_api_t* ui;
	sk_shared_lib_t lib;
} ew_ui_fixture_t;

/* Load the sk-ui shared plugin on a started app context and resolve its API
 * table. Returns 0 on success (caller must ew_ui_fixture_stop). */
static i32 ew_ui_fixture_start(ew_ui_fixture_t* fx) {
	const sk_platform_api_t* plat;
	const sk_filesystem_api_t* fs;
	sk_directory_iterator_t it;
	char base[SK_FS_PATH_MAX];
	char plugins_dir[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	char name[SK_FS_PATH_MAX];
	i32 found = 0;
	typedef int (*ew_plugin_entry_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

	memset(fx, 0, sizeof(*fx));
	fx->boot = sk_app_startup();
	if (fx->boot.context == NULL) {
		return -1;
	}
	plat = fx->boot.api->platform_api(fx->boot.context);
	fs = fx->boot.api->filesystem_api(fx->boot.context);

	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		return -1;
	}
	it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		return -1;
	}
	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		if (strncmp(name, "sk-ui.", 6u) != 0 || sk_is_shared_library_filename(name) == 0) {
			continue;
		}
		if (sk_path_join(sk_str_view_cstr(plugins_dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path)) < 0) {
			continue;
		}
		fx->lib = plat->lib_open(full_path);
		if (fx->lib == NULL) {
			continue;
		}
		found = 1;
		break;
	}
	fs->close_directory(it);
	if (found == 0) {
		return -1;
	}
	{
		void_ptr_t raw = plat->lib_symbol(fx->lib, "sk_plugin_entry_point");
		ew_plugin_entry_fn entry;
		if (raw == NULL) {
			plat->lib_close(fx->lib);
			fx->lib = NULL;
			return -1;
		}
		entry = SK_PTR_TO_FN(ew_plugin_entry_fn, raw);
		(void)entry(fx->boot.context, fx->boot.api);
	}
	fx->ui = (const sk_ui_api_t*)fx->boot.api->get_api(fx->boot.context, SK_UI_API_TYPE_ID);
	return fx->ui != NULL ? 0 : -1;
}

static void ew_ui_fixture_stop(ew_ui_fixture_t* fx) {
	const sk_platform_api_t* plat = NULL;
	if (fx->boot.context != NULL) {
		plat = fx->boot.api->platform_api(fx->boot.context);
		sk_app_shutdown(fx->boot.context);
	}
	if (fx->lib != NULL && plat != NULL) {
		plat->lib_close(fx->lib);
	}
	memset(fx, 0, sizeof(*fx));
}

SK_TEST(editor_scene_default_dock_layout) {
	ew_ui_fixture_t fx;
	sk_app_context_t* app;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_ui_context_t* ctx;
	const_chr_t tabs[8];
	u32 count;
	u32 active;
	sk_ui_dock_node_t console_leaf;
	sk_ui_dock_node_t entity_leaf;
	sk_ui_dock_node_t scene_leaf;

	TEST_ASSERT_EQUAL_INT(0, ew_ui_fixture_start(&fx));
	app = fx.boot.context;
	sk_editor_bind_tables(app, fx.boot.api);
	sk_editor_workspace_register_impls(app, fx.boot.api);
	sk_editor_windows_register_impls(app, fx.boot.api);
	editor = (const sk_editor_api_t*)fx.boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	ui = fx.ui;
	TEST_ASSERT_NOT_NULL(editor);

	/* Scene dockspace init opens exactly the All + Scene windows. */
	scene_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_DEBUGGER));
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_ENTITY_TREE));
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_HISTORY));
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_PROPERTIES));
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER));
	TEST_ASSERT_NOT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_MATERIAL_GRAPH_EDITOR));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_ANIMATOR_GRAPH));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_ANIMATOR_TREE_VIEW));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_PACKAGES));
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_SETTINGS));

	/* A real sk-ui dock model backs the workspace dockspace. */
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, "sk.editor.dock.ws1")));

	/* Console + Debugger share the bottom-right leaf, Console tab first. */
	console_leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.console");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(console_leaf));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(console_leaf, ui->dock_find_node_for_window(ctx, "sk.editor_window.debugger")));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.debugger"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, console_leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.console", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.debugger", tabs[1]);

	/* EntityTree + History share the right-top leaf, EntityTree tab first. */
	entity_leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.entity_tree");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(entity_leaf));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(entity_leaf, ui->dock_find_node_for_window(ctx, "sk.editor_window.history")));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, entity_leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.entity_tree", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.history", tabs[1]);

	/* Properties (right-bottom), ProjectBrowser (bottom-left) and SceneView
	 * (center) each land in their own leaf; SceneView is the central one. */
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.properties"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.project_browser"));
	scene_leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.scene_view");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(scene_leaf));
	TEST_ASSERT_FALSE(sk_ui_dock_node_eq(scene_leaf, console_leaf));
	TEST_ASSERT_FALSE(sk_ui_dock_node_eq(scene_leaf, entity_leaf));

	/* An on-demand window opened later docks into its default zone (center). */
	{
		sk_editor_window_t* res = editor->window_open(app, fx.boot.api, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER);
		TEST_ASSERT_NOT_NULL(res);
		TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, res->dock_id));
		TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, res->dock_id), scene_leaf));
	}

	/* Init stays idempotent with a live dock model; reset restores the
	 * default layout (default windows stay docked in their zones). */
	editor->dockspace_init(scene_ws);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.scene_view"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	editor->dockspace_reset(scene_ws);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.scene_view"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "sk.editor_window.console"), ui->dock_find_node_for_window(ctx, "sk.editor_window.debugger")));

	editor->workspace_destroy(scene_ws);
	ew_ui_fixture_stop(&fx);
}

SK_TEST(editor_preset_active_tab_is_lowest_order) {
	ew_ui_fixture_t fx;
	sk_app_context_t* app;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_editor_workspace_t* graph_ws;
	sk_ui_context_t* ctx;
	const_chr_t tabs[8];
	u32 count;
	u32 active;
	sk_ui_dock_node_t leaf;

	TEST_ASSERT_EQUAL_INT(0, ew_ui_fixture_start(&fx));
	app = fx.boot.context;
	sk_editor_bind_tables(app, fx.boot.api);
	sk_editor_workspace_register_impls(app, fx.boot.api);
	sk_editor_windows_register_impls(app, fx.boot.api);
	editor = (const sk_editor_api_t*)fx.boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	ui = fx.ui;
	TEST_ASSERT_NOT_NULL(editor);

	/* Scene preset: each leaf activates its lowest-order window (APX-379). */
	scene_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);

	/* RightTop: EntityTree (order 0) is active, not History (order 10). */
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.entity_tree");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(leaf));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.entity_tree", tabs[active]);

	/* BottomRight: Console (order 10) is active, not Debugger (order 20). */
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.console");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(leaf));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.console", tabs[active]);

	/* Single-tab leaves: Center (SceneView), RightBottom (Properties),
	 * BottomLeft (ProjectBrowser). */
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.scene_view");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.scene_view", tabs[active]);
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.properties");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.properties", tabs[active]);
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.project_browser");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.project_browser", tabs[active]);

	/* Graph preset after a workspace switch follows the same rule: Center is
	 * GraphEditor, BottomRight keeps Console active. */
	graph_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_GRAPH);
	TEST_ASSERT_NOT_NULL(graph_ws);
	editor->workspace_switch(graph_ws);
	editor->dockspace_init(graph_ws);
	ctx = editor->workspace_dock_context(graph_ws);
	TEST_ASSERT_NOT_NULL(ctx);

	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.graph_editor");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(leaf));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.graph_editor", tabs[active]);
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.console");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(leaf));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.console", tabs[active]);

	editor->workspace_destroy(graph_ws);
	editor->workspace_destroy(scene_ws);
	ew_ui_fixture_stop(&fx);
}

SK_TEST(editor_layout_roundtrip_preserves_selected_tab) {
	ew_ui_fixture_t fx;
	sk_app_context_t* app;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_editor_workspace_t* graph_ws;
	sk_ui_context_t* ctx;
	sk_editor_window_t* open[16];
	const_chr_t dock_ids[16];
	char dock_json[65536];
	u32 dock_len = 0u;
	u32 n;
	u32 i;
	const_chr_t tabs[8];
	u32 count;
	u32 active;
	sk_ui_dock_node_t leaf;

	TEST_ASSERT_EQUAL_INT(0, ew_ui_fixture_start(&fx));
	app = fx.boot.context;
	sk_editor_bind_tables(app, fx.boot.api);
	sk_editor_workspace_register_impls(app, fx.boot.api);
	sk_editor_windows_register_impls(app, fx.boot.api);
	editor = (const sk_editor_api_t*)fx.boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	ui = fx.ui;
	TEST_ASSERT_NOT_NULL(editor);

	scene_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);

	/* Simulate the user selecting Debugger in the BottomRight leaf. */
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_set_active(ctx, "sk.editor_window.debugger"));
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.debugger");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.debugger", tabs[active]);

	/* Capture the layout: dock JSON (holds per-leaf active tabs) + the open
	 * window ids the restore must keep. */
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_save_dock_json(scene_ws, dock_json, (u32)sizeof(dock_json), &dock_len));
	TEST_ASSERT_TRUE(dock_len > 0u);
	n = sk_editor_window_iterate(app, fx.boot.api, open, 16u);
	TEST_ASSERT_TRUE(n > 0u && n <= 16u);
	for (i = 0u; i < n; ++i) {
		dock_ids[i] = open[i]->dock_id;
	}

	/* Switch to Graph (fresh preset), then back to Scene (saved layout). */
	sk_editor_workspace_clear_dockspace(scene_ws);
	graph_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_GRAPH);
	TEST_ASSERT_NOT_NULL(graph_ws);
	editor->workspace_switch(graph_ws);
	editor->dockspace_init(graph_ws);
	ctx = editor->workspace_dock_context(graph_ws);
	TEST_ASSERT_NOT_NULL(ctx);
	/* The Graph preset itself defaults to the lowest-order window. */
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.console");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.console", tabs[active]);

	editor->workspace_switch(scene_ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_workspace_apply_layout(scene_ws, dock_ids, n, dock_json, dock_len));

	/* The restored Scene layout keeps the user-selected tab (Debugger), not
	 * the preset default (Console). */
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);
	leaf = ui->dock_find_node_for_window(ctx, "sk.editor_window.debugger");
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(leaf));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, leaf, tabs, 8u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("sk.editor_window.debugger", tabs[active]);

	editor->workspace_destroy(graph_ws);
	editor->workspace_destroy(scene_ws);
	ew_ui_fixture_stop(&fx);
}

SK_TEST(editor_window_close_removes_instance) {
	ew_ui_fixture_t fx;
	sk_app_context_t* app;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_ui_context_t* ctx;
	sk_editor_window_t* console;

	TEST_ASSERT_EQUAL_INT(0, ew_ui_fixture_start(&fx));
	app = fx.boot.context;
	sk_editor_bind_tables(app, fx.boot.api);
	sk_editor_workspace_register_impls(app, fx.boot.api);
	sk_editor_windows_register_impls(app, fx.boot.api);
	editor = (const sk_editor_api_t*)fx.boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	ui = fx.ui;

	scene_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);

	console = editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE);
	TEST_ASSERT_NOT_NULL(console);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, console->dock_id));

	/* Closing through the editor goes via dock_tab_close: the instance is
	 * removed and the dock tab disappears. */
	editor->window_close(app, fx.boot.api, console);
	TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "sk.editor_window.console"));
	TEST_ASSERT_EQUAL_UINT(6u, editor->window_iterate(app, fx.boot.api, NULL, 0u));

	/* The dock tab callback (dock_set_tab_callback) closes instances on a
	 * chrome-initiated close too. */
	{
		sk_editor_window_t* debugger = editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_DEBUGGER);
		TEST_ASSERT_NOT_NULL(debugger);
		TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, debugger->dock_id));
		TEST_ASSERT_NULL(editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_DEBUGGER));
		TEST_ASSERT_EQUAL_UINT(5u, editor->window_iterate(app, fx.boot.api, NULL, 0u));
	}

	editor->workspace_destroy(scene_ws);
	ew_ui_fixture_stop(&fx);
}

SK_TEST(editor_window_undock_redock_changes_parent) {
	ew_ui_fixture_t fx;
	sk_app_context_t* app;
	const sk_editor_api_t* editor;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_ui_context_t* ctx;
	sk_editor_window_t* console;
	sk_ui_dock_node_t leaf;

	TEST_ASSERT_EQUAL_INT(0, ew_ui_fixture_start(&fx));
	app = fx.boot.context;
	sk_editor_bind_tables(app, fx.boot.api);
	sk_editor_workspace_register_impls(app, fx.boot.api);
	sk_editor_windows_register_impls(app, fx.boot.api);
	editor = (const sk_editor_api_t*)fx.boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	ui = fx.ui;

	scene_ws = editor->workspace_create(app, fx.boot.api, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(scene_ws);
	editor->dockspace_init(scene_ws);
	ctx = editor->workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(ctx);

	console = editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE);
	TEST_ASSERT_NOT_NULL(console);
	leaf = ui->dock_find_node_for_window(ctx, console->dock_id);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(leaf));

	/* Undock tears the window out (floating); the instance stays open. */
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, console->dock_id));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, console->dock_id));
	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dock_find_node_for_window(ctx, console->dock_id)));
	TEST_ASSERT_EQUAL_PTR(console, editor->window_by_type(app, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE));

	/* Redock onto the original leaf: parent dock node is a leaf again. */
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, console->dock_id, leaf, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, console->dock_id));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, console->dock_id), leaf));

	editor->workspace_destroy(scene_ws);
	ew_ui_fixture_stop(&fx);
}

#endif /* SK_TESTS */
