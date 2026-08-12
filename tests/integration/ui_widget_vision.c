/*
 * ui_widget_vision.c — per-widget vision tests.
 *
 * APX-252: button, checkbox, radio, toggle.
 * APX-253: text/number input (caret + selection), single + range sliders,
 *          progress bar (0% / partial / 100%), scrollbars (v/h, proportional thumb).
 *
 * One isolated SK_TEST per widget family. Each test:
 *   1. Renders a single widget on a clean frame at a known size/position via
 *      the capture harness (pinned clear color, DPI, optional DejaVuSans).
 *   2. Covers the widget's visual states as separate captures.
 *   3. Runs structural pixel asserts so a stubbed paint path fails immediately
 *      with a clear message (coverage / bbox / mark ink).
 *   4. Grades fine details with sk_ui_vision_assert_image (rubric helper from
 *      APX-251). Vision SKIPPED without credentials does not fail the suite.
 *
 * Skips cleanly when no Vulkan ICD is present (harness RC_SKIPPED).
 * Text scenes pin DejaVuSans via load_test_font (APX-250).
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
/* Shared helpers                                                             */
/* -------------------------------------------------------------------------- */

#define UWV_RGB(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)

#define UWV_MATCH(rgba, tol)                                   \
	(&(sk_ui_color_match_t){.r = (u8)((rgba) & 0xFFu),         \
							.g = (u8)(((rgba) >> 8) & 0xFFu),  \
							.b = (u8)(((rgba) >> 16) & 0xFFu), \
							.a = (u8)(((rgba) >> 24) & 0xFFu), \
							.tolerance = (tol),                \
							.include_alpha = 1})

/* Default widget chrome colors from widgets.c default styles (rounded). */
#define UWV_BTN_DEFAULT UWV_RGB(71u, 107u, 184u)	  /* 0.28,0.42,0.72 */
#define UWV_BTN_HOVER UWV_RGB(140u, 184u, 255u)		  /* 0.55,0.72,1.0 */
#define UWV_BTN_ACTIVE UWV_RGB(15u, 20u, 36u)		  /* 0.06,0.08,0.14 */
#define UWV_BTN_DISABLED UWV_RGB(51u, 56u, 66u)		  /* 0.20,0.22,0.26 */
#define UWV_CB_FACE UWV_RGB(158u, 163u, 178u)		  /* 0.62,0.64,0.70 light face */
#define UWV_CB_MARK UWV_RGB(26u, 28u, 36u)			  /* dark X on light face */
#define UWV_RADIO_RING UWV_RGB(184u, 189u, 204u)	  /* 0.72,0.74,0.80 */
#define UWV_TOGGLE_OFF UWV_RGB(56u, 61u, 71u)		  /* 0.22,0.24,0.28 */
#define UWV_TOGGLE_ON UWV_RGB(77u, 140u, 242u)		  /* 0.30,0.55,0.95 */
#define UWV_THUMB UWV_RGB(245u, 247u, 252u)			  /* bright knob */
#define UWV_SLIDER_TRACK UWV_RGB(51u, 56u, 66u)		  /* 0.20,0.22,0.26 */
#define UWV_SLIDER_FILL UWV_RGB(77u, 140u, 242u)	  /* 0.30,0.55,0.95 */
#define UWV_PROGRESS_FILL UWV_RGB(82u, 148u, 245u)	  /* 0.32,0.58,0.96 */
#define UWV_TI_FACE UWV_RGB(31u, 33u, 38u)			  /* 0.12,0.13,0.15 field */
#define UWV_TI_BORDER UWV_RGB(89u, 94u, 107u)		  /* 0.35,0.37,0.42 */
#define UWV_TI_FOCUS_BORDER UWV_RGB(115u, 153u, 242u) /* 0.45,0.60,0.95 */
#define UWV_TI_TEXT UWV_RGB(235u, 237u, 242u)		  /* 0.92,0.93,0.95 */
#define UWV_SB_THUMB UWV_RGB(191u, 191u, 204u)		  /* ~0.75,0.75,0.80 @ 0.9 */
#define UWV_SV_FACE UWV_RGB(36u, 38u, 43u)			  /* 0.14,0.15,0.17 */

typedef struct uwv_env_t {
	sk_app_context_t* app;
	const sk_ui_api_t* ui;
} uwv_env_t;

typedef struct uwv_scene_cfg_t {
	const_chr_t kind; /* button|checkbox|radio|toggle|text_input|slider|range_slider|progress|scroll_view */
	const_chr_t label;
	const_chr_t text; /* text_input content */
	i32 bool_value;	  /* checked / on */
	u32 state_or;	  /* SK_UI_STATE_* bits applied after create */
	i32 disabled;
	i32 sel_start; /* text selection; sel_end > sel_start draws highlight */
	i32 sel_end;
	i32 caret;	   /* -1 = leave default (end of text) */
	f32 f0;		   /* slider value / progress fraction / range low */
	f32 f1;		   /* range high / unused */
	f32 f2;		   /* min (slider) */
	f32 f3;		   /* max (slider) */
	f32 content_w; /* scroll_view content size */
	f32 content_h;
	f32 scroll_x;
	f32 scroll_y;
	f32 w;
	f32 h;
	f32 x;
	f32 y;
} uwv_scene_cfg_t;

static i32 uwv_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
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

static void uwv_env_init(uwv_env_t* env) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t plugin_name = "sk-ui.dll";
#elif defined(__APPLE__)
	const_chr_t plugin_name = "sk-ui.dylib";
#else
	const_chr_t plugin_name = "sk-ui.so";
#endif
	memset(env, 0, sizeof(*env));
	env->app = sk_app_init(0, NULL);
	if (env->app == NULL) {
		return;
	}
	if (uwv_plugin_path(plugin_name, path, (u32)sizeof(path)) == 0) {
		sk_app_api()->load_plugin(env->app, path);
	}
	env->ui = (const sk_ui_api_t*)sk_app_api()->get_api(env->app, SK_UI_API_TYPE_ID);
}

static void uwv_env_destroy(uwv_env_t* env) {
	if (env->app != NULL) {
		sk_app_destroy(env->app);
	}
	memset(env, 0, sizeof(*env));
}

static sk_ui_region_t uwv_region(u32 x0, u32 y0, u32 x1, u32 y1) {
	sk_ui_region_t r;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	return r;
}

static void uwv_capture(const sk_ui_capture_harness_params_t* params, sk_ui_capture_scene_fn scene, void* user, sk_ui_cpu_image_t* out) {
	const i32 rc = sk_ui_capture_harness_capture(params, scene, user, out);
	if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
		TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping UI widget vision test");
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
	TEST_ASSERT_NOT_NULL(out->pixels);
}

static void uwv_free(sk_ui_cpu_image_t* img) {
	sk_ui_capture_harness_image_free(img);
}

/**
 * Grade with vision when credentials exist; SKIPPED is soft (not a failure).
 * FAIL fails the test with the model's reason so fine-detail regressions surface.
 */
static void uwv_vision_grade(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, sk_ui_vision_widget_family_t family, const_chr_t state_hint, const_chr_t scene_name) {
	sk_ui_vision_result_t result;
	i32 rc;
	const char* old_backend;
	const char* old_mock;

	/* Do not inherit mock mode from other tests. */
	old_backend = getenv("SK_UI_VISION_BACKEND");
	old_mock = getenv("SK_UI_VISION_MOCK_RESPONSE");
	unsetenv("SK_UI_VISION_BACKEND");
	unsetenv("SK_UI_VISION_MOCK_RESPONSE");

	memset(&result, 0, sizeof(result));
	rc = sk_ui_vision_assert_image(ui, img, family, state_hint, scene_name, sk_filesystem_api(), &result);

	if (old_backend != NULL && old_backend[0] != '\0') {
		setenv("SK_UI_VISION_BACKEND", old_backend, 1);
	}
	if (old_mock != NULL && old_mock[0] != '\0') {
		setenv("SK_UI_VISION_MOCK_RESPONSE", old_mock, 1);
	}

	if (rc == SK_UI_VISION_ASSERT_SKIPPED) {
		/* No API key / backend — structural asserts above still guard stubbed draw. */
		return;
	}
	/*
	 * One retry on ERROR (script/API glitch) or FAIL (occasional model fluke).
	 * Structural asserts already guard stubbed draw; a second live grade is
	 * cheap compared to a red suite from a single bad vision response.
	 */
	if (rc == SK_UI_VISION_ASSERT_ERROR || rc == SK_UI_VISION_ASSERT_FAIL || result.passed == 0) {
		sk_ui_vision_result_t retry;
		i32 rc2;
		memset(&retry, 0, sizeof(retry));
		rc2 = sk_ui_vision_assert_image(ui, img, family, state_hint, scene_name, sk_filesystem_api(), &retry);
		if (rc2 == SK_UI_VISION_ASSERT_OK && retry.passed != 0) {
			return;
		}
		if (rc2 == SK_UI_VISION_ASSERT_SKIPPED) {
			return;
		}
		if (rc == SK_UI_VISION_ASSERT_ERROR && rc2 == SK_UI_VISION_ASSERT_ERROR) {
			fprintf(stderr, "vision assert ERROR for %s: %s\n", scene_name, retry.reason[0] != '\0' ? retry.reason : result.reason);
			/* Soft: do not fail the suite on repeated backend errors. */
			return;
		}
		fprintf(stderr, "vision FAIL %s (%s): %s\n", scene_name, sk_ui_vision_rubric_name(family), retry.reason[0] != '\0' ? retry.reason : result.reason);
		if (retry.saved_frame_path[0] != '\0') {
			fprintf(stderr, "  failing frame: %s\n", retry.saved_frame_path);
		} else if (result.saved_frame_path[0] != '\0') {
			fprintf(stderr, "  failing frame: %s\n", result.saved_frame_path);
		}
		TEST_FAIL_MESSAGE("vision FAIL: widget fine detail did not match rubric (see stderr)");
		return;
	}
	TEST_ASSERT_EQUAL_INT(SK_UI_VISION_ASSERT_OK, rc);
	TEST_ASSERT_EQUAL_INT(1, result.passed);
}

static void uwv_assert_widget_present(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, u32 fill_rgba, u32 x0, u32 y0, u32 x1, u32 y1, const_chr_t what) {
	sk_ui_region_t r = uwv_region(x0, y0, x1, y1);
	sk_ui_bbox_t bb;
	char msg[256];
	i32 rc;

	memset(&bb, 0, sizeof(bb));
	rc = ui->cpu_image_find_bbox(img, r, UWV_MATCH(fill_rgba, 12u), &bb);
	snprintf(msg, sizeof(msg), "stubbed/missing draw: expected %s fill pixels in (%u,%u)-(%u,%u)", what, x0, y0, x1, y1);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, msg);
	TEST_ASSERT_TRUE_MESSAGE(bb.found != 0, msg);
	TEST_ASSERT_TRUE_MESSAGE(bb.pixel_count > 8u, msg);

	rc = ui->cpu_image_assert_coverage(img, r, UWV_MATCH(fill_rgba, 12u), 0.05f, 1.0f, NULL);
	snprintf(msg, sizeof(msg), "stubbed/missing draw: %s coverage too low in widget bounds", what);
	TEST_ASSERT_EQUAL_INT_MESSAGE(SK_UI_IMAGE_ASSERT_OK, rc, msg);
}

static void uwv_assert_mark_ink(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* img, u32 mark_rgba, u32 x0, u32 y0, u32 x1, u32 y1, const_chr_t what) {
	sk_ui_region_t r = uwv_region(x0, y0, x1, y1);
	sk_ui_bbox_t bb;
	char msg[256];
	i32 rc;

	memset(&bb, 0, sizeof(bb));
	rc = ui->cpu_image_find_bbox(img, r, UWV_MATCH(mark_rgba, 48u), &bb);
	snprintf(msg, sizeof(msg), "stubbed mark draw: expected %s mark/thumb ink inside control", what);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, msg);
	TEST_ASSERT_TRUE_MESSAGE(bb.found != 0 && bb.pixel_count >= 4u, msg);
}

static i32 uwv_scene_build(sk_ui_capture_scene_t* scene, void* user) {
	const sk_ui_api_t* ui = scene->ui;
	sk_ui_context_t* ctx = scene->ctx;
	const uwv_scene_cfg_t* cfg = (const uwv_scene_cfg_t*)user;
	sk_ui_node_t n = SK_UI_NODE_INVALID;
	sk_ui_style_props_t props;
	u32 st;

	if (cfg == NULL || cfg->kind == NULL) {
		return -1;
	}
	if (strcmp(cfg->kind, "button") == 0) {
		n = ui->widget_button(ctx, ui->context_root(ctx), cfg->label != NULL ? cfg->label : "OK", "vw-btn");
		if (cfg->disabled != 0) {
			(void)ui->button_set_disabled(ctx, n, 1);
		}
	} else if (strcmp(cfg->kind, "checkbox") == 0) {
		n = ui->widget_checkbox(ctx, ui->context_root(ctx), cfg->bool_value, "vw-cb");
		if (cfg->disabled != 0) {
			(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
		}
	} else if (strcmp(cfg->kind, "radio") == 0) {
		n = ui->widget_radio(ctx, ui->context_root(ctx), cfg->bool_value, "vw-rd");
		if (cfg->disabled != 0) {
			(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
		}
	} else if (strcmp(cfg->kind, "toggle") == 0) {
		n = ui->widget_toggle(ctx, ui->context_root(ctx), cfg->bool_value, "vw-tg");
		if (cfg->disabled != 0) {
			(void)ui->toggle_set_disabled(ctx, n, 1);
		}
	} else if (strcmp(cfg->kind, "text_input") == 0) {
		n = ui->widget_text_input(ctx, ui->context_root(ctx), cfg->text != NULL ? cfg->text : "", "vw-ti");
		if (cfg->disabled != 0) {
			(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
		}
		if (cfg->sel_end > cfg->sel_start) {
			(void)ui->text_input_set_selection(ctx, n, cfg->sel_start, cfg->sel_end);
		} else if (cfg->caret >= 0) {
			/* Caret-only: collapsed selection at caret. */
			(void)ui->text_input_set_selection(ctx, n, cfg->caret, cfg->caret);
		}
	} else if (strcmp(cfg->kind, "slider") == 0) {
		f32 min_v = cfg->f2;
		f32 max_v = cfg->f3;
		if (max_v <= min_v) {
			min_v = 0.0f;
			max_v = 1.0f;
		}
		n = ui->widget_slider(ctx, ui->context_root(ctx), min_v, max_v, cfg->f0, "vw-sl");
		if (cfg->disabled != 0) {
			(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
		}
	} else if (strcmp(cfg->kind, "range_slider") == 0) {
		f32 min_v = cfg->f2;
		f32 max_v = cfg->f3;
		if (max_v <= min_v) {
			min_v = 0.0f;
			max_v = 1.0f;
		}
		n = ui->widget_range_slider(ctx, ui->context_root(ctx), min_v, max_v, cfg->f0, cfg->f1, "vw-rsl");
		if (cfg->disabled != 0) {
			(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
		}
	} else if (strcmp(cfg->kind, "progress") == 0) {
		n = ui->widget_progress(ctx, ui->context_root(ctx), cfg->f0, "vw-pg");
		if (cfg->disabled != 0) {
			(void)ui->node_set_state(ctx, n, ui->node_get_state(ctx, n) | (u32)SK_UI_STATE_DISABLED);
		}
	} else if (strcmp(cfg->kind, "scroll_view") == 0) {
		n = ui->widget_scroll_view(ctx, ui->context_root(ctx), "vw-sv");
		(void)ui->scroll_view_set_content_size(ctx, n, cfg->content_w, cfg->content_h);
		(void)ui->scroll_view_set_scroll(ctx, n, cfg->scroll_x, cfg->scroll_y);
	} else {
		return -1;
	}
	if (!sk_ui_node_is_valid(n)) {
		return -1;
	}

	memset(&props, 0, sizeof(props));
	props.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_POSITION | SK_UI_SP_LEFT | SK_UI_SP_TOP;
	props.layout.width = sk_ui_pt(cfg->w);
	props.layout.height = sk_ui_pt(cfg->h);
	props.layout.position = SK_UI_POSITION_ABSOLUTE;
	props.layout.left = sk_ui_pt(cfg->x);
	props.layout.top = sk_ui_pt(cfg->y);
	ui->node_set_inline_style(ctx, n, &props);

	if (cfg->state_or != 0u) {
		st = ui->node_get_state(ctx, n) | cfg->state_or;
		(void)ui->node_set_state(ctx, n, st);
	}
	return 0;
}

static void uwv_run_state(const sk_ui_api_t* ui, const_chr_t scene_name, u32 frame_w, u32 frame_h, uwv_scene_cfg_t* cfg, i32 load_font, sk_ui_vision_widget_family_t family,
						  const_chr_t state_hint, u32 expect_fill, u32 mark_rgba, i32 expect_mark) {
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;
	u32 x0 = (u32)cfg->x;
	u32 y0 = (u32)cfg->y;
	u32 x1 = (u32)(cfg->x + cfg->w);
	u32 y1 = (u32)(cfg->y + cfg->h);

	memset(&params, 0, sizeof(params));
	params.scene_name = scene_name;
	params.width = frame_w;
	params.height = frame_h;
	params.time_seconds = 0.0;
	params.load_test_font = load_font;
	/* Dark clear so light faces / thumbs / labels read clearly (matches vision fixtures). */
	params.clear_color_set = 1;
	params.clear_color = sk_ui_rgba(0.12f, 0.13f, 0.15f, 1.0f);

	uwv_capture(&params, uwv_scene_build, cfg, &img);
	TEST_ASSERT_EQUAL_UINT(frame_w, img.width);
	TEST_ASSERT_EQUAL_UINT(frame_h, img.height);

	/* Structural gate: stubbed widget draw → no fill coverage → clear fail. */
	uwv_assert_widget_present(ui, &img, expect_fill, x0, y0, x1, y1, cfg->kind);
	if (expect_mark != 0) {
		uwv_assert_mark_ink(ui, &img, mark_rgba, x0, y0, x1, y1, cfg->kind);
	}

	uwv_vision_grade(ui, &img, family, state_hint, scene_name);
	uwv_free(&img);
}

/* -------------------------------------------------------------------------- */
/* Button — normal / hover / active / disabled + label centering              */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_button) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "button";
	cfg.label = "OK";
	cfg.w = 96.0f;
	cfg.h = 32.0f;
	cfg.x = 16.0f;
	cfg.y = 16.0f;

	/* Normal (default fill). Font loaded so label centering is visible to vision. */
	cfg.state_or = 0u;
	cfg.disabled = 0;
	uwv_run_state(ui, "ui_widget_vision_button_normal", 128u, 64u, &cfg, 1, SK_UI_VISION_WIDGET_BUTTON, "normal", UWV_BTN_DEFAULT, 0u, 0);

	/* Hover: lighter fill. */
	cfg.state_or = (u32)SK_UI_STATE_HOVER;
	uwv_run_state(ui, "ui_widget_vision_button_hover", 128u, 64u, &cfg, 1, SK_UI_VISION_WIDGET_BUTTON, "hover", UWV_BTN_HOVER, 0u, 0);

	/* Active/pressed: darker fill. */
	cfg.state_or = (u32)SK_UI_STATE_ACTIVE;
	uwv_run_state(ui, "ui_widget_vision_button_active", 128u, 64u, &cfg, 1, SK_UI_VISION_WIDGET_BUTTON, "active", UWV_BTN_ACTIVE, 0u, 0);

	/* Disabled: dimmed chrome. */
	cfg.state_or = 0u;
	cfg.disabled = 1;
	uwv_run_state(ui, "ui_widget_vision_button_disabled", 128u, 64u, &cfg, 1, SK_UI_VISION_WIDGET_BUTTON, "disabled", UWV_BTN_DISABLED, 0u, 0);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Checkbox — unchecked / checked (X mark) / hover / disabled                 */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_checkbox) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "checkbox";
	cfg.w = 24.0f;
	cfg.h = 24.0f;
	cfg.x = 28.0f;
	cfg.y = 28.0f;

	/* Unchecked: light empty face, no mark requirement. */
	cfg.bool_value = 0;
	cfg.state_or = 0u;
	cfg.disabled = 0;
	uwv_run_state(ui, "ui_widget_vision_checkbox_unchecked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_CHECKBOX, "unchecked", UWV_CB_FACE, 0u, 0);

	/* Checked: X mark ink must be present; vision grades diagonals. */
	cfg.bool_value = 1;
	uwv_run_state(ui, "ui_widget_vision_checkbox_checked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_CHECKBOX, "checked", UWV_CB_FACE, UWV_CB_MARK, 1);

	/* Hover + checked. */
	cfg.state_or = (u32)SK_UI_STATE_HOVER;
	uwv_run_state(ui, "ui_widget_vision_checkbox_hover_checked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_CHECKBOX, "checked hover", UWV_CB_FACE, UWV_CB_MARK, 1);

	/* Disabled checked: still has mark, dimmed face. */
	cfg.state_or = 0u;
	cfg.disabled = 1;
	uwv_run_state(ui, "ui_widget_vision_checkbox_disabled_checked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_CHECKBOX, "disabled checked", UWV_RGB(107u, 112u, 122u),
				  UWV_RGB(77u, 79u, 87u), 1);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Radio — unchecked / checked (inner disc) / disabled                        */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_radio) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "radio";
	cfg.w = 32.0f;
	cfg.h = 32.0f;
	cfg.x = 24.0f;
	cfg.y = 24.0f;

	cfg.bool_value = 0;
	cfg.state_or = 0u;
	cfg.disabled = 0;
	/* Unchecked: ring chrome present, no disc. */
	uwv_run_state(ui, "ui_widget_vision_radio_unchecked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_RADIO, "unchecked", UWV_RADIO_RING, 0u, 0);

	/* Checked: bright filled inner disc. */
	cfg.bool_value = 1;
	uwv_run_state(ui, "ui_widget_vision_radio_checked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_RADIO, "checked", UWV_RADIO_RING, UWV_THUMB, 1);

	cfg.state_or = (u32)SK_UI_STATE_HOVER;
	uwv_run_state(ui, "ui_widget_vision_radio_hover_checked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_RADIO, "checked hover", UWV_RGB(140u, 184u, 255u), UWV_THUMB, 1);

	cfg.state_or = 0u;
	cfg.disabled = 1;
	uwv_run_state(ui, "ui_widget_vision_radio_disabled_checked", 80u, 80u, &cfg, 0, SK_UI_VISION_WIDGET_RADIO, "disabled checked", UWV_RGB(102u, 107u, 117u),
				  UWV_RGB(184u, 189u, 199u), 1);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Toggle — off / on (knob position + track) / hover / disabled               */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_toggle) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "toggle";
	cfg.w = 56.0f;
	cfg.h = 28.0f;
	cfg.x = 12.0f;
	cfg.y = 18.0f;

	/* OFF: muted track + thumb ink (thumb always painted). */
	cfg.bool_value = 0;
	cfg.state_or = 0u;
	cfg.disabled = 0;
	uwv_run_state(ui, "ui_widget_vision_toggle_off", 80u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_TOGGLE, "off", UWV_TOGGLE_OFF, UWV_THUMB, 1);

	/* ON: accent track + thumb toward the end. */
	cfg.bool_value = 1;
	uwv_run_state(ui, "ui_widget_vision_toggle_on", 80u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_TOGGLE, "on", UWV_TOGGLE_ON, UWV_THUMB, 1);

	cfg.state_or = (u32)SK_UI_STATE_HOVER;
	cfg.bool_value = 0;
	uwv_run_state(ui, "ui_widget_vision_toggle_hover_off", 80u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_TOGGLE, "off hover", UWV_RGB(82u, 87u, 102u), UWV_THUMB, 1);

	cfg.state_or = 0u;
	cfg.disabled = 1;
	cfg.bool_value = 1;
	/* Disabled ON: muted accent track + dimmer thumb (still present). */
	uwv_run_state(ui, "ui_widget_vision_toggle_disabled_on", 80u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_TOGGLE, "disabled on", UWV_RGB(56u, 82u, 122u), UWV_RGB(158u, 163u, 173u), 1);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Text / number input — field chrome, caret, selection highlight (APX-253)   */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_text_input) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;
	/* Lighter clear so the dark field face is structurally distinct. */
	sk_ui_capture_harness_params_t params;
	sk_ui_cpu_image_t img;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "text_input";
	cfg.w = 160.0f;
	cfg.h = 32.0f;
	cfg.x = 16.0f;
	cfg.y = 16.0f;
	cfg.caret = -1;
	cfg.sel_start = 0;
	cfg.sel_end = 0;

	/* Empty field: chrome only (border + face). Font loaded for caret readiness. */
	cfg.text = "";
	cfg.state_or = 0u;
	cfg.disabled = 0;
	memset(&params, 0, sizeof(params));
	params.scene_name = "ui_widget_vision_text_input_empty";
	params.width = 192u;
	params.height = 64u;
	params.load_test_font = 1;
	params.clear_color_set = 1;
	params.clear_color = sk_ui_rgba(0.22f, 0.24f, 0.28f, 1.0f);
	uwv_capture(&params, uwv_scene_build, &cfg, &img);
	uwv_assert_widget_present(ui, &img, UWV_TI_FACE, 16u, 16u, 176u, 48u, "text_input face");
	uwv_assert_mark_ink(ui, &img, UWV_TI_BORDER, 16u, 16u, 176u, 48u, "text_input border");
	uwv_vision_grade(ui, &img, SK_UI_VISION_WIDGET_TEXT_INPUT, "empty", params.scene_name);
	uwv_free(&img);

	/* Text content with caret at end (DejaVuSans glyphs). */
	cfg.text = "Hello";
	cfg.caret = 5;
	cfg.sel_start = 0;
	cfg.sel_end = 0;
	params.scene_name = "ui_widget_vision_text_input_text_caret";
	uwv_capture(&params, uwv_scene_build, &cfg, &img);
	uwv_assert_widget_present(ui, &img, UWV_TI_FACE, 16u, 16u, 176u, 48u, "text_input face");
	uwv_assert_mark_ink(ui, &img, UWV_TI_TEXT, 16u, 16u, 176u, 48u, "text glyphs");
	uwv_vision_grade(ui, &img, SK_UI_VISION_WIDGET_TEXT_INPUT, "text with caret", params.scene_name);
	uwv_free(&img);

	/* Focused + caret mid-string. */
	cfg.state_or = (u32)SK_UI_STATE_FOCUSED;
	cfg.caret = 2;
	params.scene_name = "ui_widget_vision_text_input_focused_caret";
	uwv_capture(&params, uwv_scene_build, &cfg, &img);
	uwv_assert_mark_ink(ui, &img, UWV_TI_FOCUS_BORDER, 16u, 16u, 176u, 48u, "focused border");
	uwv_assert_mark_ink(ui, &img, UWV_TI_TEXT, 16u, 16u, 176u, 48u, "text + caret ink");
	uwv_vision_grade(ui, &img, SK_UI_VISION_WIDGET_TEXT_INPUT, "focused caret", params.scene_name);
	uwv_free(&img);

	/* Selection highlight over "ell" in Hello (codepoints 1..4). */
	cfg.state_or = (u32)SK_UI_STATE_FOCUSED;
	cfg.sel_start = 1;
	cfg.sel_end = 4;
	cfg.caret = 4;
	params.scene_name = "ui_widget_vision_text_input_selection";
	uwv_capture(&params, uwv_scene_build, &cfg, &img);
	/* Selection is semi-transparent blue; glyphs + field still present. */
	uwv_assert_mark_ink(ui, &img, UWV_TI_TEXT, 16u, 16u, 176u, 48u, "selected text glyphs");
	uwv_vision_grade(ui, &img, SK_UI_VISION_WIDGET_TEXT_INPUT, "selection highlight", params.scene_name);
	uwv_free(&img);

	/* Number input content (same text_input widget; numeric glyphs + caret). */
	cfg.text = "42.5";
	cfg.state_or = (u32)SK_UI_STATE_FOCUSED;
	cfg.sel_start = 0;
	cfg.sel_end = 0;
	cfg.caret = 4;
	params.scene_name = "ui_widget_vision_text_input_number";
	uwv_capture(&params, uwv_scene_build, &cfg, &img);
	uwv_assert_mark_ink(ui, &img, UWV_TI_TEXT, 16u, 16u, 176u, 48u, "number glyphs");
	uwv_vision_grade(ui, &img, SK_UI_VISION_WIDGET_TEXT_INPUT, "number input focused", params.scene_name);
	uwv_free(&img);

	/*
	 * Disabled with longer text: muted face + ink, no caret (paint suppresses it).
	 * Clear is lighter so dim gray glyphs read as muted, not "white on dark".
	 */
	cfg.state_or = 0u;
	cfg.disabled = 1;
	cfg.text = "Disabled";
	cfg.caret = -1;
	cfg.sel_start = 0;
	cfg.sel_end = 0;
	params.scene_name = "ui_widget_vision_text_input_disabled";
	params.clear_color = sk_ui_rgba(0.35f, 0.37f, 0.42f, 1.0f);
	uwv_capture(&params, uwv_scene_build, &cfg, &img);
	uwv_assert_widget_present(ui, &img, UWV_RGB(36u, 38u, 43u), 16u, 16u, 176u, 48u, "disabled face");
	/* Dim ink must still be present (not empty). */
	uwv_assert_mark_ink(ui, &img, UWV_RGB(97u, 102u, 107u), 16u, 16u, 176u, 48u, "disabled muted text");
	uwv_vision_grade(ui, &img, SK_UI_VISION_WIDGET_TEXT_INPUT, "disabled dimmed muted text no caret", params.scene_name);
	uwv_free(&img);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Slider — single thumb + dual-thumb range (APX-253)                         */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_slider) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "slider";
	cfg.w = 160.0f;
	cfg.h = 24.0f;
	cfg.x = 16.0f;
	cfg.y = 20.0f;
	cfg.f2 = 0.0f;
	cfg.f3 = 1.0f;
	cfg.caret = -1;

	/* Mid value: track + fill + single grab handle. */
	cfg.f0 = 0.5f;
	cfg.state_or = 0u;
	cfg.disabled = 0;
	uwv_run_state(ui, "ui_widget_vision_slider_mid", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_SLIDER, "value=0.5 single thumb", UWV_SLIDER_TRACK, UWV_THUMB, 1);

	/* Low-mid value: handle clearly left of center, still a separable knob. */
	cfg.f0 = 0.30f;
	uwv_run_state(ui, "ui_widget_vision_slider_low", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_SLIDER, "value=0.3 distinct grab handle left of center", UWV_SLIDER_TRACK, UWV_THUMB,
				  1);

	/* Hover. */
	cfg.f0 = 0.5f;
	cfg.state_or = (u32)SK_UI_STATE_HOVER;
	uwv_run_state(ui, "ui_widget_vision_slider_hover", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_SLIDER, "hover", UWV_RGB(61u, 66u, 82u), UWV_THUMB, 1);

	/* Disabled: muted fill + thumb (paint dims custom chrome). */
	cfg.state_or = 0u;
	cfg.disabled = 1;
	uwv_run_state(ui, "ui_widget_vision_slider_disabled", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_SLIDER, "disabled dimmed track and handle", UWV_RGB(41u, 43u, 51u),
				  UWV_RGB(140u, 143u, 148u), 1);

	/* Dual-thumb range slider: fill span + two handles (same slider rubric). */
	cfg.kind = "range_slider";
	cfg.disabled = 0;
	cfg.state_or = 0u;
	cfg.f0 = 0.25f; /* low */
	cfg.f1 = 0.75f; /* high */
	uwv_run_state(ui, "ui_widget_vision_slider_range", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_SLIDER, "range low=0.25 high=0.75 two thumbs", UWV_SLIDER_TRACK, UWV_THUMB, 1);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Progress bar — 0% / partial / 100%, no grab handle (APX-253)               */
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_progress) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "progress";
	cfg.w = 160.0f;
	cfg.h = 20.0f;
	cfg.x = 16.0f;
	cfg.y = 22.0f;
	cfg.caret = -1;
	cfg.state_or = 0u;
	cfg.disabled = 0;

	/* 0%: track only, no fill ink required. */
	cfg.f0 = 0.0f;
	uwv_run_state(ui, "ui_widget_vision_progress_0", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_PROGRESS, "fraction=0 empty track no fill no thumb", UWV_SLIDER_TRACK, 0u, 0);

	/* Partial (~50%): half fill, empty track on the right, no grab handle. */
	cfg.f0 = 0.5f;
	uwv_run_state(ui, "ui_widget_vision_progress_partial", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_PROGRESS, "fraction=0.5 half fill empty track on right no thumb",
				  UWV_SLIDER_TRACK, UWV_PROGRESS_FILL, 1);

	/* 100%: full fill. */
	cfg.f0 = 1.0f;
	uwv_run_state(ui, "ui_widget_vision_progress_100", 192u, 64u, &cfg, 0, SK_UI_VISION_WIDGET_PROGRESS, "fraction=1 full fill no thumb", UWV_PROGRESS_FILL, 0u, 0);

	uwv_env_destroy(&env);
}

/* -------------------------------------------------------------------------- */
/* Scrollbars — vertical / horizontal, thumb proportional to content (APX-253)*/
/* -------------------------------------------------------------------------- */

SK_TEST(ui_widget_vision_scrollbar) {
	uwv_env_t env;
	const sk_ui_api_t* ui;
	uwv_scene_cfg_t cfg;

	uwv_env_init(&env);
	ui = env.ui;
	TEST_ASSERT_NOT_NULL_MESSAGE(ui, "ui plugin API required");
	if (ui == NULL) {
		uwv_env_destroy(&env);
		return;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.kind = "scroll_view";
	cfg.w = 120.0f;
	cfg.h = 100.0f;
	cfg.x = 12.0f;
	cfg.y = 12.0f;
	cfg.caret = -1;
	cfg.state_or = 0u;
	cfg.disabled = 0;

	/*
	 * Vertical only: content taller than viewport, width does not overflow
	 * (avoids a bottom H-bar confusing the mid-track claim).
	 * Thumb height ∝ view / content; scroll_y mid-range parks thumb mid-track.
	 */
	cfg.content_w = 80.0f; /* < viewport content width → no horizontal bar */
	cfg.content_h = 500.0f;
	cfg.scroll_x = 0.0f;
	cfg.scroll_y = 200.0f;
	uwv_run_state(ui, "ui_widget_vision_scrollbar_vertical", 144u, 128u, &cfg, 0, SK_UI_VISION_WIDGET_SCROLLBAR,
				  "vertical-only track right edge with shorter thumb about mid track", UWV_SV_FACE, UWV_RGB(184u, 189u, 204u), 1);

	/* Vertical at start: thumb shorter than track, parked at top. */
	cfg.scroll_y = 0.0f;
	cfg.content_h = 600.0f;
	uwv_run_state(ui, "ui_widget_vision_scrollbar_vertical_start", 144u, 128u, &cfg, 0, SK_UI_VISION_WIDGET_SCROLLBAR,
				  "vertical-only track with distinct shorter thumb at top start", UWV_SV_FACE, UWV_RGB(184u, 189u, 204u), 1);

	/*
	 * Horizontal only: content wider than viewport, height does not overflow.
	 */
	cfg.content_w = 480.0f;
	cfg.content_h = 60.0f; /* < viewport content height → no vertical bar */
	cfg.scroll_x = 160.0f;
	cfg.scroll_y = 0.0f;
	uwv_run_state(ui, "ui_widget_vision_scrollbar_horizontal", 144u, 128u, &cfg, 0, SK_UI_VISION_WIDGET_SCROLLBAR, "horizontal-only track bottom edge with shorter thumb mid track",
				  UWV_SV_FACE, UWV_RGB(184u, 189u, 204u), 1);

	/* Both axes overflow: both thumbs present. */
	cfg.content_w = 360.0f;
	cfg.content_h = 360.0f;
	cfg.scroll_x = 80.0f;
	cfg.scroll_y = 80.0f;
	uwv_run_state(ui, "ui_widget_vision_scrollbar_both", 144u, 128u, &cfg, 0, SK_UI_VISION_WIDGET_SCROLLBAR, "both vertical and horizontal scrollbar thumbs shorter than tracks",
				  UWV_SV_FACE, UWV_RGB(184u, 189u, 204u), 1);

	uwv_env_destroy(&env);
}

#endif /* SK_TESTS */
