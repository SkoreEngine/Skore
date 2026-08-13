/*
 * ui_integration.c — deterministic UI integration cases on the real GPU
 * path (APX-231).
 *
 * Each case drives the reusable capture harness (ui_capture_harness.c):
 * build a fixed scene on a fresh UI context, render exactly one frame on
 * the offscreen Vulkan target, read the pixels back, write the PNG artifact
 * under the shared test-artifact root, then
 *   1. compare against a committed golden PNG (plugins/ui/testdata) —
 *      on mismatch the compare helper leaves {name}_actual/_expected/_diff
 *      PNGs plus counts/delta/bbox in the log;
 *   2. run at least one *structural* assertion (solid / coverage / bbox /
 *      histogram) so the failure mode is diagnosable without opening the
 *      image: a moved widget keeps its colors (golden catches it, bbox
 *      tells you where it went), a vanished widget drops coverage, a
 *      wrong theme changes the histogram.
 *
 * Coverage:
 *   1. ui_integration_empty_frame   — cleared frame (empty draw list) with
 *      an explicit fixed clear color: solid + coverage + golden.
 *   2. ui_integration_single_button — one real widget (button) absolutely
 *      placed at a known position: bbox + coverage + solid + golden.
 *   3. ui_integration_layout_nested — panel > row > buttons + body box
 *      (nesting, flex row with space-between, stretch): bboxes for every
 *      widget + coverage + histogram + golden.
 *   4. ui_integration_text_glyphs  — TEXT node over a panel with the
 *      vendored DejaVuSans.ttf (APX-250 harness load_test_font; fixed
 *      pixel size / content scale / MSDF bake): text-color coverage +
 *      bbox inside the label box + panel coverage + golden.
 *   5. ui_integration_msdf_text_*  — MSDF pipeline at small/medium/large
 *      sizes (APX-266). Structural only (no golden): letterform ink, no
 *      solid tofu, no RGB channel fringes.
 *
 * Determinism: fixed viewport, fixed clear color, fixed logical time 0,
 * pinned DejaVuSans.ttf only (no system/built-in font fallback), no wall
 * clocks, no randomness (the harness contract). Every coordinate asserted
 * below is a fixed constant, never sampled randomly.
 *
 * Plugin lifetime: the harness tears down its own app context (and unloads
 * the plugin DLLs) before returning, so the sk_ui_api_t table it handed the
 * scene callback is dangling afterwards. Each test therefore holds its own
 * app context + UI API table alive for the whole test (same recipe as
 * tests/integration/ui_capture.c) and uses that table for the structural
 * assertions on the returned image.
 *
 * Skips cleanly when no Vulkan loader/ICD is present (harness RC_SKIPPED).
 *
 * Debug aid: export SK_UI_MEASURE=1 to print measured histograms, bboxes,
 * and solid stats for every scene before asserting — useful when blessing
 * goldens or when a structural assert fails and you want the numbers
 * without opening the image.
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"
#include "ui.h"
#include "ui_capture_harness.h"

/*
 * Unity (via test.h) may include <stdnoreturn.h>, which defines `noreturn`
 * as `_Noreturn`. Windows UCRT <stdlib.h> uses `__declspec(noreturn)`, which
 * breaks under clang-tidy when the macro expands to `_Noreturn` (same fix as
 * tests/integration/ui_render.c and core/offset_allocator.c).
 */
#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS

#ifndef SK_UI_GOLDEN_DIR
#define SK_UI_GOLDEN_DIR "plugins/ui/testdata"
#endif

/* -------------------------------------------------------------------------- */
/* Scene colors (exact RGBA8 of the float colors authored below)              */
/* -------------------------------------------------------------------------- */

#define UII_RGB(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)

#define UII_COLOR_ROOT_BG UII_RGB(26u, 31u, 41u)	 /* rgba(0.10,0.12,0.16) */
#define UII_COLOR_PANEL_BG UII_RGB(41u, 43u, 51u)	 /* ui-panel default */
#define UII_COLOR_BUTTON_BG UII_RGB(71u, 107u, 184u) /* ui-button default */
#define UII_COLOR_BODY_RED UII_RGB(217u, 76u, 51u)	 /* rgba(0.85,0.30,0.20) */
#define UII_COLOR_TEXT UII_RGB(242u, 242u, 250u)	 /* rgba(0.95,0.95,0.98) */

/* -------------------------------------------------------------------------- */
/* Test-side API handle (kept alive past the harness teardown)                */
/* -------------------------------------------------------------------------- */

typedef struct uii_env_t {
	sk_app_context_t* app;
	const sk_ui_api_t* ui;
} uii_env_t;

static i32 uii_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];
	i32 n;

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static void uii_env_init(uii_env_t* env) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	memset(env, 0, sizeof(*env));
	sk_app_boot_t boot = sk_app_init(0, NULL);
	env->app = boot.context;
	if (env->app == NULL) {
		return;
	}
	if (uii_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		boot.api->load_plugin(env->app, path);
	}
	env->ui = (const sk_ui_api_t*)boot.api->get_api(env->app, SK_UI_API_TYPE_ID);
}

static void uii_env_destroy(uii_env_t* env) {
	if (env->app != NULL) {
		sk_app_shutdown(env->app);
	}
	memset(env, 0, sizeof(*env));
}

/* -------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* -------------------------------------------------------------------------- */

/* Color match as a compound-literal lvalue (the assertion APIs take a
 * pointer; no temporary variable needed at call sites). */
#define UII_MATCH(rgba, tol)                                   \
	(&(sk_ui_color_match_t){.r = (u8)((rgba) & 0xFFu),         \
							.g = (u8)(((rgba) >> 8) & 0xFFu),  \
							.b = (u8)(((rgba) >> 16) & 0xFFu), \
							.a = (u8)(((rgba) >> 24) & 0xFFu), \
							.tolerance = (tol),                \
							.include_alpha = 1})

static sk_ui_region_t uii_region(u32 x0, u32 y0, u32 x1, u32 y1) {
	sk_ui_region_t r;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	return r;
}

/* One capture through the harness; maps the no-GPU case to TEST_IGNORE. */
static void uii_capture_user(const sk_ui_capture_harness_params_t* params, sk_ui_capture_scene_fn scene, void* user, sk_ui_cpu_image_t* out) {
	const i32 rc = sk_ui_capture_harness_capture(params, scene, user, out);
	if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI integration test");
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_NOT_NULL(out->pixels);
}

static void uii_capture(const sk_ui_capture_harness_params_t* params, sk_ui_capture_scene_fn scene, sk_ui_cpu_image_t* out) {
	uii_capture_user(params, scene, NULL, out);
}

/*
 * Golden compare against plugins/ui/testdata/{base}.png (strict: tolerance
 * 2 per channel, zero pixels beyond tolerance). On mismatch the helper
 * writes {base}_actual/_expected/_diff.png under the test-artifact root and
 * logs differ count / max channel delta / bbox. Blessing is opt-in via the
 * SK_UI_REGEN_GOLDENS env or params.update_golden (see docs/ui-plugin.md
 * §9.3 / §9.5).
 */
static void uii_assert_golden(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, const_chr_t base) {
	sk_ui_image_compare_params_t params;
	sk_ui_image_compare_stats_t stats;
	char golden_path[SK_FS_PATH_MAX];
	i32 rc;

	memset(&params, 0, sizeof(params));
	params.channel_tolerance = 2u;
	params.max_diff_fraction = 0.0f;
	params.name = base;
	params.update_golden = 0;
	memset(&stats, 0, sizeof(stats));
	snprintf(golden_path, sizeof(golden_path), SK_UI_GOLDEN_DIR "/%s.png", base);
	rc = ui->cpu_image_compare_golden(img, golden_path, &params, sk_test_filesystem_table(), &stats);
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_IMAGE_COMPARE_OK, rc, "golden compare failed; see {base}_actual/_expected/_diff.png under the artifact root");
}

/*
 * Debug aid (env SK_UI_MEASURE=1): print measured histogram, per-color
 * bboxes, and solid stats. Lets a human (or a vision-capable agent) turn
 * measured values into exact expectations without opening the PNGs.
 */
static void uii_debug_measure(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, const_chr_t tag) {
	const char* env;
	sk_ui_color_histogram_t hist;
	sk_ui_region_t full;
	u32 i;

	env = getenv("SK_UI_MEASURE");
	if (env == NULL || env[0] == '\0') {
		return;
	}
	full.x0 = 0u;
	full.y0 = 0u;
	full.x1 = img->width;
	full.y1 = img->height;
	printf("[uii:%s] image %ux%u\n", tag, img->width, img->height);
	memset(&hist, 0, sizeof(hist));
	if (ui->cpu_image_histogram(img, full, 8u, SK_UI_COLOR_HIST_MAX_ENTRIES, &hist) == 0) {
		for (i = 0u; i < hist.entry_count; ++i) {
			printf("[uii:%s] hist #%u: #%02X%02X%02X%02X count=%u fraction=%.4f\n", tag, i, hist.entries[i].r, hist.entries[i].g, hist.entries[i].b, hist.entries[i].a,
				   hist.entries[i].count, (f64)hist.entries[i].fraction);
		}
	}
	{
		static const u32 colors[] = {UII_COLOR_ROOT_BG, UII_COLOR_PANEL_BG, UII_COLOR_BUTTON_BG, UII_COLOR_BODY_RED, UII_COLOR_TEXT};
		static const char* names[] = {"root_bg", "panel_bg", "button_bg", "body_red", "text"};
		for (i = 0u; i < (u32)(sizeof(colors) / sizeof(colors[0])); ++i) {
			sk_ui_bbox_t bb;
			memset(&bb, 0, sizeof(bb));
			if (ui->cpu_image_find_bbox(img, full, UII_MATCH(colors[i], 2u), &bb) == 0 && bb.found != 0) {
				printf("[uii:%s] bbox %s: (%u,%u)-(%u,%u) pixels=%u\n", tag, names[i], bb.min_x, bb.min_y, bb.max_x, bb.max_y, bb.pixel_count);
			}
		}
	}
}

static void uii_free(sk_ui_cpu_image_t* img) {
	sk_ui_capture_harness_image_free(img);
}

/* -------------------------------------------------------------------------- */
/* Scene 1: cleared frame (empty draw list, explicit clear color)             */
/* -------------------------------------------------------------------------- */

/* No-op scene: keeps the empty-frame guarantee (nothing is added to the
 * tree). The tree root has no style and paints nothing. */
static i32 uii_scene_empty(sk_ui_capture_scene_t* scene, void* user) {
	(void)scene;
	(void)user;
	return 0;
}

SK_TEST(ui_integration_empty_frame) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	uii_env_t env;
	const sk_ui_api_t* ui;

	uii_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uii_env_destroy(&env);
		return;
	}

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_integration_empty_frame";
	params.width = 96u;
	params.height = 96u;
	params.clear_color_set = 1;
	params.clear_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 1.0f); /* fixed opaque black */
	params.time_seconds = 0.0;

	uii_capture(&params, uii_scene_empty, &img);
	TEST_ASSERT_EQUAL_UINT(96u, img.width);
	TEST_ASSERT_EQUAL_UINT(96u, img.height);
	TEST_ASSERT_EQUAL_UINT(4u, img.channels);

	/* Structural: the whole frame is uniformly the clear color (exact). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_solid(&img, uii_region(0u, 0u, 96u, 96u), UII_MATCH(UII_RGB(0u, 0u, 0u), 0u), NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(0u, 0u, 96u, 96u), UII_MATCH(UII_RGB(0u, 0u, 0u), 0u), 0.999f, 1.0f, NULL));

	uii_assert_golden(ui, &img, "ui_integration_empty_frame");
	uii_debug_measure(ui, &img, "empty_frame");
	uii_free(&img);
	uii_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Scene 2: one real widget (button) at a known position                      */
/* -------------------------------------------------------------------------- */

/*
 * 128x96 frame, clear white. Root has no background (clear shows through).
 * Button "OK" (ui-button class: fill (71,107,184), 1px border, radius 4)
 * absolutely placed at (16,16), 96x32. No font system → label text is
 * skipped by paint, so the golden is pure widget geometry.
 */
static i32 uii_scene_single_button(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	sk_ui_node_t btn;
	sk_ui_style_props_t props;

	(void)user;
	btn = ui->widget_button(ctx, ui->context_root(ctx), "OK", "btn-ok");
	if (!sk_ui_node_is_valid(btn)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.layout.width = sk_ui_pt(96.0f);
	props.layout.height = sk_ui_pt(32.0f);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(16.0f);
	props.layout.top = sk_ui_pt(16.0f);
	ui->node_set_inline_style(ctx, btn, &props);
	return 0;
}

SK_TEST(ui_integration_single_button) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	uii_env_t env;
	const sk_ui_api_t* ui;

	uii_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uii_env_destroy(&env);
		return;
	}

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_integration_single_button";
	params.width = 128u;
	params.height = 96u;
	params.time_seconds = 0.0; /* default clear: opaque white */

	uii_capture(&params, uii_scene_single_button, &img);

	/* Structural 1: the button fill sits at exactly (16,16)..(111,47)
	 * (1px border insets the fill; corner radius AA only trims corners). */
	{
		sk_ui_bbox_expected_t e;
		memset(&e, 0, sizeof(e));
		e.min_x = 16u;
		e.min_y = 16u;
		e.max_x = 111u;
		e.max_y = 47u;
		e.position_tolerance = 2u;
		e.size_tolerance = 2u;
		e.min_pixels = 2000u; /* 96x32 minus border/corners is ~2800 */
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_bbox(&img, uii_region(0u, 0u, 128u, 96u), UII_MATCH(UII_COLOR_BUTTON_BG, 2u), &e, NULL));
	}

	/* Structural 2: the button region is mostly button fill (not empty,
	 * not a full-bleed blob). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(16u, 16u, 112u, 48u), UII_MATCH(UII_COLOR_BUTTON_BG, 2u), 0.80f, 0.99f, NULL));

	/* Structural 3: the cleared margin around the widget is uniform white
	 * (root transparent → clear color shows through). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_solid(&img, uii_region(0u, 0u, 16u, 96u), UII_MATCH(UII_RGB(255u, 255u, 255u), 1u), NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_solid(&img, uii_region(0u, 48u, 128u, 96u), UII_MATCH(UII_RGB(255u, 255u, 255u), 1u), NULL));

	uii_assert_golden(ui, &img, "ui_integration_single_button");
	uii_debug_measure(ui, &img, "single_button");
	uii_free(&img);
	uii_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Scene 3: nested layout — panel > header row (2 buttons) + body box         */
/* -------------------------------------------------------------------------- */

/*
 * 256x192 frame. Layout (fixed numbers):
 *   root          fill (26,31,41) covering the full frame
 *   panel-main    ui-panel at absolute (16,16), 224x160 (bg (41,43,51),
 *                 1px border (71,77,87), radius 4, padding 8, column, gap 8)
 *     row-header  ui-view: height 28, flex row, justify space-between,
 *                 gap 8
 *       btn-a     ui-button 96x28, label "A" (no fonts → bg/border only)
 *       btn-b     ui-button 96x28, label "B"
 *     body        BOX height 40, fill (217,76,51), stretch width
 *
 * Panel content box starts at (25,25) (border 1 + padding 8), 206 wide.
 * Row at (25,25)..(230,52); buttons at x 25..120 and 135..230; body at
 * (25,53)..(230,92) (no column gap on the panel). All child positions
 * asserted structurally.
 */
static i32 uii_scene_layout_nested(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel;
	sk_ui_node_t row;
	sk_ui_node_t body;
	sk_ui_style_props_t props;

	(void)user;
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.10f, 0.12f, 0.16f, 1.0f);
	props.layout.width = sk_ui_pt(256.0f);
	props.layout.height = sk_ui_pt(192.0f);
	ui->node_set_inline_style(ctx, root, &props);

	panel = ui->widget_panel(ctx, root, "panel-main");
	if (!sk_ui_node_is_valid(panel)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.layout.width = sk_ui_pt(224.0f);
	props.layout.height = sk_ui_pt(160.0f);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(16.0f);
	props.layout.top = sk_ui_pt(16.0f);
	ui->node_set_inline_style(ctx, panel, &props);

	row = ui->widget_view(ctx, panel, "row-header");
	if (!sk_ui_node_is_valid(row)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ROW_GAP;
	props.layout.height = sk_ui_pt(28.0f);
	props.layout.flex_direction = SK_UI_FLEX_ROW;
	props.layout.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
	props.layout.row_gap = 8.0f;
	ui->node_set_inline_style(ctx, row, &props);

	{
		sk_ui_node_t btn_a = ui->widget_button(ctx, row, "A", "btn-a");
		sk_ui_node_t btn_b = ui->widget_button(ctx, row, "B", "btn-b");
		sk_ui_node_t btns[2];
		u32 b;
		if (!sk_ui_node_is_valid(btn_a) || !sk_ui_node_is_valid(btn_b)) {
			return -1;
		}
		btns[0] = btn_a;
		btns[1] = btn_b;
		/* Explicit button sizes: with no font system the label measures
		 * empty, so auto width would collapse the buttons to padding only.
		 * Fixed 96x28 keeps the space-between placement deterministic. */
		for (b = 0u; b < 2u; ++b) {
			memset(&props, 0, sizeof(props));
			props.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
			props.layout.width = sk_ui_pt(96.0f);
			props.layout.height = sk_ui_pt(28.0f);
			ui->node_set_inline_style(ctx, btns[b], &props);
		}
	}

	body = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, panel);
	if (!sk_ui_node_is_valid(body)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.85f, 0.30f, 0.20f, 1.0f);
	props.layout.height = sk_ui_pt(40.0f);
	ui->node_set_inline_style(ctx, body, &props);
	return 0;
}

SK_TEST(ui_integration_layout_nested) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	uii_env_t env;
	const sk_ui_api_t* ui;

	uii_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uii_env_destroy(&env);
		return;
	}

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_integration_layout_nested";
	params.width = 256u;
	params.height = 192u;
	params.time_seconds = 0.0;

	uii_capture(&params, uii_scene_layout_nested, &img);

	/* Structural 1: root background is solid at the outer margin. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_solid(&img, uii_region(0u, 0u, 16u, 192u), UII_MATCH(UII_COLOR_ROOT_BG, 2u), NULL));

	/* Structural 2: panel fill bbox (border insets fill by 1px). */
	{
		sk_ui_bbox_expected_t e;
		memset(&e, 0, sizeof(e));
		e.min_x = 16u;
		e.min_y = 16u;
		e.max_x = 239u;
		e.max_y = 175u;
		e.position_tolerance = 2u;
		e.size_tolerance = 2u;
		e.min_pixels = 20000u; /* 222x158 fill minus the row/body children ≈ 26000 */
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_bbox(&img, uii_region(0u, 0u, 256u, 192u), UII_MATCH(UII_COLOR_PANEL_BG, 2u), &e, NULL));
	}

	/* Structural 3: body box at its expected spot inside the panel. */
	{
		sk_ui_bbox_expected_t e;
		memset(&e, 0, sizeof(e));
		e.min_x = 24u;
		e.min_y = 52u;
		e.max_x = 231u;
		e.max_y = 93u;
		e.position_tolerance = 2u;
		e.size_tolerance = 2u;
		e.min_pixels = 6000u;
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_bbox(&img, uii_region(0u, 0u, 256u, 192u), UII_MATCH(UII_COLOR_BODY_RED, 2u), &e, NULL));
	}

	/* Structural 4: both buttons exist in the header row, separated by the
	 * space-between gap: the joint bbox spans the full row width.
	 * APX-248 / D2: fills ~94x26 each inside panel content (max x ~229, max y
	 * ~51) — not pad-expanded ~108x40 overflowing past panel right ~239. */
	{
		sk_ui_bbox_expected_t e;
		memset(&e, 0, sizeof(e));
		e.min_x = 26u;
		e.min_y = 26u;
		e.max_x = 229u;
		e.max_y = 52u;
		e.position_tolerance = 2u;
		e.size_tolerance = 2u;
		e.min_pixels = 4000u; /* two 94x26 fills ≈ 4900 */
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_bbox(&img, uii_region(0u, 0u, 256u, 192u), UII_MATCH(UII_COLOR_BUTTON_BG, 2u), &e, NULL));
	}

	/* Structural 4b (APX-248): no button fill past panel content right edge or
	 * below the 28px header row (old defect painted to x=251, y=65). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(232u, 24u, 256u, 70u), UII_MATCH(UII_COLOR_BUTTON_BG, 2u), 0.0f, 0.001f, NULL));
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(24u, 54u, 234u, 70u), UII_MATCH(UII_COLOR_BUTTON_BG, 2u), 0.0f, 0.001f, NULL));

	/* Structural 5: button fill coverage inside the header row region. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(24u, 24u, 234u, 54u), UII_MATCH(UII_COLOR_BUTTON_BG, 2u), 0.70f, 0.95f, NULL));

	/* Structural 6: theme histogram — every widget color present with sane
	 * proportions (merge tolerance 8 keeps panel vs root bg separate). */
	{
		sk_ui_hist_assert_params_t hp;
		sk_ui_hist_expectation_t ex[4];
		sk_ui_color_match_t c;
		memset(&hp, 0, sizeof(hp));
		hp.merge_tolerance = 8u;
		hp.max_unexpected_fraction = 0.05f;
		c = *UII_MATCH(UII_COLOR_PANEL_BG, 2u);
		ex[0].color = c;
		ex[0].min_fraction = 0.40f;
		ex[0].max_fraction = 0.60f;
		c = *UII_MATCH(UII_COLOR_ROOT_BG, 2u);
		ex[1].color = c;
		ex[1].min_fraction = 0.20f;
		ex[1].max_fraction = 0.32f;
		c = *UII_MATCH(UII_COLOR_BODY_RED, 2u);
		ex[2].color = c;
		ex[2].min_fraction = 0.12f;
		ex[2].max_fraction = 0.22f;
		c = *UII_MATCH(UII_COLOR_BUTTON_BG, 2u);
		ex[3].color = c;
		ex[3].min_fraction = 0.07f;
		ex[3].max_fraction = 0.16f;
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_histogram(&img, uii_region(0u, 0u, 256u, 192u), &hp, ex, 4, NULL));
	}

	uii_assert_golden(ui, &img, "ui_integration_layout_nested");
	uii_debug_measure(ui, &img, "layout_nested");
	uii_free(&img);
	uii_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Scene 4: text rendering (vendored DejaVuSans.ttf, deterministic raster)    */
/* -------------------------------------------------------------------------- */

/*
 * 192x96 frame. Root fill (26,31,41); ui-panel at (8,8), 176x80; label
 * "UI 42" (ui-label: color (242,242,250)) inside the panel content box at
 * (17,17), 160x32, font size = SK_UI_CAPTURE_HARNESS_FONT_LOGICAL_SIZE (20)
 * so physical size is SK_UI_CAPTURE_HARNESS_FONT_PIXEL_SIZE at content scale
 * 1.0. Font is installed by the harness (params.load_test_font) from
 * plugins/ui/testdata/DejaVuSans.ttf — never a system or embedded built-in.
 */
static i32 uii_scene_text_glyphs(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel;
	sk_ui_node_t label;
	sk_ui_style_props_t props;

	(void)user;
	/* Harness must have loaded DejaVuSans via load_test_font; refuse silent
	 * built-in fallback if a caller forgets the param. */
	if (scene->font_system == NULL || scene->font == NULL) {
		fprintf(stderr, "ui_integration_text_glyphs: pinned test font missing (set params.load_test_font)\n");
		return -1;
	}

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.10f, 0.12f, 0.16f, 1.0f);
	props.layout.width = sk_ui_pt(192.0f);
	props.layout.height = sk_ui_pt(96.0f);
	ui->node_set_inline_style(ctx, root, &props);

	panel = ui->widget_panel(ctx, root, "text-panel");
	if (!sk_ui_node_is_valid(panel)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.layout.width = sk_ui_pt(176.0f);
	props.layout.height = sk_ui_pt(80.0f);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(8.0f);
	props.layout.top = sk_ui_pt(8.0f);
	ui->node_set_inline_style(ctx, panel, &props);

	label = ui->widget_label(ctx, panel, "UI 42", "lbl-title");
	if (!sk_ui_node_is_valid(label)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.color = sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
	props.font_size = SK_UI_CAPTURE_HARNESS_FONT_LOGICAL_SIZE;
	props.layout.width = sk_ui_pt(160.0f);
	props.layout.height = sk_ui_pt(32.0f);
	ui->node_set_inline_style(ctx, label, &props);
	return 0;
}

SK_TEST(ui_integration_text_glyphs) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	uii_env_t env;
	const sk_ui_api_t* ui;

	uii_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uii_env_destroy(&env);
		return;
	}

	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_integration_text_glyphs";
	params.width = 192u;
	params.height = 96u;
	params.time_seconds = 0.0;
	params.load_test_font = 1; /* vendored DejaVuSans.ttf; fail if missing */

	uii_capture(&params, uii_scene_text_glyphs, &img);

	/* Structural 1: root bg is solid above the panel. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_solid(&img, uii_region(0u, 0u, 192u, 8u), UII_MATCH(UII_COLOR_ROOT_BG, 2u), NULL));

	/* Structural 2: panel bg dominates its box (text is a small share). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(9u, 9u, 183u, 87u), UII_MATCH(UII_COLOR_PANEL_BG, 2u), 0.75f, 0.99f, NULL));

	/* Structural 3: letterform ink (alpha-blended over panel) exists inside
	 * the label box. Pure text color is rare after AA blend; match a wide
	 * band of light-gray composites that are neither root nor panel. */
	{
		/* Mid-light composite of ui-label over panel — covers AA fringes. */
		const u32 ink = UII_RGB(180u, 180u, 190u);
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(&img, uii_region(17u, 17u, 177u, 49u), UII_MATCH(ink, 80u), 0.01f, 0.50f, NULL));
		sk_ui_bbox_expected_t e;
		memset(&e, 0, sizeof(e));
		e.min_x = 18u;
		e.min_y = 21u;
		e.max_x = 67u;
		e.max_y = 35u;
		e.position_tolerance = 4u;
		e.size_tolerance = 6u;
		e.min_pixels = 80u; /* letterform ink (not solid tofu) */
		TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_bbox(&img, uii_region(17u, 17u, 177u, 49u), UII_MATCH(ink, 80u), &e, NULL));
	}

	uii_assert_golden(ui, &img, "ui_integration_text_glyphs");
	uii_debug_measure(ui, &img, "text_glyphs");
	uii_free(&img);
	uii_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Scene 5: MSDF text shader (APX-266) — sizes                                */
/* -------------------------------------------------------------------------- */

typedef struct uii_msdf_cfg_t {
	f32 font_size;
} uii_msdf_cfg_t;

static i32 uii_scene_msdf_text(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	sk_ui_node_t root = ui->context_root(ctx);
	sk_ui_node_t panel;
	sk_ui_node_t label;
	sk_ui_style_props_t props;
	const uii_msdf_cfg_t* cfg = (const uii_msdf_cfg_t*)user;
	f32 font_size = 20.0f;

	if (cfg != NULL) {
		font_size = cfg->font_size;
	}
	if (scene->font_system == NULL || scene->font == NULL) {
		fprintf(stderr, "ui_integration_msdf_text: pinned test font missing\n");
		return -1;
	}
	/* MSDF bake happens lazily on the first shape; nothing to pre-warm. */

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.background_color = sk_ui_rgba(0.10f, 0.12f, 0.16f, 1.0f);
	props.layout.width = sk_ui_pt(320.0f);
	props.layout.height = sk_ui_pt(160.0f);
	ui->node_set_inline_style(ctx, root, &props);

	panel = ui->widget_panel(ctx, root, "msdf-panel");
	if (!sk_ui_node_is_valid(panel)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.layout.width = sk_ui_pt(304.0f);
	props.layout.height = sk_ui_pt(144.0f);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(8.0f);
	props.layout.top = sk_ui_pt(8.0f);
	ui->node_set_inline_style(ctx, panel, &props);

	label = ui->widget_label(ctx, panel, "Hello MSDF", "lbl-msdf");
	if (!sk_ui_node_is_valid(label)) {
		return -1;
	}
	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	props.color = sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
	props.font_size = font_size;
	props.layout.width = sk_ui_pt(288.0f);
	props.layout.height = sk_ui_pt(font_size + 16.0f);
	ui->node_set_inline_style(ctx, label, &props);
	return 0;
}

static u32 uii_chroma_fringe_count(const sk_ui_cpu_image_t* img, sk_ui_region_t region) {
	u32 x;
	u32 y;
	u32 n = 0u;
	const u32 x0 = region.x0 < img->width ? region.x0 : img->width;
	const u32 y0 = region.y0 < img->height ? region.y0 : img->height;
	const u32 x1 = region.x1 < img->width ? region.x1 : img->width;
	const u32 y1 = region.y1 < img->height ? region.y1 : img->height;
	for (y = y0; y < y1; ++y) {
		for (x = x0; x < x1; ++x) {
			const u8* p = img->pixels + ((size_t)y * (size_t)img->width + (size_t)x) * 4u;
			u8 mx;
			u8 mn;
			u32 lum;
			mx = p[0] > p[1] ? p[0] : p[1];
			if (p[2] > mx) {
				mx = p[2];
			}
			mn = p[0] < p[1] ? p[0] : p[1];
			if (p[2] < mn) {
				mn = p[2];
			}
			lum = ((u32)p[0] + (u32)p[1] + (u32)p[2]) / 3u;
			/* Near-neutral light ink should not pick up a single MSDF channel. */
			if (lum >= 80u && (u32)(mx - mn) > 48u) {
				n += 1u;
			}
		}
	}
	return n;
}

static void uii_assert_msdf_letterforms(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, const_chr_t tag, f32 font_size) {
	sk_ui_region_t label = uii_region(17u, 17u, 305u, 17u + (u32)(font_size + 20.0f));
	const u32 ink = UII_RGB(180u, 180u, 190u);
	const u32 panel = UII_COLOR_PANEL_BG;
	u32 fringe;
	u32 region_px;

	(void)tag;
	/* Panel still visible around the glyphs (not a solid tofu bar). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(img, label, UII_MATCH(panel, 12u), 0.20f, 0.995f, NULL));
	/* Letterform ink exists (AA composites of light text over panel). */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_coverage(img, label, UII_MATCH(ink, 80u), 0.004f, 0.70f, NULL));

	if (label.x1 > img->width) {
		label.x1 = img->width;
	}
	if (label.y1 > img->height) {
		label.y1 = img->height;
	}
	region_px = (label.x1 - label.x0) * (label.y1 - label.y0);
	fringe = uii_chroma_fringe_count(img, label);
	TEST_ASSERT_TRUE(region_px > 0u);
	/* Fewer than 2% of the label pixels may be strongly chromatic. */
	TEST_ASSERT_TRUE(fringe * 50u < region_px);
}

static void uii_run_msdf_size(const sk_ui_api_t* ui, f32 font_size, const_chr_t scene_name) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	uii_msdf_cfg_t cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.font_size = font_size;

	memset(&params, 0, sizeof(params));
	params.scene_name = scene_name;
	params.width = 320u;
	params.height = 160u;
	params.time_seconds = 0.0;
	params.load_test_font = 1;
	uii_capture_user(&params, uii_scene_msdf_text, &cfg, &img);
	uii_assert_msdf_letterforms(ui, &img, scene_name, font_size);
	uii_debug_measure(ui, &img, scene_name);
	uii_free(&img);
}

SK_TEST(ui_integration_msdf_text_small_medium_large) {
	uii_env_t env;
	const sk_ui_api_t* ui;

	uii_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uii_env_destroy(&env);
		return;
	}

	uii_run_msdf_size(ui, 10.0f, "ui_integration_msdf_text_small");
	uii_run_msdf_size(ui, 20.0f, "ui_integration_msdf_text_medium");
	uii_run_msdf_size(ui, 48.0f, "ui_integration_msdf_text_large");

	uii_env_destroy(&env);
}

#endif /* SK_TESTS */
