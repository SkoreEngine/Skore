#pragma once

/**
 * @file console_panel.h
 * @brief Editor Console panel reimplemented on sk-ui (APX-139).
 *
 * Port of main-branch ImGui ConsoleWindow (Editor/Source/Skore/Window/ConsoleWindow)
 * onto the retained sk-ui widget set. Lives in the editor host — not inside the
 * ui plugin — so remaining ImGui (or imgui_shell stand-in) panels can share the
 * same frame without forcing every editor window through sk-ui yet.
 *
 * Retained vs ImGui: the panel tree is built once; log lines and filter state
 * are updated in place when the sink version or UI controls change.
 */

#include "common.h"
#include "logger.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max retained log lines shown in the scroll region. */
#define SK_EDITOR_CONSOLE_MAX_LINES 256u
/** Max characters per log line (including level/logger prefix). */
#define SK_EDITOR_CONSOLE_LINE_MAX 512u

/** Opaque console panel instance (owns its log buffer + UI node handles). */
typedef struct sk_editor_console_panel_t sk_editor_console_panel_t;

/**
 * Create the console panel under @p parent (or the context root when parent is
 * invalid). Registers a logger sink so subsequent sk_log_* messages appear.
 *
 * @param ui         Live sk_ui_api_t (must not be NULL; already init'd).
 * @param ctx        UI context (must not be NULL).
 * @param parent     Parent node, or SK_UI_NODE_INVALID for context root.
 * @param logger_api Host logger table (must not be NULL).
 * @param log_ctx    Host logger context that owns sinks (must not be NULL).
 * @return Panel, or NULL on failure.
 */
sk_editor_console_panel_t* sk_editor_console_panel_create(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent, const sk_logger_api_t* logger_api,
														  sk_logger_context_t* log_ctx);

/**
 * Destroy the panel: unregister sink, destroy the panel subtree, free storage.
 * Safe on NULL. Does not destroy @p ctx.
 */
void sk_editor_console_panel_destroy(sk_editor_console_panel_t* panel);

/** Root node of the panel tree (valid until destroy). */
sk_ui_node_t sk_editor_console_panel_root(const sk_editor_console_panel_t* panel);

/**
 * Sync retained log labels from the internal buffer (filter / level toggles /
 * clear / new messages). Call once per frame before style_resolve/layout, or
 * after injecting logs in tests.
 * @return 0 on success.
 */
i32 sk_editor_console_panel_sync(sk_editor_console_panel_t* panel);

/** Append a line as if emitted by the logger sink (tests / manual inject). */
void sk_editor_console_panel_push(sk_editor_console_panel_t* panel, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message);

/** Clear stored lines and rebuild the scroll content. */
void sk_editor_console_panel_clear(sk_editor_console_panel_t* panel);

/** Number of stored (unfiltered) log lines. */
u32 sk_editor_console_panel_line_count(const sk_editor_console_panel_t* panel);

/** Number of lines currently visible in the scroll content after filters. */
u32 sk_editor_console_panel_visible_count(const sk_editor_console_panel_t* panel);

/**
 * Logical rect of the panel root after layout (absolute border box).
 * Used by the dual host for input hit regions.
 * @return 0 on success, non-zero if layout not available.
 */
i32 sk_editor_console_panel_abs_rect(const sk_editor_console_panel_t* panel, sk_ui_rect_t* out);

#ifdef __cplusplus
}
#endif
