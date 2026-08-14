/**
 * @file checkbox_radio_tests.c
 * @brief Headless automation for the editor Checkbox / Radio family (APX-341).
 *
 * Clicks through the shared test-engine harness (APX-259/261): toggle writes
 * the bound value and fires changed exactly once per click, radio siblings
 * deselect, and an external mutation of the bound scalar is visible after
 * the next harness frame. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void cr_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

typedef struct cr_change_sink_t {
	i32 changes;
	i32 last;
} cr_change_sink_t;

static void cr_on_bool(sk_ui_context_t* ctx, sk_ui_node_t node, i32 value, void_ptr_t user) {
	cr_change_sink_t* sink = (cr_change_sink_t*)user;
	(void)ctx;
	(void)node;
	if (sink == NULL) {
		return;
	}
	sink->changes += 1;
	sink->last = value;
}

/**
 * Headless: programmatic click toggles the bound i32, changed fires once per
 * toggle, radio siblings deselect, external bind mutation is visible next frame.
 */
SK_UI_TEST(checkbox_radio_toggle_bind_group_and_external) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t cb;
	sk_ui_node_t group;
	sk_ui_node_t ra;
	sk_ui_node_t rb;
	sk_ui_node_t rc;
	i32 enabled = 0;
	i32 mode = 0;
	i32 flags = 0x1;
	cr_change_sink_t sink;

	cb = ui->widget_checkbox(ctx, root, 0, "cr-cb");
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_bind(ctx, cb, &enabled));
	sink.changes = 0;
	sink.last = -1;
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_on_change(ctx, cb, cr_on_bool, &sink));
	cr_place(t, cb, 16.0f, 16.0f, 20.0f, 20.0f);

	group = ui->widget_view(ctx, root, "cr-rg");
	cr_place(t, group, 16.0f, 48.0f, 80.0f, 72.0f);
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, group, &ls));
		ls.flex_direction = SK_UI_FLEX_COLUMN;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, group, &ls));
	}
	ra = ui->widget_radio(ctx, group, 1, "cr-ra");
	rb = ui->widget_radio(ctx, group, 0, "cr-rb");
	rc = ui->widget_radio(ctx, group, 0, "cr-rc");
	TEST_ASSERT_EQUAL_INT(0, ui->radio_bind(ctx, ra, &mode, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_bind(ctx, rb, &mode, 1));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_bind(ctx, rc, &mode, 2));
	{
		sk_ui_style_props_t p;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
		p.layout.width = sk_ui_pt(18.0f);
		p.layout.height = sk_ui_pt(18.0f);
		p.layout.min_width = sk_ui_pt(18.0f);
		p.layout.min_height = sk_ui_pt(18.0f);
		p.layout.max_width = sk_ui_pt(18.0f);
		p.layout.max_height = sk_ui_pt(18.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, ra, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, rb, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, rc, &p));
	}

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "cr-cb");
	sk_ui_item_exists(t, "cr-ra");
	sk_ui_item_exists(t, "cr-rb");
	sk_ui_value_equals(t, "cr-cb", "0");
	sk_ui_value_equals(t, "cr-ra", "1");
	sk_ui_value_equals(t, "cr-rb", "0");
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, enabled);

	/* Click toggles bound value; changed + on_change fire once. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cr-cb"));
	TEST_ASSERT_EQUAL_INT(1, enabled);
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_changed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, sink.changes);
	TEST_ASSERT_EQUAL_INT(1, sink.last);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "cr-cb", "1");

	/* Second toggle writes 0; changed once more. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cr-cb"));
	TEST_ASSERT_EQUAL_INT(0, enabled);
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_changed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(2, sink.changes);
	TEST_ASSERT_EQUAL_INT(0, sink.last);

	/* External mutation is reflected on the next frame (style_resolve). */
	enabled = 1;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "cr-cb", "1");
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb));

	/* Radio: click B deselects A. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cr-rb"));
	TEST_ASSERT_EQUAL_INT(1, mode);
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, ra));
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, rb));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, rc));
	TEST_ASSERT_EQUAL_INT(1, ui->radio_changed(ctx, rb));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, rb));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, ra));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "cr-ra", "0");
	sk_ui_value_equals(t, "cr-rb", "1");
	sk_ui_value_equals(t, "cr-rc", "0");

	/* External radio mutation → next frame. */
	mode = 2;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "cr-ra", "0");
	sk_ui_value_equals(t, "cr-rb", "0");
	sk_ui_value_equals(t, "cr-rc", "1");
	TEST_ASSERT_EQUAL_INT(0, ui->radio_changed(ctx, rc));

	/* Flags mixed: some bits set. Click sets all, then clears. */
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_bind_flags(ctx, cb, &flags, 0x3));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_mixed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cr-cb"));
	TEST_ASSERT_EQUAL_INT(0x3, flags);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_mixed(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(1, ui->checkbox_changed(ctx, cb));

	/* Disabled checkbox: click does not toggle. */
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_bind(ctx, cb, &enabled));
	enabled = 0;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_set_disabled(ctx, cb, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cr-cb"));
	TEST_ASSERT_EQUAL_INT(0, enabled);
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_get_checked(ctx, cb));
	TEST_ASSERT_EQUAL_INT(0, ui->checkbox_changed(ctx, cb));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
