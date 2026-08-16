#pragma once

/**
 * @file resource_debugger_window.h
 * @brief Resource Debugger window (APX-374): v2 migration.
 *
 * Port of main-branch Skore::ResourceDebuggerWindow (migration manifest
 * §3.8) onto the v2 editor shell: an on-demand Center window (no workspace
 * mask — never auto-opened) with two tabs — Types (searchable type list +
 * type info / fields / instances) and Instance (resource metadata + generic
 * field values with navigable RID links) — plus back-history navigation.
 *
 * The v2 repository API has no public type/instance enumeration (the C++
 * `Resources::GetTypes()` / `GetResourcesByType` tables are repository
 * internals), so the Types tab is approximated: it lists the registered
 * builtin payload type ids (resource_asset_builtins.h + the builtin
 * component types) that `find_type` resolves, plus every distinct type of a
 * live resource found by walking the dense RID range 1..resource_count
 * (RIDs are sequential per repository). Instance lists per type come from
 * the same walk. Without an attached repository the window shows a clearly
 * marked empty state (`set_repository` attaches one; the Project Browser /
 * Entity Tree "Show Resource Inspector" callers pass their scene/project
 * repository once they are wired to the debugger).
 *
 * Public entry points (Open / InspectResource / NavigateToInstance / back /
 * type+instance queries / generic field reads) follow the APX-365 pattern:
 * never exported as free symbols. The window publishes one process-lifetime
 * `sk_editor_resource_debugger_ops_t` table registered with
 * `app_api->add_impl` under SK_EDITOR_RESOURCE_DEBUGGER_OPS_TYPE_ID; callers
 * look it up via `sk_editor_resource_debugger_ops(ctx, api)` and call
 * through the pointers.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "repository.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_RESOURCE_DEBUGGER_OPS_TYPE_ID SK_TYPE_ID("sk.editor.resource_debugger.ops", 0x4f6a8b0c2d4e6f8aULL, 0x1b3d5f7a9c0e2f4bULL)

/** Max resource types listed in the Types tab. */
#define SK_EDITOR_RD_MAX_TYPES 64u
/** Max instances listed per type. */
#define SK_EDITOR_RD_MAX_INSTANCES 256u
/** Max instance-history depth (C++ Array<RID> instanceHistory). */
#define SK_EDITOR_RD_MAX_HISTORY 64u
/** Max characters of one type/field label or formatted value. */
#define SK_EDITOR_RD_LABEL_CAP 160u

/** Tabs (C++ "Types" / "Instance"). */
#define SK_EDITOR_RD_TAB_TYPES 0
#define SK_EDITOR_RD_TAB_INSTANCE 1

/** One formatted field value + optional navigable RID. */
typedef struct sk_editor_rd_field_value_t {
	char text[SK_EDITOR_RD_LABEL_CAP];
	sk_rid_t link; /* non-zero when the field renders as a navigable RID link */
} sk_editor_rd_field_value_t;

/**
 * Resource Debugger public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance for per-instance queries).
 */
typedef struct sk_editor_resource_debugger_ops_t {
	/** Open (or focus) the Resource Debugger window (C++ Open). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Open + inspect @p rid (C++ InspectResource(RID) — ProjectBrowser/EntityTree entry). */
	sk_editor_window_t* (*inspect_resource)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_rid_t rid);

	/** Navigate to @p rid (C++ NavigateToInstance; pushes history). */
	void (*navigate_to_instance)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_rid_t rid);

	/** Step back through the instance history (C++ Back button). */
	void (*back)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Non-zero when the Back button should be enabled. */
	i32 (*can_go_back)(const sk_editor_window_t* window);

	/** Attach the repository the window introspects (NULL detaches). */
	void (*set_repository)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, sk_repository_t* repository);

	/** Currently attached repository, or NULL. */
	sk_repository_t* (*get_repository)(const sk_editor_window_t* window);

	/** Rebuild the type/instance tables from the attached repository. */
	void (*refresh)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Active tab (SK_EDITOR_RD_TAB_TYPES / _INSTANCE). */
	i32 (*get_tab)(const sk_editor_window_t* window);

	/** Switch the active tab (C++ switchToTypesTab/switchToInstanceTab). */
	void (*set_tab)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 tab);

	/** Currently selected instance RID. */
	sk_rid_t (*get_selected_instance)(const sk_editor_window_t* window);

	/** Currently selected type index in the Types tab, or U32_MAX. */
	u32 (*get_selected_type_index)(const sk_editor_window_t* window);

	/** Select type @p index in the Types tab. */
	void (*set_selected_type_index)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, u32 index);

	/** Number of types in the Types tab. */
	u32 (*get_type_count)(const sk_editor_window_t* window);

	/** Type display name at @p index. Borrowed. */
	const_chr_t (*type_name_at)(const sk_editor_window_t* window, u32 index);

	/** Number of field descriptors on type @p index. */
	u32 (*type_field_count)(const sk_editor_window_t* window, u32 index);

	/** Field descriptor of type @p index at field position @p field. NULL out of range. */
	const sk_resource_field_t* (*type_field_at)(const sk_editor_window_t* window, u32 index, u32 field);

	/** Number of live instances of type @p index. */
	u32 (*type_instance_count)(const sk_editor_window_t* window, u32 index);

	/** RID of instance @p inst of type @p index. */
	sk_rid_t (*type_instance_at)(const sk_editor_window_t* window, u32 index, u32 inst);

	/** Field count of the selected instance (0 when none / no repo). */
	u32 (*instance_field_count)(const sk_editor_window_t* window);

	/** Field descriptor of the selected instance at position @p field. */
	const sk_resource_field_t* (*instance_field_at)(const sk_editor_window_t* window, u32 field);

	/** Format the value of selected-instance field @p field into @p out (with link RID when navigable). */
	i32 (*instance_field_value)(const sk_editor_window_t* window, u32 field, sk_editor_rd_field_value_t* out);

	/** Instance metadata: type name / uuid / path / version / parent / prototype. */
	void (*instance_info)(const sk_editor_window_t* window, char* type_name, u32 type_name_cap, char* uuid, u32 uuid_cap, char* path, u32 path_cap, u64* version, sk_rid_t* parent,
						  sk_rid_t* prototype);

	/** Current search filter of the Types tab (C++ searchFilter). Borrowed. */
	const_chr_t (*get_search)(const sk_editor_window_t* window);

	/** Set the Types tab search filter. */
	void (*set_search)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const_chr_t search);
} sk_editor_resource_debugger_ops_t;

/** First registered Resource Debugger ops table, or NULL before register. */
SK_FINLINE const sk_editor_resource_debugger_ops_t* sk_editor_resource_debugger_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_resource_debugger_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_RESOURCE_DEBUGGER_OPS_TYPE_ID);
}

/**
 * Register the Resource Debugger window impl + ops table on @p app_context.
 * Idempotent per context. Call before sk_editor_windows_register_impls so
 * the real impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_resource_debugger_shutdown in tests.
 */
void sk_editor_resource_debugger_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_resource_debugger_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
