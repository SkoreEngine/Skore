/*
 * ui_dock.c — headless dock model + layout solver (APX-288).
 *
 * Loads sk-ui via the app registry and exercises the public dockspace API
 * without a renderer or Clay layout. Plugin-local SK_TEST coverage lives in
 * plugins/ui/dock.c; this TU keeps the same contract green in Release ctest.
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"
#include "ui.h"

#ifdef noreturn
#undef noreturn
#endif
#include <string.h>

#ifdef SK_TESTS

typedef struct uidock_env_t {
	sk_app_context_t* app;
	const sk_ui_api_t* ui;
} uidock_env_t;

static i32 uidock_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];
	i32 n;

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static i32 uidock_boot(uidock_env_t* env) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	memset(env, 0, sizeof(*env));
	env->app = sk_app_init(0, NULL);
	if (env->app == NULL) {
		return -1;
	}
	if (uidock_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(env->app, path);
	}
	env->ui = (const sk_ui_api_t*)sk_app_api()->get_api(env->app, SK_UI_API_TYPE_ID);
	if (env->ui == NULL) {
		sk_app_destroy(env->app);
		env->app = NULL;
		return -1;
	}
	return 0;
}

static void uidock_shutdown(uidock_env_t* env) {
	if (env != NULL && env->app != NULL) {
		sk_app_destroy(env->app);
		env->app = NULL;
		env->ui = NULL;
	}
}

SK_TEST(ui_dock_headless_tree_and_layout) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t split;
	sk_ui_rect_t space;
	sk_ui_rect_t rl;
	sk_ui_rect_t rs;
	sk_ui_dock_dir_t dir;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const f32 leftover = 800.0f - SK_UI_DOCK_SPLITTER_PT;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip headless dock)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "editor-main", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_DOWN, 0.28f, &bottom, &center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "game", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));

	split = ui->dockspace_find(ctx, "editor-main");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, split));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, center, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("scene", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("game", tabs[1]);

	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 800.0f;
	space.height = 600.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, left, &rl));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(ctx, split, &rs));
	TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.25f, rl.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, SK_UI_DOCK_SPLITTER_PT, rs.width);
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_node_at_point(ctx, rl.x + rl.width * 0.5f, rl.y + rl.height * 0.5f, &dir), left));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_DIR_CENTER, (int)dir);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_close(ctx, "console"));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, SK_UI_DOCK_NODE_INVALID, &space));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, ui->dock_find_node_for_window(ctx, "scene")));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "console"));

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

#endif /* SK_TESTS */
