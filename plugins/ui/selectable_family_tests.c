/**
 * @file selectable_family_tests.c
 * @brief Headless automation for the editor Selectable family (APX-352).
 *
 * Drives click / double-click / hover through the shared test-engine harness
 * (APX-259/261). Asserts changed, selected look, disabled swallow, and
 * AllowDoubleClick. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void sf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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
 * Headless: hover, click (changed), double-click distinguished from single,
 * disabled swallows input, selected look is caller-owned.
 */
SK_UI_TEST(selectable_family_click_double_hover) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t row;
	sk_ui_node_t sel;
	sk_ui_node_t dis;
	sk_ui_node_t dbl;

	row = ui->widget_selectable(ctx, root, "Create Entity", 0, SK_UI_SELECTABLE_FLAG_NONE, "sf-hist", 0.0f, 0.0f);
	sel = ui->widget_selectable(ctx, root, "Default Lit", 1, SK_UI_SELECTABLE_FLAG_NONE, "sf-enum", 0.0f, 0.0f);
	dis = ui->widget_selectable(ctx, root, "ReadOnly", 0, SK_UI_SELECTABLE_FLAG_DISABLED, "sf-dis", 0.0f, 0.0f);
	dbl = ui->widget_selectable(ctx, root, "Hero", 0, SK_UI_SELECTABLE_FLAG_ALLOW_DOUBLE_CLICK, "sf-ent", 0.0f, 0.0f);

	sf_place(t, row, 16.0f, 16.0f, 200.0f, 22.0f);
	sf_place(t, sel, 16.0f, 44.0f, 200.0f, 22.0f);
	sf_place(t, dis, 16.0f, 72.0f, 200.0f, 22.0f);
	sf_place(t, dbl, 16.0f, 100.0f, 200.0f, 22.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "sf-hist");
	sk_ui_item_exists(t, "sf-enum");
	sk_ui_item_exists(t, "sf-dis");
	sk_ui_item_exists(t, "sf-ent");

	TEST_ASSERT_EQUAL_INT(0, ui->selectable_is_hovered(ctx, row));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_changed(ctx, row));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_get_selected(ctx, sel));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_get_disabled(ctx, dis));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_hover_item(t, "sf-hist"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_is_hovered(ctx, row));
	sk_ui_item_state(t, "sf-hist", (u32)SK_UI_STATE_HOVER, (u32)SK_UI_STATE_ACTIVE);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "sf-hist"));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, row));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_changed(ctx, row));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_double_clicked(ctx, row));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "sf-dis"));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_changed(ctx, dis));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_is_active(ctx, dis));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "sf-ent"));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, dbl));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_double_clicked(ctx, dbl));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_double_click(t->engine, "sf-ent"));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, dbl));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_double_clicked(ctx, dbl));

	TEST_ASSERT_EQUAL_INT(0, ui->selectable_set_selected(ctx, sel, 0));
	TEST_ASSERT_EQUAL_INT(0, ui->selectable_get_selected(ctx, sel));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "sf-enum"));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, sel));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
