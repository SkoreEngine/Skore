#pragma once

/**
 * @file resource_assets.h
 * @brief Public C contract for repository asset handlers and importers.
 *
 * Port of main-branch ResourceAssetHandler / ResourceAssetImporter as plain
 * structs: data fields plus one function pointer per former virtual method.
 * No runtime behavior lives here — registration, dispatch, and concrete
 * handlers land in later tasks.
 *
 * Concrete handlers/importers are static table instances registered on the
 * app context with multi-impl registration (not set_api):
 *
 *   static sk_resource_asset_handler_t dcc_asset_handler = { ... };
 *   app_api->add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &dcc_asset_handler);
 *
 * Hosts enumerate with impl_count / get_all_impls and index by extension or
 * resource type. Importers use a separate type id.
 *
 * Function pointers follow the multi-instance core pattern (sk_allocator_t,
 * sk_log_sink_t, sk_archive_writer_t): the first parameter is the table's
 * opaque user_data / self context. Thumbnail / PreviewGenerator surface is
 * intentionally omitted (out of scope for the repository_assets goal).
 *
 * Standalone and includable from core and editor (and plugins).
 */

#include "common.h"
#include "repository.h"
#include "serialization.h"

#ifdef __cplusplus
extern "C" {
#endif

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
/*  Importer contexts (opaque until the import pipeline lands)        */
/* ------------------------------------------------------------------ */

/**
 * Opaque ingest context passed to sk_resource_asset_importer_t::ingest.
 * Declares sub-resources and external dependencies during import ingest.
 * Full layout and helpers are defined with the pipeline implementation.
 */
typedef struct sk_resource_ingest_context_t sk_resource_ingest_context_t;

/**
 * Opaque cook context passed to sk_resource_asset_importer_t::cook.
 * Produces cooked sub-resources from source bytes / ingest declarations.
 * Full layout and helpers are defined with the pipeline implementation.
 */
typedef struct sk_resource_cook_context_t sk_resource_cook_context_t;

/* ------------------------------------------------------------------ */
/*  Handler                                                            */
/* ------------------------------------------------------------------ */

/**
 * Asset handler table: one static (or long-lived) instance per resource format.
 *
 * Replaces C++ ResourceAssetHandler. Pure virtuals and optional virtuals from
 * main become function pointers; optional entries may be NULL until defaults
 * are provided by host dispatch. user_data is the first argument to every
 * function pointer (same convention as sk_allocator_t::instance).
 *
 * Register with:
 *   app_api->add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &handler);
 */
typedef struct sk_resource_asset_handler_t {
	/** Opaque per-handler state; passed as the first argument to every fp. */
	void_ptr_t user_data;

	/**
	 * Disk extension key for this handler (e.g. ".mesh"), including the dot.
	 * @param user_data Handler user_data.
	 * @return Non-NULL static extension string (may be empty for wrappers).
	 */
	const_chr_t (*extension)(void_ptr_t user_data);

	/**
	 * Editor open action for an existing asset RID.
	 * @param user_data Handler user_data.
	 * @param asset     Asset RID to open.
	 */
	void (*open_asset)(void_ptr_t user_data, sk_rid_t asset);

	/**
	 * Runtime / repository resource type id this handler owns
	 * (e.g. MeshResource type id).
	 * @param user_data Handler user_data.
	 * @return Type id; must not be SK_TYPE_ID_ZERO for concrete handlers.
	 */
	sk_type_id_t (*get_resource_type_id)(void_ptr_t user_data);

	/**
	 * Human-readable description (e.g. "Mesh").
	 * @param user_data Handler user_data.
	 * @return Non-NULL static description string.
	 */
	const_chr_t (*get_desc)(void_ptr_t user_data);

	/**
	 * Load object data for @p asset from @p absolute_path on disk.
	 * @param user_data     Handler user_data.
	 * @param asset         Asset RID being loaded.
	 * @param absolute_path Absolute filesystem path (UTF-8, must not be NULL).
	 * @return Loaded object RID, or SK_RID_ZERO on failure.
	 */
	sk_rid_t (*load)(void_ptr_t user_data, sk_rid_t asset, const_chr_t absolute_path);

	/**
	 * Persist @p object to @p absolute_path on disk.
	 * @param user_data     Handler user_data.
	 * @param object        Object RID to save.
	 * @param absolute_path Absolute filesystem path (UTF-8, must not be NULL).
	 */
	void (*save)(void_ptr_t user_data, sk_rid_t object, const_chr_t absolute_path);

	/**
	 * Create a new asset object of this handler's resource type.
	 * @param user_data Handler user_data.
	 * @param uuid      UUID for the new resource (may be zero to let the store assign).
	 * @param scope     Optional undo/redo scope; NULL if unscoped.
	 * @return New object RID, or SK_RID_ZERO on failure.
	 */
	sk_rid_t (*create)(void_ptr_t user_data, sk_uuid_t uuid, sk_undo_redo_scope_t* scope);

	/**
	 * Hot-reload notification after the source file at @p absolute_path changed.
	 * @param user_data     Handler user_data.
	 * @param asset         Asset RID that reloaded.
	 * @param absolute_path Absolute path of the changed file (UTF-8).
	 */
	void (*reloaded)(void_ptr_t user_data, sk_rid_t asset, const_chr_t absolute_path);

	/**
	 * Path rename / move side effects after an asset file moves on disk.
	 * @param user_data         Handler user_data.
	 * @param asset             Asset RID that moved.
	 * @param old_absolute_path Previous absolute path (UTF-8).
	 * @param new_absolute_path New absolute path (UTF-8).
	 */
	void (*after_move)(void_ptr_t user_data, sk_rid_t asset, const_chr_t old_absolute_path, const_chr_t new_absolute_path);

	/**
	 * Package export: write @p object into @p writer.
	 * Named export_object (not "export") to stay valid as a C identifier.
	 * @param user_data Handler user_data.
	 * @param object    Object RID to export.
	 * @param writer    Archive writer table (must not be NULL; pass writer->instance
	 *                  through the writer's own function pointers).
	 */
	void (*export_object)(void_ptr_t user_data, sk_rid_t object, sk_archive_writer_t* writer);

	/**
	 * Icon glyph / FontAwesome (or similar) codepoint string for UI.
	 * @param user_data Handler user_data.
	 * @return Non-NULL static icon string; empty string when none.
	 */
	const_chr_t (*get_icon)(void_ptr_t user_data);

	/**
	 * Package scan / export sort key. Lower values load earlier.
	 * Main default was INT32_MAX.
	 * @param user_data Handler user_data.
	 * @return Load order rank.
	 */
	i32 (*get_load_order)(void_ptr_t user_data);

	/**
	 * Optional custom display name for @p rid.
	 * Writes a NUL-terminated name into @p out_name when a custom name is used.
	 * @param user_data Handler user_data.
	 * @param rid       Asset or object RID.
	 * @param out_name  Caller buffer for the name (may be NULL when only probing).
	 * @param out_cap   Capacity of @p out_name in bytes (including NUL).
	 * @return Non-zero if a custom name was produced; 0 to use default naming.
	 */
	i32 (*get_asset_name)(void_ptr_t user_data, sk_rid_t rid, char* out_name, u32 out_cap);
} sk_resource_asset_handler_t;

/* ------------------------------------------------------------------ */
/*  Importer                                                           */
/* ------------------------------------------------------------------ */

/**
 * Asset importer table: one static (or long-lived) instance per source format
 * family (e.g. textures, audio, FBX).
 *
 * Replaces C++ ResourceAssetImporter. Register with:
 *   app_api->add_impl(ctx, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID, &importer);
 *
 * Source extensions are claimed via imported_extensions; a non-empty
 * output_extension routes the host through the ingest/cook path.
 */
typedef struct sk_resource_asset_importer_t {
	/** Opaque per-importer state; passed as the first argument to every fp. */
	void_ptr_t user_data;

	/**
	 * Source extensions this importer claims (e.g. ".png", ".jpg").
	 * Copies up to @p out_cap extension string pointers into @p out and returns
	 * the total count (may exceed @p out_cap). When @p out is NULL or
	 * @p out_cap is 0, only the count is returned.
	 * @param user_data Importer user_data.
	 * @param out       Destination buffer of extension C-string pointers, or NULL.
	 * @param out_cap   Capacity of @p out in elements.
	 * @return Total claimed extension count.
	 */
	u32 (*imported_extensions)(void_ptr_t user_data, const_chr_t** out, u32 out_cap);

	/**
	 * Cooked / wrapper asset extension (e.g. ".texture"). Empty string or NULL
	 * means legacy import_asset path only.
	 * @param user_data Importer user_data.
	 * @return Output extension including the dot, or NULL/empty when none.
	 */
	const_chr_t (*output_extension)(void_ptr_t user_data);

	/**
	 * Cooker version; bumps invalidate the cooked cache.
	 * Main default was 1.
	 * @param user_data Importer user_data.
	 * @return Positive cooker version.
	 */
	u32 (*cooker_version)(void_ptr_t user_data);

	/**
	 * Optional import-settings resource type id.
	 * @param user_data Importer user_data.
	 * @return Settings type id, or SK_TYPE_ID_ZERO when the importer has none.
	 */
	sk_type_id_t (*get_settings_type)(void_ptr_t user_data);

	/**
	 * Ingest phase: declare sub-resources and external dependencies.
	 * @param user_data Importer user_data.
	 * @param ctx       Ingest context (must not be NULL).
	 */
	void (*ingest)(void_ptr_t user_data, sk_resource_ingest_context_t* ctx);

	/**
	 * Cook phase: produce cooked sub-resources from source / ingest data.
	 * @param user_data Importer user_data.
	 * @param ctx       Cook context (must not be NULL).
	 */
	void (*cook)(void_ptr_t user_data, sk_resource_cook_context_t* ctx);

	/**
	 * Legacy direct import (main default returned false). Prefer ingest/cook
	 * when output_extension is non-empty.
	 * @param user_data Importer user_data.
	 * @param directory Parent directory asset RID.
	 * @param settings  Optional import settings object pointer (may be NULL).
	 * @param path      Source filesystem path (UTF-8, must not be NULL).
	 * @param scope     Optional undo/redo scope; NULL if unscoped.
	 * @return Non-zero on success; 0 on failure / not implemented.
	 */
	i32 (*import_asset)(void_ptr_t user_data, sk_rid_t directory, const_ptr_t settings, const_chr_t path, sk_undo_redo_scope_t* scope);
} sk_resource_asset_importer_t;

#ifdef __cplusplus
}
#endif
