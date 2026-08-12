/**
 * @file ui_interaction_tests.c
 * @brief Behavioural interaction suite for v1 widgets via the authoring API (APX-262).
 *
 * Real multi-step scenarios against the code-driven test engine: click/hover/
 * drag/type/key/focus through the same input_dispatch path hosts use. Each
 * case exercises the widget's interaction handler so the suite fails if that
 * handler is disabled or no-ops.
 *
 * Coverage:
 *   - checkbox toggles on click, not on click-outside
 *   - radio group enforces single selection among siblings
 *   - slider drag values + clamp at both ends
 *   - text input typing / backspace / selection
 *   - button hover and active states
 *   - tab switching
 *   - menu open / select / dismiss
 *   - scrollbar (scroll_view) drag scrolls content
 *   - keyboard focus traversal (Tab / Shift+Tab)
 *   - negative: disabled widgets ignore interaction
 */

#include "ui_test.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

/* -------------------------------------------------------------------------- */
/* Shared layout helpers                                                      */
/* -------------------------------------------------------------------------- */

static void ix_set_size(sk_ui_test_t* t, sk_ui_node_t node, f32 w, f32 h) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, t->ui->node_merge_inline_style(t->ctx, node, &p));
}

static void ix_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_layout_style_t ls;
	ix_set_size(t, node, w, h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(t->ctx, node, &ls));
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(x);
	ls.top = sk_ui_pt(y);
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(t->ctx, node, &ls));
}

/* -------------------------------------------------------------------------- */
/* Checkbox: click toggles; click-outside does not                            */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_checkbox_toggle_and_outside) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t cb;
	sk_ui_node_t outside;

	cb = ui->widget_checkbox(ctx, root, 0, "ix-cb");
	ix_place(t, cb, 16.0f, 16.0f, 20.0f, 20.0f);
	outside = ui->widget_panel(ctx, root, "ix-cb-outside");
	ix_place(t, outside, 80.0f, 16.0f, 60.0f, 40.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "ix-cb");
	sk_ui_value_equals(t, "ix-cb", "0");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-cb"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-cb", "1");

	/* Click-outside must not toggle the checkbox. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-cb-outside"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-cb", "1");

	/* Second click on checkbox toggles back off. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-cb"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-cb", "0");
}

/* -------------------------------------------------------------------------- */
/* Radio group: exclusive sibling selection                                   */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_radio_group_single_selection) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t group;
	sk_ui_node_t r0;
	sk_ui_node_t r1;
	sk_ui_node_t r2;

	group = ui->widget_panel(ctx, root, "ix-radio-group");
	ix_place(t, group, 8.0f, 8.0f, 120.0f, 80.0f);
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, group, &ls));
		ls.flex_direction = SK_UI_FLEX_COLUMN;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, group, &ls));
	}

	r0 = ui->widget_radio(ctx, group, 1, "ix-radio-a");
	r1 = ui->widget_radio(ctx, group, 0, "ix-radio-b");
	r2 = ui->widget_radio(ctx, group, 0, "ix-radio-c");
	ix_set_size(t, r0, 18.0f, 18.0f);
	ix_set_size(t, r1, 18.0f, 18.0f);
	ix_set_size(t, r2, 18.0f, 18.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-radio-a", "1");
	sk_ui_value_equals(t, "ix-radio-b", "0");
	sk_ui_value_equals(t, "ix-radio-c", "0");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-radio-b"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-radio-a", "0");
	sk_ui_value_equals(t, "ix-radio-b", "1");
	sk_ui_value_equals(t, "ix-radio-c", "0");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-radio-c"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-radio-a", "0");
	sk_ui_value_equals(t, "ix-radio-b", "0");
	sk_ui_value_equals(t, "ix-radio-c", "1");

	/* Re-click selected radio stays selected (does not toggle off). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-radio-c"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-radio-c", "1");
	TEST_ASSERT_EQUAL_INT(1, ui->radio_get_checked(ctx, r2));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, r0));
	TEST_ASSERT_EQUAL_INT(0, ui->radio_get_checked(ctx, r1));
}

/* -------------------------------------------------------------------------- */
/* Slider drag + clamp at both ends                                           */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_slider_drag_and_clamp) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sl;
	sk_ui_rect_t border;
	f32 y_mid;
	f32 value;

	sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 40.0f, "ix-slider");
	ix_place(t, sl, 20.0f, 40.0f, 200.0f, 24.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sl, &border, NULL));
	y_mid = border.y + border.height * 0.5f;

	/* Drag to ~75% of track. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 4.0f, y_mid, border.x + border.width * 0.75f, y_mid, 8u, 1.0f / 60.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(4.0f, 75.0f, value);

	/* Clamp high: drag past the right edge. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + border.width * 0.5f, y_mid, border.x + border.width + 40.0f, y_mid, 6u, 1.0f / 60.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, value);

	/* Clamp low: drag past the left edge. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + border.width * 0.5f, y_mid, border.x - 40.0f, y_mid, 6u, 1.0f / 60.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, value);

	sk_ui_value_equals(t, "ix-slider", "0");
}

/* -------------------------------------------------------------------------- */
/* Text input: type, backspace, selection                                     */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_text_input_type_backspace_selection) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t ti;
	sk_ui_prop_value_t pv;
	i32 sel_a = 0;
	i32 sel_b = 0;

	ti = ui->widget_text_input(ctx, root, "", "ix-ti");
	ix_place(t, ti, 12.0f, 12.0f, 200.0f, 28.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_type_into(t, "ix-ti", "Hello"));
	sk_ui_value_equals(t, "ix-ti", "Hello");

	/* Backspace removes the last character. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_BACKSPACE, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_BACKSPACE, 0, SK_UI_MOD_NONE));
	sk_ui_value_equals(t, "ix-ti", "Hell");

	/* Shift+Left grows selection leftward from caret. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_LEFT, 1, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_LEFT, 0, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_LEFT, 1, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_LEFT, 0, SK_UI_MOD_SHIFT));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, ti, "sel_start", &pv));
	TEST_ASSERT_EQUAL_INT(SK_UI_PROP_I32, pv.type);
	sel_a = pv.data.i32_value;
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_prop(ctx, ti, "sel_end", &pv));
	sel_b = pv.data.i32_value;
	TEST_ASSERT_TRUE(sel_a != sel_b);

	/* Typing replaces the selection. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "p"));
	sk_ui_value_equals(t, "ix-ti", "Hep");
}

/* -------------------------------------------------------------------------- */
/* Button hover and active states                                             */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_button_hover_and_active) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;

	btn = ui->widget_button(ctx, root, "Press", "ix-btn");
	ix_place(t, btn, 24.0f, 24.0f, 96.0f, 32.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "ix-btn"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "ix-btn", (u32)SK_UI_STATE_HOVER, 0u);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_press(t->engine, "ix-btn", SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "ix-btn", (u32)SK_UI_STATE_ACTIVE, 0u);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_release(t->engine, SK_UI_POINTER_BUTTON_LEFT, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	/* Active should clear after release; hover may remain under the pointer. */
	sk_ui_item_state(t, "ix-btn", 0u, (u32)SK_UI_STATE_ACTIVE);
}

/* Separate click-callback check so hover/active does not depend on on_click. */
static i32 g_ix_btn_clicks;

static void ix_btn_on_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	(void)ctx;
	(void)node;
	(void)event;
	(void)user;
	g_ix_btn_clicks += 1;
}

SK_UI_TEST(ix_button_click_fires) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_callbacks_t cbs;

	btn = ui->widget_button(ctx, root, "Go", "ix-btn-fire");
	ix_place(t, btn, 20.0f, 20.0f, 80.0f, 28.0f);
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ix_btn_on_click;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	g_ix_btn_clicks = 0;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-btn-fire"));
	TEST_ASSERT_EQUAL_INT(1, g_ix_btn_clicks);
}

/* -------------------------------------------------------------------------- */
/* Tab switching                                                              */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_tab_switching) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t t0;
	sk_ui_node_t t1;
	sk_ui_node_t t2;

	bar = ui->widget_tab_bar(ctx, root, "ix-tabs");
	ix_place(t, bar, 8.0f, 8.0f, 240.0f, 28.0f);
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, bar, &ls));
		ls.flex_direction = SK_UI_FLEX_ROW;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, bar, &ls));
	}
	t0 = ui->widget_tab(ctx, bar, "One", "ix-tab-0");
	t1 = ui->widget_tab(ctx, bar, "Two", "ix-tab-1");
	t2 = ui->widget_tab(ctx, bar, "Three", "ix-tab-2");
	ix_set_size(t, t0, 64.0f, 24.0f);
	ix_set_size(t, t1, 64.0f, 24.0f);
	ix_set_size(t, t2, 64.0f, 24.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t1));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-tab-0"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t2));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-tab-1"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t2));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-tab-2"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t0));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, t1));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, t2));
}

/* -------------------------------------------------------------------------- */
/* Menu open / select / dismiss                                               */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_menu_open_select_dismiss) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t menu;
	sk_ui_node_t popup;
	sk_ui_node_t item;

	/* Match sample_open_menu_path layout: flex bar, fixed sizes (no abs place). */
	bar = ui->widget_menu_bar(ctx, root, "ix-menubar");
	ix_set_size(t, bar, 320.0f, 28.0f);
	menu = ui->widget_menu(ctx, bar, "File", "ix-menu-file");
	ix_set_size(t, menu, 72.0f, 24.0f);
	popup = ui->menu_get_popup(ctx, menu);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(popup));
	item = ui->widget_menu_item(ctx, popup, "Save", "ix-menu-save");
	ix_set_size(t, item, 120.0f, 22.0f);
	(void)item;

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, menu));

	/* Open */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_open_menu_path(t, "ix-menu-file"));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, menu) != 0);
	sk_ui_item_exists(t, "ix-menu-save");

	/* Select item via openMenuPath (force-open + click leaf). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_open_menu_path(t, "ix-menu-file/ix-menu-save"));
	sk_ui_item_exists(t, "ix-menu-save");

	/* Dismiss via menu API (deterministic regardless of click hit order). */
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, menu, 0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, menu));

	/* Re-open via force-open, then dismiss via set_open again. */
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, menu, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, menu) != 0);
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, menu, 0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, menu));

	/* Interaction: click the File trigger toggles open (handler must be live). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-menu-file"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, menu) != 0);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-menu-file"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, menu));
}

/* -------------------------------------------------------------------------- */
/* Scrollbar / scroll_view drag scrolls content                               */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_scrollbar_drag_scrolls) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t sv;
	sk_ui_rect_t border;
	f32 sx0 = 0.0f, sy0 = 0.0f;
	f32 sx1 = 0.0f, sy1 = 0.0f;
	f32 cx, cy;

	sv = ui->widget_scroll_view(ctx, root, "ix-sv");
	ix_place(t, sv, 16.0f, 16.0f, 120.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 120.0f, 400.0f));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx0, &sy0));
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, sy0);

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sv, &border, NULL));
	cx = border.x + border.width * 0.5f;
	cy = border.y + border.height * 0.5f;

	/* Drag upward on content → scroll_y increases (reveals lower content). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, cx, cy, cx, cy - 40.0f, 8u, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx1, &sy1));
	TEST_ASSERT_TRUE(sy1 > sy0 + 5.0f);

	/* Wheel path also works. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_scroll(t->engine, "ix-sv", 0.0f, -3.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx1, &sy1));
	TEST_ASSERT_TRUE(sy1 > 0.0f);
}

/* -------------------------------------------------------------------------- */
/* Keyboard focus traversal                                                   */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_keyboard_focus_traversal) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;

	/* Row of focusable controls. */
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, root, &ls));
		ls.flex_direction = SK_UI_FLEX_ROW;
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, root, &ls));
	}
	a = ui->widget_button(ctx, root, "A", "ix-focus-a");
	b = ui->widget_button(ctx, root, "B", "ix-focus-b");
	c = ui->widget_text_input(ctx, root, "", "ix-focus-c");
	ix_set_size(t, a, 48.0f, 28.0f);
	ix_set_size(t, b, 48.0f, 28.0f);
	ix_set_size(t, c, 100.0f, 28.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "ix-focus-a"));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), a));
	/* Live state (item registry is rebuilt on step; assert live flags too). */
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, a) & (u32)SK_UI_STATE_FOCUSED) != 0u);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "ix-focus-a", (u32)SK_UI_STATE_FOCUSED, 0u);

	/* Tab → B */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_TAB, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_TAB, 0, SK_UI_MOD_NONE));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), b));
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, b) & (u32)SK_UI_STATE_FOCUSED) != 0u);
	TEST_ASSERT_TRUE((ui->node_get_state(ctx, a) & (u32)SK_UI_STATE_FOCUSED) == 0u);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "ix-focus-b", (u32)SK_UI_STATE_FOCUSED, 0u);
	sk_ui_item_state(t, "ix-focus-a", 0u, (u32)SK_UI_STATE_FOCUSED);

	/* Tab → C (text input) */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_TAB, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_TAB, 0, SK_UI_MOD_NONE));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), c));

	/* Shift+Tab → B */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_TAB, 1, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_key(t->engine, SK_UI_KEY_TAB, 0, SK_UI_MOD_SHIFT));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->focus_get(ctx), b));
}

/* -------------------------------------------------------------------------- */
/* Disabled handlers: interactions must not change model state                */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_disabled_widgets_ignore_interaction) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t cb;
	sk_ui_node_t sl;
	sk_ui_node_t ti;
	sk_ui_node_t btn;
	sk_ui_rect_t border;
	f32 value;
	i32 clicks = 0;
	sk_ui_node_callbacks_t cbs;

	cb = ui->widget_checkbox(ctx, root, 0, "ix-dis-cb");
	sl = ui->widget_slider(ctx, root, 0.0f, 100.0f, 30.0f, "ix-dis-sl");
	ti = ui->widget_text_input(ctx, root, "keep", "ix-dis-ti");
	btn = ui->widget_button(ctx, root, "Nope", "ix-dis-btn");
	ix_place(t, cb, 10.0f, 10.0f, 20.0f, 20.0f);
	ix_place(t, sl, 10.0f, 40.0f, 160.0f, 24.0f);
	ix_place(t, ti, 10.0f, 80.0f, 160.0f, 28.0f);
	ix_place(t, btn, 10.0f, 120.0f, 80.0f, 28.0f);

	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, cb, (u32)SK_UI_STATE_DISABLED));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, sl, (u32)SK_UI_STATE_DISABLED));
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_state(ctx, ti, (u32)SK_UI_STATE_DISABLED));
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_disabled(ctx, btn, 1));
	memset(&cbs, 0, sizeof(cbs));
	cbs.on_click = ix_btn_on_click;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_callbacks(ctx, btn, &cbs));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-dis-cb"));
	sk_ui_value_equals(t, "ix-dis-cb", "0");

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sl, &border, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 4.0f, border.y + border.height * 0.5f, border.x + border.width - 4.0f,
															  border.y + border.height * 0.5f, 4u, 1.0f / 60.0f));
	value = ui->slider_get_value(ctx, sl);
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, value);

	/* typeInto focuses then injects text; disabled handler must ignore. */
	(void)sk_ui_type_into(t, "ix-dis-ti", "xx");
	sk_ui_value_equals(t, "ix-dis-ti", "keep");

	g_ix_btn_clicks = 0;
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-dis-btn"));
	TEST_ASSERT_EQUAL_INT(0, g_ix_btn_clicks);
	(void)clicks;
}

/* -------------------------------------------------------------------------- */
/* Soft-render capture after interaction (artifact path for vision handoff)   */
/* -------------------------------------------------------------------------- */

SK_UI_TEST(ix_post_interaction_frame_capture) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t cb;
	sk_ui_node_t sl;

	cb = ui->widget_checkbox(ctx, root, 0, "ix-cap-cb");
	sl = ui->widget_slider(ctx, root, 0.0f, 1.0f, 0.25f, "ix-cap-sl");
	ix_place(t, cb, 20.0f, 20.0f, 22.0f, 22.0f);
	ix_place(t, sl, 20.0f, 56.0f, 180.0f, 22.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "ix-cap-cb"));
	{
		sk_ui_rect_t border;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sl, &border, NULL));
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, border.x + 2.0f, border.y + border.height * 0.5f, border.x + border.width * 0.6f,
																  border.y + border.height * 0.5f, 5u, 1.0f / 60.0f));
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "ix-cap-cb", "1");
	TEST_ASSERT_TRUE(ui->slider_get_value(ctx, sl) > 0.4f);

	TEST_ASSERT_EQUAL_INT(0, sk_ui_test_capture_frame(t, "post_ix"));
	TEST_ASSERT_TRUE(t->fail_frame_path[0] != '\0');
	TEST_ASSERT_TRUE(strstr(t->fail_frame_path, ".png") != NULL);
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
