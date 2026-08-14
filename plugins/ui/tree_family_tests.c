/**
 * @file tree_family_tests.c
 * @brief Headless automation for the editor TreeNode / CollapsingHeader family (APX-350).
 *
 * Drives expand / collapse / select / double-click and the trailing header
 * button through the SK_UI_TEST harness. No rendering.
 */

#include "ui_test.h"

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

static i32 tf_hidden(sk_ui_test_t* t, sk_ui_node_t node) {
	sk_ui_prop_value_t pv;
	if (t->ui->node_get_prop(t->ctx, node, "hidden", &pv) != 0 || pv.type != SK_UI_PROP_I32) {
		return 0;
	}
	return pv.data.i32_value != 0 ? 1 : 0;
}

/**
 * Headless: expand on arrow (no select), select on row, double-click opens,
 * collapsing header toggles and the trailing '...' clicks without closing.
 */
SK_UI_TEST(tree_family_expand_select_double_click) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t col;
	sk_ui_node_t header;
	sk_ui_node_t body;
	sk_ui_node_t btn;
	sk_ui_node_t row;
	sk_ui_style_props_t p;

	sk_ui_item_set(&items[0], 1ull, 0ull, "Scene", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "EntityA", 0u);
	sk_ui_item_set(&items[2], 3ull, 2ull, "Child", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[3], 4ull, 1ull, "EntityB", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 4u;
	arr.revision = 1u;

	col = ui->widget_vertical(ctx, ui->context_root(ctx), "tff-col");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt(360.0f);
	p.layout.height = sk_ui_pt(220.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, col, &p));
	tf_place(t, col, 8.0f, 8.0f, 360.0f, 220.0f);

	host = ui->widget_tree(ctx, col, &arr, "tff");
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_set_flags(ctx, host, SK_UI_TREE_NODE_FLAGS_DEFAULT | SK_UI_TREE_NODE_FLAG_OPEN_ON_DOUBLE_CLICK));

	TEST_ASSERT_EQUAL_INT(0, ui->set_next_item_open(ctx, 1, SK_UI_COND_ONCE));
	header = ui->widget_collapsing_header(ctx, col, "Transform", "tff-ch", SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON);
	body = ui->collapsing_header_body(ctx, header);
	(void)ui->widget_text(ctx, body, "Position XYZ", "tff-ch-txt");
	btn = ui->collapsing_header_button(ctx, header);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "tff/i1");
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "tff/i2")));
	sk_ui_item_exists(t, "tff-ch");
	sk_ui_item_exists(t, "tff-ch-btn");
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_get_open(ctx, header));
	TEST_ASSERT_EQUAL_INT(0, tf_hidden(t, body));

	/* Arrow expands without selecting (TreeNodeUpdateNextOpen). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tff/a1"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_last_was_arrow(ctx, host));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_get_selected(ctx, host, 1ull));
	sk_ui_item_exists(t, "tff/i2");
	sk_ui_item_exists(t, "tff/i4");

	/* Row click selects (exclusive) and activates. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tff/i2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 2ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_get_selected(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_last_was_arrow(ctx, host));
	TEST_ASSERT_TRUE(ui->item_bind_last_activate(ctx, host) == 2ull);
	row = ui->item_bind_find(ctx, host, 2ull);

	/* Double-click a branch to open (OpenOnDoubleClick). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_double_click(t->engine, "tff/i2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 2ull));
	sk_ui_item_exists(t, "tff/i3");

	/* Mutation: open + selection survive; host is not recreated. */
	sk_ui_item_set(&items[0], 1ull, 0ull, "World", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "Hero", 0u);
	sk_ui_item_set(&items[2], 3ull, 2ull, "Child", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[3], 5ull, 1ull, "EntityC", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.count = 4u;
	arr.revision = 2u;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 2ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 2ull));
	TEST_ASSERT_TRUE(sk_ui_node_eq(row, ui->item_bind_find(ctx, host, 2ull)));
	sk_ui_item_exists(t, "tff/i5");

	/* Header click collapses the section; trailing button does not. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tff-ch"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->collapsing_header_get_open(ctx, header));
	TEST_ASSERT_EQUAL_INT(1, tf_hidden(t, body));
	TEST_ASSERT_EQUAL_INT(0, ui->collapsing_header_set_open(ctx, header, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tff-ch-btn"));
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_button_clicked(ctx, header));
	TEST_ASSERT_EQUAL_INT(1, ui->collapsing_header_get_open(ctx, header));
	TEST_ASSERT_TRUE(sk_ui_node_eq(btn, ui->collapsing_header_button(ctx, header)));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
