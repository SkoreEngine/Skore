#pragma once

/**
 * @file ui_test.h
 * @brief Ergonomic UI test authoring API (APX-261).
 *
 * Layer tests actually use on top of the code-driven test engine (APX-259/260):
 * register a named body, optional per-test setup/teardown, high-level actions
 * (clickItem, hoverItem, dragItemTo, typeInto, openMenuPath), assertions
 * (itemExists, itemRect, itemState, valueEquals), and automatic soft-render
 * frame capture on assertion failure (path included in the failure message).
 *
 * Registration uses the existing SK_TEST constructor registry so authoring
 * tests run under sk-tests (plugin) and, when the TU is also linked into
 * sk-integration-tests, alongside the vision suite with the same macros.
 *
 * All helpers take an sk_ui_test_t session that owns a soft-rendering test
 * engine. Prefer stable node test ids (node_set_id / widget id params).
 */

#include "ui.h"

#include "test.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SK_TESTS

/** Per-test session: engine + context + failure artifact path. */
typedef struct sk_ui_test_t {
	const sk_ui_api_t* ui;		 /**< UI API table (required). */
	sk_ui_test_engine_t* engine; /**< Owned engine (soft_render on). */
	sk_ui_context_t* ctx;		 /**< Convenience: engine context. */
	const_chr_t name;			 /**< Test name (not owned). */
	/** Last failure frame path (PNG under test-artifact root); empty if none. */
	char fail_frame_path[512];
	/** Last assertion / action error message (readable, may include frame path). */
	char last_error[512];
} sk_ui_test_t;

/** Optional per-test hook (setup or teardown). */
typedef void (*sk_ui_test_hook_fn)(sk_ui_test_t* t);

/** Test body receiving the live session. */
typedef void (*sk_ui_test_body_fn)(sk_ui_test_t* t);

/**
 * Create engine (soft_render=1), run setup → body → teardown, destroy engine.
 * @p ui must be non-NULL (plugin: ui_get_api_table(); integration: app get_api).
 * @p desc optional; NULL uses 400x300, scale 1, soft_render forced on.
 * Hooks may be NULL. Body must not be NULL.
 */
void sk_ui_test_run(const sk_ui_api_t* ui, const_chr_t name, const sk_ui_test_engine_desc_t* desc, sk_ui_test_hook_fn setup, sk_ui_test_hook_fn teardown, sk_ui_test_body_fn body);

/**
 * Manual lifecycle (when not using SK_UI_TEST macros).
 * @return 0 on success, non-zero if engine create failed.
 */
i32 sk_ui_test_begin(sk_ui_test_t* t, const sk_ui_api_t* ui, const_chr_t name, const sk_ui_test_engine_desc_t* desc);
void sk_ui_test_end(sk_ui_test_t* t);

/** Accessors (valid between begin/end). */
const sk_ui_api_t* sk_ui_test_api(const sk_ui_test_t* t);
sk_ui_context_t* sk_ui_test_context(const sk_ui_test_t* t);
sk_ui_test_engine_t* sk_ui_test_engine(const sk_ui_test_t* t);
const_chr_t sk_ui_test_fail_frame_path(const sk_ui_test_t* t);
const_chr_t sk_ui_test_last_error(const sk_ui_test_t* t);

/** Advance one frame (default dt = 1/60). @return SK_UI_TEST_OK or error. */
i32 sk_ui_test_step(sk_ui_test_t* t);
/** Advance @p frame_count frames. */
i32 sk_ui_test_yield(sk_ui_test_t* t, u32 frame_count);

/* ---- high-level actions (clickItem, hoverItem, dragItemTo, typeInto, openMenuPath) ---- */

/** Left-click item by test id (clickItem). */
i32 sk_ui_click_item(sk_ui_test_t* t, const_chr_t test_id);
/** Move pointer to item center (hoverItem). */
i32 sk_ui_hover_item(sk_ui_test_t* t, const_chr_t test_id);
/**
 * Drag from center of @p from_id to center of @p to_id with intermediate
 * motion frames (dragItemTo).
 */
i32 sk_ui_drag_item_to(sk_ui_test_t* t, const_chr_t from_id, const_chr_t to_id);
/** Focus and type UTF-8 into item (typeInto). */
i32 sk_ui_type_into(sk_ui_test_t* t, const_chr_t test_id, const_chr_t text);
/**
 * Open a menu path of slash-separated test ids (openMenuPath).
 * Example: "menu-file/item-open" clicks menu-file, steps, then item-open.
 */
i32 sk_ui_open_menu_path(sk_ui_test_t* t, const_chr_t id_path);

/* ---- check_* : return 0 ok / non-zero fail; capture frame + fill last_error ---- */

i32 sk_ui_check_item_exists(sk_ui_test_t* t, const_chr_t test_id);
i32 sk_ui_check_item_rect(sk_ui_test_t* t, const_chr_t test_id, f32 x, f32 y, f32 w, f32 h, f32 eps);
/** @p must_have / @p must_not are SK_UI_STATE_* masks (0 to ignore). */
i32 sk_ui_check_item_state(sk_ui_test_t* t, const_chr_t test_id, u32 must_have, u32 must_not);
/**
 * Compare widget value as string (valueEquals):
 * text_input / label / button → visible or input text;
 * checkbox / radio / toggle → "0" or "1";
 * slider / progress → printf "%.4g" of the float value.
 */
i32 sk_ui_check_value_equals(sk_ui_test_t* t, const_chr_t test_id, const_chr_t expected);

/* ---- assert_* : check + Unity TEST_FAIL_MESSAGE (includes fail frame path) ---- */

void sk_ui_item_exists(sk_ui_test_t* t, const_chr_t test_id);
void sk_ui_item_rect(sk_ui_test_t* t, const_chr_t test_id, f32 x, f32 y, f32 w, f32 h, f32 eps);
void sk_ui_item_state(sk_ui_test_t* t, const_chr_t test_id, u32 must_have, u32 must_not);
void sk_ui_value_equals(sk_ui_test_t* t, const_chr_t test_id, const_chr_t expected);

/**
 * Capture current soft-render buffer to a PNG under the test-artifact root.
 * Writes path into t->fail_frame_path. Safe to call even when no failure.
 * @return 0 on success, non-zero if pixels unavailable or write failed.
 */
i32 sk_ui_test_capture_frame(sk_ui_test_t* t, const_chr_t tag);

/* ---- registration macros (bridge onto SK_TEST runner) ---- */

/* File-local linkage for the generated body; expands at the authoring .c site. */
#define SK_UI_TEST_STATIC static

/**
 * Register a UI test with automatic engine setup/teardown.
 * Body signature: void body(sk_ui_test_t* t)
 *
 * Example:
 *   SK_UI_TEST(click_increments) {
 *     ... build tree with t->ui / t->ctx ...
 *     TEST_ASSERT_EQUAL_INT(0, sk_ui_test_step(t));
 *     TEST_ASSERT_EQUAL_INT(0, sk_ui_click_item(t, "btn"));
 *     sk_ui_item_exists(t, "btn");
 *   }
 */
#define SK_UI_TEST(name)                                                                     \
	SK_UI_TEST_STATIC void sk_ui_test_body_##name(sk_ui_test_t* t);                          \
	SK_TEST(ui_author_##name) {                                                              \
		sk_ui_test_run(ui_get_api_table(), #name, NULL, NULL, NULL, sk_ui_test_body_##name); \
	}                                                                                        \
	SK_UI_TEST_STATIC void sk_ui_test_body_##name(sk_ui_test_t* t)

/**
 * Same as SK_UI_TEST with per-test setup/teardown hooks.
 * Hooks: void hook(sk_ui_test_t* t)
 */
#define SK_UI_TEST_EX(name, setup_fn, teardown_fn)                                                          \
	SK_UI_TEST_STATIC void sk_ui_test_body_##name(sk_ui_test_t* t);                                         \
	SK_TEST(ui_author_##name) {                                                                             \
		sk_ui_test_run(ui_get_api_table(), #name, NULL, (setup_fn), (teardown_fn), sk_ui_test_body_##name); \
	}                                                                                                       \
	SK_UI_TEST_STATIC void sk_ui_test_body_##name(sk_ui_test_t* t)

/**
 * Integration / host form: pass an explicit API table (from app get_api).
 * Registers as SK_TEST(ui_author_##name).
 */
#define SK_UI_TEST_WITH_API(name, ui_expr, setup_fn, teardown_fn)                                  \
	SK_UI_TEST_STATIC void sk_ui_test_body_##name(sk_ui_test_t* t);                                \
	SK_TEST(ui_author_##name) {                                                                    \
		sk_ui_test_run((ui_expr), #name, NULL, (setup_fn), (teardown_fn), sk_ui_test_body_##name); \
	}                                                                                              \
	SK_UI_TEST_STATIC void sk_ui_test_body_##name(sk_ui_test_t* t)

/* Plugin-local default API (defined in plugins/ui; not on the public vtable). */
const sk_ui_api_t* ui_get_api_table(void);

#endif /* SK_TESTS */

#ifdef __cplusplus
}
#endif
