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
#include <stdio.h>
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

SK_TEST(ui_dock_demo_scene_identical_geometry_two_runs) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* a;
	sk_ui_context_t* b;
	sk_ui_rect_t space;
	sk_ui_rect_t a_left, b_left, a_center, b_center, a_rt, b_rt, a_rb, b_rb;
	sk_ui_dock_node_t a_ds, b_ds;
	f32 dw = 0.0f;
	f32 dh = 0.0f;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock demo)");
	}
	ui = env.ui;
	ui->sample_dock_demo_logical_size(&dw, &dh);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 1280.0f, dw);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 720.0f, dh);

	a = ui->context_create(NULL);
	b = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_dock_demo_build(a, SK_UI_NODE_INVALID)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_dock_demo_build(b, SK_UI_NODE_INVALID)));

	space.x = 0.0f;
	space.y = 0.0f;
	space.width = dw;
	space.height = dh;
	a_ds = ui->dockspace_find(a, "dock-demo");
	b_ds = ui->dockspace_find(b, "dock-demo");
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(a, a_ds, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(b, b_ds, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(a, ui->dock_find_node_for_window(a, "dock-demo-hierarchy"), &a_left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(b, ui->dock_find_node_for_window(b, "dock-demo-hierarchy"), &b_left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(a, ui->dock_find_node_for_window(a, "dock-demo-scene"), &a_center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(b, ui->dock_find_node_for_window(b, "dock-demo-scene"), &b_center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(a, ui->dock_find_node_for_window(a, "dock-demo-inspector"), &a_rt));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(b, ui->dock_find_node_for_window(b, "dock-demo-inspector"), &b_rt));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(a, ui->dock_find_node_for_window(a, "dock-demo-console"), &a_rb));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(b, ui->dock_find_node_for_window(b, "dock-demo-console"), &b_rb));
	TEST_ASSERT_EQUAL_MEMORY(&a_left, &b_left, sizeof(sk_ui_rect_t));
	TEST_ASSERT_EQUAL_MEMORY(&a_center, &b_center, sizeof(sk_ui_rect_t));
	TEST_ASSERT_EQUAL_MEMORY(&a_rt, &b_rt, sizeof(sk_ui_rect_t));
	TEST_ASSERT_EQUAL_MEMORY(&a_rb, &b_rb, sizeof(sk_ui_rect_t));

	ui->context_destroy(a);
	ui->context_destroy(b);
	uidock_shutdown(&env);
}

#ifndef SK_UI_GOLDEN_DIR
#define SK_UI_GOLDEN_DIR "plugins/ui/testdata"
#endif

static void uidock_set_float_rect(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t win, f32 x, f32 y, f32 w, f32 h, i32 z) {
	sk_ui_layout_style_t st;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, win, &st));
	st.position = SK_UI_POSITION_ABSOLUTE;
	st.left = sk_ui_pt(x);
	st.top = sk_ui_pt(y);
	st.width = sk_ui_pt(w);
	st.height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, win, &st));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_prop_i32(ctx, win, "z_index", z));
}

static f32 uidock_len_pt(sk_ui_length_t len, f32 fallback) {
	if (len.unit == SK_UI_LENGTH_POINT) {
		return len.value;
	}
	return fallback;
}

static void uidock_make_workspace_windows(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t* out_profiler, sk_ui_node_t* out_inspector) {
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Game", "game");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Console", "console");
	{
		sk_ui_node_t profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
		sk_ui_node_t inspector = ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
		if (out_profiler != NULL) {
			*out_profiler = profiler;
		}
		if (out_inspector != NULL) {
			*out_inspector = inspector;
		}
	}
}

static void uidock_build_workspace(const sk_ui_api_t* ui, sk_ui_context_t* ctx) {
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t bottom;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t split;
	sk_ui_node_t profiler;
	sk_ui_node_t inspector;

	uidock_make_workspace_windows(ui, ctx, &profiler, &inspector);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "editor-main", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	split = ui->dockspace_find(ctx, "editor-main");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, split, "root"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, left, "left"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_DOWN, 0.25f, &bottom, &center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, center, "central"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_set_node_id(ctx, bottom, "bottom"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "game", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "profiler", center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "inspector", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_tab_set_active(ctx, "game"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_undock(ctx, "inspector"));
	uidock_set_float_rect(ui, ctx, profiler, 80.0f, 60.0f, 360.0f, 240.0f, 50);
	uidock_set_float_rect(ui, ctx, inspector, 120.0f, 90.0f, 320.0f, 200.0f, 51);
}

static void uidock_assert_trees_equal(const sk_ui_api_t* ui, const sk_ui_context_t* a, sk_ui_dock_node_t na, const sk_ui_context_t* b, sk_ui_dock_node_t nb) {
	typedef struct uidock_eq_frame_t {
		sk_ui_dock_node_t a;
		sk_ui_dock_node_t b;
	} uidock_eq_frame_t;
	uidock_eq_frame_t stack[128];
	u32 sp = 0u;
	stack[sp].a = na;
	stack[sp].b = nb;
	sp += 1u;
	while (sp > 0u) {
		uidock_eq_frame_t fr = stack[--sp];
		i32 a_split = ui->dock_node_is_split(a, fr.a);
		TEST_ASSERT_EQUAL_INT(a_split, ui->dock_node_is_split(b, fr.b));
		if (a_split != 0) {
			TEST_ASSERT_EQUAL_INT((int)ui->dock_split_get_axis(a, fr.a), (int)ui->dock_split_get_axis(b, fr.b));
			TEST_ASSERT_FLOAT_WITHIN(0.0001f, ui->dock_split_get_ratio(a, fr.a), ui->dock_split_get_ratio(b, fr.b));
			if (sp >= 128u) {
				TEST_FAIL_MESSAGE("dock tree compare stack overflow");
				return;
			}
			stack[sp].a = ui->dock_split_child(a, fr.a, 1u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 1u);
			sp += 1u;
			if (sp >= 128u) {
				TEST_FAIL_MESSAGE("dock tree compare stack overflow");
				return;
			}
			stack[sp].a = ui->dock_split_child(a, fr.a, 0u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 0u);
			sp += 1u;
			continue;
		}
		{
			const_chr_t a_ids[SK_UI_DOCK_LEAF_TABS_MAX];
			const_chr_t b_ids[SK_UI_DOCK_LEAF_TABS_MAX];
			u32 a_count = 0u;
			u32 b_count = 0u;
			u32 a_active = 0u;
			u32 b_active = 0u;
			u32 i;
			TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(a, fr.a));
			TEST_ASSERT_EQUAL_INT(1, ui->dock_node_is_leaf(b, fr.b));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(a, fr.a, a_ids, SK_UI_DOCK_LEAF_TABS_MAX, &a_count, &a_active));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(b, fr.b, b_ids, SK_UI_DOCK_LEAF_TABS_MAX, &b_count, &b_active));
			TEST_ASSERT_EQUAL_UINT(a_count, b_count);
			TEST_ASSERT_EQUAL_UINT(a_active, b_active);
			for (i = 0u; i < a_count; ++i) {
				TEST_ASSERT_EQUAL_STRING(a_ids[i], b_ids[i]);
			}
		}
	}
}

static void uidock_assert_float_rect(const sk_ui_api_t* ui, const sk_ui_context_t* ctx, const_chr_t id, f32 x, f32 y, f32 w, f32 h, i32 z) {
	sk_ui_node_t win = ui->find_by_id(ctx, id);
	sk_ui_layout_style_t st;
	sk_ui_prop_value_t prop;
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(win));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, id));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, win, &st));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_POSITION_ABSOLUTE, (int)st.position);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, x, uidock_len_pt(st.left, 0.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, y, uidock_len_pt(st.top, 0.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, w, uidock_len_pt(st.width, 360.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, h, uidock_len_pt(st.height, 240.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, win, "z_index", &prop));
	TEST_ASSERT_EQUAL_INT(SK_UI_PROP_I32, (int)prop.type);
	TEST_ASSERT_EQUAL_INT(z, prop.data.i32_value);
}

static void uidock_assert_workspace(const sk_ui_api_t* ui, const sk_ui_context_t* ctx) {
	sk_ui_dock_node_t root = ui->dockspace_find(ctx, "editor-main");
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t inner;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t bottom;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_HORIZONTAL, (int)ui->dock_split_get_axis(ctx, root));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, root));
	left = ui->dock_split_child(ctx, root, 0u);
	inner = ui->dock_split_child(ctx, root, 1u);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, left));
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, inner));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)ui->dock_split_get_axis(ctx, inner));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, ui->dock_split_get_ratio(ctx, inner));
	center = ui->dock_split_child(ctx, inner, 0u);
	bottom = ui->dock_split_child(ctx, inner, 1u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, left, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_UINT(0u, active);
	TEST_ASSERT_EQUAL_STRING("hierarchy", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, center, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_UINT(1u, active);
	TEST_ASSERT_EQUAL_STRING("scene", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("game", tabs[1]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, bottom, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", tabs[0]);
	uidock_assert_float_rect(ui, ctx, "profiler", 80.0f, 60.0f, 360.0f, 240.0f, 50);
	uidock_assert_float_rect(ui, ctx, "inspector", 120.0f, 90.0f, 320.0f, 200.0f, 51);
}

static i32 uidock_read_golden(const char* name, char* out, u32 cap, u32* out_len) {
	static const char* bases[] = {
		SK_UI_GOLDEN_DIR "/dock",
		"plugins/ui/testdata/dock",
		"../plugins/ui/testdata/dock",
		"../../plugins/ui/testdata/dock",
	};
	u32 i;
	for (i = 0u; i < sizeof(bases) / sizeof(bases[0]); ++i) {
		char path[512];
		FILE* f;
		long file_size;
		u32 n;
		u32 w;
		u32 k;
		(void)snprintf(path, sizeof(path), "%s/%s.json", bases[i], name);
		f = fopen(path, "rb");
		if (f == NULL) {
			continue;
		}
		if (fseek(f, 0, SEEK_END) != 0) {
			fclose(f);
			return -1;
		}
		file_size = ftell(f);
		if (file_size < 0 || (u32)file_size + 1u > cap) {
			fclose(f);
			return -1;
		}
		if (fseek(f, 0, SEEK_SET) != 0) {
			fclose(f);
			return -1;
		}
		n = (u32)file_size;
		if (fread(out, 1u, n, f) != n) {
			fclose(f);
			return -1;
		}
		fclose(f);
		w = 0u;
		for (k = 0u; k < n; ++k) {
			if (out[k] == '\r') {
				continue;
			}
			out[w++] = out[k];
		}
		out[w] = '\0';
		if (out_len != NULL) {
			*out_len = w;
		}
		return 0;
	}
	return -1;
}

SK_TEST(ui_dock_save_then_restore_structurally_equal) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* src;
	sk_ui_context_t* dst;
	char json[8192];
	u32 len = 0u;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock restore)");
	}
	ui = env.ui;
	src = ui->context_create(NULL);
	dst = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(src);
	TEST_ASSERT_NOT_NULL(dst);

	uidock_build_workspace(ui, src);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_save_json(src, "editor-main", json, (u32)sizeof(json), &len));
	TEST_ASSERT_TRUE(len > 0u);

	uidock_make_workspace_windows(ui, dst, NULL, NULL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(dst, "editor-main", json, len));
	uidock_assert_trees_equal(ui, src, ui->dockspace_find(src, "editor-main"), dst, ui->dockspace_find(dst, "editor-main"));
	uidock_assert_workspace(ui, src);
	uidock_assert_workspace(ui, dst);

	ui->context_destroy(src);
	ui->context_destroy(dst);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_golden_workspace_fixture) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	char json[8192];
	u32 len = 0u;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock golden restore)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT(0, uidock_read_golden("v1_workspace", json, (u32)sizeof(json), &len));
	uidock_make_workspace_windows(ui, ctx, NULL, NULL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "editor-main", json, len));
	uidock_assert_workspace(ui, ctx);

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

#endif /* SK_TESTS */
