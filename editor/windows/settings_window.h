#pragma once

/**
 * @file settings_window.h
 * @brief Settings window (APX-374): v2 migration.
 *
 * Port of main-branch Skore::SettingsWindow (migration manifest §3.10) onto
 * the v2 editor shell: an on-demand floating window (dock position None,
 * never auto-opened) whose title is the opened settings group's display name
 * ("Editor Settings" / "Project Settings") and whose body is a collapsing
 * tree of setting groups (left) + the selected group's entries (right).
 *
 * The v2 engine has no `Settings::Get` / `EditableSettings` resource registry
 * yet, so the setting groups/entries are **MOCK session data**: a seeded
 * tree of groups ("General", "Rendering", "Audio", ...) with per-entry mock
 * values (bool / int / float / string) that the ops table reads and edits.
 * The Physics layer-collision matrix is mocked too (8 named layers, session
 * state) until physics settings exist (manifest: "v2: mock the collision
 * matrix until physics settings exist"). Nothing is persisted (C++ window
 * has no EditorSerialize fields either).
 *
 * Public entry points (Open(TypeID group) / entry read-write / selection /
 * search / collision matrix) follow the APX-365 pattern: never exported as
 * free symbols. The window publishes one process-lifetime
 * `sk_editor_settings_ops_t` table registered with `app_api->add_impl`
 * under SK_EDITOR_SETTINGS_OPS_TYPE_ID; callers look it up via
 * `sk_editor_settings_ops(ctx, api)` and call through the pointers. The
 * shell's Edit/Editor Settings and Edit/Project Settings menus route
 * through it.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_SETTINGS_OPS_TYPE_ID SK_TYPE_ID("sk.editor.settings.ops", 0x2a4c6e8f01b3d5f7ULL, 0x9c8e7a6b5c4d3e2fULL)

/** Settings group ids (C++ TypeInfo<EditorSettings/ProjectSettings>::ID()). */
#define SK_EDITOR_SETTINGS_GROUP_EDITOR 0u
#define SK_EDITOR_SETTINGS_GROUP_PROJECT 1u

/** Max setting groups in the mock tree. */
#define SK_EDITOR_SETTINGS_MAX_GROUPS 32u
/** Max entries per group. */
#define SK_EDITOR_SETTINGS_MAX_ENTRIES 8u
/** Max characters of a group/entry label or string value. */
#define SK_EDITOR_SETTINGS_LABEL_CAP 96u
/** Mock layer count for the collision matrix (C++ MaxLayers). */
#define SK_EDITOR_SETTINGS_LAYER_COUNT 8u

/** Entry value kinds (mock; the C++ per-entry ImGuiDrawResource editors). */
typedef enum sk_editor_settings_entry_kind_t {
	SK_EDITOR_SETTINGS_ENTRY_BOOL = 0,
	SK_EDITOR_SETTINGS_ENTRY_INT = 1,
	SK_EDITOR_SETTINGS_ENTRY_FLOAT = 2,
	SK_EDITOR_SETTINGS_ENTRY_STRING = 3,
	SK_EDITOR_SETTINGS_ENTRY_COLLISION_MATRIX = 4, /* PhysicsSettings (mocked) */
} sk_editor_settings_entry_kind_t;

/**
 * Settings window public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance for per-instance queries).
 */
typedef struct sk_editor_settings_ops_t {
	/** Open (or focus) the Settings window for @p group (C++ Open(TypeID)). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api, u32 group);

	/** Open Editor Settings (C++ Edit/Editor Settings menu action). */
	sk_editor_window_t* (*open_editor_settings)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Open Project Settings (C++ Edit/Project Settings menu action). */
	sk_editor_window_t* (*open_project_settings)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Currently opened group (SK_EDITOR_SETTINGS_GROUP_EDITOR/PROJECT). */
	u32 (*get_group)(const sk_editor_window_t* window);

	/** Window title = FormatName of the group ("Editor Settings"/"Project Settings"). */
	const_chr_t (*get_title)(const sk_editor_window_t* window);

	/** Number of setting groups in the tree. */
	u32 (*get_group_count)(const sk_editor_window_t* window);

	/** Group display label at @p index. Borrowed. */
	const_chr_t (*group_label_at)(const sk_editor_window_t* window, u32 index);

	/** Tree parent of group @p index (0 = root). */
	u32 (*group_parent_at)(const sk_editor_window_t* window, u32 index);

	/** Number of entries of group @p index. */
	u32 (*entry_count)(const sk_editor_window_t* window, u32 group_index);

	/** Entry label at (@p group_index, @p entry_index). Borrowed. */
	const_chr_t (*entry_label_at)(const sk_editor_window_t* window, u32 group_index, u32 entry_index);

	/** Entry value kind at (@p group_index, @p entry_index). */
	sk_editor_settings_entry_kind_t (*entry_kind_at)(const sk_editor_window_t* window, u32 group_index, u32 entry_index);

	/** Read one entry value by kind (out pointers may be NULL). */
	void (*get_entry_value)(const sk_editor_window_t* window, u32 group_index, u32 entry_index, i32* out_bool, i32* out_int, f32* out_float, char* out_string, u32 out_string_cap);

	/** Write one entry value by kind (MOCK: session state, not persisted). */
	void (*set_entry_value)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 group_index, u32 entry_index, i32 set_bool, i32 set_int,
							f32 set_float, const_chr_t set_string);

	/** Selected group index in the tree (C++ selectedItem). */
	u32 (*get_selected_group)(const sk_editor_window_t* window);

	/** Select group @p index in the tree. */
	void (*set_selected_group)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 group_index);

	/** Current tree search filter (C++ searchText). Borrowed. */
	const_chr_t (*get_search)(const sk_editor_window_t* window);

	/** Set the tree search filter. */
	void (*set_search)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t search);

	/** Mock layer name at @p layer (C++ LayerSystem::GetLayerName). */
	const_chr_t (*layer_name)(u32 layer);

	/** Whether layers @p a and @p b collide (C++ Physics::GetLayerCollision; MOCK). */
	i32 (*get_collision)(const sk_editor_window_t* window, u32 layer_a, u32 layer_b);

	/** Set layer collision (C++ Physics::SetLayerCollision; MOCK). */
	void (*set_collision)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 layer_a, u32 layer_b, i32 collide);
} sk_editor_settings_ops_t;

/** First registered Settings ops table, or NULL before register. */
SK_FINLINE const sk_editor_settings_ops_t* sk_editor_settings_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_settings_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_SETTINGS_OPS_TYPE_ID);
}

/**
 * Register the Settings window impl + ops table on @p app_context. Idempotent
 * per context. Call before sk_editor_windows_register_impls so the real
 * impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_settings_shutdown in tests.
 */
void sk_editor_settings_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_settings_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
