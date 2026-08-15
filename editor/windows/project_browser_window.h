#pragma once

/**
 * @file project_browser_window.h
 * @brief Reference editor window (APX-365): Project Browser ops table + notify.
 *
 * Copy this pair when migrating the next window:
 *  1. Define a `sk_editor_*_ops_t` of the C++ public methods.
 *  2. Keep every implementation `static` in the .c.
 *  3. Register the ops table with `app_api->add_impl` under a dedicated type id.
 *  4. Register the `sk_editor_window_t` impl the same way.
 *  5. Subscribe / publish through `notify.h` observer structs — not an event bus.
 *  6. Callers use `sk_editor_project_browser_ops(ctx, api)->clear_selection(...)`.
 *
 * Data is mocked (fixed folder/asset list, no thumbnails, no real import).
 */

#include "editor_window.h"
#include "main_windows.h"
#include "repository.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID SK_TYPE_ID("sk.editor.project_browser.ops", 0xf7295fc4ec81640bULL, 0x26fe9a899e550583ULL)

/** Handler "Create" menu row (C++ MenuItemCreation, stripped to what v2 needs). */
typedef struct sk_editor_menu_item_desc_t {
	const_chr_t item_name;
	i32 order;
	u8 _pad0[4];
	void (*action)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user);
	i32 (*visible)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, void* user);
	void* user;
} sk_editor_menu_item_desc_t;

/** C++ MenuItemEventData for AssetNew / CanCreateAsset. */
typedef struct sk_editor_menu_item_event_t {
	sk_editor_window_t* window;
	void* user;
} sk_editor_menu_item_event_t;

/**
 * Project Browser public table. Looked up via add_impl; never call the
 * implementations by symbol.
 */
typedef struct sk_editor_project_browser_ops_t {
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);
	void (*add_menu_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item);
	i32 (*can_create_asset)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event);
	void (*asset_new)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event);
	void (*hide_extension)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension);
	i32 (*extension_is_hidden)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension);
	void (*clear_selection)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope);
	void (*select_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope);
	void (*set_rename_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope);
	sk_rid_t (*get_open_directory)(const sk_editor_window_t* window);
	sk_rid_t (*get_last_selected_item)(const sk_editor_window_t* window);
	void (*activate_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid);
} sk_editor_project_browser_ops_t;

/** First registered Project Browser ops table, or NULL before register. */
SK_FINLINE const sk_editor_project_browser_ops_t* sk_editor_project_browser_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_project_browser_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID);
}

/**
 * Register the Project Browser window impl + ops table on @p app_context.
 * Idempotent per context. Pair with sk_editor_project_browser_shutdown in tests.
 */
void sk_editor_project_browser_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_project_browser_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
