/**
 * @file text_family_tests.c
 * @brief Headless automation for the editor Text family (APX-340).
 *
 * Builds plain / wrapped / coloured / disabled / bullet / separator labels
 * through the shared test-engine harness (APX-259/261), asserts the computed
 * layout extent (node_get_abs_rect after harness_step), and mutates the
 * source string across frames to prove the label relayouts. No rendering.
 */

#include "ui_test.h"

#include "testdata/skore_test_font_ttf.h"

#include <string.h>

#ifdef SK_TESTS
#if defined(SK_UI_PLUGIN_BUILD)

/** Set a fixed width on one axis only; the other axis stays auto. */
static void tf_set_width(sk_ui_test_t* t, sk_ui_node_t node, f32 w) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH;
	p.layout.width = sk_ui_pt(w);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.max_width = sk_ui_pt(w);
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(t->ctx, node, &p));
}

/**
 * Headless: computed layout/extent for every Text variant, wrapping at a
 * fixed width, and content updates across frames (set_text / set_text_range).
 */
SK_UI_TEST(text_family_layout_extent_and_content_updates) {
	const sk_ui_api_t* ui = t->ui;
	sk_ui_context_t* ctx = t->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t plain;
	sk_ui_node_t wrapped;
	sk_ui_node_t colored;
	sk_ui_node_t disabled;
	sk_ui_node_t bullet;
	sk_ui_node_t sep;
	sk_ui_font_system_t* fs;
	sk_ui_font_t* font;
	sk_ui_harness_t* harness;
	sk_ui_style_props_t p;
	sk_ui_rect_t r;
	sk_ui_color_t col;
	f32 plain_h;
	f32 w_before;
	f32 w_after;

	/* Real font so glyph advances drive the extents. */
	fs = ui->font_system_create(NULL);
	TEST_ASSERT_NOT_NULL(fs);
	font = ui->font_load_memory(fs, skore_test_font_ttf, (u32)sizeof(skore_test_font_ttf));
	TEST_ASSERT_NOT_NULL(font);
	harness = ui->test_engine_harness(t->engine);
	TEST_ASSERT_NOT_NULL(harness);
	ui->harness_set_font(harness, fs, font);

	/* Column root, no cross-axis stretch: each label keeps its intrinsic width. */
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_ROW_GAP;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.row_gap = 8.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, root, &p));

	plain = ui->widget_text(ctx, root, "Hello", "tf-plain");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(plain));
	wrapped = ui->widget_text_wrapped(ctx, root, "Animator validation warning text that must wrap", "tf-wrapped");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(wrapped));
	colored = ui->widget_text_colored(ctx, root, "Created", sk_ui_rgba(0.1f, 0.8f, 0.1f, 1.0f), "tf-colored");
	disabled = ui->widget_text_disabled(ctx, root, "No packs", "tf-disabled");
	bullet = ui->widget_bullet_text(ctx, root, "bullet entry", "tf-bullet");
	sep = ui->widget_separator_text(ctx, root, "Resource Info", "tf-sep");
	tf_set_width(t, wrapped, 120.0f);

	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_item_exists(t, "tf-plain");
	sk_ui_item_exists(t, "tf-wrapped");
	sk_ui_item_exists(t, "tf-colored");
	sk_ui_item_exists(t, "tf-disabled");
	sk_ui_item_exists(t, "tf-bullet");
	sk_ui_item_exists(t, "tf-sep");

	/* Plain: measured extent, single line. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plain, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 0.0f);
	TEST_ASSERT_TRUE(r.height > 0.0f);
	plain_h = r.height;
	sk_ui_item_rect(t, "tf-plain", r.x, r.y, r.width, r.height, 0.5f);

	/* Wrapped at a 120px column: height grows to several lines. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, wrapped, &r, NULL));
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, r.width);
	TEST_ASSERT_TRUE(r.height >= plain_h * 3.0f);

	/* Coloured: computed colour matches the caller RGBA (TextColored). */
	TEST_ASSERT_EQUAL_INT(0, ui->text_get_color(ctx, colored, &col));
	TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.8f, col.g);
	TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.1f, col.r);

	/* Disabled: state bit lives on the node (class variant dims paint). */
	TEST_ASSERT_EQUAL_INT(1, ui->text_get_disabled(ctx, disabled));
	sk_ui_item_state(t, "tf-disabled", (u32)SK_UI_STATE_DISABLED, 0u);

	/* Bullet + separator: non-empty measured extents. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, bullet, &r, NULL));
	TEST_ASSERT_TRUE(r.width >= 48.0f);
	TEST_ASSERT_TRUE(r.height > 0.0f);
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, sep, &r, NULL));
	TEST_ASSERT_TRUE(r.width >= 300.0f);
	TEST_ASSERT_TRUE(r.height >= 20.0f);

	/* Content update across frames: grow the string, relayout wider. */
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plain, &r, NULL));
	w_before = r.width;
	TEST_ASSERT_EQUAL_INT(0, ui->label_set_text(ctx, plain, "A much longer status line for the console"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "tf-plain", "A much longer status line for the console");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plain, &r, NULL));
	w_after = r.width;
	TEST_ASSERT_TRUE(w_after > w_before);

	/* Content update via TextUnformatted range: shrink back to a slice. */
	{
		const char hi[] = "Hi";
		TEST_ASSERT_EQUAL_INT(0, ui->text_set_text_range(ctx, plain, hi, hi + 2));
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "tf-plain", "Hi");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plain, &r, NULL));
	TEST_ASSERT_TRUE(r.width < w_after);

	/* UTF-8 source stays byte-exact across frames. */
	TEST_ASSERT_EQUAL_INT(0, ui->label_set_text(ctx, plain, "h\xc3\xa9llo \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
	TEST_ASSERT_EQUAL_INT(SK_UI_TEST_OK, sk_ui_test_step(t));
	sk_ui_value_equals(t, "tf-plain", "h\xc3\xa9llo \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(ctx, plain, &r, NULL));
	TEST_ASSERT_TRUE(r.width > 0.0f);

	ui->font_destroy(font);
	ui->font_system_destroy(fs);
}

#endif /* SK_UI_PLUGIN_BUILD */
#endif /* SK_TESTS */
