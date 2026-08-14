/**
 * @file menu_family_tests.c
 * @brief Headless automation for the editor MenuBar / Menu / MenuItem family (APX-346).
 *
 * Drives hover/click by code through bar -> menu -> submenu -> item, asserts
 * activation fires once, a disabled item never activates, and click-outside
 * closes. SK_UI_TEST harness; no rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void mf_set_size(sk_ui_test_t* t, sk_ui_node_t node, f32 w, f32 h) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_WIDTH | SK_UI_SP_MAX_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	p.layout.max_width = sk_ui_pt(w);
	p.layout.max_height = sk_ui_pt(h);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(t->ctx, node, &p));
}

/**
 * Headless: bar -> File -> Recent submenu -> item activates once; disabled
 * item never activates; click-outside closes the open menu.
 */
SK_UI_TEST(menu_family_bar_submenu_activate_disabled_outside) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t bar;
	sk_ui_node_t file_m;
	sk_ui_node_t popup;
	sk_ui_node_t open_it;
	sk_ui_node_t dis_it;
	sk_ui_node_t recent;
	sk_ui_node_t sub_pop;
	sk_ui_node_t proj;
	sk_ui_node_t host;
	sk_ui_node_t extra;
	sk_ui_node_t extra_item;

	bar = ui->widget_menu_bar(ctx, root, "mf-bar");
	mf_set_size(t, bar, 360.0f, 28.0f);
	file_m = ui->widget_menu(ctx, bar, "File", "mf-file");
	mf_set_size(t, file_m, 72.0f, 24.0f);
	popup = ui->menu_get_popup(ctx, file_m);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(popup));

	open_it = ui->widget_menu_item(ctx, popup, "Open", "mf-open");
	mf_set_size(t, open_it, 180.0f, 22.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_set_shortcut(ctx, open_it, "Ctrl+O"));

	dis_it = ui->widget_menu_item(ctx, popup, "Locked", "mf-locked");
	mf_set_size(t, dis_it, 180.0f, 22.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_set_enabled(ctx, dis_it, 0));

	recent = ui->widget_submenu(ctx, popup, "Recent", "mf-recent");
	mf_set_size(t, recent, 180.0f, 22.0f);
	sub_pop = ui->menu_get_popup(ctx, recent);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sub_pop));
	proj = ui->widget_menu_item(ctx, sub_pop, "Project A", "mf-proj");
	mf_set_size(t, proj, 140.0f, 22.0f);

	/* Standalone popup (workspace-type) with the same MenuItem factory. */
	host = ui->widget_view(ctx, root, "mf-pop-host");
	{
		sk_ui_layout_style_t ls;
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_style(ctx, host, &ls));
		ls.position = SK_UI_POSITION_ABSOLUTE;
		ls.left = sk_ui_pt(8.0f);
		ls.top = sk_ui_pt(200.0f);
		TEST_ASSERT_EQUAL_INT(0, ui->node_set_layout_style(ctx, host, &ls));
	}
	mf_set_size(t, host, 160.0f, 40.0f);
	extra = ui->widget_menu_popup(ctx, host, 0, "mf-extra-pop");
	extra_item = ui->widget_menu_item(ctx, extra, "Scene", "mf-scene");
	mf_set_size(t, extra_item, 140.0f, 22.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "mf-file");
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));

	/* bar -> menu */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "mf-file"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, file_m) != 0);
	sk_ui_item_exists(t, "mf-open");
	sk_ui_item_exists(t, "mf-recent");

	/* Disabled item never activates. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "mf-locked"));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_clicked(ctx, dis_it));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, file_m) != 0);

	/* Hover submenu open, then click the nested item once. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "mf-recent"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, recent) != 0);
	sk_ui_item_exists(t, "mf-proj");

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "mf-proj"));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_item_clicked(ctx, proj));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_item_clicked(ctx, proj));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));

	/* Re-open, then click outside closes. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "mf-file"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_TRUE(ui->menu_get_open(ctx, file_m) != 0);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_move(t->engine, 380.0f, 280.0f));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 1, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_mouse_button(t->engine, SK_UI_POINTER_BUTTON_LEFT, 0, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->menu_get_open(ctx, file_m));

	/* MenuItem inside a popup (not the bar). */
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(ctx, extra, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "mf-scene"));
	TEST_ASSERT_EQUAL_INT(1, ui->menu_item_clicked(ctx, extra_item));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
