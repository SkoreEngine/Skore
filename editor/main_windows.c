/**
 * @file main_windows.c
 * @brief The 14 main editor windows (APX-330): impl tables + registration.
 *
 * Scaffolding only: every window is a static `sk_editor_window_t` with
 * title, dock id, default dock slot (position + order) and workspace mask,
 * and an empty Draw. No per-window UI is ported yet — dockspace init opens
 * the default windows and docks their (empty) chrome, and the dock APIs
 * (close / move / undock / tab reorder) stay live through the sk-ui dock
 * chrome.
 *
 * type_id is assigned at register time: SK_TYPE_ID expands to a compound
 * literal, which -Wpedantic rejects in a file-scope initializer (same
 * constraint as the SK_TESTS scaffolding in editor_window.c).
 */

#include "main_windows.h"

/* ------------------------------------------------------------------ */
/*  Impl tables (titles + docking metadata only, empty Draw)          */
/* ------------------------------------------------------------------ */

static sk_editor_window_t editor_main_windows[] = {
	{
		.title = "Console",
		.dock_id = "sk.editor_window.console",
		.dock_position = SK_EDITOR_DOCK_BOTTOM_RIGHT,
		.order = 10,
		.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	},
	{
		.title = "Debugger",
		.dock_id = "sk.editor_window.debugger",
		.dock_position = SK_EDITOR_DOCK_BOTTOM_RIGHT,
		.order = 20,
		.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	},
	{
		.title = "Entity Tree",
		.dock_id = "sk.editor_window.entity_tree",
		.dock_position = SK_EDITOR_DOCK_RIGHT_TOP,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	},
	{
		.title = "History",
		.dock_id = "sk.editor_window.history",
		.dock_position = SK_EDITOR_DOCK_RIGHT_TOP,
		.order = 10,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	},
	{
		.title = "Properties",
		.dock_id = "sk.editor_window.properties",
		.dock_position = SK_EDITOR_DOCK_RIGHT_BOTTOM,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	},
	{
		.title = "Project Browser",
		.dock_id = "sk.editor_window.project_browser",
		.dock_position = SK_EDITOR_DOCK_BOTTOM_LEFT,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	},
	{
		.title = "Scene View",
		.dock_id = "sk.editor_window.scene_view",
		.dock_position = SK_EDITOR_DOCK_FILL,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE),
	},
	{
		.title = "Graph Editor",
		.dock_id = "sk.editor_window.graph_editor",
		.dock_position = SK_EDITOR_DOCK_FILL,
		.order = 20,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_GRAPH),
	},
	{
		.title = "Material Graph Editor",
		.dock_id = "sk.editor_window.material_graph_editor",
		.dock_position = SK_EDITOR_DOCK_FILL,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_MATERIAL),
	},
	{
		.title = "Animator Graph",
		.dock_id = "sk.editor_window.animator_graph",
		.dock_position = SK_EDITOR_DOCK_FILL,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_ANIMATOR),
	},
	{
		.title = "Animator Tree View",
		.dock_id = "sk.editor_window.animator_tree_view",
		.dock_position = SK_EDITOR_DOCK_LEFT,
		.order = 0,
		.workspace_mask = SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_ANIMATOR),
	},
	{
		.title = "Resource Debugger",
		.dock_id = "sk.editor_window.resource_debugger",
		.dock_position = SK_EDITOR_DOCK_FILL,
		.order = 50,
		.workspace_mask = 0u, /* on-demand: opened explicitly, never by dockspace init */
	},
	{
		.title = "Packages",
		.dock_id = "sk.editor_window.packages",
		.dock_position = SK_EDITOR_DOCK_NONE,
		.order = 0,
		.workspace_mask = 0u, /* menu-opened, not default-docked */
	},
	{
		.title = "Settings",
		.dock_id = "sk.editor_window.settings",
		.dock_position = SK_EDITOR_DOCK_NONE,
		.order = 0,
		.workspace_mask = 0u, /* menu-opened, not default-docked */
	},
};

#define SK_EDITOR_MAIN_WINDOW_COUNT (u32)(sizeof(editor_main_windows) / sizeof(editor_main_windows[0]))

void sk_editor_windows_register_impls(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	/* type ids are assigned here (not in the file-scope initializer) because
	 * SK_TYPE_ID's compound literal is not a constant expression. */
	editor_main_windows[0].type_id = SK_EDITOR_WINDOW_CONSOLE;
	editor_main_windows[1].type_id = SK_EDITOR_WINDOW_DEBUGGER;
	editor_main_windows[2].type_id = SK_EDITOR_WINDOW_ENTITY_TREE;
	editor_main_windows[3].type_id = SK_EDITOR_WINDOW_HISTORY;
	editor_main_windows[4].type_id = SK_EDITOR_WINDOW_PROPERTIES;
	editor_main_windows[5].type_id = SK_EDITOR_WINDOW_PROJECT_BROWSER;
	editor_main_windows[6].type_id = SK_EDITOR_WINDOW_SCENE_VIEW;
	editor_main_windows[7].type_id = SK_EDITOR_WINDOW_GRAPH_EDITOR;
	editor_main_windows[8].type_id = SK_EDITOR_WINDOW_MATERIAL_GRAPH_EDITOR;
	editor_main_windows[9].type_id = SK_EDITOR_WINDOW_ANIMATOR_GRAPH;
	editor_main_windows[10].type_id = SK_EDITOR_WINDOW_ANIMATOR_TREE_VIEW;
	editor_main_windows[11].type_id = SK_EDITOR_WINDOW_RESOURCE_DEBUGGER;
	editor_main_windows[12].type_id = SK_EDITOR_WINDOW_PACKAGES;
	editor_main_windows[13].type_id = SK_EDITOR_WINDOW_SETTINGS;

	u32 i;
	for (i = 0u; i < SK_EDITOR_MAIN_WINDOW_COUNT; ++i) {
		app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &editor_main_windows[i]);
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

SK_TEST(editor_main_windows_impl_count_covers_all_types) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* app = boot.context;
	const_ptr_t impls[16];
	u32 n;
	u32 i;

	TEST_ASSERT_NOT_NULL(app);
	TEST_ASSERT_EQUAL_UINT(0u, boot.api->impl_count(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID));

	sk_editor_windows_register_impls(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(14u, boot.api->impl_count(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID));
	n = boot.api->get_all_impls(app, SK_EDITOR_WINDOW_IMPL_TYPE_ID, impls, 16u);
	TEST_ASSERT_EQUAL_UINT(14u, n);

	/* Every registered impl carries a type id, title and dock id; the
	 * required window types are all present. */
	for (i = 0u; i < n; ++i) {
		const sk_editor_window_t* win = (const sk_editor_window_t*)impls[i];
		TEST_ASSERT_TRUE(!SK_TYPE_ID_EQ(win->type_id, SK_TYPE_ID_ZERO));
		TEST_ASSERT_NOT_NULL(win->title);
		TEST_ASSERT_NOT_NULL(win->dock_id);
	}
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(((const sk_editor_window_t*)impls[0])->type_id, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(((const sk_editor_window_t*)impls[6])->type_id, SK_EDITOR_WINDOW_SCENE_VIEW));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(((const sk_editor_window_t*)impls[13])->type_id, SK_EDITOR_WINDOW_SETTINGS));

	/* Spot-check default dock metadata of a few windows. */
	TEST_ASSERT_EQUAL_STRING("Console", ((const sk_editor_window_t*)impls[0])->title);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_BOTTOM_RIGHT, ((const sk_editor_window_t*)impls[0])->dock_position);
	TEST_ASSERT_EQUAL_INT(10, ((const sk_editor_window_t*)impls[0])->order);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(((const sk_editor_window_t*)impls[0])->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(((const sk_editor_window_t*)impls[0])->workspace_mask, SK_EDITOR_WORKSPACE_GRAPH));

	/* EntityTree is Scene-only; SceneView docks center. */
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(((const sk_editor_window_t*)impls[2])->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));
	TEST_ASSERT_FALSE(sk_editor_workspace_mask_contains(((const sk_editor_window_t*)impls[2])->workspace_mask, SK_EDITOR_WORKSPACE_GRAPH));
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_FILL, ((const sk_editor_window_t*)impls[6])->dock_position);

	/* On-demand / menu windows are never default-docked (mask 0). */
	TEST_ASSERT_EQUAL_UINT(0u, ((const sk_editor_window_t*)impls[11])->workspace_mask);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_NONE, ((const sk_editor_window_t*)impls[12])->dock_position);
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_NONE, ((const sk_editor_window_t*)impls[13])->dock_position);

	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
