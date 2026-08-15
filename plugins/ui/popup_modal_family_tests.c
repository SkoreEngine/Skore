/**
 * @file popup_modal_family_tests.c
 * @brief Headless automation for the editor Popup / Modal family (APX-347).
 *
 * Opens a context menu by code, asserts a click behind a modal does not
 * reach the widget under it, and covers escape / CloseCurrentPopup. Steps
 * frames deterministically through the SK_UI_TEST harness. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void pm_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static i32 pm_key(sk_ui_test_t* t, i32 key) {
	const sk_ui_api_t* ui = t->ui;
	i32 rc = ui->test_engine_key(t->engine, key, 1, SK_UI_MOD_NONE);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui->test_engine_key(t->engine, key, 0, SK_UI_MOD_NONE);
}

/**
 * Headless: open a context menu by code, click behind a modal does not
 * activate the widget under it, Escape / close-current dismiss popups.
 */
SK_UI_TEST(popup_modal_family_open_block_escape) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t menu;
	sk_ui_node_t cut;
	sk_ui_node_t behind;
	sk_ui_node_t modal;
	sk_ui_node_t ok;
	sk_ui_rect_t br;

	host = ui->widget_view(ctx, root, "pmf-host");
	pm_place(t, host, 8.0f, 8.0f, 96.0f, 28.0f);
	menu = ui->widget_popup_menu(ctx, host, "pmf-menu");
	cut = ui->widget_menu_item(ctx, menu, "Cut", "pmf-cut");
	(void)ui->widget_menu_item(ctx, menu, "Copy", "pmf-copy");
	(void)ui->widget_menu_separator(ctx, menu, "pmf-sep");
	(void)ui->widget_menu_item(ctx, menu, "Delete", "pmf-del");

	behind = ui->widget_button(ctx, root, "Under", "pmf-under");
	pm_place(t, behind, 16.0f, 220.0f, 80.0f, 24.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, menu));

	/* Open a context menu by code (edge-triggered OpenPopup). */
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, menu));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, menu));
	sk_ui_item_exists(t, "pmf-cut");
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "pmf-cut"));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_item_clicked(ctx, cut));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, menu));

	/* Re-open, Escape / close-current dismisses. */
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, menu));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->popup_get_open(ctx, menu));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, pm_key(t, SK_UI_KEY_ESCAPE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_get_open(ctx, menu));

	modal = ui->widget_modal(ctx, root, "Cannot save", "pmf-modal", NULL, SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE);
	(void)ui->widget_text(ctx, ui->modal_body(ctx, modal), "Disk is write-protected.", "pmf-msg");
	ok = ui->widget_button(ctx, ui->modal_button_row(ctx, modal), "OK", "pmf-ok");
	TEST_ASSERT_EQUAL_INT(0, ui->button_set_size(ctx, ok, 120.0f, 0.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->set_item_default_focus(ctx, ok));
	TEST_ASSERT_EQUAL_INT(0, ui->popup_open(ctx, modal));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->modal_get_open(ctx, modal));
	sk_ui_item_state(t, "pmf-ok", (u32)SK_UI_STATE_FOCUSED, 0u);

	/* Click the button that sits under the dim: must not activate. */
	(void)ui->button_clicked(ctx, behind);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, behind, &br, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, br.x + 12.0f, br.y + 10.0f));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 0, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->button_clicked(ctx, behind));
	TEST_ASSERT_EQUAL_INT(1, ui->modal_get_open(ctx, modal));

	/* Escape closes the modal. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, pm_key(t, SK_UI_KEY_ESCAPE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->modal_get_open(ctx, modal));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
