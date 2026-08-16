#pragma once

/**
 * @file console_window.h
 * @brief Console / log output window (APX-373): v2 migration.
 *
 * Port of main-branch Skore::ConsoleWindow (§3.1 of the migration manifest)
 * onto the v2 editor shell. The message list is bound to the v2 logger: the
 * window registers an `sk_log_sink_t` on the app logger context when it is
 * opened, so every `sk_log_*` line (all severities) lands in its retained
 * ring. Draw builds the C++ toolbar surface on the dock chrome — Clear,
 * per-severity checkboxes (Trace/Debug/Info/Warn/Error/Fatal), Collapse,
 * Auto-scroll, a Filter text input and the colored scrolling log region —
 * exactly the APX-139 console panel widget set, rehosted inside the window.
 *
 * Public entry points follow the APX-365 pattern: `AddMessage` / `Clear` and
 * the rest are never exported as free symbols. The window publishes one
 * process-lifetime `sk_editor_console_ops_t` table registered with
 * `app_api->add_impl` under SK_EDITOR_CONSOLE_OPS_TYPE_ID; callers look it up
 * via `sk_editor_console_ops(ctx, api)` and call through the pointers.
 *
 * Severity filtering is session-only (never persisted), matching the C++
 * `showTrace/.../showCritical` members.
 */

#include "editor_window.h"
#include "logger.h"
#include "main_windows.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_CONSOLE_OPS_TYPE_ID SK_TYPE_ID("sk.editor.console.ops", 0x21c9f74e8b3d5a06ULL, 0x9c02d64ab17f8e3dULL)

/** Max retained log lines shown in the scroll region. */
#define SK_EDITOR_CONSOLE_MAX_LINES 256u
/** Max characters per log line (including level/logger prefix). */
#define SK_EDITOR_CONSOLE_LINE_MAX 512u

/**
 * Console window public table. Looked up via add_impl; never call the
 * implementations by symbol.
 *
 * Every function takes the app context / api pair so callers do not need a
 * window handle; state lives on the single open console window instance.
 */
typedef struct sk_editor_console_ops_t {
	/** Open (or focus) the Console window. @see ConsoleWindow::OpenHistoryWindow. */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Append one log line as if the logger sink had delivered it (AddMessage). */
	void (*add_message)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message);

	/** Clear all stored lines. @see ConsoleSink::ClearMessages. */
	void (*clear)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Number of stored (unfiltered) log lines. */
	u32 (*line_count)(const sk_editor_window_t* window);

	/** Number of lines currently visible after severity + filter pass. */
	u32 (*visible_count)(const sk_editor_window_t* window);

	/** Show / hide one severity (Trace..Fatal) in the message list. */
	void (*set_level_visible)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_logger_type_t level, i32 visible);

	/** Whether @p level is currently shown in the message list. */
	i32 (*is_level_visible)(const sk_editor_window_t* window, sk_logger_type_t level);

	/** Set the substring filter ("Filter" search box). Empty string clears. */
	void (*set_filter)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t filter);
} sk_editor_console_ops_t;

/** First registered Console ops table, or NULL before register. */
SK_FINLINE const sk_editor_console_ops_t* sk_editor_console_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_console_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_CONSOLE_OPS_TYPE_ID);
}

/**
 * Register the Console window impl + ops table on @p app_context. Idempotent
 * per context. Call before sk_editor_windows_register_impls so the real
 * impl wins window_open over the APX-330 scaffold (window-table-pattern.md).
 * Pair with sk_editor_console_shutdown in tests.
 */
void sk_editor_console_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_console_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
