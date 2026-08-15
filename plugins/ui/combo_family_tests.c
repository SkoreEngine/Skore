/**
 * @file combo_family_tests.c
 * @brief Headless automation for the editor Combo / ListBox family (APX-353).
 *
 * Opens the combo popup, picks items by click and keyboard through the
 * shared test-engine harness (APX-259/261). Asserts bound-int write-back,
 * changed, open/close, and list-box height. No rendering.
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

static i32 cf_key(sk_ui_test_t* t, i32 key, u32 mods) {
	const sk_ui_api_t* ui = t->ui;
	i32 rc;
	rc = ui->test_engine_key(t->engine, key, 1, mods);
	if (rc != SK_UI_TEST_OK) {
		return rc;
	}
	return ui->test_engine_key(t->engine, key, 0, mods);
}

/**
 * Headless: open the popup, pick items by click and by arrow+enter, assert
 * bound int write-back, changed, close-on-pick, and list-box item-count height.
 */
SK_UI_TEST(combo_family_open_pick_keyboard_listbox) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t combo;
	sk_ui_node_t begin;
	sk_ui_node_t popup;
	sk_ui_node_t custom;
	sk_ui_node_t box;
	sk_ui_node_t content;
	sk_ui_node_t hist;
	i32 shading = 0;

	combo = ui->widget_combo(ctx, root, "##shadingmodel", &shading, "Default Lit\0Unlit\0", -1, "cf-shade");
	begin = ui->widget_begin_combo(ctx, root, "##enum", "Opaque", SK_UI_COMBO_FLAG_NONE, "cf-begin");
	popup = ui->combo_popup(ctx, begin);
	custom = ui->widget_selectable(ctx, popup, "Opaque", 1, SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH, "cf-begin/0", 0.0f, 0.0f);
	box = ui->widget_list_box(ctx, root, "###hist", 200.0f, 0.0f, 4, "cf-hist");
	content = ui->list_box_content(ctx, box);
	hist = ui->widget_selectable(ctx, content, "Create Entity", 0, SK_UI_SELECTABLE_FLAG_SPAN_AVAIL_WIDTH, "cf-hist/0", 0.0f, 0.0f);

	cf_place(t, combo, 16.0f, 16.0f, 180.0f, 24.0f);
	cf_place(t, begin, 16.0f, 48.0f, 180.0f, 24.0f);
	cf_place(t, box, 16.0f, 88.0f, 200.0f, ui->list_box_get_height(ctx, box));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "cf-shade");
	sk_ui_item_exists(t, "cf-begin");
	sk_ui_item_exists(t, "cf-hist");
	sk_ui_value_equals(t, "cf-shade", "Default Lit");
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, combo));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_changed(ctx, combo));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 4.0f * SK_UI_COMBO_ITEM_HEIGHT, ui->list_box_get_height(ctx, box));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(custom));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(hist));

	/* Open the popup and pick Unlit by code (task: pick items by code, headless). */
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, combo, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_get_open(ctx, combo));
	sk_ui_item_exists(t, "cf-shade/i1");
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_selected(ctx, combo, 1));
	TEST_ASSERT_EQUAL_INT(1, shading);
	TEST_ASSERT_EQUAL_STRING("Unlit", ui->combo_get_preview(ctx, combo));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, combo, 0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, combo));
	sk_ui_value_equals(t, "cf-shade", "Unlit");

	/* Keyboard: focus, open with Down, arrow to first item, Enter commits. */
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, ui->test_engine_focus(t->engine, "cf-shade"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, cf_key(t, SK_UI_KEY_DOWN, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_get_open(ctx, combo));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, cf_key(t, SK_UI_KEY_UP, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, cf_key(t, SK_UI_KEY_ENTER, SK_UI_MOD_NONE));
	TEST_ASSERT_EQUAL_INT(0, shading);
	TEST_ASSERT_EQUAL_STRING("Default Lit", ui->combo_get_preview(ctx, combo));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_changed(ctx, combo));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, combo));

	/* BeginCombo custom body: open / close by API. */
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, begin, 1));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(1, ui->combo_get_open(ctx, begin));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_set_open(ctx, begin, 0));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	TEST_ASSERT_EQUAL_INT(0, ui->combo_get_open(ctx, begin));

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_click_item(t, "cf-hist/0"));
	TEST_ASSERT_EQUAL_INT(1, ui->selectable_changed(ctx, hist));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
