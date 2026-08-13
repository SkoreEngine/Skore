#pragma once

/**
 * @file resource_assets.h
 * @brief Asset handler/importer tables, ingest/cook contexts, and the
 *        repository-assets engine (scan, import, registry).
 *
 * Port of main-branch ResourceAssetHandler / ResourceAssetImporter as plain
 * structs (data + function pointers), plus the ResourceAssets manager rewritten
 * against sk_repository_t and sk_app_api_t::add_impl multi-impl registration.
 *
 * ## Canonical handler registration pattern
 *
 * Define one long-lived (usually file-static) table, then register it on the
 * app multi-impl list. The engine discovers handlers via get_all_impls and
 * indexes them by extension / resource type:
 *
 * @code
 * static const_chr_t my_extension(void_ptr_t user_data) {
 *     (void)user_data;
 *     return ".mesh";
 * }
 * // ... other table callbacks ...
 *
 * static sk_resource_asset_handler_t my_handler = {
 *     .user_data = NULL,
 *     .extension = my_extension,
 *     .open_asset = NULL,
 *     .get_resource_type_id = my_get_resource_type_id,
 *     .get_desc = my_get_desc,
 *     .load = my_load,
 *     .save = my_save,
 *     .create = my_create,
 *     .reloaded = NULL,
 *     .after_move = NULL,
 *     .export_object = NULL,
 *     .get_icon = my_get_icon,
 *     .get_load_order = NULL,
 *     .get_asset_name = NULL,
 * };
 *
 * app_api->add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &my_handler);
 * // Importers use SK_RESOURCE_ASSET_IMPORTER_TYPE_ID the same way.
 * @endcode
 *
 * Built-ins call this for every table in
 * sk_resource_asset_builtins_register_impls(). Hosts resolve with
 * sk_resource_asset_handler_find_by_extension / _find_by_resource_type or
 * app_api->resource_assets_api(ctx)->get_asset_handler_for_extension, then invoke
 * callbacks only through the null-safe free functions (sk_resource_asset_handler_*).
 *
 * Thumbnail / PreviewGenerator generation is intentionally not part of this
 * system (no handler field, no cache, no dispatch). See
 * docs/repository-assets-thumbnail-drop.md and inventory §3.
 *
 * The engine (sk_resource_assets_api_t) discovers those tables, indexes them by
 * extension / resource type, scans package Assets/ trees into ResourceAsset*
 * resources, and runs import (legacy import_asset or generic ingest/cook).
 *
 * Function pointers follow the multi-instance core pattern (sk_allocator_t):
 * the first parameter is the table's opaque user_data.
 *
 * Core has no dependency on editor-only modules.
 */

#include "app.h"
#include "common.h"
#include "repository.h"
#include "serialization.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for app-context registration of `sk_resource_assets_api_t`. */
#define SK_RESOURCE_ASSETS_API_TYPE_ID SK_TYPE_ID("sk.resource_assets_api", 0xcceb767bc7a323b7ULL, 0x2d273f5d983e8243ULL)

/**
 * Type id for sk_resource_asset_handler_t implementations in the app multi-impl
 * registry (add_impl / remove_impl / impl_count / get_all_impls).
 */
#define SK_RESOURCE_ASSET_HANDLER_TYPE_ID SK_TYPE_ID("sk.resource_asset_handler", 0x3bf713f497383666ULL, 0x74e3f6b8c318e3e9ULL)

/**
 * Type id for sk_resource_asset_importer_t implementations in the app multi-impl
 * registry. Separate from handlers so hosts can enumerate each list independently.
 */
#define SK_RESOURCE_ASSET_IMPORTER_TYPE_ID SK_TYPE_ID("sk.resource_asset_importer", 0xa0cc8513be01820dULL, 0x190f2cb78426a105ULL)

/* ------------------------------------------------------------------ */
/*  Ingest / cook contexts                                            */
/* ------------------------------------------------------------------ */

/** One sub-resource declared by an importer's Ingest pass. */
typedef struct sk_resource_asset_sub_resource_decl_t {
	const_chr_t sub_id;
	sk_type_id_t type;
} sk_resource_asset_sub_resource_decl_t;

/** One file dependency recorded by an importer's Ingest pass. */
typedef struct sk_resource_asset_dependency_decl_t {
	const_chr_t rel_path;
	const u8* bytes;
	u32 size;
} sk_resource_asset_dependency_decl_t;

/**
 * Allocates resources inside an imported asset during Cook.
 * Sub-resources are addressed by a stable sub_id declared during Ingest.
 */
typedef struct sk_sub_resource_allocator_t {
	sk_rid_t imported_asset;
	sk_undo_redo_scope_t* scope;
	void_ptr_t engine;
	sk_rid_t (*create)(const struct sk_sub_resource_allocator_t* allocator, const_chr_t sub_id, sk_type_id_t type);
} sk_sub_resource_allocator_t;

/**
 * Context handed to an importer's Ingest pass (runtime-filled).
 * Zero-initialized contexts have no helper entry points and must not be used.
 */
typedef struct sk_resource_ingest_context_t {
	sk_rid_t imported_asset;
	const_chr_t source_path;
	const u8* source_bytes;
	u32 source_size;
	sk_undo_redo_scope_t* scope;
	sk_repository_t* repository;
	const sk_repository_api_t* repo_api;
	sk_uuid_t (*declare_sub_resource)(struct sk_resource_ingest_context_t* ctx, const_chr_t sub_id, sk_type_id_t type);
	i32 (*add_dependency)(struct sk_resource_ingest_context_t* ctx, const_chr_t rel_path, const u8* bytes, u32 size);
	i32 (*has_dependency)(struct sk_resource_ingest_context_t* ctx, const_chr_t rel_path);
} sk_resource_ingest_context_t;

/**
 * Context handed to an importer's Cook pass (runtime-filled).
 * Buffer creation / dependency file I/O deferred until those layers land.
 */
typedef struct sk_resource_cook_context_t {
	sk_rid_t imported_asset;
	sk_rid_t import_settings;
	const u8* source_bytes;
	u32 source_size;
	sk_undo_redo_scope_t* scope;
	sk_repository_t* repository;
	const sk_repository_api_t* repo_api;
	sk_rid_t (*sub_resource)(struct sk_resource_cook_context_t* ctx, const_chr_t sub_id, sk_type_id_t type);
	sk_sub_resource_allocator_t (*allocator)(struct sk_resource_cook_context_t* ctx);
} sk_resource_cook_context_t;

/* ------------------------------------------------------------------ */
/*  Handler                                                            */
/* ------------------------------------------------------------------ */

/**
 * Asset handler table: one static (or long-lived) instance per resource format.
 * Register with app_api->add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &handler).
 */
typedef struct sk_resource_asset_handler_t {
	void_ptr_t user_data;
	const_chr_t (*extension)(void_ptr_t user_data);
	void (*open_asset)(void_ptr_t user_data, sk_rid_t asset);
	sk_type_id_t (*get_resource_type_id)(void_ptr_t user_data);
	const_chr_t (*get_desc)(void_ptr_t user_data);
	sk_rid_t (*load)(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t absolute_path);
	void (*save)(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t object, const_chr_t absolute_path);
	sk_rid_t (*create)(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid, sk_undo_redo_scope_t* scope);
	void (*reloaded)(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t absolute_path);
	void (*after_move)(void_ptr_t user_data, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset, const_chr_t old_absolute_path,
					   const_chr_t new_absolute_path);
	void (*export_object)(void_ptr_t user_data, sk_rid_t object, sk_archive_writer_t* writer);
	const_chr_t (*get_icon)(void_ptr_t user_data);
	i32 (*get_load_order)(void_ptr_t user_data);
	i32 (*get_asset_name)(void_ptr_t user_data, sk_rid_t rid, char* out_name, u32 out_cap);
} sk_resource_asset_handler_t;

/* ------------------------------------------------------------------ */
/*  Handler registry: enumerate / resolve over add_impl               */
/* ------------------------------------------------------------------ */

u32 sk_resource_asset_handler_count(sk_app_context_t* context, const sk_app_api_t* app_api);
u32 sk_resource_asset_handler_get_all(sk_app_context_t* context, const sk_app_api_t* app_api, const sk_resource_asset_handler_t** out, u32 out_cap);
const sk_resource_asset_handler_t* sk_resource_asset_handler_find_by_extension(sk_app_context_t* context, const sk_app_api_t* app_api, const_chr_t extension);
const sk_resource_asset_handler_t* sk_resource_asset_handler_find_by_resource_type(sk_app_context_t* context, const sk_app_api_t* app_api, sk_type_id_t type_id);

/* Null-safe handler dispatch (NULL fp → documented default / no-op). */
const_chr_t sk_resource_asset_handler_extension(const sk_resource_asset_handler_t* handler);
void sk_resource_asset_handler_open_asset(const sk_resource_asset_handler_t* handler, sk_rid_t asset);
sk_type_id_t sk_resource_asset_handler_get_resource_type_id(const sk_resource_asset_handler_t* handler);
const_chr_t sk_resource_asset_handler_get_desc(const sk_resource_asset_handler_t* handler);
sk_rid_t sk_resource_asset_handler_load(const sk_resource_asset_handler_t* handler, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset,
										const_chr_t absolute_path);
void sk_resource_asset_handler_save(const sk_resource_asset_handler_t* handler, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t object,
									const_chr_t absolute_path);
sk_rid_t sk_resource_asset_handler_create(const sk_resource_asset_handler_t* handler, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_uuid_t uuid,
										  sk_undo_redo_scope_t* scope);
void sk_resource_asset_handler_reloaded(const sk_resource_asset_handler_t* handler, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset,
										const_chr_t absolute_path);
void sk_resource_asset_handler_after_move(const sk_resource_asset_handler_t* handler, sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t asset,
										  const_chr_t old_absolute_path, const_chr_t new_absolute_path);
void sk_resource_asset_handler_export_object(const sk_resource_asset_handler_t* handler, sk_rid_t object, sk_archive_writer_t* writer);
const_chr_t sk_resource_asset_handler_get_icon(const sk_resource_asset_handler_t* handler);
i32 sk_resource_asset_handler_get_load_order(const sk_resource_asset_handler_t* handler);
i32 sk_resource_asset_handler_get_asset_name(const sk_resource_asset_handler_t* handler, sk_rid_t rid, char* out_name, u32 out_cap);

/* ------------------------------------------------------------------ */
/*  Importer                                                           */
/* ------------------------------------------------------------------ */

typedef struct sk_resource_asset_importer_t {
	void_ptr_t user_data;
	u32 (*imported_extensions)(void_ptr_t user_data, const_chr_t* out, u32 out_cap);
	const_chr_t (*output_extension)(void_ptr_t user_data);
	u32 (*cooker_version)(void_ptr_t user_data);
	sk_type_id_t (*get_settings_type)(void_ptr_t user_data);
	void (*ingest)(void_ptr_t user_data, sk_resource_ingest_context_t* ctx);
	void (*cook)(void_ptr_t user_data, sk_resource_cook_context_t* ctx);
	i32 (*import_asset)(void_ptr_t user_data, sk_rid_t directory, const_ptr_t settings, const_chr_t path, sk_undo_redo_scope_t* scope);
} sk_resource_asset_importer_t;

/* ------------------------------------------------------------------ */
/*  Repository-assets engine                                          */
/* ------------------------------------------------------------------ */

/**
 * Opaque engine state: handler/importer maps, by-type asset index, scanned
 * package. Bound to one sk_repository_t and one app context.
 */
typedef struct sk_resource_assets_context_t sk_resource_assets_context_t;

/**
 * Repository-assets engine API (implemented in sk-foundation).
 * Obtain via `app_api->resource_assets_api(ctx)`. Parent parameters are
 * ResourceAssetDirectory node RIDs. Extensions are lowercase with a leading
 * dot. No thumbnails, no efsw file watching.
 */
typedef struct sk_resource_assets_api_t {
	sk_resource_assets_context_t* (*create)(sk_repository_t* repository, sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_allocator_t* allocator);
	void (*destroy)(sk_resource_assets_context_t* ctx);
	void (*reload_handlers)(sk_resource_assets_context_t* ctx);
	/**
	 * Scan package_path/Assets into an in-memory ResourceAsset graph.
	 * Hidden entries and .buffer / .buffers / .info files are skipped.
	 * File contents are not loaded (handler->load deferred until serialization).
	 */
	sk_rid_t (*scan_package_from_directory)(sk_resource_assets_context_t* ctx, const_chr_t package_name, const_chr_t package_path);
	sk_rid_t (*get_package)(const sk_resource_assets_context_t* ctx);
	sk_rid_t (*get_root_directory)(const sk_resource_assets_context_t* ctx);
	sk_rid_t (*create_asset)(sk_resource_assets_context_t* ctx, sk_rid_t parent, sk_type_id_t type_id, const_chr_t desired_name, sk_undo_redo_scope_t* scope);
	sk_rid_t (*create_asset_directory)(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, sk_undo_redo_scope_t* scope);
	sk_rid_t (*create_asset_file)(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, const_chr_t extension, sk_undo_redo_scope_t* scope);
	void (*move_asset)(sk_resource_assets_context_t* ctx, sk_rid_t new_parent, sk_rid_t rid, sk_undo_redo_scope_t* scope);
	const_chr_t (*get_absolute_path)(sk_resource_assets_context_t* ctx, sk_rid_t asset);
	const_chr_t (*get_path_id)(sk_resource_assets_context_t* ctx, sk_rid_t asset);
	i32 (*get_absolute_path_from_path_id)(sk_resource_assets_context_t* ctx, const_chr_t path_id, char_ptr_t out, u32 out_cap);
	sk_rid_t (*get_parent_asset)(sk_resource_assets_context_t* ctx, sk_rid_t rid);
	i32 (*is_child_of)(sk_resource_assets_context_t* ctx, sk_rid_t parent, sk_rid_t child);
	sk_rid_t (*get_asset_payload)(sk_resource_assets_context_t* ctx, sk_rid_t rid);
	const sk_resource_asset_handler_t* (*get_asset_handler)(sk_resource_assets_context_t* ctx, sk_rid_t rid);
	const sk_resource_asset_handler_t* (*get_asset_handler_for_type)(sk_resource_assets_context_t* ctx, sk_type_id_t type_id);
	const sk_resource_asset_handler_t* (*get_asset_handler_for_extension)(sk_resource_assets_context_t* ctx, const_chr_t extension);
	const sk_resource_asset_importer_t* (*get_importer)(sk_resource_assets_context_t* ctx, const_chr_t extension);
	/**
	 * Import a file (or every file under a directory) into parent.
	 * Direct import_asset when non-NULL; else generic ingest/cook wrapper path.
	 * Reimport: call again on the same source path (updates via unique naming /
	 * re-ingest on the generic path when content hash changes).
	 */
	i32 (*import_asset)(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t path, sk_undo_redo_scope_t* scope);
	void (*open_asset)(sk_resource_assets_context_t* ctx, sk_rid_t rid);
	void (*register_asset_by_type)(sk_resource_assets_context_t* ctx, sk_rid_t asset);
	void (*unregister_asset_by_type)(sk_resource_assets_context_t* ctx, sk_rid_t asset);
	u32 (*get_assets)(sk_resource_assets_context_t* ctx, sk_type_id_t type_id, sk_rid_t* out, u32 out_cap);
	i32 (*create_unique_asset_name)(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, const_chr_t extension, i32 directory, char_ptr_t out,
									u32 out_cap);
	i32 (*get_asset_name)(sk_resource_assets_context_t* ctx, sk_rid_t rid, char_ptr_t out, u32 out_cap);
	i32 (*get_asset_full_name)(sk_resource_assets_context_t* ctx, sk_rid_t rid, char_ptr_t out, u32 out_cap);
	sk_rid_t (*find_asset_on_directory)(sk_resource_assets_context_t* ctx, sk_rid_t directory, sk_type_id_t type_id, const_chr_t name);
} sk_resource_assets_api_t;

#ifdef __cplusplus
}
#endif
