#pragma once

/**
 * @file notify.h
 * @brief add_impl observer structs (APX-365). Replaces the C++ Event system.
 *
 * There is no event bus: no queue, no wildcard, no deferred invoke, no
 * Event::Bind/Invoke. Each notification kind is a type id plus an observer
 * struct of function pointers. Subscribers `add_impl` their observer;
 * publishers call the matching `sk_editor_notify_*` helper, which copies the
 * impl list, sorts it, and calls each function pointer.
 *
 * Rules (also in docs/editor/window-table-pattern.md):
 *
 *  - Registration: `app_api->add_impl(ctx, SK_EDITOR_NOTIFY_*, &observer)`.
 *    The observer storage is owned by the subscriber and must stay valid
 *    until `remove_impl` (typically a field on the window instance).
 *  - Lookup: `impl_count` / `get_all_impls` on that type id. Insertion order
 *    until a remove_impl (swap-remove may reorder the raw list).
 *  - Ordering: every observer starts with `i32 order` (lower runs first).
 *    Ties keep add_impl insertion order. Dispatch copies the list before
 *    calling so add/remove during a callback cannot walk a live array.
 *  - Lifetime: window `destroy` (or the subscriber's teardown) must
 *    `remove_impl` every observer it added. Impl pointers die with
 *    `sk_app_shutdown`. Do not register a stack observer without a matching
 *    remove before that stack frame returns.
 *  - Optional slots: a NULL function pointer is skipped.
 */

#include "app.h"
#include "common.h"
#include "repository.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_NOTIFY_SELECTION_CHANGED SK_TYPE_ID("sk.editor.notify.selection_changed", 0x4284b11e5e8fc089ULL, 0x6a4635f2efc88769ULL)
#define SK_EDITOR_NOTIFY_ENTITY_SELECTION SK_TYPE_ID("sk.editor.notify.entity_selection", 0x7394e0f12924159bULL, 0x473760406dcdcffaULL)
#define SK_EDITOR_NOTIFY_ENTITY_DESELECTION SK_TYPE_ID("sk.editor.notify.entity_deselection", 0xc196968c62a75f44ULL, 0x3d6af81499595e87ULL)
#define SK_EDITOR_NOTIFY_ENTITY_DEBUG_SELECTION SK_TYPE_ID("sk.editor.notify.entity_debug_selection", 0x5b0fb8f3feec68b3ULL, 0xe097133695b59977ULL)
#define SK_EDITOR_NOTIFY_ENTITY_DEBUG_DESELECTION SK_TYPE_ID("sk.editor.notify.entity_debug_deselection", 0x4eba76c77fb7e3dcULL, 0x7c09de6fe140b4e8ULL)
#define SK_EDITOR_NOTIFY_ASSET_SELECTION SK_TYPE_ID("sk.editor.notify.asset_selection", 0x6157b4f112c4a7b9ULL, 0x81be45519128c429ULL)
#define SK_EDITOR_NOTIFY_RESOURCE_SELECTION SK_TYPE_ID("sk.editor.notify.resource_selection", 0x9eee507af3f0d635ULL, 0xb67c05cc555c15efULL)
#define SK_EDITOR_NOTIFY_MATERIAL_NODE_SELECTION SK_TYPE_ID("sk.editor.notify.material_node_selection", 0xe29e779304fac197ULL, 0xbea19057e54194caULL)
#define SK_EDITOR_NOTIFY_DROP_FILE SK_TYPE_ID("sk.editor.notify.drop_file", 0x0260b7339fee5ef1ULL, 0x4e1021c7facae0bdULL)
#define SK_EDITOR_NOTIFY_ASSET_OPENED SK_TYPE_ID("sk.editor.notify.asset_opened", 0xd1a6dfb6c8f57d71ULL, 0x824b59749fe95efcULL)
#define SK_EDITOR_NOTIFY_ASSET_ACTIVATED SK_TYPE_ID("sk.editor.notify.asset_activated", 0xb3511fca436624e9ULL, 0xa4655bda5342eba3ULL)
#define SK_EDITOR_NOTIFY_ENTITY_CREATED SK_TYPE_ID("sk.editor.notify.entity_created", 0xd28fb09290b04fd8ULL, 0xa10907212f90ade0ULL)
#define SK_EDITOR_NOTIFY_ENTITY_RENAMED SK_TYPE_ID("sk.editor.notify.entity_renamed", 0x03fb4241837f24bdULL, 0x335e4d164ccf399eULL)
#define SK_EDITOR_NOTIFY_ENTITY_DELETED SK_TYPE_ID("sk.editor.notify.entity_deleted", 0xf1999c1224eb0824ULL, 0xb16416faf4a3fe89ULL)
#define SK_EDITOR_NOTIFY_ENTITY_REPARENTED SK_TYPE_ID("sk.editor.notify.entity_reparented", 0xb859cd0bcf2438dcULL, 0x4b366071d5906608ULL)
#define SK_EDITOR_NOTIFY_VIEWPORT_STATE SK_TYPE_ID("sk.editor.notify.viewport_state", 0x5e4a2f8d17b0c63aULL, 0xa3d9e04b72c1f85eULL)
#define SK_EDITOR_NOTIFY_DIRTY SK_TYPE_ID("sk.editor.notify.dirty", 0x1c8e4a7b93d0f621ULL, 0x5e2a9c4d8b17f0aeULL)
#define SK_EDITOR_NOTIFY_SAVE SK_TYPE_ID("sk.editor.notify.save", 0xa71d3e8c5b2490f6ULL, 0x0c4e8f1a6d3b27c5ULL)

/** C++ OnSelectionChanged — void(). */
typedef struct sk_editor_on_selection_changed_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_selection_changed)(void* user);
} sk_editor_on_selection_changed_t;

/** C++ OnEntitySelection — void(u32 workspaceId, RID). */
typedef struct sk_editor_on_entity_selection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_selection)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_entity_selection_t;

/** C++ OnEntityDeselection — void(u32 workspaceId, RID). */
typedef struct sk_editor_on_entity_deselection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_deselection)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_entity_deselection_t;

/** C++ OnEntityDebugSelection — void(u32 workspaceId, Entity*). */
typedef struct sk_editor_on_entity_debug_selection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_debug_selection)(void* user, u32 workspace_id, void* entity);
} sk_editor_on_entity_debug_selection_t;

/** C++ OnEntityDebugDeselection — void(u32 workspaceId, Entity*). */
typedef struct sk_editor_on_entity_debug_deselection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_debug_deselection)(void* user, u32 workspace_id, void* entity);
} sk_editor_on_entity_debug_deselection_t;

/** C++ OnAssetSelection — void(u32 workspaceId, RID). */
typedef struct sk_editor_on_asset_selection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_asset_selection)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_asset_selection_t;

/** C++ OnResourceSelection — void(u32 workspaceId, RID). */
typedef struct sk_editor_on_resource_selection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_resource_selection)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_resource_selection_t;

/** C++ OnMaterialNodeSelection — void(u32 workspaceId, RID). */
typedef struct sk_editor_on_material_node_selection_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_material_node_selection)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_material_node_selection_t;

/** C++ OnDropFileCallback — void(StringView path). @p path is borrowed for the call. */
typedef struct sk_editor_on_drop_file_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_drop_file)(void* user, const_chr_t path);
} sk_editor_on_drop_file_t;

/** Asset opened in the workspace (EditorWorkspace::OpenAsset). */
typedef struct sk_editor_on_asset_opened_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_asset_opened)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_asset_opened_t;

/** Asset activated (double-click / enter in a browser). */
typedef struct sk_editor_on_asset_activated_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_asset_activated)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_asset_activated_t;

/** Entity created in a scene workspace. */
typedef struct sk_editor_on_entity_created_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_created)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_entity_created_t;

/** Entity renamed. @p name is borrowed for the call. */
typedef struct sk_editor_on_entity_renamed_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_renamed)(void* user, u32 workspace_id, sk_rid_t rid, const_chr_t name);
} sk_editor_on_entity_renamed_t;

/** Entity deleted. @p rid is the id that was removed (do not resolve after return). */
typedef struct sk_editor_on_entity_deleted_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_deleted)(void* user, u32 workspace_id, sk_rid_t rid);
} sk_editor_on_entity_deleted_t;

/** Entity reparented (drag-drop / reparent op). @p rid moved under @p new_parent. */
typedef struct sk_editor_on_entity_reparented_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_entity_reparented)(void* user, u32 workspace_id, sk_rid_t rid, sk_rid_t new_parent);
} sk_editor_on_entity_reparented_t;

/**
 * SceneViewWindow viewport state (manifest §3.9 EditorSerialize surface).
 * Snapshot published whenever a toolbar / options change mutates it, so
 * other windows can react to view/gizmo preference changes without an event
 * bus (APX-365). Values mirror the C++ fields: gizmoOperation (0 select,
 * 1 translate, 2 rotate, 3 scale), guizmoMode (0 world, 1 local),
 * viewType (0 3D, 1 2D), cameraFov, and the render-debug toggles.
 */
typedef struct sk_editor_viewport_state_t {
	u32 gizmo_operation;
	u8 _pad0[4];
	i32 gizmo_mode;
	i32 gizmo_snap_enabled;
	f32 snap[3];
	i32 view_type;
	i32 draw_icons;
	f32 camera_fov;
	i32 draw_grid;
	i32 draw_selection_outline;
	i32 draw_debug_physics;
	i32 show_all_physics_shapes;
	i32 lock_camera_frustum;
	i32 draw_mesh_aabb;
	i32 draw_nav_mesh;
	i32 draw_component_gizmos;
	/** C++ windowStartedSimulation (mock: session-only, no live simulation). */
	i32 simulating;
	i32 interaction_disabled;
} sk_editor_viewport_state_t;

/** C++ SceneViewWindow viewport state change (void(u32 workspaceId, const state&)). */
typedef struct sk_editor_on_viewport_state_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_viewport_state)(void* user, u32 workspace_id, const sk_editor_viewport_state_t* state);
} sk_editor_on_viewport_state_t;

/**
 * Project / scene content became dirty (entity or asset mutation). Not a C++
 * Event; v2 replacement for the editor's unsaved-resource flag so the shell
 * Save All path and other windows share one observer list.
 */
typedef struct sk_editor_on_dirty_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_dirty)(void* user, u32 workspace_id);
} sk_editor_on_dirty_t;

/** Save All requested (File/Save All, toolbar). @p workspace_id is the active workspace, or 0. */
typedef struct sk_editor_on_save_t {
	i32 order;
	u8 _pad0[4];
	void* user;
	void (*on_save)(void* user, u32 workspace_id);
} sk_editor_on_save_t;

void sk_editor_notify_selection_changed(sk_app_context_t* app_context, const sk_app_api_t* app_api);
void sk_editor_notify_entity_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_entity_deselection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_entity_debug_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, void* entity);
void sk_editor_notify_entity_debug_deselection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, void* entity);
void sk_editor_notify_asset_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_resource_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_material_node_selection(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_drop_file(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path);
void sk_editor_notify_asset_opened(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_asset_activated(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_entity_created(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_entity_renamed(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid, const_chr_t name);
void sk_editor_notify_entity_deleted(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid);
void sk_editor_notify_entity_reparented(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, sk_rid_t rid, sk_rid_t new_parent);
void sk_editor_notify_viewport_state(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id, const sk_editor_viewport_state_t* state);
void sk_editor_notify_dirty(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id);
void sk_editor_notify_save(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 workspace_id);

#ifdef __cplusplus
}
#endif
