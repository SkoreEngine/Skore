#pragma once

/**
 * @file history_window.h
 * @brief History window (APX-374): v2 migration.
 *
 * Port of main-branch Skore::HistoryWindow (migration manifest §3.4) onto
 * the v2 editor shell: a Scene-workspace RightTop window that lists the
 * undo/redo stacks like the C++ `ImGuiDrawUndoRedoActions` flat list.
 *
 * The v2 editor has no undo/redo stack yet (the shell toolbar Undo/Redo
 * entries are inert), so the stacks are **MOCK data**: the window seeds a
 * small session history ("Create Entity", "Move Entity", ...) that the ops
 * table can push to / pop / clear, so the surface is fully exercisable and
 * can be wired to the real undo stack when it lands. `m_shouldAutoScroll`
 * (C++, unused) is dropped.
 *
 * Public entry points (OpenHistoryWindow / push / pop / clear / listing
 * queries) follow the APX-365 pattern: never exported as free symbols. The
 * window publishes one process-lifetime `sk_editor_history_ops_t` table
 * registered with `app_api->add_impl` under
 * SK_EDITOR_HISTORY_OPS_TYPE_ID; callers look it up via
 * `sk_editor_history_ops(ctx, api)` and call through the pointers.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_HISTORY_OPS_TYPE_ID SK_TYPE_ID("sk.editor.history.ops", 0x31c04a2ee8f5b6d7ULL, 0x7a11d3f6c9e52b48ULL)

/** Max retained undo/redo scope names (C++ undoActions/redoActions arrays). */
#define SK_EDITOR_HISTORY_MAX_ENTRIES 128u
/** Max characters of one scope name. */
#define SK_EDITOR_HISTORY_NAME_CAP 128u

/**
 * History window public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance for per-instance queries).
 */
typedef struct sk_editor_history_ops_t {
	/** Open (or focus) the History window (C++ OpenHistoryWindow). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Push one undo scope name onto the undo stack (C++ undoActions push). */
	void (*push_undo)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t name);

	/** Push one redo scope name onto the redo stack (C++ redoActions push). */
	void (*push_redo)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t name);

	/** Pop the top undo scope (returns its name; NULL/empty when empty). */
	const_chr_t (*pop_undo)(sk_app_context_t* app_context, const sk_app_api_t* app_api, char* out, u32 cap);

	/** Pop the top redo scope (returns its name; NULL/empty when empty). */
	const_chr_t (*pop_redo)(sk_app_context_t* app_context, const sk_app_api_t* app_api, char* out, u32 cap);

	/** Clear both stacks (C++ resetUndoRedoState). */
	void (*clear)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Number of undo scopes. */
	u32 (*undo_count)(const sk_editor_window_t* window);

	/** Number of redo scopes. */
	u32 (*redo_count)(const sk_editor_window_t* window);

	/** Name of undo scope @p index (0 = most recent). Borrowed. */
	const_chr_t (*undo_name_at)(const sk_editor_window_t* window, u32 index);

	/** Name of redo scope @p index (0 = most recent). Borrowed. */
	const_chr_t (*redo_name_at)(const sk_editor_window_t* window, u32 index);
} sk_editor_history_ops_t;

/** First registered History ops table, or NULL before register. */
SK_FINLINE const sk_editor_history_ops_t* sk_editor_history_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_history_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_HISTORY_OPS_TYPE_ID);
}

/**
 * Register the History window impl + ops table on @p app_context. Idempotent
 * per context. Call before sk_editor_windows_register_impls so the real
 * impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_history_shutdown in tests.
 */
void sk_editor_history_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_history_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
