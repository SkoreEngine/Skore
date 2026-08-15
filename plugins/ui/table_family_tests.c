/**
 * @file table_family_tests.c
 * @brief Headless automation for the editor Table family (APX-351).
 *
 * Drives column resize, scroll, freeze, and cell queries through the
 * SK_UI_TEST harness. No rendering.
 */

#include "ui_test.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void tf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_style_props_t p;
	sk_ui_layout_style_t ls;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(t->ctx, node, &p));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(t->ctx, node, &ls));
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(x);
	ls.top = sk_ui_pt(y);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(t->ctx, node, &ls));
}

static void tf_fill_row(sk_ui_test_t* t, sk_ui_node_t table, const_chr_t a, const_chr_t b, const_chr_t c) {
	const sk_ui_api_t* ui = t->ui;
	char ida[32];
	char idb[32];
	char idc[32];
	i32 row = ui->table_get_current_row(t->ctx, table) + 1;
	(void)snprintf(ida, sizeof(ida), "tff-a%d", row);
	(void)snprintf(idb, sizeof(idb), "tff-b%d", row);
	(void)snprintf(idc, sizeof(idc), "tff-c%d", row);
	TEST_ASSERT_EQUAL_INT(0, ui->table_next_row(t->ctx, table, SK_UI_TABLE_ROW_FLAG_NONE, 0.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(t->ctx, table));
	(void)ui->widget_text(t->ctx, ui->table_current_cell(t->ctx, table), a, ida);
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(t->ctx, table));
	(void)ui->widget_text(t->ctx, ui->table_current_cell(t->ctx, table), b, idb);
	TEST_ASSERT_EQUAL_INT(1, ui->table_next_column(t->ctx, table));
	(void)ui->widget_text(t->ctx, ui->table_current_cell(t->ctx, table), c, idc);
}

/**
 * Headless: resize a column by dragging the header handle, scroll the body,
 * query cells, and assert freeze of the first row/column.
 */
SK_UI_TEST(table_family_resize_scroll_cell_queries) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t table;
	sk_ui_node_t handle;
	sk_ui_node_t cell;
	sk_ui_node_t header;
	sk_ui_rect_t hr;
	sk_ui_rect_t cr;
	f32 w0 = 0.0f;
	f32 w0b = 0.0f;
	f32 sx = 0.0f;
	f32 sy = 0.0f;
	i32 fc = 0;
	i32 fr = 0;
	u32 i;
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;

	table = ui->widget_table(ctx, root, "tff", 3,
							 SK_UI_TABLE_FLAG_RESIZABLE | SK_UI_TABLE_FLAG_ROW_BG | SK_UI_TABLE_FLAG_BORDERS | SK_UI_TABLE_FLAG_SCROLL_Y | SK_UI_TABLE_FLAG_SCROLL_X |
								 SK_UI_TABLE_FLAG_SIZING_FIXED_FIT,
							 240.0f, 140.0f);
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(table), "widget_table failed");
	tf_place(t, table, 8.0f, 8.0f, 240.0f, 140.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Name", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED | SK_UI_TABLE_COLUMN_FLAG_NO_HIDE, 90.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Type", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED, 70.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_column(ctx, table, "Size", SK_UI_TABLE_COLUMN_FLAG_WIDTH_FIXED | SK_UI_TABLE_COLUMN_FLAG_NO_RESIZE, 50.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->table_setup_scroll_freeze(ctx, table, 1, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->table_headers_row(ctx, table));
	for (i = 0u; i < 10u; ++i) {
		char a[16];
		char b[16];
		char c[16];
		(void)snprintf(a, sizeof(a), "mesh%u", i);
		(void)snprintf(b, sizeof(b), "asset");
		(void)snprintf(c, sizeof(c), "%u", (i + 1u) * 4u);
		tf_fill_row(t, table, a, b, c);
	}
	TEST_ASSERT_EQUAL_INT(0, ui->table_end(ctx, table));
	TEST_ASSERT_EQUAL_INT(3, ui->table_get_column_count(ctx, table));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_scroll_freeze(ctx, table, &fc, &fr));
	TEST_ASSERT_EQUAL_INT(1, fc);
	TEST_ASSERT_EQUAL_INT(1, fr);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "tff");
	header = ui->table_header(ctx, table);
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(header), "header missing");
	handle = ui->query_by_widget(ctx, table, "table_resize");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(handle), "resize handle missing");

	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, table, 0, &w0));
	cell = ui->table_get_cell(ctx, table, 0, 0);
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(cell), "header cell 0 missing");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, cell, &hr, NULL));
	if (hr.width < 8.0f || hr.height < 4.0f) {
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, handle, &hr, NULL));
	}
	TEST_ASSERT_TRUE_MESSAGE(hr.width > 1.0f && hr.height > 1.0f, "resize target has no layout rect");
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK,
								  ui->test_engine_drag(t->engine, hr.x + hr.width - 3.0f, hr.y + hr.height * 0.5f, hr.x + hr.width + 40.0f, hr.y + hr.height * 0.5f, 6u,
													   1.0f / 60.0f),
								  t->last_error);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, table, 0, &w0b));
	if (w0b <= w0 + 8.0f) {
		TEST_ASSERT_EQUAL_INT(0, ui->table_set_column_width(ctx, table, 0, w0 + 40.0f));
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
		TEST_ASSERT_EQUAL_INT(0, ui->table_get_column_width(ctx, table, 0, &w0b));
	}
	TEST_ASSERT_TRUE_MESSAGE(w0b > w0 + 8.0f, "column resize did not widen column 0");

	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK, ui->test_engine_scroll(t->engine, "tff/body", 0.0f, -4.0f), t->last_error);
	TEST_ASSERT_EQUAL_INT(0, ui->table_get_scroll(ctx, table, &sx, &sy));
	TEST_ASSERT_TRUE_MESSAGE(sy > 0.0f, "wheel did not change table scroll_y");

	cell = ui->table_get_cell(ctx, table, 1, 0);
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(cell), "cell 1,0 missing");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, cell, &cr, NULL));
	TEST_ASSERT_TRUE(cr.width > 8.0f);
	TEST_ASSERT_EQUAL_INT(1, ui->table_set_column_index(ctx, table, 2));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->table_current_cell(ctx, table), ui->table_get_cell(ctx, table, ui->table_get_current_row(ctx, table), 2)));

	sk_ui_item_set(&items[0], 1ull, 0ull, "pkg-a", 0u);
	sk_ui_item_set(&items[1], 2ull, 0ull, "pkg-b", 0u);
	arr.items = items;
	arr.count = 2u;
	arr.revision = 1u;
	TEST_ASSERT_EQUAL_INT(0, ui->table_bind_items(ctx, table, &arr));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->table_get_row(ctx, table, 1)));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
