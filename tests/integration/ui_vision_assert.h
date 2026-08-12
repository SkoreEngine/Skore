/*
 * ui_vision_assert.h — per-widget grok-vision assertion helper (APX-251).
 *
 * Builds on the existing grok-vision integration path used for UI snapshot
 * audits: a rendered widget frame is graded against a strict per-widget-family
 * rubric (not a vague "does this look right"). The helper:
 *
 *   1. Resolves the authored rubric text for the widget family.
 *   2. Asks grok vision (scripts/ui_vision_assert.py → xAI chat/vision) to
 *      confirm fine-grained details (checkbox X mark, radio inner disc,
 *      slider grab handle, disabled dimming, …).
 *   3. Returns a structured pass/fail plus the model's stated reason.
 *   4. On FAIL, saves the offending frame under the shared test-artifact root
 *      as {name}_vision_fail.png so CI / agents can inspect it.
 *
 * Backend selection (env SK_UI_VISION_BACKEND):
 *   - "script" or unset: run scripts/ui_vision_assert.py (needs API key or mock)
 *   - "mock": use SK_UI_VISION_MOCK_RESPONSE JSON (deterministic unit tests)
 *
 * Auth for live vision: XAI_API_KEY / SK_UI_VISION_API_KEY (see the script).
 * Without credentials the helper returns SK_UI_VISION_ASSERT_SKIPPED so suites
 * that require live vision can TEST_IGNORE cleanly.
 */

#ifndef SK_UI_VISION_ASSERT_H
#define SK_UI_VISION_ASSERT_H

#include "ui.h"

#include "filesystem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes from sk_ui_vision_assert*. OK is 0 for `if (rc != 0)` checks. */
#define SK_UI_VISION_ASSERT_OK 0	   /**< Rubric satisfied (model pass=true). */
#define SK_UI_VISION_ASSERT_FAIL 1	   /**< Rubric failed (model pass=false); frame saved when possible. */
#define SK_UI_VISION_ASSERT_SKIPPED 2  /**< No vision backend / credentials; not a content failure. */
#define SK_UI_VISION_ASSERT_ERROR (-1) /**< Hard failure (I/O, bad args, backend crash). */

/**
 * Widget families that have authored vision rubrics under
 * plugins/ui/testdata/vision/rubrics/. Keep in sync with
 * sk_ui_vision_rubric_name / sk_ui_vision_rubric_text.
 */
typedef enum sk_ui_vision_widget_family_t {
	SK_UI_VISION_WIDGET_BUTTON = 0,
	SK_UI_VISION_WIDGET_CHECKBOX,
	SK_UI_VISION_WIDGET_RADIO,
	SK_UI_VISION_WIDGET_TOGGLE,
	SK_UI_VISION_WIDGET_SLIDER,
	SK_UI_VISION_WIDGET_PROGRESS,
	SK_UI_VISION_WIDGET_TEXT_INPUT,
	SK_UI_VISION_WIDGET_SCROLLBAR,
	SK_UI_VISION_WIDGET_SCROLL_VIEW,
	SK_UI_VISION_WIDGET_PANEL,
	SK_UI_VISION_WIDGET_LABEL,
	SK_UI_VISION_WIDGET_IMAGE,
	SK_UI_VISION_WIDGET_WINDOW,	  /**< Editor window / titled panel chrome (APX-254). */
	SK_UI_VISION_WIDGET_TAB,	  /**< Tab bar selected vs unselected (APX-254). */
	SK_UI_VISION_WIDGET_MENU,	  /**< Dropdown / menu popup with items (APX-254). */
	SK_UI_VISION_WIDGET_TABLE,	  /**< List/table header + body rows (APX-254). */
	SK_UI_VISION_WIDGET_TOOLTIP,  /**< Tooltip / floating popup (APX-254). */
	SK_UI_VISION_WIDGET_DISABLED, /**< Cross-cutting disabled-state rubric. */
	SK_UI_VISION_WIDGET_COUNT
} sk_ui_vision_widget_family_t;

/**
 * Structured vision grade. Always filled by sk_ui_vision_assert* (even on
 * ERROR/SKIPPED). reason is the model's free-text justification (or a helper
 * diagnostic). saved_frame_path is non-empty when a failing frame was written.
 */
typedef struct sk_ui_vision_result_t {
	i32 passed;							   /**< 1 = pass, 0 = fail / skip / error. */
	i32 skipped;						   /**< 1 = backend unavailable (not a content fail). */
	char reason[1024];					   /**< Model reason or helper diagnostic. */
	char saved_frame_path[SK_FS_PATH_MAX]; /**< Offending frame path on FAIL; else empty. */
	char family_name[64];				   /**< Canonical family name used for the grade. */
} sk_ui_vision_result_t;

/** Canonical short name for @p family ("checkbox", "slider", …). Never NULL. */
const_chr_t sk_ui_vision_rubric_name(sk_ui_vision_widget_family_t family);

/**
 * Authored rubric text for @p family (static storage). Never NULL for a valid
 * family enum; empty string only if the catalog entry is missing (should not
 * happen — guarded by unit tests).
 */
const_chr_t sk_ui_vision_rubric_text(sk_ui_vision_widget_family_t family);

/**
 * Parse a family name ("checkbox", "text_input", "disabled", …).
 * @return 0 on success, non-zero if @p name is unknown.
 */
i32 sk_ui_vision_widget_family_parse(const_chr_t name, sk_ui_vision_widget_family_t* out_family);

/**
 * Resolve the on-disk path of the rubric file for @p family under the UI
 * testdata vision tree (…/vision/rubrics/{name}.txt).
 * @return 0 on success, non-zero if @p out is too small.
 */
i32 sk_ui_vision_rubric_file_path(sk_ui_vision_widget_family_t family, char* out, u32 out_cap);

/**
 * Grade an on-disk PNG/JPEG frame against the per-family rubric via grok vision.
 *
 * @param image_path   Existing frame path (required for the vision backend).
 * @param image        Optional in-memory copy; when non-NULL and the grade is
 *                     FAIL, the helper also writes these pixels to the artifact
 *                     root (preferred over copying image_path when both set).
 * @param family       Widget family selecting the rubric.
 * @param state_hint   Optional claimed state ("checked", "disabled", …); may be NULL.
 * @param scene_name   Base name for failure artifacts (sanitized); may be NULL → "vision".
 * @param ui           UI API table (for test_artifact_png_path / cpu_image_write_png).
 * @param fs           Filesystem API (may be NULL → sk_filesystem_api()).
 * @param out_result   Filled with pass/fail, reason, and optional saved path.
 * @return SK_UI_VISION_ASSERT_OK / FAIL / SKIPPED / ERROR.
 */
i32 sk_ui_vision_assert_path(const sk_ui_api_t* ui, const_chr_t image_path, const sk_ui_cpu_image_t* image, sk_ui_vision_widget_family_t family, const_chr_t state_hint,
							 const_chr_t scene_name, const sk_filesystem_api_t* fs, sk_ui_vision_result_t* out_result);

/**
 * Grade an in-memory frame: writes a temporary PNG under the artifact root,
 * then runs sk_ui_vision_assert_path. On FAIL the temp path is reused as the
 * saved offending frame ({scene_name}_vision_fail.png).
 */
i32 sk_ui_vision_assert_image(const sk_ui_api_t* ui, const sk_ui_cpu_image_t* image, sk_ui_vision_widget_family_t family, const_chr_t state_hint, const_chr_t scene_name,
							  const sk_filesystem_api_t* fs, sk_ui_vision_result_t* out_result);

#ifdef __cplusplus
}
#endif

#endif /* SK_UI_VISION_ASSERT_H */
