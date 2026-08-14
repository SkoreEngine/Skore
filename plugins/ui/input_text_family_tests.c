/**
 * @file input_text_family_tests.c
 * @brief Headless automation for the editor InputText family (APX-342).
 *
 * Injects keystrokes and text events through the shared test-engine harness
 * (APX-259/261). Asserts buffer contents, caret/selection, and
 * changed/committed return values across frames. Covers focus gain/loss and
 * escape-to-revert. No rendering.
 */

#include "ui_test.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

static void it_place(sk_ui_test_t* t, sk_ui_node_t node, f32 x, f32 y, f32 w, f32 h) {
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

static i32 it_key(sk_ui_test_t* t, i32 key, u32 mods) {
	const sk_ui_api_t* ui = t->ui;
	i32 rc;
	rc = ui->test_engine_key(t->engine, key, 1, mods);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui->test_engine_key(t->engine, key, 0, mods);
}

/**
 * Headless: type/backspace/selection, EnterReturnsTrue vs live-edit,
 * focus gain/loss commit, escape-to-revert, multiline newline, numeric
 * commit. Asserts buffer, caret/selection, and changed/committed edges
 * across frames.
 */
SK_UI_TEST(input_text_family_keystrokes_focus_commit_revert) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t ti;
	sk_ui_node_t ent;
	sk_ui_node_t ml;
	sk_ui_node_t num;
	sk_ui_node_t other;
	f32 fv = 1.0f;
	i32 a = 0;
	i32 b = 0;

	ti = ui->widget_text_input(ctx, root, "", "it-ti");
	ent = ui->widget_text_input(ctx, root, "old", "it-ent");
	ml = ui->widget_text_input_multiline(ctx, root, "a\nb", 120.0f, 48.0f, "it-ml");
	num = ui->widget_input_float(ctx, root, &fv, "it-num");
	other = ui->widget_button(ctx, root, "x", "it-other");

	it_place(t, ti, 8.0f, 8.0f, 160.0f, 24.0f);
	it_place(t, ent, 8.0f, 40.0f, 160.0f, 24.0f);
	it_place(t, ml, 8.0f, 72.0f, 160.0f, 48.0f);
	it_place(t, num, 8.0f, 128.0f, 80.0f, 24.0f);
	it_place(t, other, 200.0f, 8.0f, 32.0f, 20.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "it-ti");
	sk_ui_item_exists(t, "it-ent");
	sk_ui_item_exists(t, "it-ml");
	sk_ui_item_exists(t, "it-num");

	/* Type + backspace + selection replace. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_type_into(t, "it-ti", "Hello"));
	sk_ui_value_equals(t, "it-ti", "Hello");
	TEST_ASSERT_EQUAL_INT(5, ui->text_input_get_caret(ctx, ti));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_changed(ctx, ti));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_changed(ctx, ti));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_BACKSPACE, SK_UI_MOD_NONE));
	sk_ui_value_equals(t, "it-ti", "Hell");
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_LEFT, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_LEFT, SK_UI_MOD_SHIFT));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_get_selection(ctx, ti, &a, &b));
	TEST_ASSERT_TRUE(a != b);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "p"));
	sk_ui_value_equals(t, "it-ti", "Hep");
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "it-ti", "Hep");

	/* EnterReturnsTrue: live keys do not set changed; Enter does. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_flags(ctx, ent, SK_UI_INPUT_TEXT_FLAG_ENTER_RETURNS_TRUE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "it-ent"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "it-ent", (u32)SK_UI_STATE_FOCUSED, 0u);
	(void)ui->text_input_changed(ctx, ent);
	(void)ui->text_input_committed(ctx, ent);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "x"));
	sk_ui_value_equals(t, "it-ent", "oldx");
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_changed(ctx, ent));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_ENTER, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_changed(ctx, ent));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_committed(ctx, ent));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));

	/* Focus loss after an edit: committed (DeactivatedAfterEdit). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "it-ti"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "!"));
	sk_ui_value_equals(t, "it-ti", "Hep!");
	(void)ui->text_input_committed(ctx, ti);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "it-other"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->text_input_committed(ctx, ti));
	sk_ui_item_state(t, "it-ti", 0u, (u32)SK_UI_STATE_FOCUSED);

	/* Escape reverts to the focus-in snapshot. */
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_text(ctx, ti, "snap"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "it-ti"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "x"));
	sk_ui_value_equals(t, "it-ti", "snapx");
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_ESCAPE, SK_UI_MOD_NONE));
	sk_ui_value_equals(t, "it-ti", "snap");
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_state(t, "it-ti", 0u, (u32)SK_UI_STATE_FOCUSED);

	/* Multiline: Enter inserts a newline (does not commit). */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "it-ml"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_ENTER, SK_UI_MOD_NONE));
	TEST_ASSERT_TRUE(strlen(ui->text_input_get_text(ctx, ml)) >= 4u);
	TEST_ASSERT_TRUE(strchr(ui->text_input_get_text(ctx, ml), '\n') != NULL);
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_committed(ctx, ml));

	/* Numeric: type + enter writes the bound float. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "it-num"));
	TEST_ASSERT_EQUAL_INT(0, ui->text_input_set_selection(ctx, num, 0, 8));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_text(t->engine, "2.5"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, it_key(t, SK_UI_KEY_ENTER, SK_UI_MOD_NONE));
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, fv);
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
