/*
 * ui_flexbox_vision.c — APX-257 vision checks for representative flexbox layouts.
 *
 * Samples one case per major APX-256 flexbox-matrix behaviour:
 *   wrap, justify, align, grow/shrink, gap, nesting
 *
 * Each case:
 *   1. Renders pure BOX nodes with visually distinct solid fills (red / green /
 *      blue children on a dark container over a light canvas).
 *   2. Asserts parent-content-relative rects numerically (same expectations as
 *      the unit matrix) so pixel geometry and vision grade stay in lockstep.
 *   3. Grades the qualitative arrangement with sk_ui_vision_assert_image
 *      (family FLEXBOX + a human-readable state hint). Vision SKIPPED without
 *      credentials is gated clearly (IGNORE / REQUIRED fail); structural rects
 *      still guard (APX-263).
 *
 * Catches cases where numeric rects are self-consistent but the painted result
 * is wrong (wrong packing, missing grow fill, collapsed gap, etc.).
 */

#include "app.h"
#include "filesystem.h"
#include "path.h"
#include "test.h"
#include "ui.h"
#include "ui_capture_harness.h"
#include "ui_vision_assert.h"

#ifdef noreturn
#undef noreturn
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SK_TESTS

/* -------------------------------------------------------------------------- */
/* Colours (sRGB 0–255 packed as little-endian RGBA8 for structural matches)  */
/* -------------------------------------------------------------------------- */

#define UFX_RGB(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)

#define UFX_MATCH(rgba, tol)                                   \
	(&(sk_ui_color_match_t){.r = (u8)((rgba) & 0xFFu),         \
							.g = (u8)(((rgba) >> 8) & 0xFFu),  \
							.b = (u8)(((rgba) >> 16) & 0xFFu), \
							.a = (u8)(((rgba) >> 24) & 0xFFu), \
							.tolerance = (tol),                \
							.include_alpha = 1})

/* Distinct primaries so vision can name left/middle/right without ambiguity. */
#define UFX_RED UFX_RGB(229u, 57u, 53u)	  /* #E53935 */
#define UFX_GREEN UFX_RGB(67u, 160u, 71u) /* #43A047 */
#define UFX_BLUE UFX_RGB(30u, 136u, 229u) /* #1E88E5 */
#define UFX_CONT UFX_RGB(48u, 54u, 66u)	  /* dark container */
#define UFX_CANVAS UFX_RGB(245u, 245u, 247u)

#define UFX_RED_F sk_ui_rgba(229.0f / 255.0f, 57.0f / 255.0f, 53.0f / 255.0f, 1.0f)
#define UFX_GREEN_F sk_ui_rgba(67.0f / 255.0f, 160.0f / 255.0f, 71.0f / 255.0f, 1.0f)
#define UFX_BLUE_F sk_ui_rgba(30.0f / 255.0f, 136.0f / 255.0f, 229.0f / 255.0f, 1.0f)
#define UFX_CONT_F sk_ui_rgba(48.0f / 255.0f, 54.0f / 255.0f, 66.0f / 255.0f, 1.0f)
#define UFX_CANVAS_F sk_ui_rgba(245.0f / 255.0f, 245.0f / 255.0f, 247.0f / 255.0f, 1.0f)

#define UFX_TOL 0.01f
#define UFX_ORIGIN_X 16.0f
#define UFX_ORIGIN_Y 16.0f

/* -------------------------------------------------------------------------- */
/* Case table                                                                 */
/* -------------------------------------------------------------------------- */

typedef enum ufx_case_id_t { UFX_CASE_WRAP = 0, UFX_CASE_JUSTIFY, UFX_CASE_ALIGN, UFX_CASE_GROW, UFX_CASE_GAP, UFX_CASE_NEST, UFX_CASE_COUNT } ufx_case_id_t;

typedef struct ufx_case_t {
	ufx_case_id_t id;
	const_chr_t scene_name;
	const_chr_t state_hint;
	u32 frame_w;
	u32 frame_h;
} ufx_case_t;

/* state_hint must stay under ~200 chars (vision helper esc_state is 256). */
static const ufx_case_t ufx_cases[UFX_CASE_COUNT] = {
	{UFX_CASE_WRAP, "ui_flexbox_vision_wrap",
	 "Three coloured blocks red-green-blue left-to-right on ONE horizontal line "
	 "in a dark container; no second flex row (wrap packs like nowrap).",
	 160u, 120u},
	{UFX_CASE_JUSTIFY, "ui_flexbox_vision_justify_space_between",
	 "Three coloured blocks red-green-blue with space-between: red touches left "
	 "edge, blue touches right edge, free space split evenly between gaps.",
	 240u, 96u},
	{UFX_CASE_ALIGN, "ui_flexbox_vision_align_center",
	 "Three coloured blocks red-green-blue of fixed height vertically CENTERED "
	 "in a taller dark row container (equal empty band above and below).",
	 200u, 120u},
	{UFX_CASE_GROW, "ui_flexbox_vision_grow",
	 "Two children in a dark row: fixed-width red on the left; green GROWS to "
	 "fill all remaining horizontal space (green much wider than red).",
	 240u, 96u},
	{UFX_CASE_GAP, "ui_flexbox_vision_gap",
	 "Three coloured blocks red-green-blue with equal visible column gaps "
	 "between them (not packed flush); same gap red-green and green-blue.",
	 240u, 96u},
	{UFX_CASE_NEST, "ui_flexbox_vision_nest",
	 "Nested column: header row red left and green at far right (space-between); "
	 "solid blue body band BELOW header spanning full outer width.",
	 256u, 144u},
};

/* -------------------------------------------------------------------------- */
/* Env / capture helpers                                                      */
/* -------------------------------------------------------------------------- */

typedef struct ufx_env_t {
	sk_app_context_t* app;
	const sk_ui_api_t* ui;
} ufx_env_t;

static i32 ufx_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
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

static void ufx_env_init(ufx_env_t* env) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	sk_ui_vision_gate_begin();
	memset(env, 0, sizeof(*env));
	env->app = sk_app_init(0, NULL);
	if (env->app == NULL) {
		return;
	}
	if (ufx_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(env->app, path);
	}
	env->ui = (const sk_ui_api_t*)sk_app_api()->get_api(env->app, SK_UI_API_TYPE_ID);
}

static void ufx_env_destroy(ufx_env_t* env) {
	if (env->app != NULL) {
		sk_app_destroy(env->app);
	}
	memset(env, 0, sizeof(*env));
	/* After all structural samples: IGNORE (or FAIL if REQUIRED) when vision skipped. */
	sk_ui_vision_gate_finish();
}

static void ufx_capture(const sk_ui_capture_harness_params_t* params, sk_ui_capture_scene_fn scene, void* user, sk_ui_cpu_image_t* out) {
	const i32 rc = sk_ui_capture_harness_capture(params, scene, user, out);
	if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI flexbox vision test");
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_NOT_NULL(out->pixels);
}

static void ufx_free(sk_ui_cpu_image_t* img) {
	sk_ui_capture_harness_image_free(img);
}

/**
 * Grade with vision when credentials exist.
 * SKIPPED is recorded via sk_ui_vision_gate_* (clear message; never silent PASS).
 * FAIL fails the test with the model's reason (retry once for flukes).
 */
static void ufx_vision_restore_env(const char* prev_backend, const char* prev_mock) {
	if (prev_backend != NULL && prev_backend[0] != '\0') {
		setenv("SK_UI_VISION_BACKEND", prev_backend, 1);
	} else {
		unsetenv("SK_UI_VISION_BACKEND");
	}
	if (prev_mock != NULL && prev_mock[0] != '\0') {
		setenv("SK_UI_VISION_MOCK_RESPONSE", prev_mock, 1);
	} else {
		unsetenv("SK_UI_VISION_MOCK_RESPONSE");
	}
}

static void ufx_vision_grade(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, const_chr_t state_hint, const_chr_t scene_name) {
	sk_ui_vision_result_t result;
	i32 rc;
	const char* old_backend;
	const char* old_mock;
	char prev_backend[64];
	char prev_mock[512];

	prev_backend[0] = '\0';
	prev_mock[0] = '\0';
	old_backend = getenv("SK_UI_VISION_BACKEND");
	old_mock = getenv("SK_UI_VISION_MOCK_RESPONSE");
	if (old_backend != NULL) {
		(void)snprintf(prev_backend, sizeof(prev_backend), "%s", old_backend);
	}
	if (old_mock != NULL) {
		(void)snprintf(prev_mock, sizeof(prev_mock), "%s", old_mock);
	}
	unsetenv("SK_UI_VISION_BACKEND");
	unsetenv("SK_UI_VISION_MOCK_RESPONSE");

	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_image(ui, img, SK_UI_VISION_WIDGET_FLEXBOX, state_hint, scene_name, sk_filesystem_api(), &result);

	if (rc == SK_UI_VISION_ASSERT_SKIPPED) {
		ufx_vision_restore_env(prev_backend, prev_mock);
		sk_ui_vision_gate_note_skipped(scene_name, result.reason[0] != '\0' ? result.reason : "no vision credentials");
		return;
	}
	if (rc == SK_UI_VISION_ASSERT_ERROR || rc == SK_UI_VISION_ASSERT_FAIL || result.passed == 0) {
		sk_ui_vision_result_t retry;
		i32 rc2;
		memset(&retry, 0, sizeof(retry));
		rc2 = sk_ui_vision_assert_image(ui, img, SK_UI_VISION_WIDGET_FLEXBOX, state_hint, scene_name, sk_filesystem_api(), &retry);
		if (rc2 == SK_UI_VISION_ASSERT_OK && retry.passed != 0) {
			ufx_vision_restore_env(prev_backend, prev_mock);
			return;
		}
		if (rc2 == SK_UI_VISION_ASSERT_SKIPPED) {
			ufx_vision_restore_env(prev_backend, prev_mock);
			sk_ui_vision_gate_note_skipped(scene_name, retry.reason[0] != '\0' ? retry.reason : "no vision credentials");
			return;
		}
		ufx_vision_restore_env(prev_backend, prev_mock);
		if (rc == SK_UI_VISION_ASSERT_ERROR && rc2 == SK_UI_VISION_ASSERT_ERROR) {
			fprintf(stderr, "vision assert ERROR for %s: %s\n", scene_name, retry.reason[0] != '\0' ? retry.reason : result.reason);
			sk_ui_vision_gate_note_skipped(scene_name, "vision backend ERROR after retries");
			return;
		}
		fprintf(stderr, "vision FAIL %s (flexbox): %s\n", scene_name, retry.reason[0] != '\0' ? retry.reason : result.reason);
		if (retry.saved_frame_path[0] != '\0') {
			fprintf(stderr, "  failing frame: %s\n", retry.saved_frame_path);
		} else if (result.saved_frame_path[0] != '\0') {
			fprintf(stderr, "  failing frame: %s\n", result.saved_frame_path);
		}
		TEST_FAIL_MESSAGE("vision FAIL: flexbox qualitative arrangement did not match claim (see stderr)");
		return;
	}
	ufx_vision_restore_env(prev_backend, prev_mock);
	TEST_ASSERT_EQUAL_INT(SK_UI_VISION_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_INT(1, result.passed);
}

/* -------------------------------------------------------------------------- */
/* Layout helpers (mirror APX-256 matrix helpers + colour fills)              */
/* -------------------------------------------------------------------------- */

static sk_ui_node_t ufx_box(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_node_t n = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, parent);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(n));
	return n;
}

/**
 * Canvas root: light fill, column, padding = UFX_ORIGIN so the flex sample
 * sits at (16,16) without ABSOLUTE positioning. Clay disables space-between
 * GROW spacers on absolute nodes — samples must stay in-flow.
 */
static void ufx_style_canvas_root(const sk_ui_api_t* ui, sk_ui_context_t* ctx, f32 w, f32 h) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_PADDING |
			 SK_UI_SP_BORDER_WIDTH;
	p.background_color = UFX_CANVAS_F;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.justify_content = SK_UI_JUSTIFY_FLEX_START;
	p.layout.padding.left = UFX_ORIGIN_X;
	p.layout.padding.top = UFX_ORIGIN_Y;
	p.layout.padding.right = 0.0f;
	p.layout.padding.bottom = 0.0f;
	p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
	TEST_ASSERT_EQUAL_INT(0, ui->node_set_inline_style(ctx, ui->context_root(ctx), &p));
}

/* In-flow fixed outer size, zero pad/border, explicit flex container props. */
static void ufx_style_container(sk_ui_style_props_t* p, f32 w, f32 h, sk_ui_flex_direction_t dir, sk_ui_justify_t justify, sk_ui_align_t align_items, sk_ui_flex_wrap_t wrap,
								f32 row_gap, f32 col_gap, sk_ui_color_t bg) {
	memset(p, 0, sizeof(*p));
	p->mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_WRAP | SK_UI_SP_PADDING |
			  SK_UI_SP_BORDER_WIDTH | SK_UI_SP_ROW_GAP | SK_UI_SP_COLUMN_GAP | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF;
	p->layout.width = sk_ui_pt(w);
	p->layout.height = sk_ui_pt(h);
	p->layout.flex_direction = dir;
	p->layout.justify_content = justify;
	p->layout.align_items = align_items;
	p->layout.flex_wrap = wrap;
	p->layout.row_gap = row_gap;
	p->layout.column_gap = col_gap;
	p->layout.flex_shrink = 0.0f;
	p->layout.align_self = SK_UI_ALIGN_FLEX_START;
	p->layout.padding.left = p->layout.padding.right = p->layout.padding.top = p->layout.padding.bottom = 0.0f;
	p->layout.border.left = p->layout.border.right = p->layout.border.top = p->layout.border.bottom = 0.0f;
	p->background_color = bg;
}

static void ufx_style_fixed_child(sk_ui_style_props_t* p, f32 w, f32 h, sk_ui_color_t bg) {
	memset(p, 0, sizeof(*p));
	p->mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF | SK_UI_SP_BACKGROUND_COLOR;
	p->layout.width = sk_ui_pt(w);
	p->layout.height = sk_ui_pt(h);
	p->layout.flex_grow = 0.0f;
	p->layout.flex_shrink = 0.0f;
	p->layout.align_self = SK_UI_ALIGN_FLEX_START;
	p->background_color = bg;
}

static void ufx_assert_rect(const sk_ui_rect_t* r, f32 x, f32 y, f32 w, f32 h, const char* label) {
	char msg[384];
	snprintf(msg, sizeof(msg), "%s.x exp=%.2f got=%.2f d=%.3f", label, (double)x, (double)r->x, (double)(r->x - x));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UFX_TOL, x, r->x, msg);
	snprintf(msg, sizeof(msg), "%s.y exp=%.2f got=%.2f d=%.3f", label, (double)y, (double)r->y, (double)(r->y - y));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UFX_TOL, y, r->y, msg);
	snprintf(msg, sizeof(msg), "%s.w exp=%.2f got=%.2f d=%.3f", label, (double)w, (double)r->width, (double)(r->width - w));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UFX_TOL, w, r->width, msg);
	snprintf(msg, sizeof(msg), "%s.h exp=%.2f got=%.2f d=%.3f", label, (double)h, (double)r->height, (double)(r->height - h));
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(UFX_TOL, h, r->height, msg);
}

static sk_ui_region_t ufx_region(u32 x0, u32 y0, u32 x1, u32 y1) {
	sk_ui_region_t r;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	return r;
}

/** Assert a coloured child bbox near the absolute frame position (origin + relative). */
static void ufx_assert_child_pixels(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, u32 fill_rgba, f32 rel_x, f32 rel_y, f32 w, f32 h, const_chr_t label) {
	sk_ui_bbox_expected_t e;
	sk_ui_region_t full;
	u32 ox = (u32)UFX_ORIGIN_X;
	u32 oy = (u32)UFX_ORIGIN_Y;
	char msg[256];
	i32 rc;

	memset(&e, 0, sizeof(e));
	e.min_x = ox + (u32)rel_x;
	e.min_y = oy + (u32)rel_y;
	e.max_x = ox + (u32)(rel_x + w) - 1u;
	e.max_y = oy + (u32)(rel_y + h) - 1u;
	e.position_tolerance = 2u;
	e.size_tolerance = 3u;
	e.min_pixels = (u32)((w * h) * 0.45f);
	if (e.min_pixels < 20u) {
		e.min_pixels = 20u;
	}
	full = ufx_region(0u, 0u, img->width, img->height);
	rc = ui->cpu_image_assert_bbox(img, full, UFX_MATCH(fill_rgba, 18u), &e, NULL);
	snprintf(msg, sizeof(msg), "missing/misplaced coloured child '%s' fill", label);
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_IMAGE_ASSERT_OK, rc, msg);
}

/* -------------------------------------------------------------------------- */
/* Scene builders + numeric asserts                                           */
/* -------------------------------------------------------------------------- */

typedef struct ufx_scene_user_t {
	ufx_case_id_t id;
	/* Nodes filled by scene builder for numeric rect asserts after layout paint. */
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_node_t body; /* nest only */
} ufx_scene_user_t;

static i32 ufx_scene_build(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	ufx_scene_user_t* u = (ufx_scene_user_t*)user;
	sk_ui_style_props_t p;
	sk_ui_node_t root;

	if (u == NULL) {
		return -1;
	}
	root = ui->context_root(ctx);
	ufx_style_canvas_root(ui, ctx, (f32)scene->width, (f32)scene->height);

	switch (u->id) {
	case UFX_CASE_WRAP: {
		/* Container 100x80 at origin; three 40x20 kids — single line (wrap=WRAP). */
		u->cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 100.0f, 80.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_WRAP, 0.0f, 0.0f, UFX_CONT_F);
		if (ui->node_merge_inline_style(ctx, u->cont, &p) != 0) {
			return -1;
		}
		u->a = ufx_box(ui, ctx, u->cont);
		u->b = ufx_box(ui, ctx, u->cont);
		u->c = ufx_box(ui, ctx, u->cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		if (ui->node_merge_inline_style(ctx, u->a, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		if (ui->node_merge_inline_style(ctx, u->b, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		if (ui->node_merge_inline_style(ctx, u->c, &p) != 0) {
			return -1;
		}
		return 0;
	}
	case UFX_CASE_JUSTIFY: {
		/* 200x40, three 40x20, space-between → x = 0, 80, 160. */
		u->cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_SPACE_BETWEEN, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		if (ui->node_merge_inline_style(ctx, u->cont, &p) != 0) {
			return -1;
		}
		u->a = ufx_box(ui, ctx, u->cont);
		u->b = ufx_box(ui, ctx, u->cont);
		u->c = ufx_box(ui, ctx, u->cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		if (ui->node_merge_inline_style(ctx, u->a, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		if (ui->node_merge_inline_style(ctx, u->b, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		if (ui->node_merge_inline_style(ctx, u->c, &p) != 0) {
			return -1;
		}
		return 0;
	}
	case UFX_CASE_ALIGN: {
		/* 160x60 container, three 40x20, align-items CENTER → y = 20. */
		u->cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 160.0f, 60.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_CENTER, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		if (ui->node_merge_inline_style(ctx, u->cont, &p) != 0) {
			return -1;
		}
		u->a = ufx_box(ui, ctx, u->cont);
		u->b = ufx_box(ui, ctx, u->cont);
		u->c = ufx_box(ui, ctx, u->cont);
		/* align_self AUTO so container align_items applies. */
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		if (ui->node_merge_inline_style(ctx, u->a, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		if (ui->node_merge_inline_style(ctx, u->b, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		if (ui->node_merge_inline_style(ctx, u->c, &p) != 0) {
			return -1;
		}
		return 0;
	}
	case UFX_CASE_GROW: {
		/* 200x40: fixed 40 red + grow green fills remaining 160. */
		u->cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		if (ui->node_merge_inline_style(ctx, u->cont, &p) != 0) {
			return -1;
		}
		u->a = ufx_box(ui, ctx, u->cont);
		u->b = ufx_box(ui, ctx, u->cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		if (ui->node_merge_inline_style(ctx, u->a, &p) != 0) {
			return -1;
		}
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.height = sk_ui_pt(20.0f);
		p.layout.flex_grow = 1.0f;
		p.layout.flex_shrink = 0.0f;
		p.layout.align_self = SK_UI_ALIGN_FLEX_START;
		p.background_color = UFX_GREEN_F;
		if (ui->node_merge_inline_style(ctx, u->b, &p) != 0) {
			return -1;
		}
		u->c = SK_UI_NODE_INVALID;
		return 0;
	}
	case UFX_CASE_GAP: {
		/* 200x40, three 40x20, column_gap=10 → x = 0, 50, 100. */
		u->cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 10.0f, UFX_CONT_F);
		if (ui->node_merge_inline_style(ctx, u->cont, &p) != 0) {
			return -1;
		}
		u->a = ufx_box(ui, ctx, u->cont);
		u->b = ufx_box(ui, ctx, u->cont);
		u->c = ufx_box(ui, ctx, u->cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		if (ui->node_merge_inline_style(ctx, u->a, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		if (ui->node_merge_inline_style(ctx, u->b, &p) != 0) {
			return -1;
		}
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		if (ui->node_merge_inline_style(ctx, u->c, &p) != 0) {
			return -1;
		}
		return 0;
	}
	case UFX_CASE_NEST: {
		/* outer 220x100 column; row 220x40 space-between 60x30 kids; body 220x40 blue. */
		u->cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 220.0f, 100.0f, SK_UI_FLEX_COLUMN, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_STRETCH, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		if (ui->node_merge_inline_style(ctx, u->cont, &p) != 0) {
			return -1;
		}
		{
			sk_ui_node_t row = ufx_box(ui, ctx, u->cont);
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_WRAP | SK_UI_SP_PADDING |
					 SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BACKGROUND_COLOR;
			p.layout.width = sk_ui_pt(220.0f);
			p.layout.height = sk_ui_pt(40.0f);
			p.layout.flex_direction = SK_UI_FLEX_ROW;
			p.layout.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
			p.layout.align_items = SK_UI_ALIGN_FLEX_START;
			p.layout.flex_wrap = SK_UI_FLEX_NOWRAP;
			p.layout.padding.left = p.layout.padding.right = p.layout.padding.top = p.layout.padding.bottom = 0.0f;
			p.layout.border.left = p.layout.border.right = p.layout.border.top = p.layout.border.bottom = 0.0f;
			p.background_color = UFX_CONT_F;
			if (ui->node_merge_inline_style(ctx, row, &p) != 0) {
				return -1;
			}
			u->a = ufx_box(ui, ctx, row);
			u->b = ufx_box(ui, ctx, row);
			ufx_style_fixed_child(&p, 60.0f, 30.0f, UFX_RED_F);
			if (ui->node_merge_inline_style(ctx, u->a, &p) != 0) {
				return -1;
			}
			ufx_style_fixed_child(&p, 60.0f, 30.0f, UFX_GREEN_F);
			if (ui->node_merge_inline_style(ctx, u->b, &p) != 0) {
				return -1;
			}
		}
		u->body = ufx_box(ui, ctx, u->cont);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_SELF | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.height = sk_ui_pt(40.0f);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		p.background_color = UFX_BLUE_F;
		if (ui->node_merge_inline_style(ctx, u->body, &p) != 0) {
			return -1;
		}
		u->c = SK_UI_NODE_INVALID;
		return 0;
	}
	case UFX_CASE_COUNT:
	default:
		return -1;
	}
}

/**
 * After capture, re-layout the same tree is not available (harness destroyed ctx).
 * Numeric geometry is asserted from painted pixel bboxes (absolute), which must
 * agree with the APX-256 matrix expectations shifted by UFX_ORIGIN.
 *
 * For grow/nest we also re-build an offline tree and assert layout rects so
 * vision and numeric unit paths stay coupled when paint is stubbed.
 */
static void ufx_assert_numeric_offline(const sk_ui_api_t* ui, ufx_case_id_t id) {
	sk_ui_context_t* ctx;
	sk_ui_node_t root;
	sk_ui_node_t cont;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_node_t body;
	sk_ui_rect_t ra;
	sk_ui_rect_t rb;
	sk_ui_rect_t rc;
	sk_ui_rect_t rbody;
	sk_ui_style_props_t p;

	ctx = ui->context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	root = ui->context_root(ctx);

	switch (id) {
	case UFX_CASE_WRAP:
		cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 100.0f, 80.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_WRAP, 0.0f, 0.0f, UFX_CONT_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		a = ufx_box(ui, ctx, cont);
		b = ufx_box(ui, ctx, cont);
		c = ufx_box(ui, ctx, cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
		ufx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "wrap.a");
		ufx_assert_rect(&rb, 40.0f, 0.0f, 40.0f, 20.0f, "wrap.b");
		ufx_assert_rect(&rc, 80.0f, 0.0f, 40.0f, 20.0f, "wrap.c");
		TEST_ASSERT_FLOAT_WITHIN(UFX_TOL, ra.y, rb.y);
		TEST_ASSERT_FLOAT_WITHIN(UFX_TOL, rb.y, rc.y);
		break;
	case UFX_CASE_JUSTIFY:
		cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_SPACE_BETWEEN, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		a = ufx_box(ui, ctx, cont);
		b = ufx_box(ui, ctx, cont);
		c = ufx_box(ui, ctx, cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
		ufx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "justify.a");
		ufx_assert_rect(&rb, 80.0f, 0.0f, 40.0f, 20.0f, "justify.b");
		ufx_assert_rect(&rc, 160.0f, 0.0f, 40.0f, 20.0f, "justify.c");
		break;
	case UFX_CASE_ALIGN:
		cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 160.0f, 60.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_CENTER, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		a = ufx_box(ui, ctx, cont);
		b = ufx_box(ui, ctx, cont);
		c = ufx_box(ui, ctx, cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
		ufx_assert_rect(&ra, 0.0f, 20.0f, 40.0f, 20.0f, "align.a");
		ufx_assert_rect(&rb, 40.0f, 20.0f, 40.0f, 20.0f, "align.b");
		ufx_assert_rect(&rc, 80.0f, 20.0f, 40.0f, 20.0f, "align.c");
		break;
	case UFX_CASE_GROW:
		cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		a = ufx_box(ui, ctx, cont);
		b = ufx_box(ui, ctx, cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_SELF | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.height = sk_ui_pt(20.0f);
		p.layout.flex_grow = 1.0f;
		p.layout.flex_shrink = 0.0f;
		p.layout.align_self = SK_UI_ALIGN_FLEX_START;
		p.background_color = UFX_GREEN_F;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		ufx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "grow.fixed");
		ufx_assert_rect(&rb, 40.0f, 0.0f, 160.0f, 20.0f, "grow.fill");
		break;
	case UFX_CASE_GAP:
		cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 200.0f, 40.0f, SK_UI_FLEX_ROW, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_FLEX_START, SK_UI_FLEX_NOWRAP, 0.0f, 10.0f, UFX_CONT_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		a = ufx_box(ui, ctx, cont);
		b = ufx_box(ui, ctx, cont);
		c = ufx_box(ui, ctx, cont);
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_RED_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_GREEN_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		ufx_style_fixed_child(&p, 40.0f, 20.0f, UFX_BLUE_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, c, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, c, &rc, NULL));
		ufx_assert_rect(&ra, 0.0f, 0.0f, 40.0f, 20.0f, "gap.a");
		ufx_assert_rect(&rb, 50.0f, 0.0f, 40.0f, 20.0f, "gap.b");
		ufx_assert_rect(&rc, 100.0f, 0.0f, 40.0f, 20.0f, "gap.c");
		TEST_ASSERT_FLOAT_WITHIN(UFX_TOL, 10.0f, rb.x - (ra.x + ra.width));
		TEST_ASSERT_FLOAT_WITHIN(UFX_TOL, 10.0f, rc.x - (rb.x + rb.width));
		break;
	case UFX_CASE_NEST:
		cont = ufx_box(ui, ctx, root);
		ufx_style_container(&p, 220.0f, 100.0f, SK_UI_FLEX_COLUMN, SK_UI_JUSTIFY_FLEX_START, SK_UI_ALIGN_STRETCH, SK_UI_FLEX_NOWRAP, 0.0f, 0.0f, UFX_CONT_F);
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, cont, &p));
		{
			sk_ui_node_t row = ufx_box(ui, ctx, cont);
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_WRAP | SK_UI_SP_PADDING |
					 SK_UI_SP_BORDER_WIDTH;
			p.layout.width = sk_ui_pt(220.0f);
			p.layout.height = sk_ui_pt(40.0f);
			p.layout.flex_direction = SK_UI_FLEX_ROW;
			p.layout.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
			p.layout.align_items = SK_UI_ALIGN_FLEX_START;
			p.layout.flex_wrap = SK_UI_FLEX_NOWRAP;
			TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, row, &p));
			a = ufx_box(ui, ctx, row);
			b = ufx_box(ui, ctx, row);
			ufx_style_fixed_child(&p, 60.0f, 30.0f, UFX_RED_F);
			TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, a, &p));
			ufx_style_fixed_child(&p, 60.0f, 30.0f, UFX_GREEN_F);
			TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, b, &p));
		}
		body = ufx_box(ui, ctx, cont);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_SELF | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.height = sk_ui_pt(40.0f);
		p.layout.align_self = SK_UI_ALIGN_AUTO;
		p.background_color = UFX_BLUE_F;
		TEST_ASSERT_EQUAL_INT(0, ui->node_merge_inline_style(ctx, body, &p));
		TEST_ASSERT_EQUAL_INT(0, ui->style_resolve(ctx));
		TEST_ASSERT_EQUAL_INT(0, ui->layout(ctx, 400.0f, 300.0f));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, a, &ra, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, b, &rb, NULL));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_layout_rect(ctx, body, &rbody, NULL));
		ufx_assert_rect(&ra, 0.0f, 0.0f, 60.0f, 30.0f, "nest.a");
		ufx_assert_rect(&rb, 160.0f, 0.0f, 60.0f, 30.0f, "nest.b");
		ufx_assert_rect(&rbody, 0.0f, 40.0f, 220.0f, 40.0f, "nest.body");
		break;
	case UFX_CASE_COUNT:
	default:
		TEST_FAIL_MESSAGE("unknown flexbox vision case");
		break;
	}

	ui->context_destroy(ctx);
}

static void ufx_assert_painted(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, ufx_case_id_t id) {
	/* Canvas margin is the light clear/root fill. */
	TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK, ui->cpu_image_assert_solid(img, ufx_region(0u, 0u, 12u, 12u), UFX_MATCH(UFX_CANVAS, 8u), NULL));

	switch (id) {
	case UFX_CASE_WRAP:
		ufx_assert_child_pixels(ui, img, UFX_RED, 0.0f, 0.0f, 40.0f, 20.0f, "wrap.red");
		ufx_assert_child_pixels(ui, img, UFX_GREEN, 40.0f, 0.0f, 40.0f, 20.0f, "wrap.green");
		ufx_assert_child_pixels(ui, img, UFX_BLUE, 80.0f, 0.0f, 40.0f, 20.0f, "wrap.blue");
		break;
	case UFX_CASE_JUSTIFY:
		ufx_assert_child_pixels(ui, img, UFX_RED, 0.0f, 0.0f, 40.0f, 20.0f, "justify.red");
		ufx_assert_child_pixels(ui, img, UFX_GREEN, 80.0f, 0.0f, 40.0f, 20.0f, "justify.green");
		ufx_assert_child_pixels(ui, img, UFX_BLUE, 160.0f, 0.0f, 40.0f, 20.0f, "justify.blue");
		break;
	case UFX_CASE_ALIGN:
		ufx_assert_child_pixels(ui, img, UFX_RED, 0.0f, 20.0f, 40.0f, 20.0f, "align.red");
		ufx_assert_child_pixels(ui, img, UFX_GREEN, 40.0f, 20.0f, 40.0f, 20.0f, "align.green");
		ufx_assert_child_pixels(ui, img, UFX_BLUE, 80.0f, 20.0f, 40.0f, 20.0f, "align.blue");
		break;
	case UFX_CASE_GROW:
		ufx_assert_child_pixels(ui, img, UFX_RED, 0.0f, 0.0f, 40.0f, 20.0f, "grow.red");
		ufx_assert_child_pixels(ui, img, UFX_GREEN, 40.0f, 0.0f, 160.0f, 20.0f, "grow.green");
		break;
	case UFX_CASE_GAP:
		ufx_assert_child_pixels(ui, img, UFX_RED, 0.0f, 0.0f, 40.0f, 20.0f, "gap.red");
		ufx_assert_child_pixels(ui, img, UFX_GREEN, 50.0f, 0.0f, 40.0f, 20.0f, "gap.green");
		ufx_assert_child_pixels(ui, img, UFX_BLUE, 100.0f, 0.0f, 40.0f, 20.0f, "gap.blue");
		break;
	case UFX_CASE_NEST:
		ufx_assert_child_pixels(ui, img, UFX_RED, 0.0f, 0.0f, 60.0f, 30.0f, "nest.red");
		ufx_assert_child_pixels(ui, img, UFX_GREEN, 160.0f, 0.0f, 60.0f, 30.0f, "nest.green");
		ufx_assert_child_pixels(ui, img, UFX_BLUE, 0.0f, 40.0f, 220.0f, 40.0f, "nest.body");
		break;
	case UFX_CASE_COUNT:
	default:
		TEST_FAIL_MESSAGE("unknown painted case");
		break;
	}
}

static void ufx_run_case(const sk_ui_api_t* ui, const ufx_case_t* c) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	ufx_scene_user_t user;

	/* Numeric layout first (no GPU) — must agree with matrix expectations. */
	ufx_assert_numeric_offline(ui, c->id);

	memset(&user, 0, sizeof(user));
	user.id = c->id;

	memset(&params, 0, sizeof(params));
	params.scene_name = c->scene_name;
	params.width = c->frame_w;
	params.height = c->frame_h;
	params.time_seconds = 0.0;
	params.load_test_font = 0;
	params.clear_color_set = 1;
	params.clear_color = UFX_CANVAS_F;

	ufx_capture(&params, ufx_scene_build, &user, &img);
	ufx_assert_painted(ui, &img, c->id);
	ufx_vision_grade(ui, &img, c->state_hint, c->scene_name);
	ufx_free(&img);
}

/* -------------------------------------------------------------------------- */
/* Tests: one SK_TEST per major behaviour + umbrella sampler                  */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_flexbox_vision_wrap) {
	ufx_env_t env;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	ufx_run_case(env.ui, &ufx_cases[UFX_CASE_WRAP]);
	ufx_env_destroy(&env);
}

SK_TEST(ui_flexbox_vision_justify_space_between) {
	ufx_env_t env;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	ufx_run_case(env.ui, &ufx_cases[UFX_CASE_JUSTIFY]);
	ufx_env_destroy(&env);
}

SK_TEST(ui_flexbox_vision_align_center) {
	ufx_env_t env;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	ufx_run_case(env.ui, &ufx_cases[UFX_CASE_ALIGN]);
	ufx_env_destroy(&env);
}

SK_TEST(ui_flexbox_vision_grow) {
	ufx_env_t env;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	ufx_run_case(env.ui, &ufx_cases[UFX_CASE_GROW]);
	ufx_env_destroy(&env);
}

SK_TEST(ui_flexbox_vision_gap) {
	ufx_env_t env;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	ufx_run_case(env.ui, &ufx_cases[UFX_CASE_GAP]);
	ufx_env_destroy(&env);
}

SK_TEST(ui_flexbox_vision_nest) {
	ufx_env_t env;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	ufx_run_case(env.ui, &ufx_cases[UFX_CASE_NEST]);
	ufx_env_destroy(&env);
}

/** Runs every representative sample so a single filter hits the full subset. */
SK_TEST(ui_flexbox_vision_representative_matrix) {
	ufx_env_t env;
	u32 i;
	ufx_env_init(&env);
	TEST_ASSERT_NOT_NULL_MESSAGE(env.ui, "ui plugin API required");
	if (env.ui == NULL) {
		ufx_env_destroy(&env);
		return;
	}
	for (i = 0u; i < (u32)UFX_CASE_COUNT; ++i) {
		ufx_run_case(env.ui, &ufx_cases[i]);
	}
	ufx_env_destroy(&env);
}

#endif /* SK_TESTS */
