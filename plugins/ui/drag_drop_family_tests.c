/**
 * @file drag_drop_family_tests.c
 * @brief Headless automation for the editor DragDrop payload family (APX-356).
 *
 * Presses a tree row, moves, hovers a target row / property field, and
 * releases through the SK_UI_TEST harness. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void ddf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static void ddf_center(sk_ui_test_t* t, const_chr_t id, f32* x, f32* y) {
	sk_ui_node_t n = t->ui->query_by_test_id(t->ctx, SK_UI_NODE_INVALID, id);
	sk_ui_rect_t r;
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(n));
	TEST_ASSERT_EQUAL_INT(0, t->ui->node_get_abs_rect(t->ctx, n, &r, NULL));
	*x = r.x + r.width * 0.5f;
	*y = r.y + r.height * 0.5f;
}

/**
 * Headless: press a tree row, drag onto another row (entity reparent) and
 * onto a property field (asset assign). Peek hover before release.
 */
SK_UI_TEST(drag_drop_family_tree_row_and_property_field) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_item_t items[4];
	sk_ui_item_array_t arr;
	sk_ui_node_t col;
	sk_ui_node_t host;
	sk_ui_node_t row_a;
	sk_ui_node_t row_b;
	sk_ui_node_t field;
	sk_ui_style_props_t p;
	const sk_ui_payload_t* pay;
	const u32 asset = 42u;
	f32 x0, y0, x1, y1, xf, yf;

	sk_ui_item_set(&items[0], 1ull, 0ull, "Scene", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&items[1], 2ull, 1ull, "EntityA", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[2], 3ull, 1ull, "EntityB", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 3u;
	arr.revision = 1u;

	col = ui->widget_vertical(ctx, ui->context_root(ctx), "ddf-col");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt(360.0f);
	p.layout.height = sk_ui_pt(220.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));
	ddf_place(t, col, 8.0f, 8.0f, 360.0f, 220.0f);

	host = ui->widget_tree(ctx, col, &arr, "ddf");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(host));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_flags(ctx, host, SK_UI_TREE_NODE_FLAGS_DEFAULT));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_open(ctx, host, 1ull, 1));

	field = ui->widget_text_input(ctx, col, "hero.skmesh", "ddf-field");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(field));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "ddf/i1");
	sk_ui_item_exists(t, "ddf/i2");
	sk_ui_item_exists(t, "ddf/i3");
	sk_ui_item_exists(t, "ddf-field");

	row_a = ui->item_bind_find(ctx, host, 2ull);
	row_b = ui->item_bind_find(ctx, host, 3ull);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row_a));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(row_b));

	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, row_a, SK_UI_ENTITY_PAYLOAD, NULL, 0u,
												  SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS | SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_set_preview(ctx, row_a, "1 entity"));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, row_b, SK_UI_ENTITY_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, row_a, SK_UI_ASSET_PAYLOAD, &asset, (u32)sizeof(asset),
												  SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS | SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_set_preview(ctx, row_a, "hero.skmesh"));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_target(ctx, field, SK_UI_ASSET_PAYLOAD, 0u));

	/* First pass: entity-style empty payload onto the sibling row. */
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, row_a, SK_UI_ENTITY_PAYLOAD, NULL, 0u,
												  SK_UI_DRAG_DROP_FLAG_SOURCE_NO_HOLD_TO_OPEN_OTHERS | SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_set_preview(ctx, row_a, "1 entity"));

	ddf_center(t, "ddf/i2", &x0, &y0);
	ddf_center(t, "ddf/i3", &x1, &y1);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, x0, y0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, x0 + 10.0f, y0 + 4.0f));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	pay = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(pay);
	TEST_ASSERT_EQUAL_STRING(SK_UI_ENTITY_PAYLOAD, pay->type);
	TEST_ASSERT_EQUAL_UINT32(0u, pay->size);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, x1, y1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_target_hovered(ctx, row_b));
	pay = ui->drag_drop_get_payload(ctx);
	TEST_ASSERT_NOT_NULL(pay);
	TEST_ASSERT_EQUAL_INT(1, pay->preview);
	TEST_ASSERT_NULL(ui->drag_drop_accept(ctx, SK_UI_ENTITY_PAYLOAD, 0u)); /* peek only until release */

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 0, SK_UI_MOD_NONE));
	pay = ui->drag_drop_accept(ctx, SK_UI_ENTITY_PAYLOAD, SK_UI_DRAG_DROP_FLAG_ACCEPT_NO_DRAW_DEFAULT_RECT);
	TEST_ASSERT_NOT_NULL(pay);
	TEST_ASSERT_EQUAL_STRING(SK_UI_ENTITY_PAYLOAD, pay->type);

	/* Second pass: asset blob onto the property field. */
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_source(ctx, row_a, SK_UI_ASSET_PAYLOAD, &asset, (u32)sizeof(asset), SK_UI_DRAG_DROP_FLAG_SOURCE_NO_DISABLE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, ui->drag_drop_set_preview(ctx, row_a, "hero.skmesh"));
	ddf_center(t, "ddf-field", &xf, &yf);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, x0, y0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, xf, yf));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_is_active(ctx));
	TEST_ASSERT_EQUAL_INT(1, ui->drag_drop_target_hovered(ctx, field));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 0, SK_UI_MOD_NONE));
	pay = ui->drag_drop_accept(ctx, SK_UI_ASSET_PAYLOAD, 0u);
	TEST_ASSERT_NOT_NULL(pay);
	TEST_ASSERT_EQUAL_UINT32(asset, *(const u32*)pay->data);
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
