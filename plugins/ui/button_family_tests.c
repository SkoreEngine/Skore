/**
 * @file button_family_tests.c
 * @brief Headless automation for the editor Button family (APX-339).
 *
 * Drives mouse press / release / hover / drag-off-then-release through the
 * shared test-engine harness (APX-259/261) and asserts click, press, release
 * return values plus live hovered/active flags across frames. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void bf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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
 * Headless: hover, press, hold across a frame, release on-target (click),
 * then drag off and release (no click). Also covers Small / Invisible /
 * Selection / Bordered / Arrow variants.
 */
SK_UI_TEST(button_family_press_release_drag_off) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_t small;
	sk_ui_node_t inv;
	sk_ui_node_t sel;
	sk_ui_node_t brd;
	sk_ui_node_t arr;
	sk_ui_rect_t border;

	btn = ui->widget_button(ctx, root, "OK", "bf-ok");
	small = ui->widget_small_button(ctx, root, "x", "bf-small");
	inv = ui->widget_invisible_button(ctx, root, "bf-inv", 48.0f, 32.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT | SK_UI_BUTTON_FLAG_MOUSE_MIDDLE);
	sel = ui->widget_selection_button(ctx, root, "Move", 1, "bf-sel", 56.0f, 24.0f);
	brd = ui->widget_bordered_button(ctx, root, "Add", "bf-brd", 72.0f, 24.0f);
	arr = ui->widget_arrow_button(ctx, root, SK_UI_ARROW_DOWN, "bf-arr");

	bf_place(t, btn, 16.0f, 16.0f, 80.0f, 28.0f);
	bf_place(t, small, 112.0f, 16.0f, 32.0f, 18.0f);
	bf_place(t, inv, 16.0f, 56.0f, 48.0f, 32.0f);
	bf_place(t, sel, 80.0f, 56.0f, 56.0f, 24.0f);
	bf_place(t, brd, 16.0f, 100.0f, 72.0f, 24.0f);
	bf_place(t, arr, 104.0f, 100.0f, 22.0f, 22.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "bf-ok");
	sk_ui_item_exists(t, "bf-small");
	sk_ui_item_exists(t, "bf-inv");
	sk_ui_item_exists(t, "bf-sel");
	sk_ui_item_exists(t, "bf-brd");
	sk_ui_item_exists(t, "bf-arr");

	/* Idle: no hover / active / edge. */
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_hovered(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_pressed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_released(ctx, btn));

	/* Hover frame. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "bf-ok"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_hovered(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));
	sk_ui_item_state(t, "bf-ok", (u32)SK_UI_STATE_HOVER, (u32)SK_UI_STATE_ACTIVE);

	/* Press: active this frame, not yet a click. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_press(t->engine, "bf-ok", SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->button_pressed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	/* Held across a frame: still active, press edge already consumed. */
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_pressed(ctx, btn));
	sk_ui_item_state(t, "bf-ok", (u32)SK_UI_STATE_ACTIVE, 0u);

	/* Release on-target: click + released, active clears. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_release(t->engine, SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->button_released(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "bf-ok", 0u, (u32)SK_UI_STATE_ACTIVE);

	/* Drag off then release: no click. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_press(t->engine, "bf-ok", SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	(void)ui->button_pressed(ctx, btn);
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, btn, &border, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, border.x + border.width + 40.0f, border.y + border.height + 40.0f));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->button_is_active(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_hovered(ctx, btn));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_release(t->engine, SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->button_released(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));

	/* SmallButton click. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "bf-small"));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, small));

	/* InvisibleButton: left click + middle click (editor canvas flags). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "bf-inv"));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, inv));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_click_ex(t->engine, "bf-inv", SK_UI_POINTER_BUTTON_MIDDLE, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, inv));

	/* Selection / bordered / arrow still click. */
	TEST_ASSERT_EQUAL_INT(1, ui->button_get_selected(ctx, sel));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "bf-sel"));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, sel));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "bf-brd"));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, brd));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "bf-arr"));
	TEST_ASSERT_EQUAL_INT(1, ui->button_clicked(ctx, arr));

	/* Disabled: hover/press must not click. */
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_disabled(ctx, btn, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "bf-ok"));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_pressed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
