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

/** Pixel size (client area). */
typedef struct sk_extent_t {
	u32 width;
	u32 height;
} sk_extent_t;

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
     * @param width  Client width in screen coordinates (must be > 0).
     * @param height Client height in screen coordinates (must be > 0).
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
     * @param window Valid window.
     * @return Average of x/y content scale, or 1.0 if unknown / invalid.
     */
	f32 (*get_window_dpi)(sk_window_t window);

	/**
     * Client-area size in screen coordinates.
     * @param window Valid window.
     * @return Size; zeros if invalid.
     */
	sk_extent_t (*get_window_size)(sk_window_t window);

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
