#pragma once

/**
 * @file properties_window.h
 * @brief Properties / Inspector window (APX-371): v2 migration.
 *
 * Port of main-branch Skore::PropertiesWindow (migration manifest §3.7)
 * onto the v2 editor shell: a RightBottom / All-workspaces inspector that
 * shows the current selection — entity (header + per-component property
 * rows), asset, resource or material node — through the notify.h observers
 * (ENTITY_SELECTION / ENTITY_DESELECTION, ENTITY_DEBUG_SELECTION /
 * ENTITY_DEBUG_DESELECTION, ASSET_SELECTION, RESOURCE_SELECTION,
 * MATERIAL_NODE_SELECTION, ASSET_ACTIVATED, ENTITY_RENAMED / DELETED /
 * REPARENTED), each filtered by the *active* workspace id (C++ ctor
 * bindings used the instance's workspace; v2 has one instance per type so
 * the filter follows workspace switching). Selection is consumed through
 * the observer structs published by the hierarchy and content browser —
 * no Events. Window destroy `remove_impl`s every observer so workspace
 * switch open/close cannot leak callbacks.
 *
 * In-scope surface (manifest §3.7): entity header (name / UUID / layer
 * MOCK), per-component collapsing headers with property rows drawn from the
 * payload fields using the v2 widget set (numeric, text, bool, enum,
 * vector, color, asset reference — all read-only MOCK until v2 can edit
 * arbitrary payload fields), the Add Component popup (search filter +
 * registered component types) and the '...' component settings popup
 * (Reset / Remove / Move Up / Move Down), asset name + UUID (+ MOCK import
 * settings Apply/Reimport), and generic resource fields. The manifest lists
 * no multi-select for the inspector. Out of scope (mocked): the
 * scene/texture preview pane, material-graph node property editing (RID +
 * MOCK name shown), and layer data (the v2 EntityResource payload has no
 * Layer field — session state).
 *
 * Entity/component data comes from the attached repository + scene
 * (`set_scene`; same contract as the Entity Tree): the scene payload's
 * Roots/Children own sk.entity_resource payloads whose Components
 * sub-object list carries the builtin component payload types. Add/Remove/
 * Move mutate the repository (create_resource / subobject list ops); Reset
 * records a MOCK flag until per-component reset semantics exist in v2.
 *
 * Public entry points (OpenProperties / selection + entity + component +
 * asset queries and mutations) follow the APX-365 pattern: never exported
 * as free symbols. The window publishes one process-lifetime
 * `sk_editor_properties_ops_t` table registered with `app_api->add_impl`
 * under SK_EDITOR_PROPERTIES_OPS_TYPE_ID; callers look it up via
 * `sk_editor_properties_ops(ctx, api)` and call through the pointers.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "notify.h"
#include "repository.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_PROPERTIES_OPS_TYPE_ID SK_TYPE_ID("sk.editor.properties.ops", 0x6b8d0f2a4c6e8f0bULL, 0x3d5f7a9c0e2f4b6dULL)

/** Selection kinds (C++ selectedEntity / selectedDebugEntity / selectedAsset / selectedResource / selectedMaterialNode). */
typedef enum sk_editor_properties_kind_t {
	SK_EDITOR_PROPERTIES_NONE = 0,
	SK_EDITOR_PROPERTIES_ENTITY = 1,
	SK_EDITOR_PROPERTIES_DEBUG_ENTITY = 2,
	SK_EDITOR_PROPERTIES_ASSET = 3,
	SK_EDITOR_PROPERTIES_RESOURCE = 4,
	SK_EDITOR_PROPERTIES_MATERIAL_NODE = 5,
} sk_editor_properties_kind_t;

/** Max listed components of one entity. */
#define SK_EDITOR_PROPERTIES_MAX_COMPONENTS 64u

/**
 * Properties window public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance for per-instance queries).
 */
typedef struct sk_editor_properties_ops_t {
	/** Open (or focus) the Properties window (C++ OpenProperties). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Attach the repository + scene the entity/component surface reads (C++ workspace->GetSceneEditor()). */
	void (*set_scene)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_repository_t* repository, sk_rid_t scene_rid);

	/** Current selection kind. */
	sk_editor_properties_kind_t (*get_kind)(const sk_editor_window_t* window);

	/** Currently selected RID (entity/asset/resource/material node). */
	sk_rid_t (*get_selected_rid)(const sk_editor_window_t* window);

	/** Clear the selection (C++ ClearSelection). */
	void (*clear_selection)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/* ---- entity ---- */

	/** Entity name (C++ EntityResource::Name). Borrowed. */
	const_chr_t (*get_entity_name)(const sk_editor_window_t* window, sk_rid_t entity);

	/** Rename an entity (C++ sceneEditor->Rename; real repo write). */
	void (*rename_entity)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t entity, const_chr_t name);

	/** Entity UUID (C++ entityObject.GetUUID). */
	i32 (*get_entity_uuid)(const sk_editor_window_t* window, sk_rid_t entity, char* out, u32 cap);

	/** Layer of @p entity (C++ EntityResource::Layer; MOCK — v2 payload has no Layer field). */
	u32 (*get_entity_layer)(const sk_editor_window_t* window, sk_rid_t entity);

	/** Set the layer (MOCK: session state). */
	void (*set_entity_layer)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t entity, u32 layer);

	/** Number of components on the selected entity (real Components list). */
	u32 (*get_component_count)(const sk_editor_window_t* window);

	/** Component display name at @p index (C++ FormatName of the component type). Borrowed. */
	const_chr_t (*component_name_at)(const sk_editor_window_t* window, u32 index);

	/** Component RID at @p index. */
	sk_rid_t (*component_rid_at)(const sk_editor_window_t* window, u32 index);

	/** Add a component of @p type_name (real when the type is registered in the attached repo). */
	sk_rid_t (*add_component)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t type_name);

	/** Remove component @p index (real: remove from list + destroy). */
	void (*remove_component)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index);

	/** Move component @p index by @p delta (-1 / +1; real list reorder). */
	void (*move_component)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index, i32 delta);

	/** Reset component @p index (MOCK: records a flag). */
	void (*reset_component)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index);

	/** Consume the MOCK reset flag (returns the previous component index, U32_MAX when none). */
	u32 (*consume_reset_component)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Selected component index for the settings popup (C++ selectedComponentIndex). */
	u32 (*get_selected_component_index)(const sk_editor_window_t* window);

	/** Set the selected component index (C++ ImGuiCollapsingHeaderProps click). */
	void (*set_selected_component_index)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index);

	/* ---- asset / resource ---- */

	/** Asset display name (C++ ResourceAssets::GetAssetName). Borrowed. */
	const_chr_t (*get_asset_name)(const sk_editor_window_t* window);

	/** Asset UUID (C++ ResourceAssets::GetAssetUUID). */
	i32 (*get_asset_uuid)(const sk_editor_window_t* window, char* out, u32 cap);

	/** Rename the selected asset (MOCK: records the requested name). */
	void (*rename_asset)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t name);

	/** Non-zero when the selected asset has import settings (C++ GetImportSettings). */
	i32 (*has_import_settings)(const sk_editor_window_t* window);

	/** MOCK: Apply import settings click (records a flag). */
	void (*apply_import_settings)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** MOCK: Reimport click (records a flag). */
	void (*reimport_asset)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Consume the MOCK import-settings Apply flag. */
	i32 (*consume_apply_import)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Consume the MOCK Reimport flag. */
	i32 (*consume_reimport)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Generic field count of the selected asset/resource (0 without a repo). */
	u32 (*get_field_count)(const sk_editor_window_t* window);

	/** Generic field descriptor at @p index of the selected asset/resource. */
	const sk_resource_field_t* (*get_field_at)(const sk_editor_window_t* window, u32 index);

	/** Format the value of generic field @p index of the selected asset/resource. */
	i32 (*get_field_value)(const sk_editor_window_t* window, u32 index, char* out, u32 cap);
} sk_editor_properties_ops_t;

/** First registered Properties ops table, or NULL before register. */
SK_FINLINE const sk_editor_properties_ops_t* sk_editor_properties_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_properties_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_PROPERTIES_OPS_TYPE_ID);
}

/**
 * Register the Properties window impl + ops table on @p app_context.
 * Idempotent per context. Call before sk_editor_windows_register_impls so
 * the real impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_properties_shutdown in tests.
 */
void sk_editor_properties_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_properties_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
