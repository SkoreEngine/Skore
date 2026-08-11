/*
 * ui_capture_harness.h — reusable UI capture test harness (APX-228).
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

#ifdef __cplusplus
}
#endif

#endif /* SK_UI_CAPTURE_HARNESS_H */
