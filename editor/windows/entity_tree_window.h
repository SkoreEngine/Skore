#pragma once

/**
 * @file entity_tree_window.h
 * @brief Scene hierarchy / Entity Tree window (APX-370): v2 migration.
 *
 * Port of main-branch Skore::EntityTreeWindow (migration manifest §3.3) onto
 * the v2 editor shell: hierarchical entity list with expand/collapse arrows,
 * row selection (ctrl toggles, empty-area clears), F2 in-row rename, the
 * Create / Rename / Duplicate / Delete context menu (toolbar "+" or
 * right-click), per-row visible (active) + lock toggles, and drag sources on
 * every entity row with row drop targets (reparent) + between-row bands
 * (reorder), the C++ SK_ENTITY_PAYLOAD surface (drag_drop_family_tests).
 *
 * The tree is backed by the v2 entity layer when a scene is attached
 * (`set_scene(repository, scene_rid)`): the scene payload is a
 * sk.scene_resource whose Roots list owns root sk.entity_resource payloads,
 * each with its own Name / Components / Children lists (resource_asset_builtins.h
 * field indices). Create = create_resource + add to the parent's Children (or
 * the scene Roots); Rename = set_string on field 0; Delete = destroy_resource
 * (the repository detaches the resource from its parent and cascades into
 * sub-objects); Duplicate = repository clone (deep-copies the subtree);
 * Reparent = remove_from_subobject_list on the old parent + add on the new.
 * Without an attached scene the window shows a clearly-marked MOCK scene
 * (same seed model, mutable through the same ops).
 *
 * MOCK sections (marked MOCK in this file):
 *  - Active (visible) + Locked toggles: the v2 EntityResource payload has no
 *    Deactivated / Locked fields (C++ EntityResource fields, not ported), so
 *    both live in session-only window state keyed by entity id.
 *  - Create Entity From Asset: the C++ opens a resource-selection popup; v2
 *    has no scene-asset picker yet, so the action records a test-visible
 *    flag on the window state instead of creating.
 *  - Double-click / second click of the already-selected row frames the
 *    entity through the Scene View ops table (`view_entity`).
 *  - Show Resource Inspector opens the Resource Debugger through its ops
 *    table (`inspect_resource`); never a direct symbol.
 *  - Prototype-override items (Revert Instance Overrides / Add Back This
 *    Instance) register dead handlers: v2 scenes have no prototype removal
 *    state yet, and the C++ CheckIsOverride already returns false always.
 *  - "Show Scene Entity" (debug toggle): no live scene/entity runtime
 *    hierarchy exists in v2, so the toggle only switches the session flag.
 *
 * Row icons are not ported: the C++ tree uses Font Awesome glyphs
 * (CUBE/CUBES/EYE/LOCK, manifest §3.3 "Content/Images: none"), and the v2
 * font atlas is ASCII-only, so rows are plain labels and the visible/lock
 * toggles are text buttons (V / L) — no Content/Images icon is consumed by
 * this window.
 *
 * Public entry points (OpenEntityTree / AddSceneEntity / RenameSceneEntity /
 * DuplicateSceneEntity / DeleteSceneEntity / ClearSelection / SelectEntity /
 * SetActivated / SetLocked / ...) follow the APX-365 pattern: never exported
 * as free symbols. The window publishes one process-lifetime
 * `sk_editor_entity_tree_ops_t` table registered with `app_api->add_impl`
 * under SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID; callers look it up via
 * `sk_editor_entity_tree_ops(ctx, api)` and call through the pointers.
 * Selection changes announce through notify.h (SELECTION_CHANGED +
 * ENTITY_SELECTION / ENTITY_DESELECTION); structure changes publish
 * ENTITY_CREATED / ENTITY_RENAMED / ENTITY_DELETED / ENTITY_REPARENTED so
 * the inspector and other windows can react.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "notify.h"
#include "project_browser_window.h" /* sk_editor_menu_item_desc_t */
#include "repository.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID SK_TYPE_ID("sk.editor.entity_tree.ops", 0x7b85c0622ff09a46ULL, 0x1507dd4af9db4417ULL)

/**
 * Entity Tree public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance when the operation is per-instance) so callers
 * from outside the window work without reaching into window internals.
 */
typedef struct sk_editor_entity_tree_ops_t {
	/** Open (or focus) the Entity Tree window (C++ OpenEntityTree). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Register a context-menu entry (C++ EntityTreeWindow::AddMenuItem). */
	void (*add_menu_item)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_editor_menu_item_desc_t* item);

	/**
	 * Attach the scene the window lists. @p repository is borrowed (the host
	 * owns it and must keep it alive until the window closes, or passes NULL
	 * + SK_RID_ZERO to detach back to the clearly-marked mock scene).
	 * @p scene_rid must resolve to a sk.scene_resource payload in
	 * @p repository (its Roots field owns the root entities).
	 */
	void (*set_scene)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_repository_t* repository, sk_rid_t scene_rid);

	/** Clear the selection (C++ sceneEditor->ClearSelection). */
	void (*clear_selection)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope);

	/**
	 * Select exactly @p rid, or toggle it when @p clear_others is 0
	 * (C++ SelectEntity(rid, clearSelection) / ctrl-click).
	 */
	void (*select_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, i32 clear_others, sk_undo_redo_scope_t* scope);

	/** Copy the current selection into @p out; returns the selected count. */
	u32 (*get_selected_entities)(const sk_editor_window_t* window, sk_rid_t* out, u32 out_cap);

	/** Last selected entity RID (C++ GetLastSelectedItem analog). */
	sk_rid_t (*get_last_selected)(const sk_editor_window_t* window);

	/** Put @p rid into rename mode (selects it first; C++ RenameSceneEntity). */
	void (*set_rename_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, sk_undo_redo_scope_t* scope);

	/** Apply a rename immediately (the rename overlay commit calls this; C++ sceneEditor->Rename). */
	void (*rename_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, const_chr_t new_name);

	/** Create an entity under @p parent (0 → scene roots; C++ AddSceneEntity). */
	sk_rid_t (*create_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t parent, sk_undo_redo_scope_t* scope);

	/** Create Entity From Asset (C++ AddSceneEntityFromAsset; MOCK popup). */
	void (*create_entity_from_asset)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Duplicate the selected entity (deep copy; C++ DuplicateSceneEntity). */
	void (*duplicate_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope);

	/** Delete the selected entities (C++ DeleteSceneEntity). */
	void (*delete_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_undo_redo_scope_t* scope);

	/** Reparent @p entity under @p new_parent (0 → scene roots). Cycle-safe. */
	void (*reparent_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t entity, sk_rid_t new_parent,
							sk_undo_redo_scope_t* scope);

	/** Visible (active) toggle. MOCK: session-only, not written to the payload. */
	void (*set_entity_active)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, i32 active);

	/** Lock toggle. MOCK: session-only, not written to the payload. */
	void (*set_entity_locked)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid, i32 locked);

	/** Non-zero when @p rid is visible/active (default 1; MOCK). */
	i32 (*is_entity_active)(const sk_editor_window_t* window, sk_rid_t rid);

	/** Non-zero when @p rid is locked (default 0; MOCK). */
	i32 (*is_entity_locked)(const sk_editor_window_t* window, sk_rid_t rid);

	/** Opened scene RID (C++ sceneEditor->GetOpenedResource), or mock root. */
	sk_rid_t (*get_root)(const sk_editor_window_t* window);

	/** Number of entities in the tree (scene root + descendants). */
	u32 (*entity_count)(const sk_editor_window_t* window);

	/** Name of tree entity @p index (tree preorder, root first). */
	const_chr_t (*entity_name_at)(const sk_editor_window_t* window, u32 index);

	/** RID of tree entity @p index (tree preorder, root first). */
	sk_rid_t (*entity_rid_at)(const sk_editor_window_t* window, u32 index);

	/** "Show Scene Entity" debug toggle state (MOCK: v2 has no live tree). */
	i32 (*get_show_scene_entity)(const sk_editor_window_t* window);

	/** Flip the "Show Scene Entity" debug toggle (C++ ShowSceneEntity). */
	void (*set_show_scene_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 on);

	/** Read-only scene state (C++ sceneEditor->IsReadOnly; default 0). */
	i32 (*is_read_only)(const sk_editor_window_t* window);

	/** Set the read-only scene state (C++ SceneEditor::SetReadOnly). */
	void (*set_read_only)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 ro);
} sk_editor_entity_tree_ops_t;

/** First registered Entity Tree ops table, or NULL before register. */
SK_FINLINE const sk_editor_entity_tree_ops_t* sk_editor_entity_tree_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_entity_tree_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_ENTITY_TREE_OPS_TYPE_ID);
}

/**
 * Register the Entity Tree window impl + ops table on @p app_context.
 * Idempotent per context. Call before sk_editor_windows_register_impls so
 * the real impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_entity_tree_shutdown in tests.
 */
void sk_editor_entity_tree_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_entity_tree_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
