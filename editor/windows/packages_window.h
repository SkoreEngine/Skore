#pragma once

/**
 * @file packages_window.h
 * @brief Packages window (APX-374): v2 migration.
 *
 * Port of main-branch Skore::PackagesWindow (migration manifest §3.5) onto
 * the v2 editor shell: an on-demand floating window (dock position None,
 * never auto-opened) listing the project's package folders with Add /
 * Remove. The v2 project model has no `packages:` list yet (the C++ list
 * lives in the .skore project file via Editor::SaveProjectFile), so the
 * package list is **MOCK session data**: the ops table seeds / adds /
 * removes paths so the surface is fully exercisable, and the Add button
 * records a pending flag (no OS folder picker in v2) instead of opening
 * `Platform::PickFolder`.
 *
 * Public entry points (Open / AddProjectPackage / RemoveProjectPackage /
 * listing queries) follow the APX-365 pattern: never exported as free
 * symbols. The window publishes one process-lifetime
 * `sk_editor_packages_ops_t` table registered with `app_api->add_impl`
 * under SK_EDITOR_PACKAGES_OPS_TYPE_ID; callers look it up via
 * `sk_editor_packages_ops(ctx, api)` and call through the pointers.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_PACKAGES_OPS_TYPE_ID SK_TYPE_ID("sk.editor.packages.ops", 0x5d6e7f8091a2b3c4ULL, 0x0f1e2d3c4b5a6978ULL)

/** Max tracked package folders. */
#define SK_EDITOR_PACKAGES_MAX 64u
/** Max characters of one package path. */
#define SK_EDITOR_PACKAGES_PATH_CAP 512u

/**
 * Packages window public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair.
 */
typedef struct sk_editor_packages_ops_t {
	/** Open (or focus) the Packages window (C++ PackagesWindow::Open). */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Add a package folder (C++ Editor::AddProjectPackage). */
	void (*add_package)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path);

	/** Remove a package folder (C++ Editor::RemoveProjectPackage). */
	void (*remove_package)(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t path);

	/** Clear the list (mock project reset). */
	void (*clear)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Number of tracked package folders. */
	u32 (*get_package_count)(const sk_editor_window_t* window);

	/** Package folder path at @p index. Borrowed. */
	const_chr_t (*package_path_at)(const sk_editor_window_t* window, u32 index);

	/** Package folder name (path basename) at @p index. Borrowed. */
	const_chr_t (*package_name_at)(const sk_editor_window_t* window, u32 index);

	/** Non-zero when the Add Package button was clicked (MOCK dialog). */
	i32 (*is_add_pending)(const sk_editor_window_t* window);

	/** Consume the Add Package pending flag (returns the previous value). */
	i32 (*consume_add_pending)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);
} sk_editor_packages_ops_t;

/** First registered Packages ops table, or NULL before register. */
SK_FINLINE const sk_editor_packages_ops_t* sk_editor_packages_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_packages_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_PACKAGES_OPS_TYPE_ID);
}

/**
 * Register the Packages window impl + ops table on @p app_context. Idempotent
 * per context. Call before sk_editor_windows_register_impls so the real
 * impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_packages_shutdown in tests.
 */
void sk_editor_packages_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_packages_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
