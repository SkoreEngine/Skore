#pragma once

/**
 * @file editor_window.h
 * @brief Editor window + workspace scaffolding (APX-329 / APX-330).
 *
 * EditorWindow is a classic struct-with-function-pointers table (a
 * multi-instance FP bag, not a global `sk_*_api_t`) that mirrors the main
 * engine's EditorWindow shape without migrating any per-window UI: concrete
 * windows register a static `sk_editor_window_t` with
 * `app_api->add_impl(ctx, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &window)` and the
 * editor opens/closes instances of it, enumerating the registrations via
 * `app_api->get_all_impls`. Workspace types are plain descriptors that
 * register the same way under SK_EDITOR_WORKSPACE_IMPL_TYPE_ID.
 *
 * APX-330 scaffolds the 14 main editor windows (main_windows.h/c): one
 * `add_impl sk_editor_window_t` per window with title + dock metadata only
 * (empty Draw). Dockspace init/reset additionally project the default
 * layout onto a sk-ui dockspace (main InitDockSpace splits: center/left/
 * right-top/right-bottom/bottom-left/bottom-right) and the window registry
 * wires chrome close/undock/redock/tab ops through the sk-ui dock APIs
 * (dock_tab_close / dock_window_to_node / dock_window_undock /
 * dock_tab_reorder / dock_set_tab_callback).
 *
 * Host-facing surface (APX-328/329): the functions below are the internal
 * wiring behind sk_editor_api_t. Hosts and tests resolve the editor only via
 * app_api->get_api(ctx, SK_EDITOR_API_TYPE_ID) (see editor_api.h).
 */

#include "app.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for editor window implementations (`add_impl` / `get_all_impls`). */
#define SK_EDITOR_WINDOW_IMPL_TYPE_ID SK_TYPE_ID("sk.editor_window_impl", 0x9d144da26d770ac6ULL, 0x85e08b5b5b035eb8ULL)

/** Type id for editor workspace type implementations (`add_impl` / `get_all_impls`). */
#define SK_EDITOR_WORKSPACE_IMPL_TYPE_ID SK_TYPE_ID("sk.editor_workspace_impl", 0xf8bf56175a168405ULL, 0x79f58cc41000cba0ULL)

/* Workspace type ids (sequential; a window's workspace_mask uses bit (id-1)). */
#define SK_EDITOR_WORKSPACE_SCENE 1u
#define SK_EDITOR_WORKSPACE_GRAPH 2u
#define SK_EDITOR_WORKSPACE_ANIMATOR 3u
#define SK_EDITOR_WORKSPACE_MATERIAL 4u

/** Mask constant covering every workspace (all 8 mask bits). */
#define SK_EDITOR_WORKSPACE_ALL 0xFFu

/** Single-bit mask for one workspace type in a window's workspace_mask. */
#define SK_EDITOR_WORKSPACE_MASK(_workspace_id) (1u << ((_workspace_id) - 1u))

/**
 * Default dock slot a window asks for when its dockspace is initialized/reset.
 *
 * Zone names match the main InitDockSpace splits (APX-330): the dock model
 * of a workspace splits into a bottom strip (BOTTOM_LEFT | BOTTOM_RIGHT), a
 * LEFT column, a CENTER leaf, and a right column split into RIGHT_TOP /
 * RIGHT_BOTTOM. SK_EDITOR_DOCK_NONE windows (menu-opened, e.g. Packages /
 * Settings) and on-demand windows (e.g. ResourceDebugger) are never opened
 * by dockspace init; they appear only when opened explicitly.
 */
typedef enum sk_editor_dock_position_t {
	SK_EDITOR_DOCK_NONE = 0,
	SK_EDITOR_DOCK_LEFT = 1,
	SK_EDITOR_DOCK_RIGHT = 2,
	SK_EDITOR_DOCK_TOP = 3,
	SK_EDITOR_DOCK_BOTTOM = 4,
	SK_EDITOR_DOCK_FILL = 5, /* center leaf */
	SK_EDITOR_DOCK_FLOATING = 6,
	SK_EDITOR_DOCK_RIGHT_TOP = 7,
	SK_EDITOR_DOCK_RIGHT_BOTTOM = 8,
	SK_EDITOR_DOCK_BOTTOM_LEFT = 9,
	SK_EDITOR_DOCK_BOTTOM_RIGHT = 10,
} sk_editor_dock_position_t;

/**
 * Workspace type descriptor (APX-329).
 *
 * One static instance per workspace type registers under
 * SK_EDITOR_WORKSPACE_IMPL_TYPE_ID so the editor can enumerate the known
 * workspace kinds and name/order them in the workspace switcher. The built-in
 * four (Scene/Graph/Animator/Material) are registered by
 * sk_editor_workspace_register_impls; hosts may register more.
 */
typedef struct sk_editor_workspace_type_t {
	/** Workspace type id (SK_EDITOR_WORKSPACE_SCENE / GRAPH / ANIMATOR / MATERIAL, or a host-defined id). */
	u32 id;

	/** Human-readable workspace name shown in the switcher (e.g. "Scene"). Borrowed, process-lifetime. */
	const_chr_t display_name;

	/** Sort order in the workspace switcher (lower first). */
	i32 order;
} sk_editor_workspace_type_t;

/**
 * Editor window implementation table (APX-329).
 *
 * Mirrors the main engine's EditorWindow shape (identity + layout metadata +
 * lifecycle callbacks) without the per-window UI that lands later. A concrete
 * window registers a static instance of this struct via
 * `app_api->add_impl`; `sk_editor_window_open` copies the table per open so
 * each instance owns its `user_data`.
 *
 * The `workspace_mask` field uses bit (id-1) per workspace type:
 * `SK_EDITOR_WORKSPACE_MASK(SK_EDITOR_WORKSPACE_SCENE)` admits only Scene,
 * `SK_EDITOR_WORKSPACE_ALL` admits every workspace. A dockspace init opens
 * the registered windows whose mask admits the workspace's type.
 */
typedef struct sk_editor_window_t {
	/** Opaque per-open state owned by the window implementation. */
	void* user_data;

	/** Distinct type id of this window class (e.g. "sk.editor_window.scene"). */
	sk_type_id_t type_id;

	/** Display title shown on the dock tab / title bar. Borrowed. */
	const_chr_t title;

	/** Stable sk-ui dock window id (node id) used by the dock chrome and the
	 *  tab close/undock/redock APIs. Borrowed, process-lifetime. */
	const_chr_t dock_id;

	/** Default dock slot requested by dockspace init/reset (sk_editor_dock_position_t). */
	i32 dock_position;

	/** Dock order relative to other windows in the same dock slot (lower first). */
	i32 order;

	/** Workspace availability mask (SK_EDITOR_WORKSPACE_MASK / SK_EDITOR_WORKSPACE_ALL). */
	u32 workspace_mask;

	/** Called once right after the window instance is opened. May be NULL. */
	void (*init)(struct sk_editor_window_t* window);

	/**
	 * Called every frame with the window's open flag; the window may clear
	 * `*open` (set to 0) to request close. May be NULL.
	 */
	void (*draw)(struct sk_editor_window_t* window, i32* open);

	/** Optional per-frame render pass (NULL when the window has no render stage). */
	void (*render)(struct sk_editor_window_t* window);

	/** Called when the window instance is closed / destroyed. May be NULL. */
	void (*destroy)(struct sk_editor_window_t* window);
} sk_editor_window_t;

/** Non-zero when @p workspace_mask admits the workspace with type id @p workspace_id. */
SK_FINLINE i32 sk_editor_workspace_mask_contains(u32 workspace_mask, u32 workspace_id) {
	if (workspace_id < 1u || workspace_id > 8u) {
		return 0;
	}
	return (workspace_mask & SK_EDITOR_WORKSPACE_MASK(workspace_id)) != 0u;
}

/**
 * Open workspace (opaque; runtime state lives on the object).
 * A workspace must be destroyed before the app context it was created on
 * shuts down.
 */
typedef struct sk_editor_workspace_t sk_editor_workspace_t;

/**
 * Register the four built-in workspace types (Scene/Graph/Animator/Material)
 * as SK_EDITOR_WORKSPACE_IMPL_TYPE_ID impls on @p app_context. Call once at
 * editor boot (after sk_editor_bind_tables) and at the start of host tests.
 * Repeated calls append duplicates; callers register once.
 */
void sk_editor_workspace_register_impls(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Create a workspace of type @p workspace_type_id (name/order come from the
 * registered SK_EDITOR_WORKSPACE_IMPL_TYPE_ID descriptor).
 * @return Open workspace, or NULL when the workspace type is not registered
 *         or on allocation failure.
 */
sk_editor_workspace_t* sk_editor_workspace_create(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_type_id);

/** Destroy a workspace (frees its dock layout; open windows stay open). */
void sk_editor_workspace_destroy(sk_editor_workspace_t* workspace);

/** Make @p workspace the active workspace (switcher target for the frame pipeline). */
void sk_editor_workspace_switch(sk_editor_workspace_t* workspace);

/**
 * Copy up to @p out_cap open workspace pointers into @p out and return the
 * total count (NULL/0 queries the count only). Insertion order.
 */
u32 sk_editor_workspace_list(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_workspace_t** out, u32 out_cap);

/** Currently active workspace, or NULL when none is active. */
sk_editor_workspace_t* sk_editor_workspace_active(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Open an instance of the window implementation registered under
 * SK_EDITOR_WINDOW_IMPL_TYPE_ID whose type_id equals @p window_type_id. One
 * open instance per window type: reopening an already-open type returns the
 * existing instance. The instance's `init` callback runs once on open.
 * @return Open window instance, or NULL when the type is not registered (or
 *         on allocation failure).
 */
sk_editor_window_t* sk_editor_window_open(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id);

/** Close an open window instance (runs its `destroy` callback and frees it). */
void sk_editor_window_close(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

/** First open window instance with type id @p window_type_id, or NULL. */
sk_editor_window_t* sk_editor_window_by_type(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t window_type_id);

/**
 * Copy up to @p out_cap open window pointers into @p out and return the total
 * count (NULL/0 queries the count only). Insertion order.
 */
u32 sk_editor_window_iterate(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t** out, u32 out_cap);

/**
 * Initialize the dockspace layout of @p workspace from the registered window
 * impls whose workspace_mask admits the workspace's type: the default dock
 * layout (type/dock position/order slots) is recorded and one instance of
 * each matching window is opened (deduped by type_id). Idempotent.
 *
 * When the sk-ui API is registered on the workspace's app context (editor
 * host, or tests that loaded the ui plugin), init also projects the default
 * layout onto a workspace-owned sk-ui dockspace: it creates a UI context,
 * splits it into the main InitDockSpace zones (center/left/right-top/
 * right-bottom/bottom-left/bottom-right), creates the editor-window chrome
 * for each default window, docks them tab-ordered by `order`, and installs
 * the tab close callback (close/undock/redock/reorder stay live through the
 * sk-ui dock chrome).
 */
void sk_editor_dockspace_init(sk_editor_workspace_t* workspace);

/**
 * Reset the dockspace layout of @p workspace to the same default derived by
 * sk_editor_dockspace_init (restores defaults after future runtime dock
 * mutations; open windows are left as-is). Rebuilds the sk-ui dockspace
 * model when one exists.
 */
void sk_editor_dockspace_reset(sk_editor_workspace_t* workspace);

/**
 * sk-ui context backing @p workspace's dockspace, or NULL before
 * sk_editor_dockspace_init (or when no ui API is registered on the app
 * context). The dockspace is named `sk.editor.dock.ws<type_id>` on this
 * context; dock queries (dock_find_node_for_window, dock_leaf_tabs, ...)
 * run against it.
 */
struct sk_ui_context_t* sk_editor_workspace_dock_context(const sk_editor_workspace_t* workspace);

#ifdef __cplusplus
}
#endif
