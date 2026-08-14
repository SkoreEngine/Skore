/**
 * @file tab_bar_family_tests.c
 * @brief Headless automation for the editor TabBar family (APX-348).
 *
 * Clicks page tabs, a per-tab close (caller-owned bool), and the trailing
 * TabItemButton ('+') through the SK_UI_TEST harness. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void tb_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static i32 tb_hidden(sk_ui_test_t* t, sk_ui_node_t node) {
	sk_ui_prop_value_t pv;
	if (t->ui->node_get_prop(t->ctx, node, "hidden", &pv) != 0 || pv.type != SK_UI_PROP_I32) {
		return 0;
	}
	return pv.data.i32_value != 0 ? 1 : 0;
}

/**
 * Headless: click tabs by code, click a close button and assert the caller
 * bool cleared, click '+' and assert selection unchanged.
 */
SK_UI_TEST(tab_bar_family_click_close_plus) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t host;
	sk_ui_node_t bar;
	sk_ui_node_t scene;
	sk_ui_node_t game;
	sk_ui_node_t plus;
	sk_ui_node_t scene_body;
	sk_ui_node_t game_body;
	sk_ui_node_t xbtn;
	i32 scene_open = 1;
	i32 selected = 0;
	sk_ui_style_props_t p;

	host = ui->widget_vertical(ctx, root, "tbf-host");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt(360.0f);
	p.layout.height = sk_ui_pt(140.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, host, &p));
	tb_place(t, host, 8.0f, 8.0f, 360.0f, 140.0f);

	bar = ui->widget_tab_bar(ctx, host, "tbf-bar");
	scene = ui->widget_tab_item(ctx, bar, "Scene", "tbf-scene", &scene_open, SK_UI_TAB_ITEM_FLAG_NONE);
	game = ui->widget_tab_item(ctx, bar, "Game", "tbf-game", NULL, SK_UI_TAB_ITEM_FLAG_NONE);
	plus = ui->widget_tab_button(ctx, bar, "+", "tbf-plus");
	scene_body = ui->tab_body(ctx, scene);
	game_body = ui->tab_body(ctx, game);
	(void)ui->widget_text(ctx, scene_body, "Scene page", "tbf-scene-txt");
	(void)ui->widget_text(ctx, game_body, "Game page", "tbf-game-txt");

	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_bind_selected(ctx, bar, &selected));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_set_selected(ctx, bar, 0));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "tbf-scene");
	sk_ui_item_exists(t, "tbf-game");
	sk_ui_item_exists(t, "tbf-plus");
	xbtn = ui->tab_close_button(ctx, scene);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(xbtn));
	sk_ui_item_exists(t, "tbf-scene-close");

	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, scene));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, game));
	TEST_ASSERT_EQUAL_INT(0, tb_hidden(t, scene_body));
	TEST_ASSERT_EQUAL_INT(1, tb_hidden(t, game_body));

	/* Click Game: selection + body swap. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tbf-game"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, scene));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, game));
	TEST_ASSERT_EQUAL_INT(1, selected);
	TEST_ASSERT_EQUAL_INT(1, tb_hidden(t, scene_body));
	TEST_ASSERT_EQUAL_INT(0, tb_hidden(t, game_body));

	/* Click Scene back. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tbf-scene"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, scene));
	TEST_ASSERT_EQUAL_INT(0, selected);

	/* Close button writes the caller bool. */
	TEST_ASSERT_EQUAL_INT(1, scene_open);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tbf-scene-close"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, scene_open);
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_open(ctx, scene));

	/* After Scene closed, Game is the remaining page. */
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, game));
	TEST_ASSERT_EQUAL_INT(0, selected);

	/* '+' is clickable but never becomes the selection. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "tbf-plus"));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_clicked(ctx, plus));
	TEST_ASSERT_EQUAL_INT(1, ui->tab_get_active(ctx, game));
	TEST_ASSERT_EQUAL_INT(0, ui->tab_get_active(ctx, plus));
	TEST_ASSERT_EQUAL_INT(0, selected);
	TEST_ASSERT_EQUAL_INT(0, ui->tab_bar_get_selected(ctx, bar));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
