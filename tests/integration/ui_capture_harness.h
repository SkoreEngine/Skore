/*
 * ui_capture_harness.h — reusable UI capture test harness (APX-228 / APX-250).
 *
 * One call captures a UI scene on the headless renderer, writes the PNG
 * artifact, and returns the pixels for assertions:
 *
 *     sk_ui_cpu_image_t img;
 *     i32 rc = sk_ui_capture_harness_capture(&params, scene, user, &img);
 *     if (rc == SK_UI_CAPTURE_HARNESS_RC_SKIPPED) {
 *         TEST_IGNORE_MESSAGE("no Vulkan ICD; skipping");  // no GPU present
 *     }
 *     TEST_ASSERT_EQUAL_INT(SK_UI_CAPTURE_HARNESS_RC_OK, rc);
 *     ... pixel assertions on img ...
 *     sk_ui_capture_harness_image_free(&img);
 *
 * Given a scene name, a viewport size, and a scene callback (which builds
 * the UI tree), the harness bootstraps the headless stack (app context,
 * sk-vulkan-render-device + sk-dxc-compiler + sk-ui plugins, device with
 * adapter selection, offscreen capture), renders exactly one frame, reads
 * the pixels back, writes the PNG artifact under the shared test-artifact
 * root, and returns the in-memory image.
 *
 * Determinism contract (mandatory):
 *   - fixed viewport: params.width/height in pixels; the offscreen target
 *     is always RGBA8 with sample_count = 1 (no MSAA);
 *   - fixed clear color: params.clear_color when clear_color_set is
 *     non-zero, else the documented fixed default
 *     (SK_UI_CAPTURE_HARNESS_DEFAULT_CLEAR_COLOR — opaque white);
 *   - fixed content scale / DPI: always 1.0x / 96 DPI
 *     (SK_UI_CAPTURE_HARNESS_CONTENT_SCALE / _DPI) — never host scale;
 *   - fixed text font when load_test_font != 0: vendored DejaVuSans.ttf
 *     under the UI test-assets dir (see SK_UI_CAPTURE_HARNESS_FONT_*),
 *     loaded at a fixed atlas size; no system-font or embedded built-in
 *     fallback (missing asset → RC_ERROR);
 *   - fixed FreeType raster flags in the font pipeline
 *     (FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
 *   - no wall-clock or frame-counter dependent state anywhere: every call
 *     uses a fresh app context, device, capture, and UI context, and the
 *     scene callback only sees the fixed logical time
 *     (params.time_seconds, default 0.0) for any animation work — never a
 *     clock query;
 *   - consistent filtering: the UI renderer's texture sampler is always
 *     LINEAR/LINEAR/NEAREST (fixed in the renderer, not per-call).
 *
 * Setup/teardown is safe to call repeatedly in one process: every call
 * fully creates and destroys its own resources (plugin init/shutdown and
 * device create/destroy are idempotent), so repeated captures never share
 * mutable state.
 *
 * The PNG artifact is written before the call returns, so even when the
 * caller's subsequent assertions fail, an inspectable frame is left on
 * disk.
 */

#ifndef SK_UI_CAPTURE_HARNESS_H
#define SK_UI_CAPTURE_HARNESS_H

#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes from sk_ui_capture_harness_capture. */
#define SK_UI_CAPTURE_HARNESS_RC_OK 0		/**< Captured, PNG written, out_image filled. */
#define SK_UI_CAPTURE_HARNESS_RC_SKIPPED 1	/**< No Vulkan loader/ICD/adapter; not captured. */
#define SK_UI_CAPTURE_HARNESS_RC_ERROR (-1) /**< Hard failure (bootstrap, capture, or artifact write). */

/** Fixed default clear color (opaque white) when clear_color_set == 0. */
#define SK_UI_CAPTURE_HARNESS_DEFAULT_CLEAR_COLOR {1.0f, 1.0f, 1.0f, 1.0f}

/* ---- Deterministic font / scale pins (APX-250) ---- */

/** Content scale applied by the harness (1.0 = 96 DPI reference). */
#define SK_UI_CAPTURE_HARNESS_CONTENT_SCALE 1.0f

/** Logical DPI corresponding to CONTENT_SCALE 1.0 (reference, not queried from OS). */
#define SK_UI_CAPTURE_HARNESS_DPI 96.0f

/** Default logical font size for text scenes that use the harness font. */
#define SK_UI_CAPTURE_HARNESS_FONT_LOGICAL_SIZE 20.0f

/** Physical pixel size at the pinned scale: round(logical * scale). */
#define SK_UI_CAPTURE_HARNESS_FONT_PIXEL_SIZE 20u

/** Fixed atlas page size used when the harness loads the test font. */
#define SK_UI_CAPTURE_HARNESS_FONT_ATLAS_W 256u
#define SK_UI_CAPTURE_HARNESS_FONT_ATLAS_H 256u

/** Vendored TTF filename under the UI test-assets directory. */
#define SK_UI_CAPTURE_HARNESS_FONT_FILENAME "DejaVuSans.ttf"

/**
 * Exact byte size of plugins/ui/testdata/DejaVuSans.ttf copied from main
 * (Content/Fonts/DejaVuSans.ttf). Guard tests fail if the asset is missing,
 * truncated, or replaced — no silent fallback to a built-in/subset font.
 */
#define SK_UI_CAPTURE_HARNESS_FONT_FILE_SIZE 757076u

/**
 * Capture parameters. Zero-init then set scene_name/width/height; the rest
 * have fixed defaults.
 */
typedef struct sk_ui_capture_harness_params_t {
	const_chr_t scene_name;	   /**< PNG artifact name (sanitized to one file under the artifact root). */
	u32 width;				   /**< Fixed viewport width in pixels (>= 1). */
	u32 height;				   /**< Fixed viewport height in pixels (>= 1). */
	i32 clear_color_set;	   /**< Non-zero: use @p clear_color; zero: fixed default (opaque white). */
	sk_ui_color_t clear_color; /**< Fixed clear color applied before every frame (when clear_color_set). */
	f64 time_seconds;		   /**< Fixed logical time for animations (default 0.0). */
	/**
	 * Non-zero: harness loads vendored DejaVuSans.ttf with fixed atlas size
	 * and installs it on the scene before the callback runs. Missing or
	 * wrong-sized asset fails the capture (RC_ERROR) — never falls back to
	 * a system or embedded built-in font. Scene may still replace
	 * font_system/font; ownership of any final pair remains with the harness.
	 */
	i32 load_test_font;
} sk_ui_capture_harness_params_t;

/**
 * Per-capture scene view handed to the scene callback. The callback builds
 * the UI tree on @p ctx (a fresh context owned by the harness) and returns
 * 0 on success. It may set @p font_system / @p font to enable text; the
 * harness destroys them during teardown (do not free them in the callback).
 */
typedef struct sk_ui_capture_scene_t {
	const sk_ui_api_t* ui;			  /**< Loaded UI API table. */
	sk_ui_context_t* ctx;			  /**< Fresh UI context; build the tree here. */
	f64 time_seconds;				  /**< Fixed logical time (pinned; never wall clock). */
	u32 width;						  /**< Fixed viewport width in pixels. */
	u32 height;						  /**< Fixed viewport height in pixels. */
	sk_ui_font_system_t* font_system; /**< Optional; set to emit text glyphs. */
	sk_ui_font_t* font;				  /**< Optional default face (with font_system). */
} sk_ui_capture_scene_t;

/** Scene callback: build/draw the UI tree on scene->ctx. @p user is forwarded. */
typedef i32 (*sk_ui_capture_scene_fn)(sk_ui_capture_scene_t* scene, void* user);

/**
 * Capture one deterministic frame of @p scene at the fixed viewport/clear
 * color/time, write the PNG artifact, and fill @p out_image with the
 * readback pixels (tightly packed RGBA8, stride = width * 4, R in the low
 * byte; caller-owned — free with sk_ui_capture_harness_image_free).
 *
 * @p scene may be NULL for a clear-only frame.
 *
 * @p out_image is filled whenever readback succeeded, even if the artifact
 * write failed afterwards (returned as RC_ERROR). On RC_SKIPPED or early
 * RC_ERROR it is zeroed.
 *
 * Safe to call repeatedly in one process.
 */
i32 sk_ui_capture_harness_capture(const sk_ui_capture_harness_params_t* params, sk_ui_capture_scene_fn scene, void* user, sk_ui_cpu_image_t* out_image);

/** Free an image returned by sk_ui_capture_harness_capture. Safe on zeroed/NULL. */
void sk_ui_capture_harness_image_free(sk_ui_cpu_image_t* image);

/**
 * Resolve the absolute path of the vendored DejaVuSans.ttf test asset.
 * Uses SK_UI_GOLDEN_DIR (compile-time) when defined, else
 * "plugins/ui/testdata/DejaVuSans.ttf" relative to the process cwd / app
 * folder. Does not check existence.
 * @return 0 on success, non-zero if @p out is too small or invalid.
 */
i32 sk_ui_capture_harness_test_font_path(char* out, u32 out_cap);

/**
 * Load the pinned test font (DejaVuSans.ttf) with fixed atlas size.
 * Fails if the asset is missing, the size is not
 * SK_UI_CAPTURE_HARNESS_FONT_FILE_SIZE, or FreeType rejects the face —
 * never falls back to an embedded/system font.
 * On success, *out_system / *out_font are non-NULL and owned by the caller
 * (destroy font then system, or let the capture harness own them via scene).
 * @return 0 on success, non-zero on failure.
 */
i32 sk_ui_capture_harness_load_test_font(const sk_ui_api_t* ui, sk_ui_font_system_t** out_system, sk_ui_font_t** out_font);

#ifdef __cplusplus
}
#endif

#endif /* SK_UI_CAPTURE_HARNESS_H */
