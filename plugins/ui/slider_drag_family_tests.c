/**
 * @file slider_drag_family_tests.c
 * @brief Headless automation for the editor Slider / Drag family (APX-343).
 *
 * Injects press → move → release drags through the shared test-engine
 * harness and asserts mapped values, clamp, the consume-on-read changed
 * flag, and the ctrl-click text-entry commit path. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void sd_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static i32 sd_key(sk_ui_test_t* t, i32 key, u32 mods) {
	const sk_ui_api_t* ui = t->ui;
	i32 rc = ui->test_engine_key(t->engine, key, 1, mods);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui->test_engine_key(t->engine, key, 0, mods);
}

/**
 * Headless: press → move → release maps the slider, changed fires only when
 * the value actually changes, both ends clamp, and ctrl-click text-entry
 * commits a typed value.
 */
SK_UI_TEST(slider_drag_family_drag_clamp_changed_text_entry) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sl;
	sk_ui_node_t dg;
	sk_ui_rect_t border;
	f32 y_mid;
	f32 value;

	sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 20.0f, "sd-sl");
	dg = ui->widget_drag_float(ctx, root, 1.0f, 0.0f, 50.0f, 5.0f, "%.1f", "sd-dg");
	sd_place(t, sl, 16.0f, 20.0f, 200.0f, 24.0f);
	sd_place(t, dg, 16.0f, 56.0f, 160.0f, 24.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "sd-sl");
	sk_ui_item_exists(t, "sd-dg");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sl, &border, NULL));
	y_mid = border.y + border.height * 0.5f;

	(void)ui->slider_changed(ctx, sl);
	TEST_ASSERT_EQUAL_INT(0, ui->slider_changed(ctx, sl));

	/* Press → move → release to ~75. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 8.0f, y_mid, border.x + border.width * 0.75f, y_mid, 8u, 1.0f / 60.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(4.0f, 75.0f, value);
	TEST_ASSERT_EQUAL_INT(1, ui->slider_changed(ctx, sl));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_changed(ctx, sl));

	/* Clamp high, then low. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + border.width * 0.5f, y_mid, border.x + border.width + 48.0f, y_mid, 6u, 1.0f / 60.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, ui->slider_get_value(ctx, sl));
	TEST_ASSERT_EQUAL_INT(1, ui->slider_changed(ctx, sl));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_changed(ctx, sl));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + border.width * 0.5f, y_mid, border.x - 48.0f, y_mid, 6u, 1.0f / 60.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, ui->slider_get_value(ctx, sl));
	sk_ui_value_equals(t, "sd-sl", "0");

	/* DragFloat: press, move 20px at speed 1 → +20, then clamp at 50. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, dg, &border, NULL));
	y_mid = border.y + border.height * 0.5f;
	(void)ui->slider_changed(ctx, dg);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 20.0f, y_mid, border.x + 40.0f, y_mid, 4u, 1.0f / 60.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 25.0f, ui->slider_get_value(ctx, dg));
	TEST_ASSERT_EQUAL_INT(1, ui->slider_changed(ctx, dg));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 20.0f, y_mid, border.x + 80.0f, y_mid, 6u, 1.0f / 60.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, ui->slider_get_value(ctx, dg));
	(void)ui->slider_changed(ctx, dg);
	/* Already clamped: further drag must not fire changed. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 20.0f, y_mid, border.x + 80.0f, y_mid, 4u, 1.0f / 60.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, ui->slider_get_value(ctx, dg));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_changed(ctx, dg));

	/* Ctrl-click text-entry commits a typed value. */
	TEST_ASSERT_EQUAL_INT(0, ui->slider_set_value(ctx, sl, 20.0f));
	(void)ui->slider_changed(ctx, sl);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_press(t->engine, "sd-sl", SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_CTRL));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_release(t->engine, SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_CTRL));
	TEST_ASSERT_EQUAL_INT(1, ui->slider_is_text_input(ctx, sl));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "42"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sd_key(t, SK_UI_KEY_ENTER, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_is_text_input(ctx, sl));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, ui->slider_get_value(ctx, sl));
	TEST_ASSERT_EQUAL_INT(1, ui->slider_changed(ctx, sl));
	TEST_ASSERT_EQUAL_INT(0, ui->slider_changed(ctx, sl));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
