#pragma once

/**
 * @file platform_window.h
 * @brief Platform window, message box, and native file-dialog API.
 *
 * Implemented by the sk-platform-window (GLFW + nativefiledialog).
 * The plugin registers a static sk_platform_window_api_t on the app context.
 * Hosts obtain it **only** via the app registry (no free-function mirrors,
 * no public sk_platform_window_api() accessor):
 *
 *   const sk_platform_window_api_t* win =
 *       (const sk_platform_window_api_t*)app_api->get_api(
 *           ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
 *   sk_window_t w = win->create_window(...);
 *
 * HiDPI conventions (design doc §3.7 / gaps G2–G5):
 * - **Logical size** (`get_window_size`): client area in screen coordinates /
 *   points — layout, hit-test, UI root size.
 * - **Physical size** (`get_framebuffer_size`): framebuffer pixels — swapchain
 *   extent, viewport, scissor in device pixels.
 * - **Content scale** (`get_window_content_scale` / per-monitor): multiply
 *   logical → physical; 1.0 = 96 DPI baseline (GLFW content scale).
 * - Scale change: `set_window_content_scale_callback` (OS scale change or
 *   window moved to a monitor with a different DPI). Callers may also poll
 *   `get_window_content_scale` each frame.
 *
 * Manual multi-monitor verification (not automated in CI):
 * 1. Create a window; log get_window_content_scale and get_framebuffer_size.
 * 2. Drag the window between a 1x and 2x (or fractional) monitor.
 * 3. Confirm the content-scale callback fires with the new scale and that
 *    get_monitor_content_scale for each monitor matches the OS display
 *    settings. Also toggle OS display scale and re-check.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_platform_window_api_t in the app registry. */
#define SK_PLATFORM_WINDOW_API_TYPE_ID SK_TYPE_ID("sk.platform_window_api", 0x27f500d367e27812ULL, 0x20323443377bc3efULL)

/**
 * Opaque window handle (GLFW window pointer on the current backend).
 * NULL / zero is invalid.
 */
typedef void_ptr_t sk_window_t;

/**
 * Opaque monitor handle (GLFW monitor pointer on the current backend).
 * NULL / zero is invalid.
 */
typedef void_ptr_t sk_monitor_t;

/** Pixel / point size (client area or framebuffer). */
typedef struct sk_extent_t {
	u32 width;
	u32 height;
} sk_extent_t;

/**
 * Content-scale factors (1.0 = 96 DPI baseline).
 * X and Y are usually equal; non-uniform scales exist on some setups.
 */
typedef struct sk_content_scale_t {
	f32 x;
	f32 y;
} sk_content_scale_t;

/**
 * Callback when a window's content scale changes (moved between monitors
 * with different DPI, or OS display scale changed).
 * @param window    Window whose scale changed.
 * @param scale     New content scale (x/y already normalized; > 0).
 * @param user_data Cookie from set_window_content_scale_callback.
 */
typedef void (*sk_window_content_scale_callback_t)(sk_window_t window, sk_content_scale_t scale, void_ptr_t user_data);

/**
 * Window creation flags (bitmask).
 */
typedef enum sk_window_flags_t {
	SK_WINDOW_FLAG_NONE = 0,
	SK_WINDOW_FLAG_RESIZABLE = 1u << 0,
	SK_WINDOW_FLAG_BORDERLESS = 1u << 1,
	SK_WINDOW_FLAG_MAXIMIZED = 1u << 2,
	SK_WINDOW_FLAG_HIDDEN = 1u << 3,
	SK_WINDOW_FLAG_DECORATED = 1u << 4, /* default when neither borderless nor this is set */
} sk_window_flags_t;

/**
 * Cursor capture mode for a window.
 */
typedef enum sk_cursor_lock_mode_t {
	SK_CURSOR_LOCK_NONE = 0,	 /**< Normal free cursor. */
	SK_CURSOR_LOCK_LOCKED = 1,	 /**< Cursor disabled / raw capture (FPS style). */
	SK_CURSOR_LOCK_CONFINED = 2, /**< Cursor confined to the client area. */
} sk_cursor_lock_mode_t;

/**
 * Simple message-box severity.
 */
typedef enum sk_message_box_type_t {
	SK_MESSAGE_BOX_INFO = 0,
	SK_MESSAGE_BOX_WARNING = 1,
	SK_MESSAGE_BOX_ERROR = 2,
} sk_message_box_type_t;

/**
 * File dialog filter entry.
 * @field name        Human label (e.g. "Images"). May be NULL.
 * @field extensions  Comma-separated extensions without dots (e.g. "png,jpg,jpeg").
 *                    May be NULL or empty for "all files".
 */
typedef struct sk_file_filter_t {
	const_chr_t name;
	const_chr_t extensions;
} sk_file_filter_t;

/**
 * Single-path dialog result callback.
 * @param path      UTF-8 path, or NULL if the user cancelled / error.
 * @param user_data Caller cookie.
 */
typedef void (*sk_path_callback_t)(const_chr_t path, void_ptr_t user_data);

/**
 * Multi-path dialog result callback.
 * @param paths     Array of UTF-8 paths (valid only during the call).
 * @param count     Number of paths (0 if cancelled / error).
 * @param user_data Caller cookie.
 */
typedef void (*sk_paths_callback_t)(const_chr_t* paths, u32 count, void_ptr_t user_data);

/* ---- HiDPI conversion helpers (pure math; no window required) ---- */

/**
 * Treat non-positive scale as 1.0 (safe for divide / multiply).
 */
SK_FINLINE f32 sk_content_scale_axis(f32 scale) {
	return (scale > 0.0f) ? scale : 1.0f;
}

/**
 * Average of x/y content scale (legacy single-factor DPI).
 */
SK_FINLINE f32 sk_content_scale_average(sk_content_scale_t scale) {
	const f32 x = sk_content_scale_axis(scale.x);
	const f32 y = sk_content_scale_axis(scale.y);
	return (x + y) * 0.5f;
}

/**
 * Scale one logical dimension to physical pixels (nearest).
 */
SK_FINLINE u32 sk_logical_to_physical_u32(u32 logical, f32 scale) {
	const f32 s = sk_content_scale_axis(scale);
	const f32 p = (f32)logical * s;
	if (p <= 0.0f) {
		return 0u;
	}
	return (u32)(p + 0.5f);
}

/**
 * Scale one physical dimension to logical points (nearest).
 */
SK_FINLINE u32 sk_physical_to_logical_u32(u32 physical, f32 scale) {
	const f32 s = sk_content_scale_axis(scale);
	const f32 l = (f32)physical / s;
	if (l <= 0.0f) {
		return 0u;
	}
	return (u32)(l + 0.5f);
}

/**
 * Logical extent (points) → physical extent (framebuffer pixels).
 * Uses scale.x for width and scale.y for height.
 */
SK_FINLINE sk_extent_t sk_extent_logical_to_physical(sk_extent_t logical, sk_content_scale_t scale) {
	sk_extent_t out;
	out.width = sk_logical_to_physical_u32(logical.width, scale.x);
	out.height = sk_logical_to_physical_u32(logical.height, scale.y);
	return out;
}

/**
 * Physical extent (framebuffer pixels) → logical extent (points).
 */
SK_FINLINE sk_extent_t sk_extent_physical_to_logical(sk_extent_t physical, sk_content_scale_t scale) {
	sk_extent_t out;
	out.width = sk_physical_to_logical_u32(physical.width, scale.x);
	out.height = sk_physical_to_logical_u32(physical.height, scale.y);
	return out;
}

/**
 * Scale a floating logical coordinate to physical (no rounding).
 */
SK_FINLINE f32 sk_logical_to_physical_f(f32 logical, f32 scale) {
	return logical * sk_content_scale_axis(scale);
}

/**
 * Scale a floating physical coordinate to logical (no rounding).
 */
SK_FINLINE f32 sk_physical_to_logical_f(f32 physical, f32 scale) {
	return physical / sk_content_scale_axis(scale);
}

/**
 * Global platform-window module API (one table per process after plugin load).
 * Call init before other window ops; other entry points do not auto-init.
 */
typedef struct sk_platform_window_api_t {
	/**
     * Initialize the window subsystem (GLFW). Idempotent.
     * Hosts must call this before create_window / poll_events.
     * @return 0 on success, non-zero on failure.
     */
	i32 (*init)(void);

	/**
     * Create a platform window.
     * Requires a prior successful init.
     * @param title  UTF-8 title (NULL → "").
     * @param width  Client width in screen coordinates / logical points (must be > 0).
     * @param height Client height in screen coordinates / logical points (must be > 0).
     * @param flags  sk_window_flags_t bits.
     * @return Window handle, or NULL on failure.
     */
	sk_window_t (*create_window)(const_chr_t title, u32 width, u32 height, u32 flags);

	/**
     * Destroy a window. NULL is a no-op.
     * @param window Handle from create_window.
     */
	void (*destroy_window)(sk_window_t window);

	/**
     * Whether the user (or OS) requested the window to close (GLFW glfwWindowShouldClose).
     * @param window Valid window.
     * @return Non-zero if close was requested, 0 otherwise.
     */
	i32 (*window_should_close)(sk_window_t window);

	/**
     * Content-scale DPI factor for the window (1.0 = 96 DPI baseline).
     * Average of x/y from get_window_content_scale (legacy single factor).
     * Prefer get_window_content_scale when x/y may differ.
     * @param window Valid window.
     * @return Average content scale, or 1.0 if unknown / invalid.
     */
	f32 (*get_window_dpi)(sk_window_t window);

	/**
     * Client-area size in **logical** screen coordinates (points).
     * Use for UI layout and hit-testing. For swapchain / GPU viewport use
     * get_framebuffer_size (physical pixels).
     * @param window Valid window.
     * @return Size; zeros if invalid.
     */
	sk_extent_t (*get_window_size)(sk_window_t window);

	/**
     * Window content scale (x and y). 1.0 = 96 DPI baseline.
     * @param window Valid window.
     * @return Scale; {1,1} if unknown.
     */
	sk_content_scale_t (*get_window_content_scale)(sk_window_t window);

	/**
     * Framebuffer size in **physical** pixels (device pixels).
     * Use for swapchain extent, viewport, and scissor. On HiDPI displays this
     * is typically logical size × content scale.
     * @param window Valid window.
     * @return Size; zeros if invalid.
     */
	sk_extent_t (*get_framebuffer_size)(sk_window_t window);

	/**
     * Register a callback for content-scale changes on @p window.
     * Invoked from poll_events when the OS reports a new scale (monitor move
     * or display-scale change). Pass @p callback NULL to clear.
     * Only one callback per window; replaces any previous registration.
     * @param window    Valid window.
     * @param callback  Handler or NULL.
     * @param user_data Cookie passed to @p callback.
     */
	void (*set_window_content_scale_callback)(sk_window_t window, sk_window_content_scale_callback_t callback, void_ptr_t user_data);

	/**
     * Number of connected monitors. Requires prior init.
     * @return Count (0 if not initialized or none).
     */
	u32 (*get_monitor_count)(void);

	/**
     * Primary monitor handle. Requires prior init.
     * @return Monitor, or NULL if none.
     */
	sk_monitor_t (*get_primary_monitor)(void);

	/**
     * Monitor at @p index in [0, get_monitor_count()). Requires prior init.
     * @param index Zero-based index.
     * @return Monitor, or NULL if out of range / unavailable.
     */
	sk_monitor_t (*get_monitor)(u32 index);

	/**
     * Content scale for a monitor (1.0 = 96 DPI baseline).
     * @param monitor Valid monitor from get_primary_monitor / get_monitor.
     * @return Scale; {1,1} if unknown.
     */
	sk_content_scale_t (*get_monitor_content_scale)(sk_monitor_t monitor);

	/**
     * @param window Valid window.
     * @return Non-zero if the window is iconified/minimized.
     */
	i32 (*is_window_minimized)(sk_window_t window);

	/**
     * Set cursor lock / confine mode.
     * @param window         Valid window.
     * @param cursor_lock_mode sk_cursor_lock_mode_t value.
     */
	void (*set_window_cursor_lock_mode)(sk_window_t window, sk_cursor_lock_mode_t cursor_lock_mode);

	/**
     * Maximize the window. No-op if invalid.
     * @param window Valid window.
     */
	void (*maximize_window)(sk_window_t window);

	/**
     * Set the window icon from tightly packed RGBA8 pixels (top-left origin).
     * @param window     Valid window.
     * @param rgba_pixels Row-major RGBA bytes; NULL clears the icon where supported.
     * @param width      Icon width in pixels (> 0 when pixels non-NULL).
     * @param height     Icon height in pixels (> 0 when pixels non-NULL).
     */
	void (*set_window_icon)(sk_window_t window, const u8* rgba_pixels, i32 width, i32 height);

	/**
     * Native OS window handle (HWND / NSWindow* / X11 Window as void*).
     * @param window Valid window.
     * @return Native handle, or NULL if unavailable.
     */
	void_ptr_t (*get_native_window_handle)(sk_window_t window);

	/**
     * Modal simple message box.
     * @param type    Severity.
     * @param title   UTF-8 title (NULL → "").
     * @param message UTF-8 body (NULL → "").
     * @param window  Optional parent (may be NULL).
     * @return Non-zero if shown successfully, 0 on failure.
     */
	i32 (*show_simple_message_box)(sk_message_box_type_t type, const_chr_t title, const_chr_t message, sk_window_t window);

	/**
     * Blocking save-file dialog. Invokes @p callback with the path on OK,
     * or with NULL on cancel/error.
     * @return 0 on OK, 1 on cancel, negative on error.
     */
	i32 (*save_dialog)(sk_path_callback_t callback, void_ptr_t user_data, const sk_file_filter_t* filters, u32 filter_count, const_chr_t default_path, const_chr_t file_name,
					   sk_window_t window);

	/**
     * Blocking open-file dialog (single selection).
     * @return 0 on OK, 1 on cancel, negative on error.
     */
	i32 (*open_dialog)(sk_path_callback_t callback, void_ptr_t user_data, const sk_file_filter_t* filters, u32 filter_count, const_chr_t default_path, sk_window_t window);

	/**
     * Blocking open-file dialog (multiple selection).
     * @return 0 on OK, 1 on cancel, negative on error.
     */
	i32 (*open_dialog_multiple)(sk_paths_callback_t callback, void_ptr_t user_data, const sk_file_filter_t* filters, u32 filter_count, const_chr_t default_path,
								sk_window_t window);

	/**
     * Blocking folder picker.
     * @return 0 on OK, 1 on cancel, negative on error.
     */
	i32 (*pick_folder)(sk_path_callback_t callback, void_ptr_t user_data, const_chr_t default_path, sk_window_t window);

	/**
     * Poll OS window events (GLFW). Call once per frame from the host loop.
     * Delivers content-scale callbacks registered via
     * set_window_content_scale_callback.
     */
	void (*poll_events)(void);

	/**
     * Shutdown the window subsystem (destroy leftover windows, terminate GLFW).
     * Safe to call multiple times.
     */
	void (*shutdown)(void);
} sk_platform_window_api_t;

#ifdef __cplusplus
}
#endif
