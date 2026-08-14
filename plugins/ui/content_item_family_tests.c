/**
 * @file content_item_family_tests.c
 * @brief Headless automation for the content-item thumbnail grid (APX-358).
 *
 * Click to select, double-click activate, rename + type, and mutate the
 * bound item array between frames. No rendering.
 */

#include "ui_test.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void cgf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

/**
 * Headless: click select, double-click activate, rename + type, mutate the
 * caller array and re-assert selection / node identity.
 */
SK_UI_TEST(content_item_family_select_activate_rename_mutate) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;
	sk_ui_node_t grid;
	sk_ui_node_t row;
	sk_ui_node_t input;
	sk_ui_content_item_state_t st;
	char rid[32];

	sk_ui_item_set(&items[0], 1ull, 0ull, "Mesh.skmesh", (u32)SK_UI_ITEM_FLAG_LEAF);
	items[0].icon = 11u;
	sk_ui_item_set(&items[1], 2ull, 0ull, "Hero.skent", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[2], 3ull, 0ull, "Broken.skmat", (u32)SK_UI_ITEM_FLAG_LEAF | (u32)SK_UI_ITEM_FLAG_ERROR);
	sk_ui_item_set(&items[3], 4ull, 0ull, "Folder", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 4u;
	arr.revision = 1u;

	grid = ui->widget_content_grid(ctx, ui->context_root(ctx), &arr, 1.0f, "cgf");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(grid), "widget_content_grid failed");
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_set_available_width(ctx, grid, 336.0f));
	cgf_place(t, grid, 8.0f, 8.0f, 336.0f, 240.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT((int)SK_UI_ITEM_BIND_LIST, (int)ui->item_bind_get_kind(ctx, grid));
	TEST_ASSERT_EQUAL_INT(3, ui->content_grid_get_column_count(ctx, grid));
	sk_ui_item_exists(t, "cgf/i1");
	sk_ui_item_exists(t, "cgf/i2");
	sk_ui_item_exists(t, "cgf/i3");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(ui->content_grid_thumb(ctx, grid, 1ull)), "thumb missing");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(ui->content_grid_icon(ctx, grid, 2ull)), "icon missing");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(ui->content_grid_error(ctx, grid, 3ull)), "error mark missing");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cgf/i2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, grid, 2ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_get_selected(ctx, grid, 1ull));
	TEST_ASSERT_TRUE_MESSAGE(ui->item_bind_last_activate(ctx, grid) == 2ull, "click did not activate id 2");
	row = ui->item_bind_find(ctx, grid, 2ull);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_click_ex(t->engine, "cgf/i3", SK_UI_POINTER_BUTTON_RIGHT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE_MESSAGE(ui->content_grid_last_right_click(ctx, grid) == 3ull, "right-click not recorded");
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_item_state(ctx, grid, 3ull, &st));
	TEST_ASSERT_EQUAL_INT(1, st.right_clicked);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_double_click(t->engine, "cgf/i2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE_MESSAGE(ui->content_grid_last_enter(ctx, grid) == 2ull, "double-click did not activate");
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_item_state(ctx, grid, 2ull, &st));
	TEST_ASSERT_EQUAL_INT_MESSAGE(1, st.enter, "item_state.enter not set after double-click");

	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_begin_rename(ctx, grid, 2ull));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	(void)snprintf(rid, sizeof(rid), "cgf/r2");
	input = ui->content_grid_rename_input(ctx, grid, 2ull);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(input));
	sk_ui_item_exists(t, rid);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, input, ""));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_type_into(t, rid, "RenamedHero"));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_commit_rename(ctx, grid));
	TEST_ASSERT_EQUAL_INT(0, ui->content_grid_item_state(ctx, grid, 2ull, &st));
	TEST_ASSERT_EQUAL_INT_MESSAGE(1, st.rename_finish, "rename commit did not finish");
	TEST_ASSERT_EQUAL_STRING("RenamedHero", st.new_name);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cgf/i2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, grid, 2ull));
	row = ui->item_bind_find(ctx, grid, 2ull);

	sk_ui_item_set(&items[0], 1ull, 0ull, "MeshB.skmesh", (u32)SK_UI_ITEM_FLAG_LEAF);
	items[0].icon = 11u;
	sk_ui_item_set(&items[1], 2ull, 0ull, "RenamedHero", (u32)SK_UI_ITEM_FLAG_LEAF | (u32)SK_UI_ITEM_FLAG_SELECTED);
	sk_ui_item_set(&items[2], 5ull, 0ull, "NewTile", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 3u;
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT_MESSAGE(1, ui->item_bind_get_selected(ctx, grid, 2ull), "selection lost after mutate");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_eq(row, ui->item_bind_find(ctx, grid, 2ull)), "row node churned");
	sk_ui_item_exists(t, "cgf/i5");
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "cgf/i3")));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "cgf/i4")));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
