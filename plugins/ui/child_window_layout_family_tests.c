/**
 * @file child_window_layout_family_tests.c
 * @brief Headless automation for the editor Child / Window / Layout family (APX-345).
 *
 * Drives scroll, a click into a disabled subtree (must stay inert), and a
 * ResizeX child drag through the shared test-engine harness. Steps frames
 * deterministically. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void cwl_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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
 * Headless: scroll a child (query + scroll-to-bottom), click a button inside
 * a disabled subtree and assert it is inert, then drag a ResizeX handle.
 */
SK_UI_TEST(child_window_layout_family_scroll_disabled_resizex) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t win;
	sk_ui_node_t content;
	sk_ui_node_t sv;
	sk_ui_node_t dis_host;
	sk_ui_node_t btn;
	sk_ui_node_t rx;
	sk_ui_node_t handle;
	sk_ui_rect_t border;
	i32 open = 1;
	f32 sx = 0.0f;
	f32 sy = 0.0f;
	f32 start_w;

	win = ui->widget_window(ctx, root, "Console", "cwl-win", &open);
	content = ui->editor_window_content(ctx, win);
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(content), "window content missing");
	cwl_place(t, win, 200.0f, 8.0f, 180.0f, 72.0f);

	sv = ui->widget_child(ctx, root, "cwl-sv", 200.0f, 80.0f, SK_UI_CHILD_FLAG_HORIZONTAL_SCROLLBAR);
	cwl_place(t, sv, 8.0f, 8.0f, 180.0f, 80.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_set_content_size(ctx, sv, 400.0f, 240.0f));

	dis_host = ui->widget_group(ctx, root, "cwl-dis");
	btn = ui->widget_button(ctx, dis_host, "Lock", "cwl-lock");
	cwl_place(t, dis_host, 16.0f, 180.0f, 80.0f, 28.0f);
	cwl_place(t, btn, 16.0f, 180.0f, 80.0f, 28.0f);

	rx = ui->widget_child(ctx, root, "cwl-rx", 96.0f, 48.0f, SK_UI_CHILD_FLAG_BORDER | SK_UI_CHILD_FLAG_RESIZE_X);
	cwl_place(t, rx, 16.0f, 220.0f, 96.0f, 48.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "cwl-win");
	sk_ui_item_exists(t, "cwl-sv");
	sk_ui_item_exists(t, "cwl-lock");
	sk_ui_item_exists(t, "cwl-rx");

	/* Scroll: wheel then SetScrollHereY(1) console auto-scroll. */
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK, ui->test_engine_scroll(t->engine, "cwl-sv", 0.0f, -3.0f), t->last_error);
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_TRUE_MESSAGE(sy > 0.0f, "wheel did not change scroll_y");
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_scroll_to_bottom(ctx, sv));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->scroll_view_get_scroll(ctx, sv, &sx, &sy));
	TEST_ASSERT_TRUE_MESSAGE(sy > 40.0f, "scroll_to_bottom did not reach the end");

	/* Disabled subtree: grey + no input. */
	TEST_ASSERT_EQUAL_INT(0, ui->set_disabled(ctx, dis_host, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->node_is_enabled(ctx, btn));
	(void)ui->button_clicked(ctx, btn);
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK, sk_ui_click_item(t, "cwl-lock"), t->last_error);
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_pressed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(0, ui->button_is_active(ctx, btn));

	/* ResizeX: drag the child's right edge (handle chrome may be overlayed). */
	handle = ui->query_by_widget(ctx, rx, "child_resize");
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(handle), "ResizeX handle missing");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, rx, &border, NULL));
	start_w = border.width;
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_TEST_OK,
								  ui->test_engine_drag(t->engine, border.x + border.width - 2.0f, border.y + border.height * 0.5f, border.x + border.width + 48.0f,
													   border.y + border.height * 0.5f, 6u, 1.0f / 60.0f),
								  t->last_error);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, rx, &border, NULL));
	TEST_ASSERT_TRUE_MESSAGE(border.width > start_w + 8.0f, "ResizeX drag did not widen the child");

	/* Named window still exposes the close flag (click path is unit-tested). */
	TEST_ASSERT_EQUAL_INT(1, open);
	TEST_ASSERT_EQUAL_INT(1, ui->editor_window_get_open(ctx, win));
	TEST_ASSERT_TRUE_MESSAGE(sk_ui_node_is_valid(ui->editor_window_close_button(ctx, win)), "close button missing");
	TEST_ASSERT_EQUAL_INT(0, ui->editor_window_set_open(ctx, win, 0));
	TEST_ASSERT_EQUAL_INT(0, open);
	TEST_ASSERT_EQUAL_INT(0, ui->editor_window_get_open(ctx, win));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
