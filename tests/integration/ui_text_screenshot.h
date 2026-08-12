/*
 * ui_text_screenshot.h — deterministic text rendering screenshot harness
 * (APX-268).
 *
 * One call renders a fixed suite of text samples on the headless offscreen
 * renderer and writes PNG captures into a known, mode-scoped output folder
 * for side-by-side comparison of the two UI text renderers:
 *
 *     {artifact-root}/text-screenshot/freetype/  (legacy FreeType R8 path)
 *     {artifact-root}/text-screenshot/msdf/      (msdf-atlas-c RGB8 path)
 *
 * Suite coverage (fixed set, byte-stable across runs):
 *   - pangram     — alphanumeric + punctuation pangram (wrapped label);
 *   - sizes       — the same phrase at several font sizes, very small (8px)
 *                   through very large (96px), stacked in one frame;
 *   - colors_alpha— colored text and alpha-blended (translucent) text over
 *                   colored backgrounds;
 *   - scaled      — text rendered at fixed content scale 2.0 (scaled text;
 *                   rotation is NOT supported by the UI — no transform API —
 *                   so the suite covers scaling only, see below);
 *   - glyph_grid  — printable ASCII (0x20..0x7E) laid out in a wrapped grid
 *                   plus a raw atlas dump written next to the capture:
 *                   atlas_freetype.png (R8 page expanded to grayscale) or
 *                   atlas_msdf.png (RGB8 multi-channel SDF atlas).
 *
 * Determinism contract (identical to sk_ui_capture_harness_capture, see
 * ui_capture_harness.h):
 *   - fixed viewport: 640x480 physical pixels for every sample (RGBA8
 *     offscreen target, sample_count 1, no MSAA);
 *   - fixed clear color: SK_UI_TEXT_SCREENSHOT_CLEAR (dark, opaque);
 *   - fixed logical time 0.0 for every scene (no wall clock / frame counter);
 *   - fixed font: vendored DejaVuSans.ttf at a fixed atlas size, fixed
 *     FreeType raster flags on the legacy path, fixed MSDF bake parameters
 *     (px_range 2, INKTRAP edge coloring, seed 0) on the MSDF path;
 *   - fixed content scale per sample (1.0 everywhere except the "scaled"
 *     sample, which pins 2.0);
 *   - rotation: the UI has no transform/rotation support today, so there is
 *     no rotated sample; the scaled sample covers the transform axis the UI
 *     does support.
 *
 * Verification:
 *   1. Completeness: run() checks that every expected capture exists and is
 *      non-empty in the mode folder, and the SK_TEST suite asserts the
 *      FreeType and MSDF folders carry the identical file set.
 *   2. Determinism: with verify != 0, run() re-captures every sample and
 *      byte-compares the raw RGBA readback AND the PNG artifact bytes of the
 *      two runs. Re-running the whole harness twice (same mode, same output
 *      folder) must produce byte-identical images.
 *
 * GPU requirement: the suite renders through the headless Vulkan offscreen
 * capture (same as sk_ui_capture_harness_capture); without a Vulkan
 * loader/ICD it returns SK_UI_TEXT_SCREENSHOT_RC_SKIPPED. The FreeType mode
 * also paints through the GPU pipeline (CPU coverage quads), so both modes
 * are equally gated.
 */

#ifndef SK_UI_TEXT_SCREENSHOT_H
#define SK_UI_TEXT_SCREENSHOT_H

#include "ui_capture_harness.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Text renderer path captured by one suite run. */
typedef enum sk_ui_text_screenshot_mode_t {
	SK_UI_TEXT_SCREENSHOT_MODE_FREETYPE = 0, /**< Legacy FreeType R8 coverage path. */
	SK_UI_TEXT_SCREENSHOT_MODE_MSDF = 1,	 /**< msdf-atlas-c RGB8 + MSDF shader path. */
	SK_UI_TEXT_SCREENSHOT_MODE_COUNT
} sk_ui_text_screenshot_mode_t;

/** Human-readable mode folder/flag name: "freetype" / "msdf". */
const_chr_t sk_ui_text_screenshot_mode_name(sk_ui_text_screenshot_mode_t mode);

/* Return codes from sk_ui_text_screenshot_run. */
#define SK_UI_TEXT_SCREENSHOT_RC_OK 0		/**< All captures written; verify clean (when requested). */
#define SK_UI_TEXT_SCREENSHOT_RC_SKIPPED 1	/**< No Vulkan loader/ICD/adapter; nothing captured. */
#define SK_UI_TEXT_SCREENSHOT_RC_ERROR (-1) /**< Hard failure (bootstrap, capture, artifact, or verify mismatch). */

/** Fixed viewport for every sample (physical pixels). */
#define SK_UI_TEXT_SCREENSHOT_VIEW_W 640u
#define SK_UI_TEXT_SCREENSHOT_VIEW_H 480u

/** Fixed clear color (dark, opaque) — deterministic across runs. */
#define SK_UI_TEXT_SCREENSHOT_CLEAR_R 0.10f
#define SK_UI_TEXT_SCREENSHOT_CLEAR_G 0.12f
#define SK_UI_TEXT_SCREENSHOT_CLEAR_B 0.16f
#define SK_UI_TEXT_SCREENSHOT_CLEAR_A 1.0f

/** Subdirectory under the artifact root where captures are written. */
#define SK_UI_TEXT_SCREENSHOT_SUBDIR "text-screenshot"

/**
 * Suite run parameters. Zero-init then set mode; the rest have fixed
 * defaults (captures land in {artifact-root}/text-screenshot/{mode}/).
 */
typedef struct sk_ui_text_screenshot_params_t {
	/** Renderer path to capture (required, < SK_UI_TEXT_SCREENSHOT_MODE_COUNT). */
	sk_ui_text_screenshot_mode_t mode;
	/**
	 * Non-zero: re-capture every sample and byte-compare the two runs
	 * (raw RGBA readback + PNG artifact bytes). Reports per-sample results
	 * and fails (RC_ERROR) on any byte difference.
	 */
	i32 verify;
	/**
	 * Optional override of the subdirectory under the artifact root
	 * (default SK_UI_TEXT_SCREENSHOT_SUBDIR). The mode folder is appended:
	 * {subdir}/{mode_name}/. Useful for side-by-side runs into a custom
	 * output tree.
	 */
	const_chr_t subdir;
	/** Optional progress printer; NULL = silent. */
	void (*log_fn)(void_ptr_t user, const_chr_t line);
	void_ptr_t log_user;
} sk_ui_text_screenshot_params_t;

/**
 * Render the full fixed text-sample suite for @p params->mode and write the
 * PNG captures into {artifact-root}/{subdir}/{mode_name}/. Also writes a
 * manifest.txt (file name + byte size per capture) into the same folder and
 * verifies completeness (every expected capture exists and is non-empty).
 *
 * When @p params->verify is non-zero, each sample is captured a second time
 * and byte-compared against the first (raw pixels + PNG bytes); any
 * difference fails the run with RC_ERROR.
 *
 * @return SK_UI_TEXT_SCREENSHOT_RC_OK on success (complete set written),
 *         SK_UI_TEXT_SCREENSHOT_RC_SKIPPED when no Vulkan ICD is available,
 *         SK_UI_TEXT_SCREENSHOT_RC_ERROR on any hard failure.
 */
i32 sk_ui_text_screenshot_run(const sk_ui_text_screenshot_params_t* params);

/**
 * Number of expected captures per mode (samples + atlas dump + manifest).
 * Used by tests to assert both modes produce the complete set.
 */
u32 sk_ui_text_screenshot_expected_count(void);

/**
 * Expected capture file name for a mode folder at index @p i
 * (0 .. expected_count-1; manifest.txt is last; the atlas file name embeds
 * the mode). Returns NULL out of range.
 */
const_chr_t sk_ui_text_screenshot_expected_name(sk_ui_text_screenshot_mode_t mode, u32 i);

#ifdef __cplusplus
}
#endif

#endif /* SK_UI_TEXT_SCREENSHOT_H */
