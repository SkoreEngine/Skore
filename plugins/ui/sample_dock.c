/**
 * @file sample_dock.c
 * @brief Deterministic docking demo scene (APX-292).
 *
 * Builds a fixed, reproducible workspace at startup:
 *   - left leaf (Hierarchy)
 *   - right column split vertically (Inspector over Console)
 *   - central leaf with two tabs (Scene, Game)
 *
 * Layout comes only from dock_builder_* with hardcoded ratios. No persist
 * load, no clock/RNG, no reused .ini. Two consecutive builds on a fresh
 * context produce identical node rects at the documented logical size.
 */

#include "ui.internal.h"

#include <stdio.h>
#include <string.h>

#define SK_UI_SAMPLE_DOCK_W 1280.0f
#define SK_UI_SAMPLE_DOCK_H 720.0f

#define SK_UI_SAMPLE_DOCKSPACE_ID "dock-demo"
#define SK_UI_SAMPLE_DOCK_WORKSPACE_ID "dock-demo-workspace"

#define SK_UI_SAMPLE_DOCK_WIN_HIERARCHY "dock-demo-hierarchy"
#define SK_UI_SAMPLE_DOCK_WIN_SCENE "dock-demo-scene"
#define SK_UI_SAMPLE_DOCK_WIN_GAME "dock-demo-game"
#define SK_UI_SAMPLE_DOCK_WIN_INSPECTOR "dock-demo-inspector"
#define SK_UI_SAMPLE_DOCK_WIN_CONSOLE "dock-demo-console"

#define SK_UI_SAMPLE_DOCK_NODE_LEFT "dock-demo-left"
#define SK_UI_SAMPLE_DOCK_NODE_CENTER "dock-demo-center"
#define SK_UI_SAMPLE_DOCK_NODE_RIGHT "dock-demo-right"
#define SK_UI_SAMPLE_DOCK_NODE_RIGHT_TOP "dock-demo-right-top"
#define SK_UI_SAMPLE_DOCK_NODE_RIGHT_BOTTOM "dock-demo-right-bottom"

#define SK_UI_SAMPLE_DOCK_LEFT_RATIO 0.22f
#define SK_UI_SAMPLE_DOCK_RIGHT_RATIO 0.24f
#define SK_UI_SAMPLE_DOCK_RIGHT_SPLIT_RATIO 0.50f

#define SK_UI_SAMPLE_DOCK_CLS_WORKSPACE "dock-demo-workspace"
#define SK_UI_SAMPLE_DOCK_CLS_FILL "dock-demo-fill"
#define SK_UI_SAMPLE_DOCK_CLS_TITLE "dock-demo-title"
#define SK_UI_SAMPLE_DOCK_CLS_HINT "dock-demo-hint"
#define SK_UI_SAMPLE_DOCK_CLS_HIERARCHY "dock-demo-fill-hierarchy"
#define SK_UI_SAMPLE_DOCK_CLS_SCENE "dock-demo-fill-scene"
#define SK_UI_SAMPLE_DOCK_CLS_GAME "dock-demo-fill-game"
#define SK_UI_SAMPLE_DOCK_CLS_INSPECTOR "dock-demo-fill-inspector"
#define SK_UI_SAMPLE_DOCK_CLS_CONSOLE "dock-demo-fill-console"

static const sk_ui_api_t* sample_dock_api(void) {
	return ui_get_api_table();
}

void ui_sample_dock_demo_logical_size_impl(f32* out_width, f32* out_height) {
	if (out_width != NULL) {
		*out_width = SK_UI_SAMPLE_DOCK_W;
	}
	if (out_height != NULL) {
		*out_height = SK_UI_SAMPLE_DOCK_H;
	}
}

i32 ui_sample_dock_demo_register_styles_impl(sk_ui_context_t* ctx) {
	const sk_ui_api_t* ui = sample_dock_api();
	sk_ui_style_props_t p;

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.background_color = sk_ui_rgba(0.06f, 0.07f, 0.09f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_WORKSPACE, &p) != 0) {
		return -1;
	}

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.padding.left = 12.0f;
	p.layout.padding.top = 12.0f;
	p.layout.padding.right = 12.0f;
	p.layout.padding.bottom = 12.0f;
	p.layout.row_gap = 8.0f;
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_FILL, &p) != 0) {
		return -1;
	}

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_BACKGROUND_COLOR;
	p.background_color = sk_ui_rgba(0.16f, 0.28f, 0.42f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_HIERARCHY, &p) != 0) {
		return -1;
	}
	p.background_color = sk_ui_rgba(0.12f, 0.32f, 0.30f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_SCENE, &p) != 0) {
		return -1;
	}
	p.background_color = sk_ui_rgba(0.36f, 0.26f, 0.12f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_GAME, &p) != 0) {
		return -1;
	}
	p.background_color = sk_ui_rgba(0.30f, 0.16f, 0.38f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_INSPECTOR, &p) != 0) {
		return -1;
	}
	p.background_color = sk_ui_rgba(0.14f, 0.28f, 0.16f, 1.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_CONSOLE, &p) != 0) {
		return -1;
	}

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH;
	p.font_size = 18.0f;
	p.color = sk_ui_rgba(0.96f, 0.97f, 0.99f, 1.0f);
	p.layout.height = sk_ui_pt(24.0f);
	p.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_TITLE, &p) != 0) {
		return -1;
	}

	ui_style_props_clear(&p);
	p.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR | SK_UI_SP_HEIGHT | SK_UI_SP_WIDTH;
	p.font_size = 13.0f;
	p.color = sk_ui_rgba(0.82f, 0.86f, 0.90f, 1.0f);
	p.layout.height = sk_ui_pt(18.0f);
	p.layout.width = sk_ui_percent(100.0f);
	if (ui->style_class_register(ctx, SK_UI_SAMPLE_DOCK_CLS_HINT, &p) != 0) {
		return -1;
	}

	return 0;
}

static void sample_dock_destroy_if_alive(const sk_ui_api_t* ui, sk_ui_context_t* ctx, const_chr_t id) {
	sk_ui_node_t node = ui->find_by_id(ctx, id);
	if (sk_ui_node_is_valid(node) && ui->node_alive(ctx, node) != 0) {
		(void)ui->node_destroy(ctx, node);
	}
}

static void sample_dock_reset(const sk_ui_api_t* ui, sk_ui_context_t* ctx) {
	static const char* window_ids[] = {
		SK_UI_SAMPLE_DOCK_WIN_HIERARCHY, SK_UI_SAMPLE_DOCK_WIN_SCENE, SK_UI_SAMPLE_DOCK_WIN_GAME, SK_UI_SAMPLE_DOCK_WIN_INSPECTOR, SK_UI_SAMPLE_DOCK_WIN_CONSOLE,
	};
	u32 i;

	if (sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, SK_UI_SAMPLE_DOCKSPACE_ID))) {
		(void)ui->dockspace_destroy(ctx, SK_UI_SAMPLE_DOCKSPACE_ID);
	}
	for (i = 0u; i < sizeof(window_ids) / sizeof(window_ids[0]); ++i) {
		sample_dock_destroy_if_alive(ui, ctx, window_ids[i]);
	}
	sample_dock_destroy_if_alive(ui, ctx, SK_UI_SAMPLE_DOCK_WORKSPACE_ID);
}

static sk_ui_node_t sample_dock_make_window(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent, const_chr_t title, const_chr_t window_id, const_chr_t fill_class,
											const_chr_t heading, const_chr_t hint) {
	sk_ui_node_t win;
	sk_ui_node_t body;
	sk_ui_node_t fill;
	sk_ui_node_t title_lbl;
	sk_ui_node_t hint_lbl;
	char fill_id[80];
	char title_id[80];
	char hint_id[80];

	win = ui->widget_editor_window(ctx, parent, title, window_id);
	if (!sk_ui_node_is_valid(win)) {
		return SK_UI_NODE_INVALID;
	}
	body = ui->editor_window_content(ctx, win);
	if (!sk_ui_node_is_valid(body)) {
		return SK_UI_NODE_INVALID;
	}

	(void)snprintf(fill_id, sizeof(fill_id), "%s-fill", window_id);
	(void)snprintf(title_id, sizeof(title_id), "%s-label", window_id);
	(void)snprintf(hint_id, sizeof(hint_id), "%s-hint", window_id);

	fill = ui->widget_view(ctx, body, fill_id);
	if (!sk_ui_node_is_valid(fill)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_add_class(ctx, fill, SK_UI_SAMPLE_DOCK_CLS_FILL);
	(void)ui->node_add_class(ctx, fill, fill_class);

	title_lbl = ui->widget_label(ctx, fill, heading, title_id);
	(void)ui->node_add_class(ctx, title_lbl, SK_UI_SAMPLE_DOCK_CLS_TITLE);
	hint_lbl = ui->widget_label(ctx, fill, hint, hint_id);
	(void)ui->node_add_class(ctx, hint_lbl, SK_UI_SAMPLE_DOCK_CLS_HINT);
	return win;
}

sk_ui_node_t ui_sample_dock_demo_build_impl(sk_ui_context_t* ctx, sk_ui_node_t parent) {
	const sk_ui_api_t* ui = sample_dock_api();
	sk_ui_node_t workspace;
	sk_ui_dock_node_t root;
	sk_ui_dock_node_t left;
	sk_ui_dock_node_t rest;
	sk_ui_dock_node_t right;
	sk_ui_dock_node_t center;
	sk_ui_dock_node_t right_top;
	sk_ui_dock_node_t right_bottom;
	sk_ui_style_props_t root_style;

	if (ui_sample_dock_demo_register_styles_impl(ctx) != 0) {
		return SK_UI_NODE_INVALID;
	}

	if (!sk_ui_node_is_valid(parent)) {
		parent = ui->context_root(ctx);
	}

	/* Always start from a clean model so a second call cannot reuse leftover
	 * dock state (no persist file is ever loaded). */
	sample_dock_reset(ui, ctx);

	ui_style_props_clear(&root_style);
	root_style.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_BACKGROUND_COLOR;
	root_style.layout.width = sk_ui_percent(100.0f);
	root_style.layout.height = sk_ui_percent(100.0f);
	root_style.layout.flex_direction = SK_UI_FLEX_COLUMN;
	root_style.background_color = sk_ui_rgba(0.06f, 0.07f, 0.09f, 1.0f);
	(void)ui->node_merge_inline_style(ctx, parent, &root_style);

	workspace = ui->widget_view(ctx, parent, SK_UI_SAMPLE_DOCK_WORKSPACE_ID);
	if (!sk_ui_node_is_valid(workspace)) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->node_add_class(ctx, workspace, SK_UI_SAMPLE_DOCK_CLS_WORKSPACE);

	if (!sk_ui_node_is_valid(sample_dock_make_window(ui, ctx, workspace, "Hierarchy", SK_UI_SAMPLE_DOCK_WIN_HIERARCHY, SK_UI_SAMPLE_DOCK_CLS_HIERARCHY, "LEFT — Hierarchy",
													 "Left dock leaf"))) {
		return SK_UI_NODE_INVALID;
	}
	if (!sk_ui_node_is_valid(sample_dock_make_window(ui, ctx, workspace, "Scene", SK_UI_SAMPLE_DOCK_WIN_SCENE, SK_UI_SAMPLE_DOCK_CLS_SCENE, "CENTER — Scene",
													 "Central tab 1 of 2"))) {
		return SK_UI_NODE_INVALID;
	}
	if (!sk_ui_node_is_valid(sample_dock_make_window(ui, ctx, workspace, "Game", SK_UI_SAMPLE_DOCK_WIN_GAME, SK_UI_SAMPLE_DOCK_CLS_GAME, "CENTER — Game", "Central tab 2 of 2"))) {
		return SK_UI_NODE_INVALID;
	}
	if (!sk_ui_node_is_valid(sample_dock_make_window(ui, ctx, workspace, "Inspector", SK_UI_SAMPLE_DOCK_WIN_INSPECTOR, SK_UI_SAMPLE_DOCK_CLS_INSPECTOR, "RIGHT TOP — Inspector",
													 "Right column, upper leaf"))) {
		return SK_UI_NODE_INVALID;
	}
	if (!sk_ui_node_is_valid(sample_dock_make_window(ui, ctx, workspace, "Console", SK_UI_SAMPLE_DOCK_WIN_CONSOLE, SK_UI_SAMPLE_DOCK_CLS_CONSOLE, "RIGHT BOTTOM — Console",
													 "Right column, lower leaf"))) {
		return SK_UI_NODE_INVALID;
	}

	root = ui->dockspace_begin(ctx, workspace, SK_UI_SAMPLE_DOCKSPACE_ID, SK_UI_DOCKSPACE_KEEP_CENTRAL | SK_UI_DOCKSPACE_AUTO_APPLY);
	if (!sk_ui_dock_node_is_valid(root)) {
		return SK_UI_NODE_INVALID;
	}
	if (ui->dock_builder_begin(ctx, root) != 0) {
		return SK_UI_NODE_INVALID;
	}
	if (ui->dock_builder_split_node(ctx, root, SK_UI_DOCK_DIR_LEFT, SK_UI_SAMPLE_DOCK_LEFT_RATIO, &left, &rest) != 0) {
		(void)ui->dock_builder_finish(ctx);
		return SK_UI_NODE_INVALID;
	}
	(void)ui->dock_builder_set_node_id(ctx, left, SK_UI_SAMPLE_DOCK_NODE_LEFT);
	if (ui->dock_builder_split_node(ctx, rest, SK_UI_DOCK_DIR_RIGHT, SK_UI_SAMPLE_DOCK_RIGHT_RATIO, &right, &center) != 0) {
		(void)ui->dock_builder_finish(ctx);
		return SK_UI_NODE_INVALID;
	}
	(void)ui->dock_builder_set_node_id(ctx, center, SK_UI_SAMPLE_DOCK_NODE_CENTER);
	(void)ui->dock_builder_set_node_id(ctx, right, SK_UI_SAMPLE_DOCK_NODE_RIGHT);
	if (ui->dock_builder_split_node(ctx, right, SK_UI_DOCK_DIR_DOWN, SK_UI_SAMPLE_DOCK_RIGHT_SPLIT_RATIO, &right_bottom, &right_top) != 0) {
		(void)ui->dock_builder_finish(ctx);
		return SK_UI_NODE_INVALID;
	}
	(void)ui->dock_builder_set_node_id(ctx, right_top, SK_UI_SAMPLE_DOCK_NODE_RIGHT_TOP);
	(void)ui->dock_builder_set_node_id(ctx, right_bottom, SK_UI_SAMPLE_DOCK_NODE_RIGHT_BOTTOM);
	if (ui->dock_builder_dock_window(ctx, SK_UI_SAMPLE_DOCK_WIN_HIERARCHY, left) != 0 || ui->dock_builder_dock_window(ctx, SK_UI_SAMPLE_DOCK_WIN_SCENE, center) != 0 ||
		ui->dock_builder_dock_window(ctx, SK_UI_SAMPLE_DOCK_WIN_GAME, center) != 0 || ui->dock_builder_dock_window(ctx, SK_UI_SAMPLE_DOCK_WIN_INSPECTOR, right_top) != 0 ||
		ui->dock_builder_dock_window(ctx, SK_UI_SAMPLE_DOCK_WIN_CONSOLE, right_bottom) != 0) {
		(void)ui->dock_builder_finish(ctx);
		return SK_UI_NODE_INVALID;
	}
	if (ui->dock_builder_finish(ctx) != 0) {
		return SK_UI_NODE_INVALID;
	}
	(void)ui->dock_tab_set_active(ctx, SK_UI_SAMPLE_DOCK_WIN_SCENE);
	(void)ui->dockspace_end(ctx);
	return workspace;
}

#ifdef SK_TESTS
#include "test.h"

#ifdef noreturn
#undef noreturn
#endif

typedef struct sample_dock_geom_t {
	sk_ui_rect_t left;
	sk_ui_rect_t center;
	sk_ui_rect_t right_top;
	sk_ui_rect_t right_bottom;
	u32 center_tabs;
	u32 center_active;
	const_chr_t tab0;
	const_chr_t tab1;
} sample_dock_geom_t;

static void sample_dock_capture_geom(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sample_dock_geom_t* out) {
	sk_ui_dock_node_t left = ui->dock_find_node_for_window(ctx, SK_UI_SAMPLE_DOCK_WIN_HIERARCHY);
	sk_ui_dock_node_t center = ui->dock_find_node_for_window(ctx, SK_UI_SAMPLE_DOCK_WIN_SCENE);
	sk_ui_dock_node_t right_top = ui->dock_find_node_for_window(ctx, SK_UI_SAMPLE_DOCK_WIN_INSPECTOR);
	sk_ui_dock_node_t right_bottom = ui->dock_find_node_for_window(ctx, SK_UI_SAMPLE_DOCK_WIN_CONSOLE);
	sk_ui_rect_t space;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;

	memset(out, 0, sizeof(*out));
	space.x = 0.0f;
	space.y = 0.0f;
	space.width = SK_UI_SAMPLE_DOCK_W;
	space.height = SK_UI_SAMPLE_DOCK_H;
	TEST_ASSERT_EQUAL_INT(0, ui->dockspace_layout(ctx, ui->dockspace_find(ctx, SK_UI_SAMPLE_DOCKSPACE_ID), &space));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, left, &out->left));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, center, &out->center));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, right_top, &out->right_top));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_node_get_rect(ctx, right_bottom, &out->right_bottom));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, center, tabs, 4u, &count, &active));
	out->center_tabs = count;
	out->center_active = active;
	out->tab0 = (count > 0u) ? tabs[0] : NULL;
	out->tab1 = (count > 1u) ? tabs[1] : NULL;
}

static void sample_dock_assert_same_rect(const sk_ui_rect_t* a, const sk_ui_rect_t* b) {
	TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(sk_ui_rect_t));
}

SK_TEST(ui_sample_dock_demo_tree_and_labels) {
	const sk_ui_api_t* ui = sample_dock_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sk_ui_node_t workspace;
	sk_ui_dock_node_t center;
	const_chr_t tabs[4];
	u32 count = 0u;
	u32 active = 0u;
	TEST_ASSERT_NOT_NULL(ctx);

	workspace = ui->sample_dock_demo_build(ctx, SK_UI_NODE_INVALID);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(workspace));
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(ui->dockspace_find(ctx, SK_UI_SAMPLE_DOCKSPACE_ID)));
	TEST_ASSERT_TRUE(ui->dock_window_is_docked(ctx, SK_UI_SAMPLE_DOCK_WIN_HIERARCHY));
	TEST_ASSERT_TRUE(ui->dock_window_is_docked(ctx, SK_UI_SAMPLE_DOCK_WIN_SCENE));
	TEST_ASSERT_TRUE(ui->dock_window_is_docked(ctx, SK_UI_SAMPLE_DOCK_WIN_GAME));
	TEST_ASSERT_TRUE(ui->dock_window_is_docked(ctx, SK_UI_SAMPLE_DOCK_WIN_INSPECTOR));
	TEST_ASSERT_TRUE(ui->dock_window_is_docked(ctx, SK_UI_SAMPLE_DOCK_WIN_CONSOLE));

	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "dock-demo-hierarchy-label")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "dock-demo-scene-label")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "dock-demo-game-label")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "dock-demo-inspector-label")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "dock-demo-console-label")));

	center = ui->dock_find_node_for_window(ctx, SK_UI_SAMPLE_DOCK_WIN_SCENE);
	TEST_ASSERT_EQUAL_INT(0, ui->dock_leaf_tabs(ctx, center, tabs, 4u, &count, &active));
	TEST_ASSERT_EQUAL_UINT(2u, count);
	TEST_ASSERT_EQUAL_STRING(SK_UI_SAMPLE_DOCK_WIN_SCENE, tabs[0]);
	TEST_ASSERT_EQUAL_STRING(SK_UI_SAMPLE_DOCK_WIN_GAME, tabs[1]);
	TEST_ASSERT_EQUAL_UINT(0u, active);
	TEST_ASSERT_TRUE(sk_ui_dock_node_eq(center, ui->dock_find_node_for_window(ctx, SK_UI_SAMPLE_DOCK_WIN_GAME)));

	ui->context_destroy(ctx);
}

SK_TEST(ui_sample_dock_demo_geometry_identical_across_runs) {
	const sk_ui_api_t* ui = sample_dock_api();
	sk_ui_context_t* a = ui->context_create(NULL);
	sk_ui_context_t* b = ui->context_create(NULL);
	sample_dock_geom_t ga;
	sample_dock_geom_t gb;
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);

	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_dock_demo_build(a, SK_UI_NODE_INVALID)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_dock_demo_build(b, SK_UI_NODE_INVALID)));
	sample_dock_capture_geom(ui, a, &ga);
	sample_dock_capture_geom(ui, b, &gb);

	sample_dock_assert_same_rect(&ga.left, &gb.left);
	sample_dock_assert_same_rect(&ga.center, &gb.center);
	sample_dock_assert_same_rect(&ga.right_top, &gb.right_top);
	sample_dock_assert_same_rect(&ga.right_bottom, &gb.right_bottom);
	TEST_ASSERT_EQUAL_UINT(ga.center_tabs, gb.center_tabs);
	TEST_ASSERT_EQUAL_UINT(ga.center_active, gb.center_active);
	TEST_ASSERT_EQUAL_STRING(ga.tab0, gb.tab0);
	TEST_ASSERT_EQUAL_STRING(ga.tab1, gb.tab1);

	TEST_ASSERT_TRUE(ga.left.width > 1.0f);
	TEST_ASSERT_TRUE(ga.center.width > ga.left.width);
	TEST_ASSERT_TRUE(ga.right_top.x > ga.center.x);
	TEST_ASSERT_TRUE(ga.right_bottom.y > ga.right_top.y);

	ui->context_destroy(a);
	ui->context_destroy(b);
}

SK_TEST(ui_sample_dock_demo_rebuild_resets_state) {
	const sk_ui_api_t* ui = sample_dock_api();
	sk_ui_context_t* ctx = ui->context_create(NULL);
	sample_dock_geom_t first;
	sample_dock_geom_t second;
	TEST_ASSERT_NOT_NULL(ctx);

	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_dock_demo_build(ctx, SK_UI_NODE_INVALID)));
	sample_dock_capture_geom(ui, ctx, &first);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->sample_dock_demo_build(ctx, SK_UI_NODE_INVALID)));
	sample_dock_capture_geom(ui, ctx, &second);
	sample_dock_assert_same_rect(&first.left, &second.left);
	sample_dock_assert_same_rect(&first.center, &second.center);
	sample_dock_assert_same_rect(&first.right_top, &second.right_top);
	sample_dock_assert_same_rect(&first.right_bottom, &second.right_bottom);

	ui->context_destroy(ctx);
}

#endif /* SK_TESTS */
