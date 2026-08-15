/**
 * @file separator_layout_family_tests.c
 * @brief Headless automation for the editor Separator / Spacing / SameLine
 *        family (APX-349, manifest §19).
 *
 * Author a ConsoleWindow-style toolbar row (SameLine + vertical Separator
 * between groups, a SameLine-packed checkbox group, and a trailing horizontal
 * Separator) plus a SettingsWindow-style collision-matrix grid (SameLine with
 * an absolute offset per cell), step frames through the shared test-engine
 * harness, and assert the resolved packing: chained gap arithmetic, vertical
 * dividers spanning the row height, the group row collapsing to one line, and
 * matrix cells at labelWidth + k*cell. No rendering.
 */

#include "ui_test.h"

#include "testdata/skore_test_font_ttf.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

/** Model of the ConsoleWindow.cpp:44-70 toolbar, returned via out nodes. */
static void slf_build_toolbar(sk_ui_test_t* t, sk_ui_node_t panel, sk_ui_node_t* out_clear, sk_ui_node_t* out_sep1, sk_ui_node_t* out_group, sk_ui_node_t* out_trace,
							  sk_ui_node_t* out_debug, sk_ui_node_t* out_info, sk_ui_node_t* out_warn, sk_ui_node_t* out_error, sk_ui_node_t* out_sep2, sk_ui_node_t* out_collapse,
							  sk_ui_node_t* out_autoscroll, sk_ui_node_t* out_sep_h) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_node_t group;

	*out_clear = ui->widget_button(t->ctx, panel, "Clear", "sl-clear");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, panel, 0.0f, -1.0f));
	*out_sep1 = ui->widget_separator(t->ctx, panel, "sl-sep1");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, panel, 0.0f, -1.0f));
	group = ui->widget_group(t->ctx, panel, "sl-group");
	*out_group = group;
	*out_trace = ui->widget_checkbox(t->ctx, group, 1, "sl-trace");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, group, 0.0f, -1.0f));
	*out_debug = ui->widget_checkbox(t->ctx, group, 0, "sl-debug");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, group, 0.0f, -1.0f));
	*out_info = ui->widget_checkbox(t->ctx, group, 0, "sl-info");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, group, 0.0f, -1.0f));
	*out_warn = ui->widget_checkbox(t->ctx, group, 0, "sl-warn");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, group, 0.0f, -1.0f));
	*out_error = ui->widget_checkbox(t->ctx, group, 0, "sl-error");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, panel, 0.0f, -1.0f));
	*out_sep2 = ui->widget_separator(t->ctx, panel, "sl-sep2");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, panel, 0.0f, -1.0f));
	*out_collapse = ui->widget_checkbox(t->ctx, panel, 0, "sl-collapse");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(t->ctx, panel, 0.0f, -1.0f));
	*out_autoscroll = ui->widget_checkbox(t->ctx, panel, 0, "sl-autoscroll");
	/* End of the toolbar row: a bare Separator is a full-width horizontal rule
	 * that starts a new row. */
	*out_sep_h = ui->widget_separator(t->ctx, panel, "sl-sep-h");
}

/**
 * Headless: ConsoleWindow toolbar packing + SettingsWindow collision matrix.
 * Assert gap arithmetic, vertical dividers spanning the row height, the group
 * row collapsing to one line, and absolute-offset matrix cells.
 */
SK_UI_TEST(separator_layout_family_toolbar_and_matrix_packing) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel;
	sk_ui_node_t matrix;
	sk_ui_node_t clear;
	sk_ui_node_t sep1;
	sk_ui_node_t group;
	sk_ui_node_t trace;
	sk_ui_node_t debug;
	sk_ui_node_t info;
	sk_ui_node_t warn;
	sk_ui_node_t error;
	sk_ui_node_t sep2;
	sk_ui_node_t collapse;
	sk_ui_node_t autoscroll;
	sk_ui_node_t sep_h;
	sk_ui_node_t mlabel;
	sk_ui_node_t m1;
	sk_ui_node_t m2;
	sk_ui_node_t m3;
	sk_ui_node_t spacer;
	sk_ui_node_t dummy;
	sk_ui_font_system_t* fs;
	sk_ui_font_t* font;
	sk_ui_harness_t* harness;
	sk_ui_style_props_t p;
	sk_ui_rect_t rc;
	sk_ui_rect_t rs1;
	sk_ui_rect_t rg;
	sk_ui_rect_t rt;
	sk_ui_rect_t rd;
	sk_ui_rect_t ri;
	sk_ui_rect_t rw;
	sk_ui_rect_t re;
	sk_ui_rect_t rs2;
	sk_ui_rect_t rcol;
	sk_ui_rect_t rau;
	sk_ui_rect_t rsh;
	sk_ui_rect_t rml;
	sk_ui_rect_t rm1;
	sk_ui_rect_t rm2;
	sk_ui_rect_t rm3;
	sk_ui_rect_t rsp;
	sk_ui_rect_t rdum;

	/* Real font so button / checkbox text widths drive the packing. */
	fs = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(fs);
	font = ui->font_load_memory(fs, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
	TEST_ASSERT_NOT_NULL(font);
	harness = ui->test_engine_harness(t->engine);
	TEST_ASSERT_NOT_NULL(harness);
	ui->harness_set_font(harness, fs, font);
	TEST_ASSERT_EQUAL_INT(0, ui->harness_set_size(harness, 640.0f, 320.0f));

	/* Start-aligned stage so the toolbar keeps its measured width. */
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_ALIGN_ITEMS;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));

	/* Toolbar row (ConsoleWindow.cpp:44-70): Clear | Sep | filter group |
	 * Sep | Collapse | Auto-scroll, then a full-width horizontal rule.
	 * Toolbars span the window in the editor, so the panel is full width. */
	panel = ui->widget_panel(ctx, root, "sl-toolbar");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(panel));
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH;
	p.layout.width = sk_ui_percent(100.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, panel, &p));
	slf_build_toolbar(t, panel, &clear, &sep1, &group, &trace, &debug, &info, &warn, &error, &sep2, &collapse, &autoscroll, &sep_h);

	/* SettingsWindow.cpp:326 collision matrix: label + SameLine(labelWidth)
	 * cells at absolute offsets (labelWidth + k*cell). */
	matrix = ui->widget_vertical(ctx, root, "sl-matrix");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(matrix));
	mlabel = ui->widget_label(ctx, matrix, "Default", "sl-mlabel");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, matrix, 80.0f, -1.0f));
	m1 = ui->widget_checkbox(ctx, matrix, 1, "sl-m1");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, matrix, 104.0f, -1.0f));
	m2 = ui->widget_checkbox(ctx, matrix, 0, "sl-m2");
	TEST_ASSERT_EQUAL_INT(0, ui->widget_same_line(ctx, matrix, 128.0f, -1.0f));
	m3 = ui->widget_checkbox(ctx, matrix, 0, "sl-m3");

	/* Spacing + Dummy vertical extents between the rows. */
	spacer = ui->widget_spacing(ctx, root, "sl-spacing");
	dummy = ui->widget_dummy(ctx, root, 0.0f, 2.0f, "sl-dummy");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "sl-clear");
	sk_ui_item_exists(t, "sl-sep1");
	sk_ui_item_exists(t, "sl-group");
	sk_ui_item_exists(t, "sl-trace");
	sk_ui_item_exists(t, "sl-debug");
	sk_ui_item_exists(t, "sl-error");
	sk_ui_item_exists(t, "sl-sep2");
	sk_ui_item_exists(t, "sl-collapse");
	sk_ui_item_exists(t, "sl-autoscroll");
	sk_ui_item_exists(t, "sl-sep-h");
	sk_ui_item_exists(t, "sl-m1");
	sk_ui_item_exists(t, "sl-m2");
	sk_ui_item_exists(t, "sl-m3");

	/* --- Toolbar row packing --- */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, clear, &rc, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep1, &rs1, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, group, &rg, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, trace, &rt, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, debug, &rd, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, info, &ri, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, warn, &rw, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, error, &re, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep2, &rs2, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, collapse, &rcol, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, autoscroll, &rau, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep_h, &rsh, NULL));

	/* Vertical dividers: 1px wide, spanning the row height, right after the
	 * previous item with the default 8px gap. */
	TEST_ASSERT_TRUE(rs1.width <= 1.5f);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.height, rs1.height);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.x + rc.width + SK_UI_SAMELINE_DEFAULT_GAP, rs1.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.y, rs1.y);

	/* The checkbox group is one item on the row, right after sep1. */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rs1.x + rs1.width + SK_UI_SAMELINE_DEFAULT_GAP, rg.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.y, rg.y);

	/* Inside the group: chained SameLine checkboxes with the default gap. */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rg.x, rt.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rg.y, rt.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rt.x + rt.width + SK_UI_SAMELINE_DEFAULT_GAP, rd.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rd.x + rd.width + SK_UI_SAMELINE_DEFAULT_GAP, ri.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, ri.x + ri.width + SK_UI_SAMELINE_DEFAULT_GAP, rw.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rw.x + rw.width + SK_UI_SAMELINE_DEFAULT_GAP, re.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rt.y, rd.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rt.y, re.y);
	/* Row collapse: the group is one line high, not the sum of 5 checkboxes. */
	/* Row collapse: the group is one line high, not the sum of 5 checkboxes,
	 * and every checkbox sits on that single row (all share the row y). */
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rt.height, rg.height);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rt.y, re.y);

	/* sep2, Collapse, Auto-scroll continue the toolbar row. */
	TEST_ASSERT_TRUE(rs2.width <= 1.5f);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rg.x + rg.width + SK_UI_SAMELINE_DEFAULT_GAP, rs2.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rs2.x + rs2.width + SK_UI_SAMELINE_DEFAULT_GAP, rcol.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rcol.x + rcol.width + SK_UI_SAMELINE_DEFAULT_GAP, rau.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.y, rcol.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.y, rau.y);

	/* The bare separator after the row is a full-width horizontal rule on a
	 * new row (SameLine was NOT called before it). */
	TEST_ASSERT_EQUAL_INT(0, ui->separator_get_vertical(ctx, sep_h));
	TEST_ASSERT_TRUE(rsh.y > rau.y + 0.5f);
	TEST_ASSERT_TRUE(rsh.width >= 200.0f);
	TEST_ASSERT_TRUE(rsh.height >= 8.0f);

	/* --- Collision matrix: SameLine(offset) absolute cells --- */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, mlabel, &rml, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, m1, &rm1, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, m2, &rm2, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, m3, &rm3, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rml.x + 80.0f, rm1.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rml.x + 104.0f, rm2.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rml.x + 128.0f, rm3.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rml.y, rm1.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rml.y, rm2.y);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rml.y, rm3.y);

	/* Spacing / Dummy extents. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, spacer, &rsp, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, SK_UI_SPACING_DEFAULT, rsp.height);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dummy, &rdum, NULL));
	TEST_ASSERT_TRUE(rdum.width <= 0.5f);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 2.0f, rdum.height);

	/* A second frame keeps the packing stable (same_line props are retained
	 * on the nodes, not re-applied). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep1, &rs1, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, autoscroll, &rau, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.x + rc.width + SK_UI_SAMELINE_DEFAULT_GAP, rs1.x);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, rc.y, rau.y);

	ui->font_destroy(font);
	ui->font_system_destroy(fs);
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
