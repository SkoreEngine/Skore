/**
 * @file tooltip_family_tests.c
 * @brief Headless automation for the editor Tooltip family (APX-354).
 *
 * Moves the mouse over the previous item, ticks frames, and asserts the
 * tooltip appears after DelayNormal, disappears on leave, clamps to the
 * viewport, and never becomes the hovered / active hit target. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void tt_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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
 * Headless: hover the previous item, DelayNormal, clamp, no hover steal.
 */
SK_UI_TEST(tooltip_family_hover_delay_clamp_passthrough) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t item;
	sk_ui_node_t tip;
	sk_ui_node_t name;
	sk_ui_rect_t ir;
	sk_ui_rect_t tr;
	sk_ui_node_t hit;

	item = ui->widget_button(ctx, root, "hero.skmesh", "ttf-item");
	(void)ui->button_set_size(ctx, item, 140.0f, 28.0f);
	tip = ui->widget_tooltip(ctx, root, "ttf-tip");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(tip));
	name = ui->widget_text(ctx, tip, "hero.skmesh", "ttf-name");
	(void)ui->widget_text(ctx, tip, "Type  Mesh", "ttf-type");
	(void)ui->widget_text_colored(ctx, tip, "1.24 ms", sk_ui_rgba(0.45f, 0.85f, 0.40f, 1.0f), "ttf-ms");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(name));
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_set_delay(ctx, tip, SK_UI_TOOLTIP_DELAY_NORMAL));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(0, ui->node_is_visible(ctx, tip));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "ttf-item"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_hovered(ctx, item));
	/* One 1/60 frame is well under DelayNormal. */
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(t->engine, 0.20f));
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_step(t->engine, 0.25f));
	TEST_ASSERT_EQUAL_INT(1, ui->tooltip_get_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->node_is_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(1, ui->node_is_visible(ctx, name));

	/* Leave the item: tooltip disappears. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, 8.0f, 200.0f));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_get_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(0, ui->node_is_visible(ctx, tip));

	/* Immediate delay + click-through: tooltip is not the hit / active item. */
	TEST_ASSERT_EQUAL_INT(0, ui->tooltip_set_delay(ctx, tip, 0.0f));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "ttf-item"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->tooltip_get_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(1, ui->node_is_visible(ctx, tip));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, item, &ir, NULL));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tip, &tr, NULL));
	TEST_ASSERT_TRUE(tr.width > 8.0f);
	TEST_ASSERT_TRUE(tr.height > 8.0f);
	hit = ui->hit_test(ctx, ir.x + ir.width * 0.5f, ir.y + ir.height * 0.5f);
	TEST_ASSERT_TRUE(sk_ui_node_eq(hit, item));
	TEST_ASSERT_FALSE(sk_ui_node_eq(hit, tip));
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_hovered(ctx, item));
	TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, tip) & (u32)SK_UI_STATE_HOVER));
	TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, tip) & (u32)SK_UI_STATE_ACTIVE));
	(void)ui->button_clicked(ctx, item);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 0, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, item));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, (i32)(ui->node_get_state(ctx, tip) & (u32)SK_UI_STATE_ACTIVE));

	/* Clamp: hover near the bottom-right of a small viewport. */
	{
		sk_ui_style_props_t p;
		sk_ui_rect_t cr;
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
		p.layout.min_width = sk_ui_pt(140.0f);
		p.layout.min_height = sk_ui_pt(72.0f);
		p.layout.width = sk_ui_pt(140.0f);
		p.layout.height = sk_ui_pt(72.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, tip, &p));
		tt_place(t, item, 0.0f, 0.0f, 240.0f, 180.0f);
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->harness_set_size(ui->test_engine_harness(t->engine), 240.0f, 180.0f));
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, 232.0f, 172.0f));
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
		TEST_ASSERT_EQUAL_INT(1, ui->tooltip_get_visible(ctx, tip));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, tip, &cr, NULL));
		TEST_ASSERT_TRUE(cr.x + 0.5f >= 0.0f);
		TEST_ASSERT_TRUE(cr.y + 0.5f >= 0.0f);
		TEST_ASSERT_TRUE(cr.x + cr.width <= 240.0f + 0.5f);
		TEST_ASSERT_TRUE(cr.y + cr.height <= 180.0f + 0.5f);
		/* Unclamped cursor+offset would sit past the right / bottom edge. */
		TEST_ASSERT_TRUE(cr.x + cr.width < 232.0f + SK_UI_TOOLTIP_OFFSET_X);
		TEST_ASSERT_TRUE(cr.y + cr.height < 172.0f + SK_UI_TOOLTIP_OFFSET_Y);
	}
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
