#pragma once

/**
 * @file project_browser_window.h
 * @brief Project / Content Browser window (APX-369): v2 migration.
 *
 * Port of main-branch Skore::ProjectBrowserWindow (migration manifest §3.6)
 * onto the v2 editor shell: folder tree, content item grid, breadcrumb
 * navigation, single + multi selection, the Create/Delete/Rename context
 * menu, search filtering, and drag sources (content grid + tree leaves)
 * with folder drop targets that move assets. Folder/file tiles use the
 * Content/Images icon atlas (APX-367); thumbnails stay out of scope
 * (manifest §2.2) — every asset shows the generic file icon.
 *
 * The v2 asset layer (sk_resource_assets_api_t, via sk_editor_project_t)
 * answers the real surface: directory trees, item names/extensions, parent
 * links, create folder/file, move, import. Anything it cannot answer yet is
 * mocked and clearly marked MOCK in this file:
 *
 *  - No project attached (sk_editor_project_browser_ops->set_project never
 *    called, e.g. the shell host without a project): a fixed mock folder
 *    tree + item list is shown (same seed as the APX-365 reference window).
 *  - Import button: opens the OS file dialog in C++; v2 has no dialog yet,
 *    so the button is a no-op (MOCK). The drop-file observer (OnDropFile)
 *    is real: it imports the dropped path into the open directory.
 *  - "Show in Explorer": platform OpenURL not ported; records the path id
 *    on the window state (test-visible), no OS call.
 *  - "Copy Path Id": clipboard hooks are host-provided; copies through the
 *    ui clipboard when set, else records the path id (MOCK fallback).
 *  - "Show Resource Inspector": ResourceDebuggerWindow is not migrated yet;
 *    records the inspected path id on the window state (MOCK).
 *
 * Public entry points (ClearSelection / SelectItem / SetSelection /
 * SetRenameItem / RevealPath / Refresh / GetOpenDirectory / ...) follow the
 * APX-365 pattern: never exported as free symbols. The window publishes one
 * process-lifetime `sk_editor_project_browser_ops_t` table registered with
 * `app_api->add_impl` under SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID; callers
 * look it up via `sk_editor_project_browser_ops(ctx, api)` and call through
 * the pointers. Selection changes are announced through the notify.h
 * observer structs (SK_EDITOR_NOTIFY_SELECTION_CHANGED /
 * SK_EDITOR_NOTIFY_ASSET_SELECTION), never an event bus.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "project.h"
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
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance when the operation is per-instance) so callers
 * from outside the window work without reaching into window internals.
 */
typedef struct sk_editor_project_browser_ops_t {
	/** Open (or focus) the Project Browser window (C++ OpenProjectBrowser). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Register a "Create/<Kind>" context-menu entry (C++ AddMenuItem). */
	void (*add_menu_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item);

	/** Whether new assets may be created (C++ CanCreateAsset; true). */
	i32 (*can_create_asset)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event);

	/** Create a new asset in the open directory and start renaming it (C++ AssetNew). */
	void (*asset_new)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_event_t* event);

	/** Hide files with @p extension (e.g. ".meta") from listings (C++ HideExtension). */
	void (*hide_extension)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension);

	/** Non-zero when @p extension was hidden with hide_extension. */
	i32 (*extension_is_hidden)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t extension);

	/** Clear the selection (C++ ClearSelection). */
	void (*clear_selection)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope);

	/** Select exactly @p rid (single selection; C++ SelectItem). */
	void (*select_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope);

	/** Select exactly the given list of RIDs (multi selection). */
	void (*set_selection)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_rid_t* rids, u32 count, sk_undo_redo_scope_t* scope);

	/** Put @p rid into rename mode (selects it first; C++ SetRenameItem). */
	void (*set_rename_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope);

	/** Currently open directory node RID (C++ GetOpenDirectory). */
	sk_rid_t (*get_open_directory)(const sk_editor_window_t* window);

	/** Last selected item RID (C++ GetLastSelectedItem). */
	sk_rid_t (*get_last_selected_item)(const sk_editor_window_t* window);

	/** Copy the current selection into @p out; returns the selected count. */
	u32 (*get_selected_items)(const sk_editor_window_t* window, sk_rid_t* out, u32 out_cap);

	/** Open/activate an item (double-click / Enter in a browser; C++ OpenAsset path). */
	void (*activate_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid);

	/** Navigate to @p rid's directory, open its tree ancestors and select it. */
	void (*reveal_path)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid);

	/** Drop caches and rebuild the folder tree + item list from the asset layer. */
	void (*refresh)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/**
	 * Attach the open editor project the browser lists. Borrowed: the host
	 * owns the project and must keep it alive until the window closes (or
	 * passes NULL to detach). NULL falls back to the clearly-marked mock
	 * data set.
	 */
	void (*set_project)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_editor_project_t* project);

	/** Number of items in the current (unfiltered) open-directory listing. */
	u32 (*item_count)(const sk_editor_window_t* window);

	/** Number of items visible after the search filter + hidden extensions. */
	u32 (*visible_item_count)(const sk_editor_window_t* window);

	/** Name of listing item @p index; @p out_is_directory receives its folder flag. */
	const_chr_t (*item_name_at)(const sk_editor_window_t* window, u32 index, i32* out_is_directory);
} sk_editor_project_browser_ops_t;

/** First registered Project Browser ops table, or NULL before register. */
SK_FINLINE const sk_editor_project_browser_ops_t* sk_editor_project_browser_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_project_browser_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID);
}

/**
 * Register the Project Browser window impl + ops table on @p app_context.
 * Idempotent per context. Call before sk_editor_windows_register_impls so
 * the real impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_project_browser_shutdown in tests.
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
