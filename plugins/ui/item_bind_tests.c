/**
 * @file item_bind_tests.c
 * @brief Headless automation for retained item-array binding (APX-338).
 *
 * Drives expand / collapse / select through the test-engine harness
 * (APX-259/261) against a mutating caller-owned array.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void ib_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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
 * Headless automation: expand, select, then mutate the caller array and
 * confirm open + selection survive without recreating the host widget.
 */
SK_UI_TEST(item_bind_mutate_expand_select) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_item_t items[6];
	sk_ui_item_array_t arr;
	sk_ui_node_t host;
	sk_ui_node_t ent;

	sk_ui_item_set(&items[0], 1ull, 0ull, "Scene", 0u);
	sk_ui_item_set(&items[1], 2ull, 1ull, "EntityA", 0u);
	sk_ui_item_set(&items[2], 3ull, 2ull, "Child", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&items[3], 4ull, 1ull, "EntityB", (u32)SK_UI_ITEM_FLAG_LEAF);
	arr.items = items;
	arr.count = 4u;
	arr.revision = 1u;

	host = ui->widget_tree(ctx, ui->context_root(ctx), &arr, "ib");
	ib_place(t, host, 8.0f, 8.0f, 280.0f, 220.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "ib/i1");
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "ib/i2")));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ib/a1"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_open(ctx, host, 1ull));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_last_was_arrow(ctx, host));
	sk_ui_item_exists(t, "ib/i2");
	sk_ui_item_exists(t, "ib/i4");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ib/i2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->item_bind_get_selected(ctx, host, 2ull));
	TEST_ASSERT_EQUAL_INT(0, ui->item_bind_last_was_arrow(ctx, host));
	TEST_ASSERT_TRUE(ui->item_bind_last_activate(ctx, host) == 2ull);
	ent = ui->item_bind_find(ctx, host, 2ull);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ib/a2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "ib/i3");

	/* Mutate: relabel, drop EntityB, insert EntityC, keep ids 1 and 2. */
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
	TEST_ASSERT_TRUE(sk_ui_node_eq(ent, ui->item_bind_find(ctx, host, 2ull)));
	sk_ui_item_exists(t, "ib/i5");
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "ib/i4")));
	sk_ui_item_exists(t, "ib/i3");
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
