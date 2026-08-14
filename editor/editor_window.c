/**
 * @file editor_window.c
 * @brief Editor window + workspace registry (APX-329).
 *
 * Scaffolding for the main editor's window/workspace model:
 *
 *  - Window implementations (static `sk_editor_window_t` tables) and
 *    workspace types (static `sk_editor_workspace_type_t` descriptors)
 *    register on the app context via `app_api->add_impl` under two distinct
 *    type ids and are enumerated via `app_api->get_all_impls`.
 *  - Open workspaces and open window instances live in a per-app-context
 *    editor registry stashed on the app registry (no file-scope mutable
 *    state). The registry is created lazily and freed when the last
 *    workspace/window is destroyed, so contexts that create+destroy cleanly
 *    never leak.
 *  - `dockspace_init`/`dockspace_reset` derive a workspace's default dock
 *    layout from the registered window impls whose workspace_mask admits the
 *    workspace type and open one instance of each (deduped by type_id).
 *
 * The 14 concrete editor windows land in a later task; this file only owns
 * the contract + registry surface they plug into.
 */

#include "editor_window.h"

#include "allocator.h"
#include "array.h"

#include <string.h>

/* Private registry type id: the editor registry object is stashed on the app
 * registry so it lives exactly as long as the app context that owns it. */
#define SK_EDITOR_REGISTRY_TYPE_ID SK_TYPE_ID("sk.editor_registry", 0x5afff7f680b3cb29ULL, 0xd548604f7f3f7392ULL)

/* One default dock slot per docked window type (type + placement metadata for
 * the future dockspace renderer). */
typedef struct sk_editor_dock_slot_t {
	sk_type_id_t window_type_id;
	i32 dock_position;
	i32 order;
} sk_editor_dock_slot_t;

typedef struct sk_editor_registry_t {
	SK_ARRAY(sk_editor_workspace_t*) workspaces;
	SK_ARRAY(sk_editor_window_t*) windows;
	sk_editor_workspace_t* active;
} sk_editor_registry_t;

struct sk_editor_workspace_t {
	sk_editor_registry_t* registry;
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	u32 type_id;
	const_chr_t display_name;
	SK_ARRAY(sk_editor_dock_slot_t) docks;
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
	return window;
}

void sk_editor_window_close(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	sk_editor_registry_t* registry = editor_registry_get(app_context, app_api);
	for (u32 i = 0u; registry != NULL && i < registry->windows.count; ++i) {
		if (registry->windows.items[i] == window) {
			if (window->destroy != NULL) {
				window->destroy(window);
			}
			sk_array_swap_remove(&registry->windows, i);
			sk_allocator_default()->free(sk_allocator_default()->instance, window);
			editor_registry_maybe_free(app_context, app_api, registry);
			return;
		}
	}
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

/* Derive the workspace's default dock layout from the registered window
 * impls whose workspace_mask admits the workspace type; open one instance of
 * each docked window (deduped by type). Both init and reset restore this
 * default derivation. */
static void dockspace_build(sk_editor_workspace_t* workspace) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_app_context_t* app_context = workspace->app_context;
	const sk_app_api_t* app_api = workspace->app_api;
	const_ptr_t* impls;
	u32 count = app_api->impl_count(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID);
	sk_array_free(&workspace->docks);
	sk_array_init(&workspace->docks, alloc);
	if (count == 0u) {
		return;
	}
	impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
	if (impls == NULL) {
		return;
	}
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

void sk_editor_dockspace_init(sk_editor_workspace_t* workspace) {
	dockspace_build(workspace);
}

void sk_editor_dockspace_reset(sk_editor_workspace_t* workspace) {
	dockspace_build(workspace);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
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

#endif /* SK_TESTS */
