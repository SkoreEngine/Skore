#pragma once

/**
 * @file project.h
 * @brief Editor project host: thin consumer of core repository-assets.
 *
 * All asset handler/importer registration, package scan, and import run through
 * sk_resource_assets_api_t and sk_resource_asset_builtins_* — there is no
 * editor-side ResourceAssetHandler / ResourceAssetImporter hierarchy and no
 * thumbnail / PreviewGenerator plumbing (intentionally dropped; see
 * docs/repository-assets-thumbnail-drop.md).
 *
 * Host-facing surface (APX-328): the functions below are the internal wiring
 * behind sk_editor_api_t. Hosts and tests resolve the editor only via
 * app_api->get_api(ctx, SK_EDITOR_API_TYPE_ID) (see editor_api.h).
 */

#include "app.h"
#include "common.h"
#include "repository.h"
#include "resource_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open editor project bound to one package on disk.
 * Owns repository + resource-assets engine for the process lifetime of the open.
 */
typedef struct sk_editor_project_t sk_editor_project_t;

/**
 * Create a project, register core asset types + built-in handlers/importers via
 * add_impl, and scan package_path/Assets into the core engine.
 *
 * @param app_context Host context from sk_app_init / sk_app_create (not owned).
 * @param app_api     Process app API table (must not be NULL).
 * @param package_name Short package id used in path_id prefixes (e.g. "Game").
 * @param package_path Absolute or relative path to the package root (parent of Assets/).
 * @return Open project, or NULL on failure.
 */
sk_editor_project_t* sk_editor_project_open(sk_app_context_t* app_context, const sk_app_api_t* app_api, const_chr_t package_name, const_chr_t package_path);

/**
 * Close project: destroy resource-assets engine and repository.
 * Does not destroy the app context.
 */
void sk_editor_project_close(sk_editor_project_t* project);

/** Core engine for this open project (valid until close). */
sk_resource_assets_context_t* sk_editor_project_assets(const sk_editor_project_t* project);

/** Repository used by the open project (valid until close). */
sk_repository_t* sk_editor_project_repository(const sk_editor_project_t* project);

/** Package root directory node (ResourceAssetDirectory) after a successful open. */
sk_rid_t sk_editor_project_root_directory(const sk_editor_project_t* project);

/**
 * Import a source file (or every file under a directory) into the package root
 * via core import_asset. Reimport uses the same path.
 * @return 0 on success, non-zero on failure.
 */
i32 sk_editor_project_import(sk_editor_project_t* project, const_chr_t path);

/**
 * Import a source file (or directory) under a specific parent directory node.
 * @return 0 on success, non-zero on failure.
 */
i32 sk_editor_project_import_into(sk_editor_project_t* project, sk_rid_t parent, const_chr_t path);

/**
 * Open an asset by RID through the core engine (handler open_asset; no-op for
 * most builtins until editor UI actions land).
 */
void sk_editor_project_open_asset(sk_editor_project_t* project, sk_rid_t rid);

#ifdef __cplusplus
}
#endif
