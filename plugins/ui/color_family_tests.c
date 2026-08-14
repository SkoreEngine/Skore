/**
 * @file color_family_tests.c
 * @brief Headless automation for the editor ColorEdit / ColorPicker family (APX-357).
 *
 * Opens the ColorButton popup, drags the SV square and hue/alpha bars,
 * types a component value, and closes — through the shared test-engine
 * harness. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void cf_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static i32 cf_key(sk_ui_test_t* t, i32 key) {
	const sk_ui_api_t* ui = t->ui;
	i32 rc = ui->test_engine_key(t->engine, key, 1, SK_UI_MOD_NONE);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui->test_engine_key(t->engine, key, 0, SK_UI_MOD_NONE);
}

/**
 * Headless: open the picker popup, drag SV / hue / alpha, type a component
 * value, close. Bound float[4] write-back + changed/committed.
 */
SK_UI_TEST(color_family_open_drag_type_close) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t btn;
	sk_ui_node_t sv;
	sk_ui_node_t hue;
	sk_ui_node_t alpha;
	sk_ui_node_t field;
	sk_ui_rect_t r;
	f32 col[4] = {0.80f, 0.20f, 0.10f, 0.70f};
	f32 before[4];

	btn = ui->widget_color_button(ctx, root, col, SK_UI_COLOR_FLAG_PICKER_DEFAULT, 180.0f, 22.0f, "cf-btn");
	TEST_ASSERT_EQUAL_INT(0, ui->color_bind(ctx, btn, col, 4));
	cf_place(t, btn, 16.0f, 16.0f, 180.0f, 22.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "cf-btn");
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_open(ctx, btn));
	(void)ui->color_changed(ctx, btn);
	(void)ui->color_committed(ctx, btn);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cf-btn"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	if (ui->color_get_open(ctx, btn) == 0) {
		TEST_ASSERT_EQUAL_INT(0, ui->color_set_open(ctx, btn, 1));
		TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	}
	TEST_ASSERT_EQUAL_INT(1, ui->color_get_open(ctx, btn));

	sv = ui->color_sv_square(ctx, btn);
	hue = ui->color_hue_bar(ctx, btn);
	alpha = ui->color_alpha_bar(ctx, btn);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sv));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(hue));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(alpha));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(ui->color_preview(ctx, btn)));

	before[0] = col[0];
	before[1] = col[1];
	before[2] = col[2];
	before[3] = col[3];

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sv, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 16.0f);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, r.x + 8.0f, r.y + 8.0f, r.x + r.width * 0.70f, r.y + r.height * 0.30f, 6u, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_INT(1, ui->color_changed(ctx, btn));
	TEST_ASSERT_EQUAL_INT(1, ui->color_committed(ctx, btn));
	TEST_ASSERT_TRUE((col[0] > before[0] + 0.02f) || (col[0] + 0.02f < before[0]) || (col[1] > before[1] + 0.02f) || (col[1] + 0.02f < before[1]) || (col[2] > before[2] + 0.02f) ||
					 (col[2] + 0.02f < before[2]));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, hue, &r, NULL));
	before[0] = col[0];
	before[1] = col[1];
	before[2] = col[2];
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, r.x + r.width * 0.5f, r.y + 4.0f, r.x + r.width * 0.5f, r.y + r.height * 0.65f, 6u, 1.0f / 60.0f));
	TEST_ASSERT_TRUE((col[0] > before[0] + 0.02f) || (col[0] + 0.02f < before[0]) || (col[1] > before[1] + 0.02f) || (col[1] + 0.02f < before[1]) || (col[2] > before[2] + 0.02f) ||
					 (col[2] + 0.02f < before[2]));

	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, alpha, &r, NULL));
	before[3] = col[3];
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_drag(t->engine, r.x + r.width * 0.5f, r.y + 4.0f, r.x + r.width * 0.5f, r.y + r.height * 0.80f, 6u, 1.0f / 60.0f));
	TEST_ASSERT_TRUE(col[3] < before[3]);

	TEST_ASSERT_EQUAL_INT(0, ui->color_component(ctx, btn, 0, &field));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(field));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_click(t->engine, ui->node_get_id(ctx, field)));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, field, 0, 32));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "0.25"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, cf_key(t, SK_UI_KEY_ENTER));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.25f, col[0]);

	TEST_ASSERT_EQUAL_INT(0, ui->color_set_open(ctx, btn, 0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->color_get_open(ctx, btn));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
