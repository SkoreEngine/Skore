/*
 * ui_dock.c — dock model, layout solver, and public-API e2e (APX-288 / APX-291).
 *
 * Loads sk-ui via the app registry. Headless tests use dockspace_layout;
 * interaction tests drive pointer events through input_dispatch after Clay
 * layout. Plugin-local SK_TEST coverage lives in plugins/ui/dock.c; this TU
 * keeps the same contract green in Release ctest.
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
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
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
	sk_app_boot_t boot;
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	memset(env, 0, sizeof(*env));
	boot = sk_app_init(0, NULL);
	env->app = boot.context;
	if (env->app == NULL) {
		return -1;
	}
	if (uidock_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		boot.api->load_plugin(env->app, path);
	}
	env->ui = (const sk_ui_api_t*)boot.api->get_api(env->app, SK_UI_API_TYPE_ID);
	if (env->ui == NULL) {
		sk_app_shutdown(env->app);
		env->app = NULL;
		return -1;
	}
	return 0;
}

static void uidock_shutdown(uidock_env_t* env) {
	if (env != NULL && env->app != NULL) {
		sk_app_shutdown(env->app);
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
	{
		sk_ui_rect_t space;
		sk_ui_rect_t src_left;
		sk_ui_rect_t dst_left;
		sk_ui_rect_t src_center;
		sk_ui_rect_t dst_center;
		sk_ui_rect_t src_split;
		sk_ui_rect_t dst_split;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 1280.0f;
		space.height = 720.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(src, ui->dockspace_find(src, "editor-main"), &space));
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(dst, ui->dockspace_find(dst, "editor-main"), &space));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(src, ui->dock_find_node_for_window(src, "hierarchy"), &src_left));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(dst, ui->dock_find_node_for_window(dst, "hierarchy"), &dst_left));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(src, ui->dock_find_node_for_window(src, "scene"), &src_center));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(dst, ui->dock_find_node_for_window(dst, "scene"), &dst_center));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(src, ui->dockspace_find(src, "editor-main"), &src_split));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(dst, ui->dockspace_find(dst, "editor-main"), &dst_split));
		TEST_ASSERT_FLOAT_WITHIN(0.01f, src_left.x, dst_left.x);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, src_left.width, dst_left.width);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, src_center.y, dst_center.y);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, src_center.height, dst_center.height);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, src_split.width, dst_split.width);
	}
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

SK_TEST(ui_dock_restore_drop_collapses_split) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t a;
	sk_ui_dock_node_t b;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"mismatch\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.3,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"gone-side\", \"flags\": 0, \"tabs\": [\"gone\"], \"active_index\": 0 },\n"
					   "        \"b\": {\n"
					   "            \"kind\": \"split\",\n"
					   "            \"axis\": 1,\n"
					   "            \"ratio\": 0.6,\n"
					   "            \"id\": \"inner\",\n"
					   "            \"flags\": 0,\n"
					   "            \"a\": { \"kind\": \"leaf\", \"id\": \"top\", \"flags\": 0, \"tabs\": [\"keep-a\"], \"active_index\": 0 },\n"
					   "            \"b\": { \"kind\": \"leaf\", \"id\": \"bot\", \"flags\": 0, \"tabs\": [\"keep-b\"], \"active_index\": 0 }\n"
					   "        }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock mismatch)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "A", "keep-a");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "B", "keep-b");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "mismatch", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "mismatch");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_DOCK_SPLIT_VERTICAL, (int)ui->dock_split_get_axis(ctx, root));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f, ui->dock_split_get_ratio(ctx, root));
	a = ui->dock_split_child(ctx, root, 0u);
	b = ui->dock_split_child(ctx, root, 1u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, a, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep-a", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, b, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep-b", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone"));
	{
		sk_ui_rect_t space;
		sk_ui_rect_t box;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 600.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, root, &box));
		TEST_ASSERT_FLOAT_WITHIN(0.01f, space.width, box.width);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, space.height, box.height);
	}
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_drop_cascades_nested) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"cascade\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": {\n"
					   "            \"kind\": \"split\",\n"
					   "            \"axis\": 1,\n"
					   "            \"ratio\": 0.4,\n"
					   "            \"id\": \"dead\",\n"
					   "            \"flags\": 0,\n"
					   "            \"a\": { \"kind\": \"leaf\", \"id\": \"gone-a\", \"flags\": 0, \"tabs\": [\"gone-1\"], \"active_index\": 0 },\n"
					   "            \"b\": { \"kind\": \"leaf\", \"id\": \"gone-b\", \"flags\": 0, \"tabs\": [\"gone-2\"], \"active_index\": 0 }\n"
					   "        },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"keep\", \"flags\": 0, \"tabs\": [\"keep\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock cascade)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Keep", "keep");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "cascade", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "cascade");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep", tabs[0]);
	{
		sk_ui_rect_t space;
		sk_ui_rect_t box;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 600.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, root, &box));
		TEST_ASSERT_FLOAT_WITHIN(0.01f, space.width, box.width);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, space.height, box.height);
	}
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_unsaved_default_dock_and_float) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_node_t profiler;
	sk_ui_rect_t def;
	sk_ui_layout_style_t st;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"unsaved\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"left\", \"flags\": 0, \"tabs\": [\"hierarchy\"], \"active_index\": 0 },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"center\", \"flags\": 1, \"tabs\": [\"scene\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock unsaved)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	def.x = 88.0f;
	def.y = 66.0f;
	def.width = 320.0f;
	def.height = 200.0f;
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
	profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "inspector", "left", NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "profiler", NULL, &def));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "unsaved", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "unsaved");
	left = ui->dock_split_child(ctx, root, 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, left, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("inspector", tabs[1]);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "inspector"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, profiler, &st));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_POSITION_ABSOLUTE, (int)st.position);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, uidock_len_pt(st.left, 0.0f), def.x);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, uidock_len_pt(st.top, 0.0f), def.y);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, uidock_len_pt(st.width, 360.0f), def.width);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, uidock_len_pt(st.height, 240.0f), def.height);
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_removed_window_and_new_window_defaults) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_node_t profiler;
	sk_ui_layout_style_t st;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"mismatch-new\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.3,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"gone-side\", \"flags\": 0, \"tabs\": [\"gone\"], \"active_index\": 0 },\n"
					   "        \"b\": { \"kind\": \"leaf\", \"id\": \"left\", \"flags\": 0, \"tabs\": [\"hierarchy\", \"removed\"], \"active_index\": 0 }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock removed/new)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Hierarchy", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Inspector", "inspector");
	profiler = ui->widget_editor_window(ctx, ui->context_root(ctx), "Profiler", "profiler");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "inspector", "left", NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "profiler", NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "mismatch-new", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "mismatch-new");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("hierarchy", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("inspector", tabs[1]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "gone"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "removed"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "inspector"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "profiler"));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, profiler, &st));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_POSITION_ABSOLUTE, (int)st.position);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, uidock_len_pt(st.left, 0.0f), 80.0f);
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, uidock_len_pt(st.top, 0.0f), 60.0f);
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_empty_branch_renormalizes_leftover) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t a;
	sk_ui_dock_node_t b;
	sk_ui_rect_t space;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	const f32 leftover = 800.0f - SK_UI_DOCK_SPLITTER_PT;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"empty-branch\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": {\n"
					   "        \"kind\": \"split\",\n"
					   "        \"axis\": 0,\n"
					   "        \"ratio\": 0.25,\n"
					   "        \"id\": \"root\",\n"
					   "        \"flags\": 0,\n"
					   "        \"a\": { \"kind\": \"leaf\", \"id\": \"keep-a\", \"flags\": 0, \"tabs\": [\"keep-a\"], \"active_index\": 0 },\n"
					   "        \"b\": {\n"
					   "            \"kind\": \"split\",\n"
					   "            \"axis\": 0,\n"
					   "            \"ratio\": 0.4,\n"
					   "            \"id\": \"dead\",\n"
					   "            \"flags\": 0,\n"
					   "            \"a\": { \"kind\": \"leaf\", \"id\": \"gone\", \"flags\": 0, \"tabs\": [\"gone\"], \"active_index\": 0 },\n"
					   "            \"b\": { \"kind\": \"leaf\", \"id\": \"keep-b\", \"flags\": 0, \"tabs\": [\"keep-b\"], \"active_index\": 0 }\n"
					   "        }\n"
					   "    },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock empty branch)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "A", "keep-a");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "B", "keep-b");
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "empty-branch", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "empty-branch");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, root));
	TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, ui->dock_split_get_ratio(ctx, root));
	a = ui->dock_split_child(ctx, root, 0u);
	b = ui->dock_split_child(ctx, root, 1u);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, a));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, b));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, a, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_STRING("keep-a", tabs[0]);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, b, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_STRING("keep-b", tabs[0]);
	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 800.0f;
	space.height = 600.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, a, &ra));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, b, &rb));
	TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.25f, ra.width);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.75f, rb.width);
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_future_version_and_corrupt_fallback) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* newer = "{\n"
						"    \"version\": 99,\n"
						"    \"id\": \"leaf-space\",\n"
						"    \"flags\": 0,\n"
						"    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"other\"], \"active_index\": 0 },\n"
						"    \"floating\": []\n"
						"}";
	const char* corrupt = "{ not json at all [[[";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock version fallback)");
	}
	ui = env.ui;
	TEST_ASSERT_TRUE(sk_ui_dock_layout_version_supported(99) != 0);
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "leaf-space", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "console", root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", newer, (u32)strlen(newer)) != 0);
	TEST_ASSERT_TRUE(ui->dock_layout_load_json(ctx, "leaf-space", corrupt, (u32)strlen(corrupt)) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, ui->dockspace_find(ctx, "leaf-space"), ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_creates_missing_dockspace) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"startup\",\n"
					   "    \"flags\": 1,\n"
					   "    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 1, \"tabs\": [\"console\"], \"active_index\": 0 },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock startup restore)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Console", "console");
	TEST_ASSERT_FALSE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, "startup")));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "startup", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "startup");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("console", ids[0]);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "console"));
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

static void uidock_ptr(sk_ui_input_event_t* ev, sk_ui_input_kind_t kind, f32 x, f32 y, i32 down) {
	memset(ev, 0, sizeof(*ev));
	ev->kind = kind;
	ev->x = x;
	ev->y = y;
	ev->button = SK_UI_POINTER_BUTTON_LEFT;
	ev->down = down;
}

static void uidock_live_layout(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
	TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, w, h));
}

static void uidock_click_xy(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x, f32 y) {
	sk_ui_input_event_t ev;
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, x, y, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, x, y, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, x, y, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static void uidock_drag_xy(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 x0, f32 y0, f32 x1, f32 y1) {
	sk_ui_input_event_t ev;
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, x0, y0, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, x0, y0, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, x1, y1, 1);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	uidock_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, x1, y1, 0);
	TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
}

static sk_ui_node_t uidock_find_splitter(const sk_ui_api_t* ui, const sk_ui_context_t* ctx, sk_ui_dock_node_t split) {
	sk_ui_node_t host = ui->dock_node_host(ctx, split);
	u32 i;
	u32 n;
	if (!sk_ui_node_is_valid(host)) {
		return SK_UI_NODE_INVALID;
	}
	n = ui->node_child_count(ctx, host);
	for (i = 0u; i < n; ++i) {
		sk_ui_node_t child = ui->node_child_at(ctx, host, i);
		if (ui->node_has_class(ctx, child, SK_UI_CLASS_SPLITTER)) {
			return child;
		}
	}
	return SK_UI_NODE_INVALID;
}

static void uidock_assert_resolved_equal(const sk_ui_api_t* ui, const sk_ui_context_t* a, sk_ui_dock_node_t na, const sk_ui_context_t* b, sk_ui_dock_node_t nb) {
	typedef struct uidock_res_frame_t {
		sk_ui_dock_node_t a;
		sk_ui_dock_node_t b;
	} uidock_res_frame_t;
	uidock_res_frame_t stack[128];
	u32 sp = 0u;
	stack[sp].a = na;
	stack[sp].b = nb;
	sp += 1u;
	while (sp > 0u) {
		uidock_res_frame_t fr = stack[--sp];
		sk_ui_rect_t ra;
		sk_ui_rect_t rb;
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(a, fr.a, &ra));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(b, fr.b, &rb));
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.x, rb.x);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.y, rb.y);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.width, rb.width);
		TEST_ASSERT_FLOAT_WITHIN(0.01f, ra.height, rb.height);
		if (ui->dock_node_is_split(a, fr.a) != 0) {
			sk_ui_rect_t sa;
			sk_ui_rect_t sb;
			TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(a, fr.a, &sa));
			TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(b, fr.b, &sb));
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.x, sb.x);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.y, sb.y);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.width, sb.width);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, sa.height, sb.height);
			if (sp + 2u > 128u) {
				TEST_FAIL_MESSAGE("dock resolved compare stack overflow");
				return;
			}
			stack[sp].a = ui->dock_split_child(a, fr.a, 1u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 1u);
			sp += 1u;
			stack[sp].a = ui->dock_split_child(a, fr.a, 0u);
			stack[sp].b = ui->dock_split_child(b, fr.b, 0u);
			sp += 1u;
		}
	}
}

SK_TEST(ui_dock_e2e_dock_empty_space) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_rect_t space;
	sk_ui_rect_t box;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 99u;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e empty)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "empty", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(root));
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "scene", root, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "scene"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "scene"), root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_UINT(0u, active);
	TEST_ASSERT_EQUAL_STRING("scene", tabs[0]);

	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 800.0f;
	space.height = 600.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, root, &box));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space.x, box.x);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space.y, box.y);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space.width, box.width);
	TEST_ASSERT_FLOAT_WITHIN(0.01f, space.height, box.height);

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_split_four_dirs) {
	static const sk_ui_dock_dir_t dirs[4] = {
		SK_UI_DOCK_DIR_LEFT,
		SK_UI_DOCK_DIR_RIGHT,
		SK_UI_DOCK_DIR_UP,
		SK_UI_DOCK_DIR_DOWN,
	};
	static const char* names[4] = {"left", "right", "up", "down"};
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	u32 d;
	const f32 leftover_w = 800.0f - SK_UI_DOCK_SPLITTER_PT;
	const f32 leftover_h = 600.0f - SK_UI_DOCK_SPLITTER_PT;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e dirs)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	for (d = 0u; d < 4u; ++d) {
		char space_id[32];
		char center_id[32];
		char edge_id[32];
		sk_ui_dock_node_t root;
		sk_ui_dock_node_t split;
		sk_ui_dock_node_t edge;
		sk_ui_dock_node_t opposite;
		sk_ui_rect_t space;
		sk_ui_rect_t re;
		sk_ui_rect_t ro;
		sk_ui_rect_t rs;
		const i32 horizontal = (dirs[d] == SK_UI_DOCK_DIR_LEFT || dirs[d] == SK_UI_DOCK_DIR_RIGHT) ? 1 : 0;
		const u32 edge_index = (dirs[d] == SK_UI_DOCK_DIR_LEFT || dirs[d] == SK_UI_DOCK_DIR_UP) ? 0u : 1u;
		const f32 model_ratio = (edge_index == 0u) ? 0.25f : 0.75f;
		const f32 leftover = (horizontal != 0) ? leftover_w : leftover_h;

		(void)snprintf(space_id, sizeof(space_id), "dir-%s", names[d]);
		(void)snprintf(center_id, sizeof(center_id), "center-%s", names[d]);
		(void)snprintf(edge_id, sizeof(edge_id), "edge-%s", names[d]);
		(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Center", center_id);
		(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "Edge", edge_id);
		root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, space_id, SK_UI_DOCKSPACE_KEEP_CENTRAL);
		TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, center_id, root, SK_UI_DOCK_DIR_CENTER));
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 600.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, root, &space));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, edge_id, root, dirs[d]));

		split = ui->dockspace_find(ctx, space_id);
		TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
		TEST_ASSERT_EQUAL_INT(horizontal != 0 ? (int)SK_UI_DOCK_SPLIT_HORIZONTAL : (int)SK_UI_DOCK_SPLIT_VERTICAL, (int)ui->dock_split_get_axis(ctx, split));
		TEST_ASSERT_FLOAT_WITHIN(0.0001f, model_ratio, ui->dock_split_get_ratio(ctx, split));
		edge = ui->dock_find_node_for_window(ctx, edge_id);
		opposite = ui->dock_find_node_for_window(ctx, center_id);
		TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_split_child(ctx, split, edge_index), edge));
		TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_split_child(ctx, split, 1u - edge_index), opposite));

		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, edge, &re));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, opposite, &ro));
		TEST_ASSERT_EQUAL_INT(0, ui->dock_split_get_splitter_rect(ctx, split, &rs));
		if (horizontal != 0) {
			TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.25f, re.width);
			TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.75f, ro.width);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, SK_UI_DOCK_SPLITTER_PT, rs.width);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, 600.0f, re.height);
			if (dirs[d] == SK_UI_DOCK_DIR_LEFT) {
				TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, re.x);
				TEST_ASSERT_FLOAT_WITHIN(0.01f, re.x + re.width + rs.width, ro.x);
			} else {
				TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ro.x);
				TEST_ASSERT_FLOAT_WITHIN(0.01f, ro.x + ro.width + rs.width, re.x);
			}
		} else {
			TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.25f, re.height);
			TEST_ASSERT_FLOAT_WITHIN(2.0f, leftover * 0.75f, ro.height);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, SK_UI_DOCK_SPLITTER_PT, rs.height);
			TEST_ASSERT_FLOAT_WITHIN(0.01f, 800.0f, re.width);
			if (dirs[d] == SK_UI_DOCK_DIR_UP) {
				TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, re.y);
				TEST_ASSERT_FLOAT_WITHIN(0.01f, re.y + re.height + rs.height, ro.y);
			} else {
				TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ro.y);
				TEST_ASSERT_FLOAT_WITHIN(0.01f, ro.y + ro.height + rs.height, re.y);
			}
		}
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_destroy(ctx, space_id));
	}

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_tab_switch_via_input) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_node_t scene;
	sk_ui_node_t game;
	sk_ui_node_t tab_scene;
	sk_ui_node_t tab_game;
	sk_ui_rect_t tr;
	sk_ui_node_t stash;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 99u;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e tab switch)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	scene = ui->widget_editor_window(ctx, ui->context_root(ctx), "Scene", "scene");
	game = ui->widget_editor_window(ctx, ui->context_root(ctx), "Game", "game");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "tabs-switch", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "scene", root, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "game", root, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("scene", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("game", tabs[1]);
	TEST_ASSERT_EQUAL_UINT(1u, active);
	uidock_live_layout(ui, ctx, 800.0f, 500.0f);

	tab_scene = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "scene-dock-tab");
	tab_game = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "game-dock-tab");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab_scene));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab_game));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, tab_scene));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, tab_game));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab_scene, &tr, NULL));
	uidock_click_xy(ui, ctx, tr.x + tr.width * 0.5f, tr.y + tr.height * 0.5f);
	uidock_live_layout(ui, ctx, 800.0f, 500.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(0u, active);
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, tab_scene));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, tab_game));
	stash = ui->find_by_id(ctx, "ui-dock-stash");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(stash));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, game), stash));
	TEST_ASSERT_FALSE(sk_ui_node_eq(ui->node_parent(ctx, scene), stash));

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_tab_reorder_via_input) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_node_t tab_a;
	sk_ui_node_t tab_b;
	sk_ui_rect_t a;
	sk_ui_rect_t b;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e reorder)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "A", "win-a");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "B", "win-b");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "tabs-reorder", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "win-a", root, SK_UI_DOCK_DIR_CENTER));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_to_node(ctx, "win-b", root, SK_UI_DOCK_DIR_CENTER));
	uidock_live_layout(ui, ctx, 640.0f, 400.0f);

	tab_a = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "win-a-dock-tab");
	tab_b = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "win-b-dock-tab");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab_a));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab_b));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab_a, &a, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab_b, &b, NULL));
	uidock_drag_xy(ui, ctx, a.x + 4.0f, a.y + 8.0f, b.x + b.width * 0.5f, b.y + 8.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING("win-b", tabs[0]);
	TEST_ASSERT_EQUAL_STRING("win-a", tabs[1]);

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_splitter_drag_min_size) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t split;
	sk_ui_node_t splitter;
	sk_ui_rect_t sr;
	sk_ui_rect_t before_l;
	sk_ui_rect_t before_r;
	sk_ui_rect_t after_l;
	sk_ui_rect_t after_r;
	sk_ui_rect_t min_l;
	sk_ui_rect_t min_r;
	f32 before_ratio;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e splitter)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "H", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "S", "scene");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "split-e2e", 0u);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	uidock_live_layout(ui, ctx, 800.0f, 500.0f);

	split = ui->dockspace_find(ctx, "split-e2e");
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, split));
	splitter = uidock_find_splitter(ui, ctx, split);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(splitter));
	before_ratio = ui->dock_split_get_ratio(ctx, split);
	TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.25f, before_ratio);
	{
		sk_ui_rect_t space;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 500.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, left, &before_l));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, rest, &before_r));
	uidock_live_layout(ui, ctx, 800.0f, 500.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, splitter, &sr, NULL));

	uidock_drag_xy(ui, ctx, sr.x + sr.width * 0.5f, sr.y + sr.height * 0.5f, sr.x + 120.0f, sr.y + sr.height * 0.5f);
	TEST_ASSERT_TRUE(ui->dock_split_get_ratio(ctx, split) > before_ratio + 0.02f);
	{
		sk_ui_rect_t space;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 500.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, left, &after_l));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, rest, &after_r));
	TEST_ASSERT_TRUE(after_l.width > before_l.width + 8.0f);
	TEST_ASSERT_TRUE(after_r.width + 8.0f < before_r.width);
	TEST_ASSERT_TRUE(after_l.width + 0.01f >= SK_UI_DOCK_NODE_MIN_PT);
	TEST_ASSERT_TRUE(after_r.width + 0.01f >= SK_UI_DOCK_NODE_MIN_PT);

	uidock_live_layout(ui, ctx, 800.0f, 500.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, splitter, &sr, NULL));
	uidock_drag_xy(ui, ctx, sr.x + sr.width * 0.5f, sr.y + sr.height * 0.5f, 2.0f, sr.y + sr.height * 0.5f);
	{
		sk_ui_rect_t space;
		space.x = 0.0f;
		space.y = 0.0f;
		space.width = 800.0f;
		space.height = 500.0f;
		TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, split, &space));
	}
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, left, &min_l));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, rest, &min_r));
	TEST_ASSERT_TRUE(min_l.width + 0.01f >= SK_UI_DOCK_NODE_MIN_PT);
	TEST_ASSERT_TRUE(min_r.width + 0.01f >= SK_UI_DOCK_NODE_MIN_PT);
	TEST_ASSERT_FLOAT_WITHIN(2.0f, 800.0f - SK_UI_DOCK_SPLITTER_PT, min_l.width + min_r.width);

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_undock_tab_via_input) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_node_t hier;
	sk_ui_node_t tab;
	sk_ui_node_t overlay;
	sk_ui_rect_t tr;
	sk_ui_layout_style_t st;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e undock)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	hier = ui->widget_editor_window(ctx, ui->context_root(ctx), "H", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "S", "scene");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "undock-e2e", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.35f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	uidock_live_layout(ui, ctx, 800.0f, 500.0f);

	tab = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "hierarchy-dock-tab");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tab));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tab, &tr, NULL));
	/* Tear off, then release outside the dockspace so commit_drop does not redock. */
	{
		sk_ui_input_event_t ev;
		uidock_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tr.x + 8.0f, tr.y + 8.0f, 0);
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		uidock_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, tr.x + 8.0f, tr.y + 8.0f, 1);
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		uidock_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, tr.x + 8.0f, tr.y + 80.0f, 1);
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		uidock_live_layout(ui, ctx, 800.0f, 500.0f);
		uidock_ptr(&ev, SK_UI_INPUT_POINTER_MOVE, 900.0f, 600.0f, 1);
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
		uidock_ptr(&ev, SK_UI_INPUT_POINTER_BUTTON, 900.0f, 600.0f, 0);
		TEST_ASSERT_EQUAL_INT(0, ui->input_dispatch(ctx, &ev));
	}
	uidock_live_layout(ui, ctx, 800.0f, 500.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "hierarchy"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "scene"));
	overlay = ui->find_by_id(ctx, "ui-dock-overlay");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(overlay));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(ctx, hier), overlay));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, hier, &st));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_POSITION_ABSOLUTE, (int)st.position);
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, ui->dockspace_find(ctx, "undock-e2e")));

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_remove_window_collapses) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t after;
	sk_ui_node_t side;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e collapse)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);

	side = ui->widget_editor_window(ctx, ui->context_root(ctx), "H", "hierarchy");
	(void)ui->widget_editor_window(ctx, ui->context_root(ctx), "S", "scene");
	root = ui->dockspace_begin(ctx, SK_UI_NODE_INVALID, "collapse-e2e", SK_UI_DOCKSPACE_KEEP_CENTRAL);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_begin(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, 0.25f, &left, &rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "hierarchy", left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_dock_window(ctx, "scene", rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_builder_finish(ctx));
	TEST_ASSERT_TRUE(ui->dock_node_is_split(ctx, ui->dockspace_find(ctx, "collapse-e2e")));

	TEST_ASSERT_EQUAL_INT(0, ui->node_destroy(ctx, side));
	after = ui->dockspace_find(ctx, "collapse-e2e");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, after));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(after, rest));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(ctx, "hierarchy"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "scene"));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, after, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("scene", tabs[0]);

	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_e2e_roundtrip_resolved_layout) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* src;
	sk_ui_context_t* dst;
	char json[8192];
	u32 len = 0u;
	sk_ui_rect_t space;

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock e2e roundtrip)");
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

	space.x = 0.0f;
	space.y = 0.0f;
	space.width = 1280.0f;
	space.height = 720.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(src, ui->dockspace_find(src, "editor-main"), &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(dst, ui->dockspace_find(dst, "editor-main"), &space));
	uidock_assert_resolved_equal(ui, src, ui->dockspace_find(src, "editor-main"), dst, ui->dockspace_find(dst, "editor-main"));
	uidock_assert_workspace(ui, src);
	uidock_assert_workspace(ui, dst);

	ui->context_destroy(src);
	ui->context_destroy(dst);
	uidock_shutdown(&env);
}

SK_TEST(ui_dock_restore_load_then_create) {
	uidock_env_t env;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_ui_dock_node_t root;
	sk_ui_node_t later;
	const_chr_t ids[4];
	u32 count = 0u;
	u32 active = 99u;
	const char* json = "{\n"
					   "    \"version\": 1,\n"
					   "    \"id\": \"late\",\n"
					   "    \"flags\": 0,\n"
					   "    \"root\": { \"kind\": \"leaf\", \"id\": \"root\", \"flags\": 0, \"tabs\": [\"later\"], \"active_index\": 0 },\n"
					   "    \"floating\": []\n"
					   "}";

	if (uidock_boot(&env) != 0) {
		TEST_IGNORE_MESSAGE("sk-ui not available via app registry (skip dock load-then-create)");
	}
	ui = env.ui;
	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_register(ctx, "later", NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_layout_load_json(ctx, "late", json, (u32)strlen(json)));
	root = ui->dockspace_find(ctx, "late");
	TEST_ASSERT_TRUE(ui->dock_node_is_leaf(ctx, root));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, root, ids, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(1u, count);
	TEST_ASSERT_EQUAL_STRING("later", ids[0]);
	later = ui->widget_editor_window(ctx, ui->context_root(ctx), "Later", "later");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(later));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(ctx, "later"));
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(ui->dock_find_node_for_window(ctx, "later"), root));
	ui->context_destroy(ctx);
	uidock_shutdown(&env);
}

#endif /* SK_TESTS */
