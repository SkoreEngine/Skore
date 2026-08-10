/**
 * @file resource_assets.c
 * @brief Asset handler registry/dispatch + repository-assets engine.
 *
 * Handler/importer multi-impl lookup and null-safe free-function dispatch,
 * plus the ResourceAssets engine: package scanning, create/move, import
 * (direct import_asset or generic ingest/cook), and by-type asset index.
 *
 * Thumbnails, efsw file watching, and Serialize/Deserialize load paths are
 * intentionally omitted (see docs/repository-assets-inventory.md §3 for
 * thumbnail call sites on main that are not reimplemented here).
 */

/**
 * @file resource_assets.c
 * @brief Asset handler registry lookup and null-safe dispatch over add_impl.
 */

#include "resource_assets.h"

#include "allocator.h"

#include <string.h>

/* Default load order when get_load_order is NULL (main used INT32_MAX). */
#define SK_RESOURCE_ASSET_HANDLER_DEFAULT_LOAD_ORDER ((i32)0x7fffffff)

/* Stack capacity for handler enumeration before falling back to heap. */
#define SK_RESOURCE_ASSET_HANDLER_STACK_CAP 32u

/* ------------------------------------------------------------------ */
/*  Registry: count / get_all / find                                   */
/* ------------------------------------------------------------------ */

u32 sk_resource_asset_handler_count(sk_app_context_t* context, const sk_app_api_t* app_api) {
	return app_api->impl_count(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID);
}

u32 sk_resource_asset_handler_get_all(sk_app_context_t* context, const sk_app_api_t* app_api, const sk_resource_asset_handler_t** out, u32 out_cap) {
	/* get_all_impls stores opaque const_ptr_t; layout-compatible with handler*. */
	return app_api->get_all_impls(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, (const_ptr_t*)out, out_cap);
}

/**
 * Borrow registered handler pointers into @p stack_buf (capacity
 * SK_RESOURCE_ASSET_HANDLER_STACK_CAP) or a heap buffer when more are
 * registered. Caller must free *out_heap with the default allocator when
 * non-NULL.
 *
 * @return Total handler count; *out_items points at stack_buf or *out_heap.
 */
static u32 handler_snapshot(sk_app_context_t* context, const sk_app_api_t* app_api, const_ptr_t* stack_buf, const_ptr_t** out_items, const_ptr_t** out_heap) {
	u32 total = app_api->impl_count(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID);
	*out_heap = NULL;
	*out_items = stack_buf;

	if (total == 0u) {
		return 0u;
	}

	if (total <= SK_RESOURCE_ASSET_HANDLER_STACK_CAP) {
		(void)app_api->get_all_impls(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, stack_buf, SK_RESOURCE_ASSET_HANDLER_STACK_CAP);
		return total;
	}

	{
		const sk_allocator_t* alloc = sk_allocator_default();
		const_ptr_t* heap = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)total * sizeof(const_ptr_t));
		if (heap == NULL) {
			return 0u;
		}
		(void)app_api->get_all_impls(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, heap, total);
		*out_heap = heap;
		*out_items = heap;
		return total;
	}
}

static void handler_snapshot_free(const_ptr_t* heap) {
	if (heap != NULL) {
		const sk_allocator_t* alloc = sk_allocator_default();
		alloc->free(alloc->instance, heap);
	}
}

const sk_resource_asset_handler_t* sk_resource_asset_handler_find_by_extension(sk_app_context_t* context, const sk_app_api_t* app_api, const_chr_t extension) {
	const_ptr_t stack_buf[SK_RESOURCE_ASSET_HANDLER_STACK_CAP];
	const_ptr_t* items = NULL;
	const_ptr_t* heap = NULL;
	u32 total;
	u32 i;
	const sk_resource_asset_handler_t* found = NULL;

	total = handler_snapshot(context, app_api, stack_buf, &items, &heap);
	for (i = 0u; i < total; i++) {
		const sk_resource_asset_handler_t* handler = (const sk_resource_asset_handler_t*)items[i];
		const_chr_t claim;

		if (handler == NULL || handler->extension == NULL) {
			continue;
		}
		claim = handler->extension(handler->user_data);
		if (claim != NULL && strcmp(claim, extension) == 0) {
			found = handler;
			break;
		}
	}
	handler_snapshot_free(heap);
	return found;
}

const sk_resource_asset_handler_t* sk_resource_asset_handler_find_by_resource_type(sk_app_context_t* context, const sk_app_api_t* app_api, sk_type_id_t type_id) {
	const_ptr_t stack_buf[SK_RESOURCE_ASSET_HANDLER_STACK_CAP];
	const_ptr_t* items = NULL;
	const_ptr_t* heap = NULL;
	u32 total;
	u32 i;
	const sk_resource_asset_handler_t* found = NULL;

	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
		return NULL;
	}

	total = handler_snapshot(context, app_api, stack_buf, &items, &heap);
	for (i = 0u; i < total; i++) {
		const sk_resource_asset_handler_t* handler = (const sk_resource_asset_handler_t*)items[i];
		sk_type_id_t claimed;

		if (handler == NULL || handler->get_resource_type_id == NULL) {
			continue;
		}
		claimed = handler->get_resource_type_id(handler->user_data);
		if (SK_TYPE_ID_EQ(claimed, type_id)) {
			found = handler;
			break;
		}
	}
	handler_snapshot_free(heap);
	return found;
}

/* ------------------------------------------------------------------ */
/*  Null-safe dispatch                                                 */
/* ------------------------------------------------------------------ */

const_chr_t sk_resource_asset_handler_extension(const sk_resource_asset_handler_t* handler) {
	const_chr_t value;
	if (handler->extension == NULL) {
		return "";
	}
	value = handler->extension(handler->user_data);
	return (value != NULL) ? value : "";
}

void sk_resource_asset_handler_open_asset(const sk_resource_asset_handler_t* handler, sk_rid_t asset) {
	if (handler->open_asset != NULL) {
		handler->open_asset(handler->user_data, asset);
	}
}

sk_type_id_t sk_resource_asset_handler_get_resource_type_id(const sk_resource_asset_handler_t* handler) {
	if (handler->get_resource_type_id == NULL) {
		return SK_TYPE_ID_ZERO;
	}
	return handler->get_resource_type_id(handler->user_data);
}

const_chr_t sk_resource_asset_handler_get_desc(const sk_resource_asset_handler_t* handler) {
	const_chr_t value;
	if (handler->get_desc == NULL) {
		return "";
	}
	value = handler->get_desc(handler->user_data);
	return (value != NULL) ? value : "";
}

sk_rid_t sk_resource_asset_handler_load(const sk_resource_asset_handler_t* handler, sk_rid_t asset, const_chr_t absolute_path) {
	if (handler->load == NULL) {
		return SK_RID_ZERO;
	}
	return handler->load(handler->user_data, asset, absolute_path);
}

void sk_resource_asset_handler_save(const sk_resource_asset_handler_t* handler, sk_rid_t object, const_chr_t absolute_path) {
	if (handler->save != NULL) {
		handler->save(handler->user_data, object, absolute_path);
	}
}

sk_rid_t sk_resource_asset_handler_create(const sk_resource_asset_handler_t* handler, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	if (handler->create == NULL) {
		return SK_RID_ZERO;
	}
	return handler->create(handler->user_data, uuid, scope);
}

void sk_resource_asset_handler_reloaded(const sk_resource_asset_handler_t* handler, sk_rid_t asset, const_chr_t absolute_path) {
	if (handler->reloaded != NULL) {
		handler->reloaded(handler->user_data, asset, absolute_path);
	}
}

void sk_resource_asset_handler_after_move(const sk_resource_asset_handler_t* handler, sk_rid_t asset, const_chr_t old_absolute_path, const_chr_t new_absolute_path) {
	if (handler->after_move != NULL) {
		handler->after_move(handler->user_data, asset, old_absolute_path, new_absolute_path);
	}
}

void sk_resource_asset_handler_export_object(const sk_resource_asset_handler_t* handler, sk_rid_t object, sk_archive_writer_t* writer) {
	if (handler->export_object != NULL) {
		handler->export_object(handler->user_data, object, writer);
	}
}

const_chr_t sk_resource_asset_handler_get_icon(const sk_resource_asset_handler_t* handler) {
	const_chr_t value;
	if (handler->get_icon == NULL) {
		return "";
	}
	value = handler->get_icon(handler->user_data);
	return (value != NULL) ? value : "";
}

i32 sk_resource_asset_handler_get_load_order(const sk_resource_asset_handler_t* handler) {
	if (handler->get_load_order == NULL) {
		return SK_RESOURCE_ASSET_HANDLER_DEFAULT_LOAD_ORDER;
	}
	return handler->get_load_order(handler->user_data);
}

i32 sk_resource_asset_handler_get_asset_name(const sk_resource_asset_handler_t* handler, sk_rid_t rid, char* out_name, u32 out_cap) {
	if (handler->get_asset_name == NULL) {
		return 0;
	}
	return handler->get_asset_name(handler->user_data, rid, out_name, out_cap);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Repository-assets engine                                           */
/* ------------------------------------------------------------------ */

#include "resource_assets_types.h"
#include "array.h"
#include "filesystem.h"
#include "hashmap.h"
#include "logger.h"
#include "path.h"

#include <stdio.h>
#include <string.h>

typedef SK_ARRAY(sk_rid_t) resource_asset_list_t;

typedef struct directory_to_scan_t {
	char path[SK_FS_PATH_MAX]; /* path id, e.g. "Game:/Textures" */
	char absolute_path[SK_FS_PATH_MAX];
	sk_rid_t directory; /* ResourceAssetDirectory node RID */
} directory_to_scan_t;

typedef struct pending_file_t {
	char path[SK_FS_PATH_MAX]; /* path id */
	char absolute_path[SK_FS_PATH_MAX];
	char extension[64];
	char file_name[256];
	sk_rid_t directory; /* ResourceAssetDirectory node RID */
	u64 file_size;
	i32 load_order;
} pending_file_t;

typedef struct import_path_t {
	char path[SK_FS_PATH_MAX];
} import_path_t;

struct sk_resource_assets_context_t {
	sk_repository_t* repository;
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	const sk_allocator_t* allocator;

	const sk_resource_type_t* package_type;
	const sk_resource_type_t* file_type;
	const sk_resource_type_t* asset_type;
	const sk_resource_type_t* directory_type;
	const sk_resource_type_t* imported_asset_type;
	const sk_resource_type_t* sub_id_entry_type;
	const sk_resource_type_t* dependency_entry_type;
	const sk_resource_type_t* extracted_entry_type;

	SK_ARRAY(const sk_resource_asset_handler_t*) handlers;
	SK_ARRAY(const sk_resource_asset_importer_t*) importers;

	SK_HASH_MAP(const_chr_t, const sk_resource_asset_handler_t*) handlers_by_extension;
	SK_HASH_MAP(sk_type_id_t, const sk_resource_asset_handler_t*) handlers_by_type;
	SK_HASH_MAP(const_chr_t, const sk_resource_asset_importer_t*) importers_by_extension;
	SK_HASH_MAP(sk_type_id_t, resource_asset_list_t) assets_by_type;

	char package_name[SK_FS_PATH_MAX];
	char package_root_absolute_path[SK_FS_PATH_MAX];
	sk_rid_t package;
	sk_rid_t root_directory;
};

/* ------------------------------------------------------------------ */
/*  Forward declarations / small helpers                              */
/* ------------------------------------------------------------------ */

static void register_asset_by_type(sk_resource_assets_context_t* ctx, sk_rid_t asset);
static void unregister_asset_by_type(sk_resource_assets_context_t* ctx, sk_rid_t asset);

static const sk_repository_api_t* resource_repo_api(void) {
	return sk_repository_api();
}

static sk_rid_t resource_create(sk_resource_assets_context_t* ctx, const sk_resource_type_t* type, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	return repo->create_resource(ctx->repository, type, SK_UUID_ZERO, scope);
}

static sk_uuid_t path_uuid(const_chr_t path) {
	size_t n = strlen(path);
	sk_uuid_t result;
	result.lo = sk_hash_bytes(path, n);
	result.hi = sk_hash_bytes(path, n + 1u); /* include the NUL so hi != lo */
	return result;
}

static sk_uuid_t sub_resource_uuid(sk_uuid_t base, const_chr_t sub_id) {
	char key[SK_FS_PATH_MAX];
	i32 n = snprintf(key, sizeof(key), "%016llx%016llx:%s", base.lo, base.hi, sub_id);
	if (n < 0) {
		return SK_UUID_ZERO;
	}
	sk_uuid_t result;
	result.lo = sk_hash_bytes(key, (size_t)n);
	result.hi = sk_hash_bytes(key, (size_t)n + 1u); /* include the NUL so hi != lo */
	return result;
}

static void uuid_to_string(sk_uuid_t uuid, char_ptr_t out, u32 out_cap) {
	snprintf(out, (size_t)out_cap, "%016llx%016llx", uuid.lo, uuid.hi);
}

static sk_type_id_t resource_type_id_for_type(sk_resource_assets_context_t* ctx, const sk_resource_type_t* type) {
	if (type == NULL) {
		return SK_TYPE_ID_ZERO;
	}
	const sk_repository_api_t* repo = resource_repo_api();
	for (u32 i = 0u; i < ctx->handlers.count; ++i) {
		sk_type_id_t type_id = sk_resource_asset_handler_get_resource_type_id(ctx->handlers.items[i]);
		if (repo->find_type(ctx->repository, type_id) == type) {
			return type_id;
		}
	}
	return SK_TYPE_ID_ZERO;
}

static sk_type_id_t resource_type_of(sk_resource_assets_context_t* ctx, sk_rid_t rid) {
	const sk_repository_api_t* repo = resource_repo_api();
	return resource_type_id_for_type(ctx, repo->resource_type(ctx->repository, rid));
}

static i32 handler_ext_put(sk_resource_assets_context_t* ctx, const_chr_t extension, const sk_resource_asset_handler_t* handler) {
	return sk_hash_map_put_(&ctx->handlers_by_extension._hm, &extension, &handler);
}

static i32 handler_ext_get(sk_resource_assets_context_t* ctx, const_chr_t extension, const sk_resource_asset_handler_t** out_handler) {
	return sk_hash_map_get_(&ctx->handlers_by_extension._hm, &extension, out_handler);
}

static i32 handler_type_put(sk_resource_assets_context_t* ctx, sk_type_id_t type_id, const sk_resource_asset_handler_t* handler) {
	return sk_hash_map_put_(&ctx->handlers_by_type._hm, &type_id, &handler);
}

static i32 handler_type_get(sk_resource_assets_context_t* ctx, sk_type_id_t type_id, const sk_resource_asset_handler_t** out_handler) {
	return sk_hash_map_get_(&ctx->handlers_by_type._hm, &type_id, out_handler);
}

static i32 importer_ext_put(sk_resource_assets_context_t* ctx, const_chr_t extension, const sk_resource_asset_importer_t* importer) {
	return sk_hash_map_put_(&ctx->importers_by_extension._hm, &extension, &importer);
}

static i32 importer_ext_get(sk_resource_assets_context_t* ctx, const_chr_t extension, const sk_resource_asset_importer_t** out_importer) {
	return sk_hash_map_get_(&ctx->importers_by_extension._hm, &extension, out_importer);
}

/* ------------------------------------------------------------------ */
/*  Handler / importer discovery                                       */
/* ------------------------------------------------------------------ */

static void reload_handlers(sk_resource_assets_context_t* ctx) {
	const sk_logger_api_t* logger_api = sk_logger_api();
	sk_logger_t* log = logger_api->create_logger("Skore::ResourceAssets");

	sk_hash_map_clear_(&ctx->handlers_by_extension._hm);
	sk_hash_map_clear_(&ctx->handlers_by_type._hm);
	sk_hash_map_clear_(&ctx->importers_by_extension._hm);
	sk_array_clear(&ctx->handlers);
	sk_array_clear(&ctx->importers);

	u32 handler_count = ctx->app_api->get_all_impls(ctx->app_context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, NULL, 0u);
	if (handler_count > 0u) {
		SK_ARRAY(const_ptr_t) impls;
		sk_array_init(&impls, ctx->allocator);
		if (sk_array_resize(&impls, handler_count) == 0) {
			ctx->app_api->get_all_impls(ctx->app_context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, impls.items, handler_count);
			for (u32 i = 0u; i < handler_count; ++i) {
				const sk_resource_asset_handler_t* handler = (const sk_resource_asset_handler_t*)impls.items[i];
				sk_array_push(&ctx->handlers, handler);
				const_chr_t extension = sk_resource_asset_handler_extension(handler);
				if (extension != NULL && extension[0] != '\0') {
					handler_ext_put(ctx, extension, handler);
				}
				sk_type_id_t type_id = sk_resource_asset_handler_get_resource_type_id(handler);
				if (!SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
					handler_type_put(ctx, type_id, handler);
				}
				sk_log_debug(logger_api, log, "registered asset handler for extension '%s'", extension != NULL ? extension : "");
			}
		}
		sk_array_free(&impls);
	}

	u32 importer_count = ctx->app_api->get_all_impls(ctx->app_context, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID, NULL, 0u);
	if (importer_count > 0u) {
		SK_ARRAY(const_ptr_t) impls;
		sk_array_init(&impls, ctx->allocator);
		if (sk_array_resize(&impls, importer_count) == 0) {
			ctx->app_api->get_all_impls(ctx->app_context, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID, impls.items, importer_count);
			/* Main ReloadAssetHandlers: non-empty OutputExtension maps onto ImportedAssetHandler. */
			const sk_resource_asset_handler_t* imported_handler = NULL;
			(void)handler_type_get(ctx, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID, &imported_handler);
			for (u32 i = 0u; i < importer_count; ++i) {
				const sk_resource_asset_importer_t* importer = (const sk_resource_asset_importer_t*)impls.items[i];
				sk_array_push(&ctx->importers, importer);
				const_chr_t extensions_buf[32];
				u32 ext_count = 0u;
				if (importer->imported_extensions != NULL) {
					ext_count = importer->imported_extensions(importer->user_data, extensions_buf, 32u);
					if (ext_count > 32u) {
						ext_count = 32u;
					}
				}
				for (u32 k = 0u; k < ext_count; ++k) {
					if (extensions_buf[k] != NULL) {
						importer_ext_put(ctx, extensions_buf[k], importer);
					}
				}
				if (importer->output_extension != NULL && imported_handler != NULL) {
					const_chr_t output_extension = importer->output_extension(importer->user_data);
					if (output_extension != NULL && output_extension[0] != '\0') {
						handler_ext_put(ctx, output_extension, imported_handler);
					}
				}
				sk_log_debug(logger_api, log, "registered asset importer for %u extension(s)", ext_count);
			}
		}
		sk_array_free(&impls);
	}

	logger_api->destroy_logger(log);
}

/* ------------------------------------------------------------------ */
/*  Context lifecycle                                                  */
/* ------------------------------------------------------------------ */

static sk_resource_assets_context_t* resource_assets_create(sk_repository_t* repository, sk_app_context_t* app_context, const sk_app_api_t* app_api,
															const sk_allocator_t* allocator) {
	sk_resource_assets_context_t* ctx = (sk_resource_assets_context_t*)allocator->alloc(allocator->instance, sizeof(sk_resource_assets_context_t));
	if (ctx == NULL) {
		return NULL;
	}
	memset(ctx, 0, sizeof(*ctx));

	ctx->repository = repository;
	ctx->app_context = app_context;
	ctx->app_api = app_api;
	ctx->allocator = allocator;

	const sk_repository_api_t* repo = resource_repo_api();
	ctx->package_type = repo->find_type(repository, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID);
	ctx->file_type = repo->find_type(repository, SK_RESOURCE_ASSET_FILE_TYPE_ID);
	ctx->asset_type = repo->find_type(repository, SK_RESOURCE_ASSET_TYPE_ID);
	ctx->directory_type = repo->find_type(repository, SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID);
	ctx->imported_asset_type = repo->find_type(repository, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID);
	ctx->sub_id_entry_type = repo->find_type(repository, SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID);
	ctx->dependency_entry_type = repo->find_type(repository, SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID);
	ctx->extracted_entry_type = repo->find_type(repository, SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID);

	sk_array_init(&ctx->handlers, allocator);
	sk_array_init(&ctx->importers, allocator);
	sk_hash_map_init_(&ctx->handlers_by_extension._hm, allocator, (u32)sizeof(const_chr_t), (u32)sizeof(const sk_resource_asset_handler_t*), sk_hash_cstr, sk_equals_cstr);
	sk_hash_map_init_(&ctx->handlers_by_type._hm, allocator, (u32)sizeof(sk_type_id_t), (u32)sizeof(const sk_resource_asset_handler_t*), NULL, NULL);
	sk_hash_map_init_(&ctx->importers_by_extension._hm, allocator, (u32)sizeof(const_chr_t), (u32)sizeof(const sk_resource_asset_importer_t*), sk_hash_cstr, sk_equals_cstr);
	sk_hash_map_init_(&ctx->assets_by_type._hm, allocator, (u32)sizeof(sk_type_id_t), (u32)sizeof(resource_asset_list_t), NULL, NULL);

	reload_handlers(ctx);
	return ctx;
}

static void resource_assets_destroy(sk_resource_assets_context_t* ctx) {
	if (ctx == NULL) {
		return;
	}
	for (u32 i = 0u; i < ctx->assets_by_type._hm.capacity; ++i) {
		if (sk_hash_map_slot_occupied_(&ctx->assets_by_type._hm, i)) {
			resource_asset_list_t* list = (resource_asset_list_t*)sk_hash_map_value_at_(&ctx->assets_by_type._hm, i);
			if (list != NULL) {
				sk_array_free(list);
			}
		}
	}
	sk_hash_map_free_(&ctx->assets_by_type._hm);
	sk_hash_map_free_(&ctx->handlers_by_extension._hm);
	sk_hash_map_free_(&ctx->handlers_by_type._hm);
	sk_hash_map_free_(&ctx->importers_by_extension._hm);
	sk_array_free(&ctx->handlers);
	sk_array_free(&ctx->importers);
	ctx->allocator->free(ctx->allocator->instance, ctx);
}

/* ------------------------------------------------------------------ */
/*  ScanPackageFromDirectory                                           */
/* ------------------------------------------------------------------ */

static sk_rid_t scan_create_file(sk_resource_assets_context_t* ctx, sk_rid_t asset, const_chr_t absolute_path, const_chr_t relative_path, u64 persisted_version, u64 total_size) {
	const sk_repository_api_t* repo = resource_repo_api();
	sk_rid_t file = resource_create(ctx, ctx->file_type, NULL);
	if (file.id == 0u) {
		return SK_RID_ZERO;
	}
	sk_resource_object_t view = repo->write(ctx->repository, file);
	repo->set_reference(view, SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF, asset);
	repo->set_string(view, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH, absolute_path);
	repo->set_string(view, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH, relative_path);
	repo->set_uint(view, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION, persisted_version);
	repo->set_uint(view, SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK, total_size);
	repo->set_uint(view, SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME, 0u);
	repo->commit(view, NULL);
	return file;
}

static sk_rid_t scan_package_from_directory(sk_resource_assets_context_t* ctx, const_chr_t package_name, const_chr_t package_path) {
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_filesystem_api_t* fs = sk_filesystem_api();

	resource_asset_list_t package_files;
	sk_array_init(&package_files, ctx->allocator);

	sk_rid_t package = resource_create(ctx, ctx->package_type, NULL);
	if (package.id == 0u) {
		sk_array_free(&package_files);
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, package);
		repo->set_string(view, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, package_name);
		repo->commit(view, NULL);
	}

	char root_absolute[SK_FS_PATH_MAX];
	if (sk_path_join(sk_str_view_cstr(package_path), sk_str_view_cstr("Assets"), root_absolute, (u32)sizeof(root_absolute)) < 0) {
		sk_array_free(&package_files);
		return SK_RID_ZERO;
	}

	char root_path_id[SK_FS_PATH_MAX];
	snprintf(root_path_id, sizeof(root_path_id), "%s:/", package_name);

	sk_rid_t root_asset = resource_create(ctx, ctx->asset_type, NULL);
	if (root_asset.id == 0u) {
		sk_array_free(&package_files);
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, root_asset);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, package_name);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, "");
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, root_path_id);
		repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 1);
		repo->commit(view, NULL);
	}

	sk_rid_t root_file = scan_create_file(ctx, root_asset, root_absolute, root_path_id, repo->get_version(ctx->repository, root_asset), 0u);
	sk_rid_t root_node = resource_create(ctx, ctx->directory_type, NULL);
	if (root_file.id == 0u || root_node.id == 0u) {
		sk_array_free(&package_files);
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, root_node);
		repo->set_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET, root_asset);
		repo->commit(view, NULL);
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, root_asset);
		repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, root_file);
		repo->commit(view, NULL);
	}
	sk_array_push(&package_files, root_file);

	strncpy(ctx->package_name, package_name, sizeof(ctx->package_name) - 1u);
	ctx->package_name[sizeof(ctx->package_name) - 1u] = '\0';
	strncpy(ctx->package_root_absolute_path, root_absolute, sizeof(ctx->package_root_absolute_path) - 1u);
	ctx->package_root_absolute_path[sizeof(ctx->package_root_absolute_path) - 1u] = '\0';
	ctx->package = package;
	ctx->root_directory = root_node;

	/* First pass: BFS directories, create directory structures, collect files. */
	SK_ARRAY(directory_to_scan_t) pending_dirs;
	SK_ARRAY(pending_file_t) pending_files;
	sk_array_init(&pending_dirs, ctx->allocator);
	sk_array_init(&pending_files, ctx->allocator);

	directory_to_scan_t root_entry;
	memset(&root_entry, 0, sizeof(root_entry));
	snprintf(root_entry.path, sizeof(root_entry.path), "%s", root_path_id);
	snprintf(root_entry.absolute_path, sizeof(root_entry.absolute_path), "%s", root_absolute);
	root_entry.directory = root_node;
	sk_array_push(&pending_dirs, root_entry);

	for (u32 head = 0u; head < pending_dirs.count; ++head) {
		directory_to_scan_t scan = pending_dirs.items[head];
		sk_directory_iterator_t it = fs->open_directory(scan.absolute_path);
		if (it == NULL) {
			continue;
		}

		resource_asset_list_t child_nodes;
		resource_asset_list_t child_assets;
		sk_array_init(&child_nodes, ctx->allocator);
		sk_array_init(&child_assets, ctx->allocator);

		char entry_name[SK_FS_PATH_MAX];
		while (fs->next_directory(it, entry_name, (u32)sizeof(entry_name)) == 0) {
			if (entry_name[0] == '\0' || entry_name[0] == '.') {
				continue;
			}
			sk_str_view_t ext_view = sk_path_extension(sk_str_view_cstr(entry_name));
			if (ext_view.size > 0u && (strcmp(ext_view.data, ".buffer") == 0 || strcmp(ext_view.data, ".buffers") == 0 || strcmp(ext_view.data, ".info") == 0)) {
				continue;
			}

			char full_path[SK_FS_PATH_MAX];
			if (sk_path_join(sk_str_view_cstr(scan.absolute_path), sk_str_view_cstr(entry_name), full_path, (u32)sizeof(full_path)) < 0) {
				continue;
			}

			char file_name[256];
			if (sk_path_name(sk_str_view_cstr(entry_name), file_name, (u32)sizeof(file_name)) < 0) {
				continue;
			}

			char extension[64];
			extension[0] = '\0';
			if (ext_view.size > 0u) {
				size_t ext_len = (size_t)ext_view.size;
				if (ext_len >= sizeof(extension)) {
					ext_len = sizeof(extension) - 1u;
				}
				memcpy(extension, ext_view.data, ext_len);
				extension[ext_len] = '\0';
			}

			char path_id[SK_FS_PATH_MAX];
			if (sk_path_join(sk_str_view_cstr(scan.path), sk_str_view_cstr(file_name), path_id, (u32)sizeof(path_id)) < 0) {
				continue;
			}
			{
				size_t ext_len = strlen(extension);
				size_t len = strlen(path_id);
				if (len + ext_len + 1u <= sizeof(path_id)) {
					memcpy(path_id + len, extension, ext_len);
					path_id[len + ext_len] = '\0';
				}
			}

			sk_file_status_t status = fs->get_file_status(full_path);
			if (status == SK_FILE_STATUS_DIRECTORY) {
				sk_rid_t asset = resource_create(ctx, ctx->asset_type, NULL);
				sk_rid_t file = scan_create_file(ctx, asset, full_path, path_id, 0u, 0u);
				sk_rid_t node = resource_create(ctx, ctx->directory_type, NULL);
				if (asset.id == 0u || file.id == 0u || node.id == 0u) {
					continue;
				}
				{
					sk_resource_object_t view = repo->write(ctx->repository, asset);
					repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, file_name);
					repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, extension);
					repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, path_id);
					repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, scan.directory);
					repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 1);
					repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, file);
					repo->commit(view, NULL);
				}
				{
					sk_resource_object_t view = repo->write(ctx->repository, node);
					repo->set_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET, asset);
					repo->commit(view, NULL);
				}
				sk_array_push(&child_nodes, node);
				sk_array_push(&child_assets, asset);
				sk_array_push(&package_files, file);

				directory_to_scan_t child;
				memset(&child, 0, sizeof(child));
				snprintf(child.path, sizeof(child.path), "%s", path_id);
				snprintf(child.absolute_path, sizeof(child.absolute_path), "%s", full_path);
				child.directory = node;
				sk_array_push(&pending_dirs, child);
			} else if (status == SK_FILE_STATUS_FILE) {
				i32 load_order = 0x7FFFFFFF;
				const sk_resource_asset_handler_t* handler = NULL;
				if (handler_ext_get(ctx, extension, &handler) == 0 && handler != NULL) {
					load_order = sk_resource_asset_handler_get_load_order(handler);
				}
				pending_file_t pending;
				memset(&pending, 0, sizeof(pending));
				snprintf(pending.path, sizeof(pending.path), "%s", path_id);
				snprintf(pending.absolute_path, sizeof(pending.absolute_path), "%s", full_path);
				snprintf(pending.extension, sizeof(pending.extension), "%s", extension);
				snprintf(pending.file_name, sizeof(pending.file_name), "%s", file_name);
				pending.directory = scan.directory;
				pending.file_size = fs->get_path_size(full_path);
				pending.load_order = load_order;
				sk_array_push(&pending_files, pending);
			}
		}
		fs->close_directory(it);

		if (child_nodes.count > 0u || child_assets.count > 0u) {
			sk_resource_object_t view = repo->write(ctx->repository, scan.directory);
			for (u32 i = 0u; i < child_nodes.count; ++i) {
				repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, child_nodes.items[i]);
			}
			for (u32 i = 0u; i < child_assets.count; ++i) {
				repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, child_assets.items[i]);
			}
			repo->commit(view, NULL);
		}
		sk_array_free(&child_nodes);
		sk_array_free(&child_assets);
	}

	/* Second pass: create file assets in load order (payload load deferred
	 * until serialization lands; Object stays empty). */
	for (u32 i = 1u; i < pending_files.count; ++i) {
		pending_file_t key = pending_files.items[i];
		u32 j = i;
		while (j > 0u && pending_files.items[j - 1u].load_order > key.load_order) {
			pending_files.items[j] = pending_files.items[j - 1u];
			--j;
		}
		pending_files.items[j] = key;
	}

	for (u32 i = 0u; i < pending_files.count; ++i) {
		pending_file_t pending = pending_files.items[i];
		sk_rid_t asset = resource_create(ctx, ctx->asset_type, NULL);
		sk_rid_t file = scan_create_file(ctx, asset, pending.absolute_path, pending.path, 0u, pending.file_size);
		if (asset.id == 0u || file.id == 0u) {
			continue;
		}
		{
			sk_resource_object_t view = repo->write(ctx->repository, asset);
			repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, pending.file_name);
			repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, pending.extension);
			repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, pending.path);
			repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, pending.directory);
			repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0);
			repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, file);
			repo->commit(view, NULL);
		}
		sk_array_push(&package_files, file);
		sk_resource_object_t view = repo->write(ctx->repository, pending.directory);
		repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, asset);
		repo->commit(view, NULL);
	}

	sk_array_free(&pending_dirs);
	sk_array_free(&pending_files);

	/* Finish the package: Files list + Root node. */
	{
		sk_resource_object_t view = repo->write(ctx->repository, package);
		for (u32 i = 0u; i < package_files.count; ++i) {
			repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, package_files.items[i]);
		}
		repo->set_subobject(view, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, root_node);
		repo->commit(view, NULL);
	}
	sk_array_free(&package_files);

	return package;
}

/* ------------------------------------------------------------------ */
/*  Creation                                                           */
/* ------------------------------------------------------------------ */

static const_chr_t get_directory_path_id(sk_resource_assets_context_t* ctx, sk_rid_t directory) {
	const sk_repository_api_t* repo = resource_repo_api();
	sk_resource_object_t node_view = repo->read(ctx->repository, directory);
	sk_rid_t directory_asset = repo->get_subobject(node_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
	if (directory_asset.id == 0u) {
		return NULL;
	}
	sk_resource_object_t asset_view = repo->read(ctx->repository, directory_asset);
	return repo->get_string(asset_view, SK_RESOURCE_ASSET_FIELD_PATH_ID);
}

static i32 join_asset_path(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t name, const_chr_t extension, char_ptr_t out, u32 out_cap) {
	const_chr_t parent_path = get_directory_path_id(ctx, parent);
	if (parent_path == NULL || parent_path[0] == '\0') {
		return -1;
	}
	i32 n = sk_path_join(sk_str_view_cstr(parent_path), sk_str_view_cstr(name), out, out_cap);
	if (n < 0) {
		return -1;
	}
	size_t len = (size_t)n;
	size_t ext_len = (extension != NULL) ? strlen(extension) : 0u;
	if (len + ext_len + 1u > out_cap) {
		return -1;
	}
	if (ext_len > 0u) {
		memcpy(out + len, extension, ext_len);
	}
	out[len + ext_len] = '\0';
	return (i32)(len + ext_len);
}

static i32 create_unique_asset_name(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, const_chr_t extension, i32 directory, char_ptr_t out,
									u32 out_cap) {
	if (parent.id == 0u) {
		return -1;
	}
	const sk_repository_api_t* repo = resource_repo_api();
	sk_resource_object_t parent_view = repo->read(ctx->repository, parent);
	u32 child_count = 0u;
	const sk_rid_t* children = repo->get_subobject_list(parent_view, directory ? SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES : SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS,
														&child_count);

	u32 attempt = 0u;
	for (;;) {
		if (attempt == 0u) {
			snprintf(out, (size_t)out_cap, "%s", desired_name != NULL ? desired_name : "");
		} else {
			snprintf(out, (size_t)out_cap, "%s (%u)", desired_name != NULL ? desired_name : "", attempt);
		}

		i32 taken = 0;
		for (u32 i = 0u; i < child_count; ++i) {
			sk_rid_t child_asset = children[i];
			if (directory) {
				sk_resource_object_t node_view = repo->read(ctx->repository, child_asset);
				child_asset = repo->get_subobject(node_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
			}
			sk_resource_object_t child_view = repo->read(ctx->repository, child_asset);
			const_chr_t child_name = repo->get_string(child_view, SK_RESOURCE_ASSET_FIELD_NAME);
			const_chr_t child_ext = repo->get_string(child_view, SK_RESOURCE_ASSET_FIELD_EXTENSION);
			const_chr_t this_ext = (extension != NULL) ? extension : "";
			if (child_name != NULL && strcmp(child_name, out) == 0 && child_ext != NULL && strcmp(child_ext, this_ext) == 0) {
				taken = 1;
				break;
			}
		}
		if (!taken) {
			return (i32)strlen(out);
		}
		++attempt;
	}
}

static sk_rid_t create_asset(sk_resource_assets_context_t* ctx, sk_rid_t parent, sk_type_id_t type_id, const_chr_t desired_name, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_resource_asset_handler_t* handler = NULL;
	if (handler_type_get(ctx, type_id, &handler) != 0 || handler == NULL) {
		const sk_logger_api_t* logger_api = sk_logger_api();
		sk_logger_t* log = logger_api->create_logger("Skore::ResourceAssets");
		sk_log_error(logger_api, log, "asset from type cannot be created, no handler found");
		logger_api->destroy_logger(log);
		return SK_RID_ZERO;
	}

	const_chr_t extension = sk_resource_asset_handler_extension(handler);
	const_chr_t desc = sk_resource_asset_handler_get_desc(handler);
	char default_name[256];
	snprintf(default_name, sizeof(default_name), "New %s", desc != NULL ? desc : "");
	const_chr_t base_name = (desired_name != NULL && desired_name[0] != '\0') ? desired_name : default_name;

	char name[256];
	if (create_unique_asset_name(ctx, parent, base_name, extension, 0, name, (u32)sizeof(name)) < 0) {
		return SK_RID_ZERO;
	}
	char path[SK_FS_PATH_MAX];
	if (join_asset_path(ctx, parent, name, extension, path, (u32)sizeof(path)) < 0) {
		return SK_RID_ZERO;
	}

	sk_rid_t payload = sk_resource_asset_handler_create(handler, SK_UUID_ZERO, scope);
	sk_rid_t rid = resource_create(ctx, ctx->asset_type, scope);
	if (payload.id == 0u || rid.id == 0u) {
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, rid);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, name);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, extension);
		repo->set_subobject(view, SK_RESOURCE_ASSET_FIELD_OBJECT, payload);
		repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, parent);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, path);
		repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0);
		repo->commit(view, scope);
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, parent);
		repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, rid);
		repo->commit(view, scope);
	}
	register_asset_by_type(ctx, payload);
	return rid;
}

static sk_rid_t create_asset_directory(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	char name[256];
	if (create_unique_asset_name(ctx, parent, desired_name, "", 1, name, (u32)sizeof(name)) < 0) {
		return SK_RID_ZERO;
	}
	char path[SK_FS_PATH_MAX];
	if (join_asset_path(ctx, parent, name, "", path, (u32)sizeof(path)) < 0) {
		return SK_RID_ZERO;
	}

	sk_rid_t asset = resource_create(ctx, ctx->asset_type, scope);
	sk_rid_t node = resource_create(ctx, ctx->directory_type, scope);
	if (asset.id == 0u || node.id == 0u) {
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, asset);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, name);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, "");
		repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, parent);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, path);
		repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 1);
		repo->commit(view, scope);
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, node);
		repo->set_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET, asset);
		repo->commit(view, scope);
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, parent);
		repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, node);
		repo->commit(view, scope);
	}
	return asset;
}

static sk_rid_t create_asset_file(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, const_chr_t extension, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	const_chr_t base_name = (desired_name != NULL && desired_name[0] != '\0') ? desired_name : "New File";
	const_chr_t ext = (extension != NULL) ? extension : "";
	char name[256];
	if (create_unique_asset_name(ctx, parent, base_name, ext, 0, name, (u32)sizeof(name)) < 0) {
		return SK_RID_ZERO;
	}
	char path[SK_FS_PATH_MAX];
	if (join_asset_path(ctx, parent, name, ext, path, (u32)sizeof(path)) < 0) {
		return SK_RID_ZERO;
	}

	sk_rid_t asset = resource_create(ctx, ctx->asset_type, scope);
	if (asset.id == 0u) {
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, asset);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, name);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, ext);
		repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, parent);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, path);
		repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0);
		repo->commit(view, scope);
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, parent);
		repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, asset);
		repo->commit(view, scope);
	}
	return asset;
}

static void move_asset(sk_resource_assets_context_t* ctx, sk_rid_t new_parent, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	if (new_parent.id == 0u || rid.id == 0u) {
		return;
	}
	const sk_repository_api_t* repo = resource_repo_api();
	sk_rid_t old_parent = repo->get_parent(ctx->repository, rid);
	if (old_parent.id == 0u || SK_RID_EQ(old_parent, new_parent)) {
		return;
	}

	sk_resource_object_t asset_view = repo->read(ctx->repository, rid);
	i32 is_directory = repo->get_bool(asset_view, SK_RESOURCE_ASSET_FIELD_DIRECTORY);
	const_chr_t name = repo->get_string(asset_view, SK_RESOURCE_ASSET_FIELD_NAME);
	const_chr_t extension = repo->get_string(asset_view, SK_RESOURCE_ASSET_FIELD_EXTENSION);

	char new_name[256];
	if (create_unique_asset_name(ctx, new_parent, name != NULL ? name : "", extension != NULL ? extension : "", is_directory, new_name, (u32)sizeof(new_name)) < 0) {
		return;
	}

	if (is_directory) {
		sk_rid_t node = repo->get_parent(ctx->repository, rid);
		sk_rid_t old_containing = repo->get_parent(ctx->repository, old_parent);
		if (old_containing.id != 0u) {
			sk_resource_object_t view = repo->write(ctx->repository, old_containing);
			repo->remove_from_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, node);
			repo->commit(view, scope);
		}
		{
			sk_resource_object_t view = repo->write(ctx->repository, new_parent);
			repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, node);
			repo->commit(view, scope);
		}
	} else {
		{
			sk_resource_object_t view = repo->write(ctx->repository, old_parent);
			repo->remove_from_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, rid);
			repo->commit(view, scope);
		}
		{
			sk_resource_object_t view = repo->write(ctx->repository, new_parent);
			repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, rid);
			repo->commit(view, scope);
		}
	}

	{
		sk_resource_object_t view = repo->write(ctx->repository, rid);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, new_name);
		char path[SK_FS_PATH_MAX];
		if (join_asset_path(ctx, new_parent, new_name, extension, path, (u32)sizeof(path)) >= 0) {
			repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, path);
		}
		repo->commit(view, scope);
	}
}

/* ------------------------------------------------------------------ */
/*  Queries                                                            */
/* ------------------------------------------------------------------ */

static const_chr_t get_absolute_path(sk_resource_assets_context_t* ctx, sk_rid_t asset) {
	const sk_repository_api_t* repo = resource_repo_api();
	sk_rid_t resource_asset = asset;
	if (repo->resource_type(ctx->repository, asset) != ctx->asset_type) {
		resource_asset = repo->get_parent(ctx->repository, asset);
		if (resource_asset.id == 0u) {
			return NULL;
		}
	}
	sk_resource_object_t asset_view = repo->read(ctx->repository, resource_asset);
	sk_rid_t asset_file = repo->get_reference(asset_view, SK_RESOURCE_ASSET_FIELD_ASSET_FILE);
	if (asset_file.id == 0u) {
		return NULL;
	}
	sk_resource_object_t file_view = repo->read(ctx->repository, asset_file);
	return repo->get_string(file_view, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH);
}

static const_chr_t get_path_id(sk_resource_assets_context_t* ctx, sk_rid_t asset) {
	const sk_repository_api_t* repo = resource_repo_api();
	sk_resource_object_t view = repo->read(ctx->repository, asset);
	return repo->get_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID);
}

static i32 get_absolute_path_from_path_id(sk_resource_assets_context_t* ctx, const_chr_t path_id, char_ptr_t out, u32 out_cap) {
	if (path_id == NULL || path_id[0] == '\0') {
		return -1;
	}
	const_chr_t sep = strstr(path_id, ":/");
	if (sep == NULL) {
		return -1;
	}
	size_t name_len = (size_t)(sep - path_id);
	if (name_len != strlen(ctx->package_name) || strncmp(path_id, ctx->package_name, name_len) != 0) {
		return -1;
	}
	if (ctx->package_root_absolute_path[0] == '\0') {
		return -1;
	}
	const_chr_t relative = sep + 2u;
	i32 n = sk_path_join(sk_str_view_cstr(ctx->package_root_absolute_path), sk_str_view_cstr(relative), out, out_cap);
	return (n < 0) ? -1 : 0;
}

static sk_rid_t get_parent_asset(sk_resource_assets_context_t* ctx, sk_rid_t rid) {
	const sk_repository_api_t* repo = resource_repo_api();
	sk_resource_object_t view = repo->read(ctx->repository, rid);
	if (SK_RESOURCE_OBJECT_IS_VALID(view) && repo->get_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY)) {
		return repo->get_parent(ctx->repository, repo->get_parent(ctx->repository, rid));
	}
	return repo->get_parent(ctx->repository, rid);
}

static i32 is_child_of(sk_resource_assets_context_t* ctx, sk_rid_t parent, sk_rid_t child) {
	const sk_repository_api_t* repo = resource_repo_api();
	sk_resource_object_t view = repo->read(ctx->repository, parent);
	return repo->has_on_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, child);
}

static sk_rid_t get_asset_payload(sk_resource_assets_context_t* ctx, sk_rid_t rid) {
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_resource_type_t* type = repo->resource_type(ctx->repository, rid);
	sk_resource_object_t view = repo->read(ctx->repository, rid);
	if (type == ctx->directory_type) {
		return repo->get_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
	}
	if (type == ctx->asset_type) {
		return repo->get_subobject(view, SK_RESOURCE_ASSET_FIELD_OBJECT);
	}
	return rid;
}

static const sk_resource_asset_handler_t* get_asset_handler(sk_resource_assets_context_t* ctx, sk_rid_t rid) {
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_resource_type_t* type = repo->resource_type(ctx->repository, rid);
	if (type == ctx->asset_type) {
		sk_resource_object_t view = repo->read(ctx->repository, rid);
		const_chr_t extension = repo->get_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION);
		if (extension != NULL && extension[0] != '\0') {
			const sk_resource_asset_handler_t* handler = NULL;
			if (handler_ext_get(ctx, extension, &handler) == 0 && handler != NULL) {
				return handler;
			}
		}
	}
	sk_type_id_t type_id = resource_type_id_for_type(ctx, type);
	if (!SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
		const sk_resource_asset_handler_t* handler = NULL;
		if (handler_type_get(ctx, type_id, &handler) == 0) {
			return handler;
		}
	}
	return NULL;
}

static const sk_resource_asset_handler_t* get_asset_handler_for_type(sk_resource_assets_context_t* ctx, sk_type_id_t type_id) {
	const sk_resource_asset_handler_t* handler = NULL;
	handler_type_get(ctx, type_id, &handler);
	return handler;
}

static const sk_resource_asset_handler_t* get_asset_handler_for_extension(sk_resource_assets_context_t* ctx, const_chr_t extension) {
	const sk_resource_asset_handler_t* handler = NULL;
	handler_ext_get(ctx, extension, &handler);
	return handler;
}

static const sk_resource_asset_importer_t* get_importer(sk_resource_assets_context_t* ctx, const_chr_t extension) {
	const sk_resource_asset_importer_t* importer = NULL;
	importer_ext_get(ctx, extension, &importer);
	return importer;
}

static i32 get_asset_name(sk_resource_assets_context_t* ctx, sk_rid_t rid, char_ptr_t out, u32 out_cap) {
	if (rid.id == 0u) {
		return 0;
	}
	const sk_repository_api_t* repo = resource_repo_api();
	if (repo->resource_type(ctx->repository, rid) == ctx->directory_type) {
		sk_resource_object_t view = repo->read(ctx->repository, rid);
		rid = repo->get_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
	}

	const sk_resource_asset_handler_t* handler = get_asset_handler(ctx, rid);
	if (handler != NULL && sk_resource_asset_handler_get_asset_name(handler, rid, out, out_cap)) {
		return 1;
	}

	sk_rid_t parent = repo->get_parent(ctx->repository, rid);
	if (parent.id != 0u && repo->resource_type(ctx->repository, parent) == ctx->asset_type && repo->has_value(ctx->repository, parent)) {
		sk_resource_object_t view = repo->read(ctx->repository, parent);
		const_chr_t name = repo->get_string(view, SK_RESOURCE_ASSET_FIELD_NAME);
		const_chr_t extension = repo->get_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION);
		snprintf(out, (size_t)out_cap, "%s%s", name != NULL ? name : "", extension != NULL ? extension : "");
		return 1;
	}

	sk_resource_object_t view = repo->read(ctx->repository, rid);
	const_chr_t field0 = repo->get_string(view, 0u);
	if (field0 != NULL) {
		snprintf(out, (size_t)out_cap, "%s", field0);
		return 1;
	}
	return 0;
}

static i32 get_asset_full_name(sk_resource_assets_context_t* ctx, sk_rid_t rid, char_ptr_t out, u32 out_cap) {
	if (!get_asset_name(ctx, rid, out, out_cap)) {
		return 0;
	}
	const sk_resource_asset_handler_t* handler = get_asset_handler(ctx, rid);
	if (handler != NULL && sk_resource_asset_handler_extension(handler) != NULL) {
		size_t len = strlen(out);
		size_t ext_len = strlen(sk_resource_asset_handler_extension(handler));
		if (len + ext_len + 1u <= out_cap) {
			memcpy(out + len, sk_resource_asset_handler_extension(handler), ext_len);
			out[len + ext_len] = '\0';
		}
	}
	return 1;
}

static sk_rid_t find_asset_on_directory(sk_resource_assets_context_t* ctx, sk_rid_t directory, sk_type_id_t type_id, const_chr_t name) {
	if (directory.id == 0u) {
		return SK_RID_ZERO;
	}
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_resource_asset_handler_t* handler = get_asset_handler_for_type(ctx, type_id);
	char full_name[256];
	if (handler != NULL && sk_resource_asset_handler_extension(handler) != NULL) {
		snprintf(full_name, sizeof(full_name), "%s%s", name != NULL ? name : "", sk_resource_asset_handler_extension(handler));
	} else {
		snprintf(full_name, sizeof(full_name), "%s", name != NULL ? name : "");
	}

	sk_resource_object_t directory_view = repo->read(ctx->repository, directory);
	u32 count = 0u;
	const sk_rid_t* children = repo->get_subobject_list(directory_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	for (u32 i = 0u; i < count; ++i) {
		char child_name[256];
		if (get_asset_full_name(ctx, children[i], child_name, (u32)sizeof(child_name)) && strcmp(child_name, full_name) == 0) {
			sk_resource_object_t view = repo->read(ctx->repository, children[i]);
			return repo->get_subobject(view, SK_RESOURCE_ASSET_FIELD_OBJECT);
		}
	}
	return SK_RID_ZERO;
}

static void open_asset(sk_resource_assets_context_t* ctx, sk_rid_t rid) {
	const sk_resource_asset_handler_t* handler = get_asset_handler(ctx, rid);
	if (handler != NULL) {
		sk_resource_asset_handler_open_asset(handler, rid);
	}
}

/* ------------------------------------------------------------------ */
/*  By-type asset index                                                */
/* ------------------------------------------------------------------ */

static resource_asset_list_t* asset_list_for_type(sk_resource_assets_context_t* ctx, sk_type_id_t type_id) {
	resource_asset_list_t* list = (resource_asset_list_t*)sk_hash_map_get_ptr_(&ctx->assets_by_type._hm, &type_id);
	if (list == NULL) {
		resource_asset_list_t empty;
		memset(&empty, 0, sizeof(empty));
		if (sk_hash_map_put_(&ctx->assets_by_type._hm, &type_id, &empty) != 0) {
			return NULL;
		}
		list = (resource_asset_list_t*)sk_hash_map_get_ptr_(&ctx->assets_by_type._hm, &type_id);
		if (list == NULL) {
			return NULL;
		}
		sk_array_init(list, ctx->allocator);
	} else if (list->allocator == NULL) {
		sk_array_init(list, ctx->allocator);
	}
	return list;
}

static void register_asset_by_type(sk_resource_assets_context_t* ctx, sk_rid_t asset) {
	if (asset.id == 0u) {
		return;
	}
	sk_type_id_t type_id = resource_type_of(ctx, asset);
	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
		return;
	}
	resource_asset_list_t* list = asset_list_for_type(ctx, type_id);
	if (list == NULL) {
		return;
	}
	for (u32 i = 0u; i < list->count; ++i) {
		if (SK_RID_EQ(list->items[i], asset)) {
			return;
		}
	}
	sk_array_push(list, asset);
}

static void unregister_asset_by_type(sk_resource_assets_context_t* ctx, sk_rid_t asset) {
	if (asset.id == 0u) {
		return;
	}
	sk_type_id_t type_id = resource_type_of(ctx, asset);
	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
		return;
	}
	resource_asset_list_t* list = (resource_asset_list_t*)sk_hash_map_get_ptr_(&ctx->assets_by_type._hm, &type_id);
	if (list == NULL) {
		return;
	}
	for (u32 i = 0u; i < list->count; ++i) {
		if (SK_RID_EQ(list->items[i], asset)) {
			sk_array_swap_remove(list, i);
			return;
		}
	}
}

static u32 get_assets(sk_resource_assets_context_t* ctx, sk_type_id_t type_id, sk_rid_t* out, u32 out_cap) {
	resource_asset_list_t* list = (resource_asset_list_t*)sk_hash_map_get_ptr_(&ctx->assets_by_type._hm, &type_id);
	if (list == NULL) {
		return 0u;
	}
	u32 copy = (list->count < out_cap) ? list->count : out_cap;
	if (out != NULL && copy > 0u) {
		memcpy(out, list->items, (size_t)copy * sizeof(sk_rid_t));
	}
	return list->count;
}

/* ------------------------------------------------------------------ */
/*  Import: importer dispatch + generic ingest / cook                  */
/* ------------------------------------------------------------------ */

typedef struct sk_ingest_decl_t {
	char* sub_id;
	sk_uuid_t uuid;
	sk_type_id_t type;
} sk_ingest_decl_t;

typedef struct sk_ingest_dep_t {
	char* rel_path;
	u8* bytes;
	u32 size;
} sk_ingest_dep_t;

typedef struct ingest_runtime_t {
	sk_resource_ingest_context_t base;
	SK_ARRAY(sk_ingest_decl_t) decls;
	SK_ARRAY(sk_ingest_dep_t) deps;
	const sk_allocator_t* allocator;
	sk_uuid_t base_uuid;
} ingest_runtime_t;

static sk_uuid_t ingest_declare_sub_resource(sk_resource_ingest_context_t* ctx, const_chr_t sub_id, sk_type_id_t type) {
	ingest_runtime_t* rt = (ingest_runtime_t*)ctx;
	for (u32 i = 0u; i < rt->decls.count; ++i) {
		if (strcmp(rt->decls.items[i].sub_id, sub_id) == 0) {
			return rt->decls.items[i].uuid;
		}
	}
	sk_uuid_t uuid = sub_resource_uuid(rt->base_uuid, sub_id);
	size_t len = strlen(sub_id);
	char* copy = (char*)rt->allocator->alloc(rt->allocator->instance, len + 1u);
	if (copy == NULL) {
		return SK_UUID_ZERO;
	}
	memcpy(copy, sub_id, len + 1u);
	sk_ingest_decl_t decl;
	decl.sub_id = copy;
	decl.uuid = uuid;
	decl.type = type;
	sk_array_push(&rt->decls, decl);
	return uuid;
}

static i32 ingest_add_dependency(sk_resource_ingest_context_t* ctx, const_chr_t rel_path, const u8* bytes, u32 size) {
	ingest_runtime_t* rt = (ingest_runtime_t*)ctx;
	for (u32 i = 0u; i < rt->deps.count; ++i) {
		if (strcmp(rt->deps.items[i].rel_path, rel_path) == 0) {
			return -1;
		}
	}
	size_t len = strlen(rel_path);
	char* path_copy = (char*)rt->allocator->alloc(rt->allocator->instance, len + 1u);
	u8* byte_copy = NULL;
	if (size > 0u) {
		byte_copy = (u8*)rt->allocator->alloc(rt->allocator->instance, size);
	}
	if (path_copy == NULL || (size > 0u && byte_copy == NULL)) {
		if (path_copy != NULL) {
			rt->allocator->free(rt->allocator->instance, path_copy);
		}
		return -1;
	}
	memcpy(path_copy, rel_path, len + 1u);
	if (size > 0u) {
		memcpy(byte_copy, bytes, size);
	}
	sk_ingest_dep_t dep;
	dep.rel_path = path_copy;
	dep.bytes = byte_copy;
	dep.size = size;
	sk_array_push(&rt->deps, dep);
	return 0;
}

static i32 ingest_has_dependency(sk_resource_ingest_context_t* ctx, const_chr_t rel_path) {
	ingest_runtime_t* rt = (ingest_runtime_t*)ctx;
	for (u32 i = 0u; i < rt->deps.count; ++i) {
		if (strcmp(rt->deps.items[i].rel_path, rel_path) == 0) {
			return 1;
		}
	}
	return 0;
}

static void ingest_runtime_free(ingest_runtime_t* rt) {
	for (u32 i = 0u; i < rt->decls.count; ++i) {
		rt->allocator->free(rt->allocator->instance, rt->decls.items[i].sub_id);
	}
	for (u32 i = 0u; i < rt->deps.count; ++i) {
		rt->allocator->free(rt->allocator->instance, rt->deps.items[i].rel_path);
		if (rt->deps.items[i].bytes != NULL) {
			rt->allocator->free(rt->allocator->instance, rt->deps.items[i].bytes);
		}
	}
	sk_array_free(&rt->decls);
	sk_array_free(&rt->deps);
}

static void compute_import_content_hash(const u8* source, u32 source_size, const ingest_runtime_t* rt, char_ptr_t out, u32 out_cap) {
	u64 acc = sk_hash_bytes(source, (size_t)source_size);
	for (u32 i = 0u; i < rt->deps.count; ++i) {
		acc = acc * 31ull + sk_hash_bytes(rt->deps.items[i].rel_path, strlen(rt->deps.items[i].rel_path));
		acc = acc * 31ull + sk_hash_bytes(rt->deps.items[i].bytes, (size_t)rt->deps.items[i].size);
	}
	snprintf(out, (size_t)out_cap, "%016llx", acc);
}

static void apply_ingest(sk_resource_assets_context_t* ctx, ingest_runtime_t* rt, sk_rid_t wrapper, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	resource_asset_list_t dependency_rids;
	resource_asset_list_t sub_resource_rids;
	sk_array_init(&dependency_rids, ctx->allocator);
	sk_array_init(&sub_resource_rids, ctx->allocator);

	for (u32 i = 0u; i < rt->deps.count; ++i) {
		sk_rid_t entry = resource_create(ctx, ctx->dependency_entry_type, scope);
		if (entry.id == 0u) {
			continue;
		}
		sk_resource_object_t view = repo->write(ctx->repository, entry);
		repo->set_string(view, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH, rt->deps.items[i].rel_path);
		repo->set_uint(view, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE, rt->deps.items[i].size);
		repo->commit(view, scope);
		sk_array_push(&dependency_rids, entry);
	}

	for (u32 i = 0u; i < rt->decls.count; ++i) {
		sk_rid_t entry = resource_create(ctx, ctx->sub_id_entry_type, scope);
		if (entry.id == 0u) {
			continue;
		}
		char uuid_text[64];
		uuid_to_string(rt->decls.items[i].uuid, uuid_text, (u32)sizeof(uuid_text));
		sk_resource_object_t view = repo->write(ctx->repository, entry);
		repo->set_string(view, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID, rt->decls.items[i].sub_id);
		repo->set_string(view, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID, uuid_text);
		repo->commit(view, scope);
		sk_array_push(&sub_resource_rids, entry);
	}

	{
		char hash[64];
		compute_import_content_hash(rt->base.source_bytes, rt->base.source_size, rt, hash, (u32)sizeof(hash));
		sk_resource_object_t view = repo->write(ctx->repository, wrapper);
		repo->set_string(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH, hash);
		for (u32 i = 0u; i < dependency_rids.count; ++i) {
			repo->add_to_subobject_list(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, dependency_rids.items[i]);
		}
		for (u32 i = 0u; i < sub_resource_rids.count; ++i) {
			repo->add_to_subobject_list(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, sub_resource_rids.items[i]);
		}
		repo->commit(view, scope);
	}

	sk_array_free(&dependency_rids);
	sk_array_free(&sub_resource_rids);
}

typedef struct cook_runtime_t {
	sk_resource_cook_context_t base;
	resource_asset_list_t produced;
	sk_resource_assets_context_t* engine;
	sk_uuid_t base_uuid;
} cook_runtime_t;

static sk_rid_t cook_sub_resource(sk_resource_cook_context_t* ctx, const_chr_t sub_id, sk_type_id_t type) {
	cook_runtime_t* rt = (cook_runtime_t*)ctx;
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_resource_type_t* type_ptr = repo->find_type(rt->engine->repository, type);
	if (type_ptr == NULL) {
		return SK_RID_ZERO;
	}
	sk_uuid_t uuid = sub_resource_uuid(rt->base_uuid, sub_id);
	sk_rid_t rid = repo->create_resource(rt->engine->repository, type_ptr, uuid, rt->base.scope);
	if (rid.id == 0u) {
		return SK_RID_ZERO;
	}
	for (u32 i = 0u; i < rt->produced.count; ++i) {
		if (SK_RID_EQ(rt->produced.items[i], rid)) {
			return rid;
		}
	}
	sk_array_push(&rt->produced, rid);
	return rid;
}

static sk_rid_t sub_allocator_create(const sk_sub_resource_allocator_t* allocator, const_chr_t sub_id, sk_type_id_t type) {
	sk_resource_assets_context_t* engine = (sk_resource_assets_context_t*)allocator->engine;
	if (engine == NULL) {
		return SK_RID_ZERO;
	}
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_resource_type_t* type_ptr = repo->find_type(engine->repository, type);
	if (type_ptr == NULL) {
		return SK_RID_ZERO;
	}
	sk_uuid_t base = repo->resource_uuid(engine->repository, allocator->imported_asset);
	sk_uuid_t uuid = sub_resource_uuid(base, sub_id);
	return repo->create_resource(engine->repository, type_ptr, uuid, allocator->scope);
}

static sk_sub_resource_allocator_t cook_allocator(sk_resource_cook_context_t* ctx) {
	cook_runtime_t* rt = (cook_runtime_t*)ctx;
	sk_sub_resource_allocator_t allocator;
	memset(&allocator, 0, sizeof(allocator));
	allocator.imported_asset = rt->base.imported_asset;
	allocator.scope = rt->base.scope;
	allocator.engine = rt->engine;
	allocator.create = sub_allocator_create;
	return allocator;
}

static sk_rid_t create_imported_asset_wrapper(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t desired_name, const_chr_t extension, sk_uuid_t uuid,
											  sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	const_chr_t base_name = (desired_name != NULL && desired_name[0] != '\0') ? desired_name : "New Asset";
	const_chr_t ext = (extension != NULL) ? extension : "";
	char name[256];
	if (create_unique_asset_name(ctx, parent, base_name, ext, 0, name, (u32)sizeof(name)) < 0) {
		return SK_RID_ZERO;
	}
	char path[SK_FS_PATH_MAX];
	if (join_asset_path(ctx, parent, name, ext, path, (u32)sizeof(path)) < 0) {
		return SK_RID_ZERO;
	}

	sk_rid_t wrapper = repo->create_resource(ctx->repository, ctx->imported_asset_type, uuid, scope);
	sk_rid_t rid = resource_create(ctx, ctx->asset_type, scope);
	if (wrapper.id == 0u || rid.id == 0u) {
		return SK_RID_ZERO;
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, rid);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, name);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, ext);
		if (repo->set_subobject(view, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET, wrapper) != 0) {
			repo->discard(view);
			return SK_RID_ZERO;
		}
		repo->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, parent);
		repo->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, path);
		repo->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0);
		repo->commit(view, scope);
	}
	{
		sk_resource_object_t view = repo->write(ctx->repository, parent);
		repo->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, rid);
		repo->commit(view, scope);
	}
	return wrapper;
}

static i32 import_generic(sk_resource_assets_context_t* ctx, const sk_resource_asset_importer_t* importer, sk_rid_t parent, const_chr_t path, sk_undo_redo_scope_t* scope) {
	const sk_repository_api_t* repo = resource_repo_api();
	const sk_filesystem_api_t* fs = sk_filesystem_api();

	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		return -1;
	}
	u64 size_u64 = fs->get_file_size(file);
	if (size_u64 > 0xFFFFFFFFull) {
		fs->close_file(file);
		return -1;
	}
	size_t size = size_u64;
	u8* bytes = NULL;
	if (size > 0u) {
		bytes = (u8*)ctx->allocator->alloc(ctx->allocator->instance, size);
		if (bytes == NULL) {
			fs->close_file(file);
			return -1;
		}
		fs->read_file(file, bytes, size);
	}
	fs->close_file(file);

	char base_name[256];
	if (sk_path_name(sk_str_view_cstr(path), base_name, (u32)sizeof(base_name)) < 0) {
		base_name[0] = '\0';
	}
	const_chr_t output_extension = (importer->output_extension != NULL ? importer->output_extension(importer->user_data) : NULL);
	sk_str_view_t src_ext = sk_path_extension(sk_str_view_cstr(path));
	const_chr_t wrapper_extension = (output_extension != NULL && output_extension[0] != '\0') ? output_extension : (src_ext.size > 0u ? src_ext.data : "");

	sk_uuid_t wrapper_uuid = path_uuid(path);
	sk_rid_t wrapper = create_imported_asset_wrapper(ctx, parent, base_name, wrapper_extension, wrapper_uuid, scope);
	if (wrapper.id == 0u) {
		ctx->allocator->free(ctx->allocator->instance, bytes);
		return -1;
	}

	{
		sk_resource_object_t view = repo->write(ctx->repository, wrapper);
		repo->set_string(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME, base_name);
		repo->set_string(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION, wrapper_extension);
		repo->set_uint(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION, (importer->cooker_version != NULL ? importer->cooker_version(importer->user_data) : 1u));
		repo->set_uint(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE, size_u64);
		repo->commit(view, scope);
	}

	ingest_runtime_t rt;
	memset(&rt, 0, sizeof(rt));
	rt.base.imported_asset = wrapper;
	rt.base.source_path = path;
	rt.base.source_bytes = bytes;
	rt.base.source_size = (u32)size_u64;
	rt.base.scope = scope;
	rt.base.declare_sub_resource = ingest_declare_sub_resource;
	rt.base.add_dependency = ingest_add_dependency;
	rt.base.has_dependency = ingest_has_dependency;
	sk_array_init(&rt.decls, ctx->allocator);
	sk_array_init(&rt.deps, ctx->allocator);
	rt.allocator = ctx->allocator;
	rt.base_uuid = wrapper_uuid;

	if (importer->ingest != NULL) {
		importer->ingest(importer->user_data, &rt.base);
	}
	apply_ingest(ctx, &rt, wrapper, scope);
	ingest_runtime_free(&rt);

	if (importer->cook != NULL) {
		cook_runtime_t cr;
		memset(&cr, 0, sizeof(cr));
		cr.base.imported_asset = wrapper;
		cr.base.import_settings = SK_RID_ZERO;
		cr.base.source_bytes = bytes;
		cr.base.source_size = (u32)size_u64;
		cr.base.scope = scope;
		cr.base.sub_resource = cook_sub_resource;
		cr.base.allocator = cook_allocator;
		sk_array_init(&cr.produced, ctx->allocator);
		cr.engine = ctx;
		cr.base_uuid = wrapper_uuid;
		importer->cook(importer->user_data, &cr.base);
		sk_array_free(&cr.produced);
	}

	ctx->allocator->free(ctx->allocator->instance, bytes);
	return 0;
}

static i32 import_single(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t path, sk_undo_redo_scope_t* scope) {
	sk_str_view_t ext_view = sk_path_extension(sk_str_view_cstr(path));
	char extension[64];
	extension[0] = '\0';
	if (ext_view.size > 0u) {
		size_t ext_len = (size_t)ext_view.size;
		if (ext_len >= sizeof(extension)) {
			ext_len = sizeof(extension) - 1u;
		}
		for (size_t i = 0u; i < ext_len; ++i) {
			char c = ext_view.data[i];
			/* Avoid int→char narrowing (clang-tidy cppcoreguidelines-narrowing-conversions). */
			if (c >= 'A' && c <= 'Z') {
				extension[i] = "abcdefghijklmnopqrstuvwxyz"[c - 'A'];
			} else {
				extension[i] = c;
			}
		}
		extension[ext_len] = '\0';
	}

	const sk_resource_asset_importer_t* importer = get_importer(ctx, extension);
	if (importer == NULL) {
		return 0; /* unsupported extension: skip */
	}
	if (importer->import_asset != NULL) {
		return importer->import_asset(importer->user_data, parent, NULL, path, scope);
	}
	return import_generic(ctx, importer, parent, path, scope);
}

static i32 import_asset(sk_resource_assets_context_t* ctx, sk_rid_t parent, const_chr_t path, sk_undo_redo_scope_t* scope) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	SK_ARRAY(import_path_t) queue;
	sk_array_init(&queue, ctx->allocator);

	import_path_t root;
	memset(&root, 0, sizeof(root));
	strncpy(root.path, path, sizeof(root.path) - 1u);
	sk_array_push(&queue, root);

	i32 result = 0;
	for (u32 head = 0u; head < queue.count; ++head) {
		const_chr_t current = queue.items[head].path;
		sk_file_status_t status = fs->get_file_status(current);
		if (status == SK_FILE_STATUS_DIRECTORY) {
			sk_directory_iterator_t it = fs->open_directory(current);
			if (it == NULL) {
				continue;
			}
			char name[SK_FS_PATH_MAX];
			while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
				if (name[0] == '\0' || name[0] == '.') {
					continue;
				}
				import_path_t item;
				memset(&item, 0, sizeof(item));
				if (sk_path_join(sk_str_view_cstr(current), sk_str_view_cstr(name), item.path, (u32)sizeof(item.path)) >= 0) {
					sk_array_push(&queue, item);
				}
			}
			fs->close_directory(it);
			continue;
		}
		if (status != SK_FILE_STATUS_FILE) {
			continue;
		}
		if (import_single(ctx, parent, current, scope) != 0) {
			result = -1;
		}
	}
	sk_array_free(&queue);
	return result;
}

/* ------------------------------------------------------------------ */
/*  Module table                                                       */
/* ------------------------------------------------------------------ */

static sk_rid_t get_package(const sk_resource_assets_context_t* ctx) {
	return ctx->package;
}

static sk_rid_t get_root_directory(const sk_resource_assets_context_t* ctx) {
	return ctx->root_directory;
}

SK_API const sk_resource_assets_api_t* sk_resource_assets_api(void) {
	static const sk_resource_assets_api_t api = {
		resource_assets_create,
		resource_assets_destroy,
		reload_handlers,
		scan_package_from_directory,
		get_package,
		get_root_directory,
		create_asset,
		create_asset_directory,
		create_asset_file,
		move_asset,
		get_absolute_path,
		get_path_id,
		get_absolute_path_from_path_id,
		get_parent_asset,
		is_child_of,
		get_asset_payload,
		get_asset_handler,
		get_asset_handler_for_type,
		get_asset_handler_for_extension,
		get_importer,
		import_asset,
		open_asset,
		register_asset_by_type,
		unregister_asset_by_type,
		get_assets,
		create_unique_asset_name,
		get_asset_name,
		get_asset_full_name,
		find_asset_on_directory,
	};
	return &api;
}

#ifdef SK_TESTS

#include "test.h"
#include "unity.h"

#include "test.h"
#include "unity.h"

SK_TEST(resource_asset_handler_type_id_nonzero) {
	sk_type_id_t id = SK_RESOURCE_ASSET_HANDLER_TYPE_ID;
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
	sk_type_id_t expected = SK_TYPE_ID("sk.resource_asset_handler", 0x3bf713f497383666ULL, 0x74e3f6b8c318e3e9ULL);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, expected));
}

SK_TEST(resource_asset_importer_type_id_nonzero) {
	sk_type_id_t id = SK_RESOURCE_ASSET_IMPORTER_TYPE_ID;
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
	sk_type_id_t expected = SK_TYPE_ID("sk.resource_asset_importer", 0xa0cc8513be01820dULL, 0x190f2cb78426a105ULL);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, expected));
}

SK_TEST(resource_asset_handler_importer_type_ids_distinct) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_RESOURCE_ASSET_HANDLER_TYPE_ID, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID));
}

/* Compile-time shape checks: tables are plain aggregates usable as static
 * initializers and as add_impl payloads. */
SK_TEST(resource_asset_handler_static_init_shape) {
	static sk_resource_asset_handler_t handler = {
		.user_data = NULL,
		.extension = NULL,
		.open_asset = NULL,
		.get_resource_type_id = NULL,
		.get_desc = NULL,
		.load = NULL,
		.save = NULL,
		.create = NULL,
		.reloaded = NULL,
		.after_move = NULL,
		.export_object = NULL,
		.get_icon = NULL,
		.get_load_order = NULL,
		.get_asset_name = NULL,
	};
	const void* as_impl = &handler;
	TEST_ASSERT_NOT_NULL(as_impl);
	TEST_ASSERT_NULL(handler.user_data);
}

SK_TEST(resource_asset_importer_static_init_shape) {
	static sk_resource_asset_importer_t importer = {
		.user_data = NULL,
		.imported_extensions = NULL,
		.output_extension = NULL,
		.cooker_version = NULL,
		.get_settings_type = NULL,
		.ingest = NULL,
		.cook = NULL,
		.import_asset = NULL,
	};
	const void* as_impl = &importer;
	TEST_ASSERT_NOT_NULL(as_impl);
	TEST_ASSERT_NULL(importer.user_data);
}

/* ---- Dummy handler call tracking for end-to-end registration tests ---- */

typedef struct dummy_handler_state_t {
	u32 extension_calls;
	u32 open_asset_calls;
	u32 get_resource_type_id_calls;
	u32 get_desc_calls;
	u32 load_calls;
	u32 save_calls;
	u32 create_calls;
	u32 reloaded_calls;
	u32 after_move_calls;
	u32 export_object_calls;
	u32 get_icon_calls;
	u32 get_load_order_calls;
	u32 get_asset_name_calls;
	sk_rid_t last_asset;
	sk_rid_t last_object;
	sk_uuid_t last_uuid;
	const_chr_t last_path;
	const_chr_t last_old_path;
	const_chr_t last_new_path;
	sk_archive_writer_t* last_writer;
	sk_undo_redo_scope_t* last_scope;
	char last_name_buf[64];
	u32 last_name_cap;
} dummy_handler_state_t;

/* Local type id for the dummy handler (not a file-scope compound literal). */
#define DUMMY_RESOURCE_TYPE_ID SK_TYPE_ID("test.dummy_resource", 0xaaaaaaaaaaaaaaaaULL, 0xbbbbbbbbbbbbbbbbULL)

static const_chr_t dummy_extension(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->extension_calls += 1u;
	return ".dummy";
}

static void dummy_open_asset(void_ptr_t user_data, sk_rid_t asset) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->open_asset_calls += 1u;
	s->last_asset = asset;
}

static sk_type_id_t dummy_get_resource_type_id(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_resource_type_id_calls += 1u;
	return DUMMY_RESOURCE_TYPE_ID;
}

static const_chr_t dummy_get_desc(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_desc_calls += 1u;
	return "Dummy Asset";
}

static sk_rid_t dummy_load(void_ptr_t user_data, sk_rid_t asset, const_chr_t absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->load_calls += 1u;
	s->last_asset = asset;
	s->last_path = absolute_path;
	return (sk_rid_t){42ull};
}

static void dummy_save(void_ptr_t user_data, sk_rid_t object, const_chr_t absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->save_calls += 1u;
	s->last_object = object;
	s->last_path = absolute_path;
}

static sk_rid_t dummy_create(void_ptr_t user_data, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->create_calls += 1u;
	s->last_uuid = uuid;
	s->last_scope = scope;
	return (sk_rid_t){7ull};
}

static void dummy_reloaded(void_ptr_t user_data, sk_rid_t asset, const_chr_t absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->reloaded_calls += 1u;
	s->last_asset = asset;
	s->last_path = absolute_path;
}

static void dummy_after_move(void_ptr_t user_data, sk_rid_t asset, const_chr_t old_absolute_path, const_chr_t new_absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->after_move_calls += 1u;
	s->last_asset = asset;
	s->last_old_path = old_absolute_path;
	s->last_new_path = new_absolute_path;
}

static void dummy_export_object(void_ptr_t user_data, sk_rid_t object, sk_archive_writer_t* writer) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->export_object_calls += 1u;
	s->last_object = object;
	s->last_writer = writer;
}

static const_chr_t dummy_get_icon(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_icon_calls += 1u;
	return "icon-dummy";
}

static i32 dummy_get_load_order(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_load_order_calls += 1u;
	return 10;
}

static i32 dummy_get_asset_name(void_ptr_t user_data, sk_rid_t rid, char* out_name, u32 out_cap) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_asset_name_calls += 1u;
	s->last_asset = rid;
	s->last_name_cap = out_cap;
	if (out_name != NULL && out_cap > 0u) {
		const char* name = "dummy-name";
		size_t len = strlen(name);
		if (len + 1u > (size_t)out_cap) {
			len = (size_t)out_cap - 1u;
		}
		memcpy(out_name, name, len);
		out_name[len] = '\0';
		memcpy(s->last_name_buf, out_name, len + 1u);
	}
	return 1;
}

/**
 * End-to-end: register a static dummy handler via add_impl, resolve by
 * extension, and assert every function pointer is invoked through null-safe
 * dispatch. Format-independent (no real asset I/O).
 */
SK_TEST(resource_asset_handler_add_impl_lookup_and_dispatch) {
	static dummy_handler_state_t state;
	static sk_resource_asset_handler_t handler;

	sk_app_context_t* ctx;
	const sk_app_api_t* api;
	const sk_resource_asset_handler_t* found;
	const sk_resource_asset_handler_t* found_by_type;
	const sk_resource_asset_handler_t* all[4];
	sk_rid_t load_rid;
	sk_rid_t create_rid;
	sk_rid_t probe = {99ull};
	sk_uuid_t uuid = {0x1111111111111111ULL, 0x2222222222222222ULL};
	sk_archive_writer_t writer;
	char name_buf[32];
	i32 name_ok;

	memset(&state, 0, sizeof(state));
	memset(&handler, 0, sizeof(handler));
	memset(&writer, 0, sizeof(writer));

	handler.user_data = &state;
	handler.extension = dummy_extension;
	handler.open_asset = dummy_open_asset;
	handler.get_resource_type_id = dummy_get_resource_type_id;
	handler.get_desc = dummy_get_desc;
	handler.load = dummy_load;
	handler.save = dummy_save;
	handler.create = dummy_create;
	handler.reloaded = dummy_reloaded;
	handler.after_move = dummy_after_move;
	handler.export_object = dummy_export_object;
	handler.get_icon = dummy_get_icon;
	handler.get_load_order = dummy_get_load_order;
	handler.get_asset_name = dummy_get_asset_name;

	ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	api = sk_app_api();
	TEST_ASSERT_NOT_NULL(api);

	TEST_ASSERT_EQUAL_UINT32(0u, sk_resource_asset_handler_count(ctx, api));
	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_extension(ctx, api, ".dummy"));

	api->add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &handler);

	TEST_ASSERT_EQUAL_UINT32(1u, sk_resource_asset_handler_count(ctx, api));
	TEST_ASSERT_EQUAL_UINT32(1u, sk_resource_asset_handler_get_all(ctx, api, all, 4u));
	TEST_ASSERT_EQUAL_PTR(&handler, all[0]);

	found = sk_resource_asset_handler_find_by_extension(ctx, api, ".dummy");
	TEST_ASSERT_EQUAL_PTR(&handler, found);
	TEST_ASSERT_TRUE(state.extension_calls >= 1u);

	found_by_type = sk_resource_asset_handler_find_by_resource_type(ctx, api, DUMMY_RESOURCE_TYPE_ID);
	TEST_ASSERT_EQUAL_PTR(&handler, found_by_type);
	TEST_ASSERT_TRUE(state.get_resource_type_id_calls >= 1u);

	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_extension(ctx, api, ".missing"));
	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_resource_type(ctx, api, SK_TYPE_ID_ZERO));

	/* Dispatch every entry through the null-safe free functions. */
	TEST_ASSERT_EQUAL_STRING(".dummy", sk_resource_asset_handler_extension(found));
	TEST_ASSERT_EQUAL_STRING("Dummy Asset", sk_resource_asset_handler_get_desc(found));
	TEST_ASSERT_EQUAL_STRING("icon-dummy", sk_resource_asset_handler_get_icon(found));
	TEST_ASSERT_EQUAL_INT(10, sk_resource_asset_handler_get_load_order(found));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_asset_handler_get_resource_type_id(found), DUMMY_RESOURCE_TYPE_ID));

	sk_resource_asset_handler_open_asset(found, probe);
	TEST_ASSERT_EQUAL_UINT32(1u, state.open_asset_calls);
	TEST_ASSERT_TRUE(SK_RID_EQ(state.last_asset, probe));

	load_rid = sk_resource_asset_handler_load(found, probe, "/tmp/x.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.load_calls);
	TEST_ASSERT_EQUAL_UINT64(42ull, load_rid.id);
	TEST_ASSERT_EQUAL_STRING("/tmp/x.dummy", state.last_path);

	sk_resource_asset_handler_save(found, (sk_rid_t){3ull}, "/tmp/y.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.save_calls);
	TEST_ASSERT_EQUAL_UINT64(3ull, state.last_object.id);

	create_rid = sk_resource_asset_handler_create(found, uuid, NULL);
	TEST_ASSERT_EQUAL_UINT32(1u, state.create_calls);
	TEST_ASSERT_EQUAL_UINT64(7ull, create_rid.id);
	TEST_ASSERT_TRUE(SK_UUID_EQ(state.last_uuid, uuid));
	TEST_ASSERT_NULL(state.last_scope);

	sk_resource_asset_handler_reloaded(found, probe, "/tmp/z.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.reloaded_calls);

	sk_resource_asset_handler_after_move(found, probe, "/old.dummy", "/new.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.after_move_calls);
	TEST_ASSERT_EQUAL_STRING("/old.dummy", state.last_old_path);
	TEST_ASSERT_EQUAL_STRING("/new.dummy", state.last_new_path);

	sk_resource_asset_handler_export_object(found, (sk_rid_t){5ull}, &writer);
	TEST_ASSERT_EQUAL_UINT32(1u, state.export_object_calls);
	TEST_ASSERT_EQUAL_PTR(&writer, state.last_writer);

	name_ok = sk_resource_asset_handler_get_asset_name(found, probe, name_buf, (u32)sizeof(name_buf));
	TEST_ASSERT_EQUAL_INT(1, name_ok);
	TEST_ASSERT_EQUAL_STRING("dummy-name", name_buf);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_asset_name_calls);

	/* Every fp must have been invoked at least once (lookup + dispatch). */
	TEST_ASSERT_TRUE(state.extension_calls >= 1u);
	TEST_ASSERT_EQUAL_UINT32(1u, state.open_asset_calls);
	TEST_ASSERT_TRUE(state.get_resource_type_id_calls >= 1u);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_desc_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.load_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.save_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.create_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.reloaded_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.after_move_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.export_object_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_icon_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_load_order_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_asset_name_calls);

	api->remove_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &handler);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_resource_asset_handler_count(ctx, api));
	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_extension(ctx, api, ".dummy"));

	sk_app_destroy(ctx);
}

/**
 * NULL function pointers must not crash: defaults / no-ops only.
 */
SK_TEST(resource_asset_handler_null_fp_dispatch_defaults) {
	static sk_resource_asset_handler_t empty = {
		.user_data = NULL,
		.extension = NULL,
		.open_asset = NULL,
		.get_resource_type_id = NULL,
		.get_desc = NULL,
		.load = NULL,
		.save = NULL,
		.create = NULL,
		.reloaded = NULL,
		.after_move = NULL,
		.export_object = NULL,
		.get_icon = NULL,
		.get_load_order = NULL,
		.get_asset_name = NULL,
	};
	sk_rid_t rid = {1ull};
	sk_uuid_t uuid = SK_UUID_ZERO;
	sk_archive_writer_t writer;
	char name[8];

	memset(&writer, 0, sizeof(writer));
	name[0] = 'x';

	TEST_ASSERT_EQUAL_STRING("", sk_resource_asset_handler_extension(&empty));
	TEST_ASSERT_EQUAL_STRING("", sk_resource_asset_handler_get_desc(&empty));
	TEST_ASSERT_EQUAL_STRING("", sk_resource_asset_handler_get_icon(&empty));
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_ASSET_HANDLER_DEFAULT_LOAD_ORDER, sk_resource_asset_handler_get_load_order(&empty));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_asset_handler_get_resource_type_id(&empty), SK_TYPE_ID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(sk_resource_asset_handler_load(&empty, rid, "/x"), SK_RID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(sk_resource_asset_handler_create(&empty, uuid, NULL), SK_RID_ZERO));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_handler_get_asset_name(&empty, rid, name, (u32)sizeof(name)));

	/* No-ops — must not crash. */
	sk_resource_asset_handler_open_asset(&empty, rid);
	sk_resource_asset_handler_save(&empty, rid, "/x");
	sk_resource_asset_handler_reloaded(&empty, rid, "/x");
	sk_resource_asset_handler_after_move(&empty, rid, "/a", "/b");
	sk_resource_asset_handler_export_object(&empty, rid, &writer);
}

/* ---- Engine tests: fixture package scan + import pipeline ---- */

#include "resource_assets_types.h"
#include "filesystem.h"
#include "path.h"
#include "allocator.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RA_TEST_MATERIAL_TYPE_LO 0x9123abcd9123abcdULL
#define RA_TEST_MATERIAL_TYPE_HI 0x4567ef014567ef01ULL
#define RA_TEST_MATERIAL_TYPE SK_TYPE_ID("sk.ra_test_material", RA_TEST_MATERIAL_TYPE_LO, RA_TEST_MATERIAL_TYPE_HI)

typedef struct ra_test_resource_t {
	sk_field_string_t name;
} ra_test_resource_t;

static const sk_resource_field_t ra_test_resource_fields[] = {
	{"Name", 0u, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(ra_test_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
};

static const sk_resource_type_desc_t ra_test_resource_desc = {
	{RA_TEST_MATERIAL_TYPE_LO, RA_TEST_MATERIAL_TYPE_HI}, "RaTestResource", (u32)sizeof(ra_test_resource_t), ra_test_resource_fields, 1u, NULL,
};

static sk_repository_t* ra_test_payload_repo = NULL;
static i32 ra_test_direct_import_count = 0;

static const_chr_t ra_test_ext(void_ptr_t user_data) {
	(void)user_data;
	return ".testmat";
}

static sk_type_id_t ra_test_type(void_ptr_t user_data) {
	(void)user_data;
	return RA_TEST_MATERIAL_TYPE;
}

static const_chr_t ra_test_desc(void_ptr_t user_data) {
	(void)user_data;
	return "Test Material";
}

static sk_rid_t ra_test_load(void_ptr_t user_data, sk_rid_t asset, const_chr_t path) {
	(void)user_data;
	(void)path;
	return asset;
}

static void ra_test_save(void_ptr_t user_data, sk_rid_t object, const_chr_t path) {
	(void)user_data;
	(void)object;
	(void)path;
}

static sk_rid_t ra_test_create(void_ptr_t user_data, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	const sk_repository_api_t* repo = sk_repository_api();
	const sk_resource_type_t* type = repo->find_type(ra_test_payload_repo, RA_TEST_MATERIAL_TYPE);
	return repo->create_resource(ra_test_payload_repo, type, uuid, scope);
}

static void ra_test_reloaded(void_ptr_t user_data, sk_rid_t asset, const_chr_t path) {
	(void)user_data;
	(void)asset;
	(void)path;
}

static void ra_test_after_move(void_ptr_t user_data, sk_rid_t asset, const_chr_t old_path, const_chr_t new_path) {
	(void)user_data;
	(void)asset;
	(void)old_path;
	(void)new_path;
}

static void ra_test_export(void_ptr_t user_data, sk_rid_t object, sk_archive_writer_t* writer) {
	(void)user_data;
	(void)object;
	(void)writer;
}

static const_chr_t ra_test_icon(void_ptr_t user_data) {
	(void)user_data;
	return "file";
}

static i32 ra_test_load_order(void_ptr_t user_data) {
	(void)user_data;
	return 100;
}

static i32 ra_test_asset_name_fn(void_ptr_t user_data, sk_rid_t asset, char* out, u32 out_cap) { /* NOLINT(readability-non-const-parameter): handler ABI requires char* */
	(void)user_data;
	(void)asset;
	(void)out;
	(void)out_cap;
	return 0;
}

static const sk_resource_asset_handler_t ra_test_handler = {
	.user_data = NULL,
	.extension = ra_test_ext,
	.open_asset = NULL,
	.get_resource_type_id = ra_test_type,
	.get_desc = ra_test_desc,
	.load = ra_test_load,
	.save = ra_test_save,
	.create = ra_test_create,
	.reloaded = ra_test_reloaded,
	.after_move = ra_test_after_move,
	.export_object = ra_test_export,
	.get_icon = ra_test_icon,
	.get_load_order = ra_test_load_order,
	.get_asset_name = ra_test_asset_name_fn,
};

static u32 ra_test_importer_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const char ext[] = ".testimp";
	if (out != NULL && out_cap > 0u) {
		out[0] = ext;
	}
	return 1u;
}

static const_chr_t ra_test_importer_out(void_ptr_t user_data) {
	(void)user_data;
	return ".testasset";
}

static u32 ra_test_importer_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 2u;
}

static sk_type_id_t ra_test_importer_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_TYPE_ID_ZERO;
}

static void ra_test_importer_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	(void)ctx;
}

static void ra_test_importer_cook(void_ptr_t user_data, sk_resource_cook_context_t* ctx) {
	(void)user_data;
	(void)ctx;
}

static i32 ra_test_importer_import_asset(void_ptr_t user_data, sk_rid_t directory, const_ptr_t settings, const_chr_t path, sk_undo_redo_scope_t* scope) {
	(void)user_data;
	(void)directory;
	(void)settings;
	(void)path;
	(void)scope;
	++ra_test_direct_import_count;
	return 0;
}

static const sk_resource_asset_importer_t ra_test_importer = {
	.user_data = NULL,
	.imported_extensions = ra_test_importer_exts,
	.output_extension = ra_test_importer_out,
	.cooker_version = ra_test_importer_cooker,
	.get_settings_type = ra_test_importer_settings,
	.ingest = ra_test_importer_ingest,
	.cook = ra_test_importer_cook,
	.import_asset = ra_test_importer_import_asset,
};

static u32 ra_test_generic_exts(void_ptr_t user_data, const_chr_t* out, u32 out_cap) {
	(void)user_data;
	static const char ext[] = ".testgen";
	if (out != NULL && out_cap > 0u) {
		out[0] = ext;
	}
	return 1u;
}

static const_chr_t ra_test_generic_out(void_ptr_t user_data) {
	(void)user_data;
	return ".testasset";
}

static u32 ra_test_generic_cooker(void_ptr_t user_data) {
	(void)user_data;
	return 1u;
}

static sk_type_id_t ra_test_generic_settings(void_ptr_t user_data) {
	(void)user_data;
	return SK_TYPE_ID_ZERO;
}

static void ra_test_generic_ingest(void_ptr_t user_data, sk_resource_ingest_context_t* ctx) {
	(void)user_data;
	ctx->declare_sub_resource(ctx, "mesh", RA_TEST_MATERIAL_TYPE);
	ctx->add_dependency(ctx, "deps/helper.txt", (const u8*)"helper", 6u);
}

static void ra_test_generic_cook(void_ptr_t user_data, sk_resource_cook_context_t* ctx) {
	(void)user_data;
	ctx->sub_resource(ctx, "mesh", RA_TEST_MATERIAL_TYPE);
}

static const sk_resource_asset_importer_t ra_test_generic_importer = {
	.user_data = NULL,
	.imported_extensions = ra_test_generic_exts,
	.output_extension = ra_test_generic_out,
	.cooker_version = ra_test_generic_cooker,
	.get_settings_type = ra_test_generic_settings,
	.ingest = ra_test_generic_ingest,
	.cook = ra_test_generic_cook,
	.import_asset = NULL,
};

static sk_repository_t* ra_test_repository(void) {
	const sk_repository_api_t* repo = sk_repository_api();
	sk_repository_t* repository = repo->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repository);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repository));
	TEST_ASSERT_EQUAL_INT(0, repo->register_type(repository, &ra_test_resource_desc));
	return repository;
}

static sk_app_context_t* ra_test_app_context(void) {
	sk_app_context_t* app = sk_app_create();
	TEST_ASSERT_NOT_NULL(app);
	const sk_app_api_t* app_api = sk_app_api();
	app_api->add_impl(app, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, (const_ptr_t)&ra_test_handler);
	app_api->add_impl(app, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID, (const_ptr_t)&ra_test_importer);
	app_api->add_impl(app, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID, (const_ptr_t)&ra_test_generic_importer);
	return app;
}

static sk_resource_assets_context_t* ra_test_context(sk_repository_t* repository, sk_app_context_t* app) {
	sk_resource_assets_context_t* ctx = sk_resource_assets_api()->create(repository, app, sk_app_api(), sk_allocator_default());
	TEST_ASSERT_NOT_NULL(ctx);
	return ctx;
}

static void ra_test_path(const_chr_t a, const_chr_t b, char* out, u32 out_cap) {
	TEST_ASSERT_TRUE(sk_path_join(sk_str_view_cstr(a), sk_str_view_cstr(b), out, out_cap) >= 0);
}

static void ra_test_write_file(const_chr_t path, const_chr_t text) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_TRUE(fs->write_file(file, text, strlen(text)) == strlen(text));
	fs->close_file(file);
}

static void ra_test_make_tree(const_chr_t root, const_chr_t assets) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(root));
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(assets));
	char textures[SK_FS_PATH_MAX];
	ra_test_path(assets, "Textures", textures, (u32)sizeof(textures));
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(textures));
	char file[SK_FS_PATH_MAX];
	ra_test_path(assets, "scene.material", file, (u32)sizeof(file));
	ra_test_write_file(file, "material");
	ra_test_path(assets, "shader.glsl", file, (u32)sizeof(file));
	ra_test_write_file(file, "glsl");
	ra_test_path(textures, "wood.png", file, (u32)sizeof(file));
	ra_test_write_file(file, "png");
	ra_test_path(assets, ".hidden.txt", file, (u32)sizeof(file));
	ra_test_write_file(file, "hidden");
	ra_test_path(assets, "ignore.buffer", file, (u32)sizeof(file));
	ra_test_write_file(file, "buffer");
}

static void ra_test_remove_tree(const_chr_t root, const_chr_t assets) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char file[SK_FS_PATH_MAX];
	char textures[SK_FS_PATH_MAX];
	char batch[SK_FS_PATH_MAX];
	/* Known scan/import fixtures (best-effort; ignore missing paths). */
	ra_test_path(assets, "scene.material", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(assets, "shader.glsl", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(assets, ".hidden.txt", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(assets, "ignore.buffer", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(assets, "mesh.testgen", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(assets, "model.testimp", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(assets, "Textures", textures, (u32)sizeof(textures));
	ra_test_path(textures, "wood.png", file, (u32)sizeof(file));
	(void)fs->remove(file);
	(void)fs->remove(textures);
	ra_test_path(assets, "Batch", batch, (u32)sizeof(batch));
	ra_test_path(batch, "a.testimp", file, (u32)sizeof(file));
	(void)fs->remove(file);
	ra_test_path(batch, "b.unknown_ext", file, (u32)sizeof(file));
	(void)fs->remove(file);
	(void)fs->remove(batch);
	(void)fs->remove(assets);
	(void)fs->remove(root);
}

static void ra_test_temp_paths(char* root, u32 root_cap, char* assets, u32 assets_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char temp[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	ra_test_path(temp, "skore_ra_pkg", root, root_cap);
	ra_test_path(root, "Assets", assets, assets_cap);
}

static sk_rid_t ra_test_asset_by_name_ext(sk_resource_assets_context_t* ctx, sk_rid_t node, const_chr_t name, const_chr_t extension) {
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(ctx->repository, node);
	u32 count = 0u;
	const sk_rid_t* children = repo->get_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	for (u32 i = 0u; i < count; ++i) {
		sk_resource_object_t child = repo->read(ctx->repository, children[i]);
		const_chr_t child_name = repo->get_string(child, SK_RESOURCE_ASSET_FIELD_NAME);
		if (child_name == NULL || strcmp(child_name, name) != 0) {
			continue;
		}
		if (extension != NULL) {
			const_chr_t child_ext = repo->get_string(child, SK_RESOURCE_ASSET_FIELD_EXTENSION);
			if (child_ext == NULL || strcmp(child_ext, extension) != 0) {
				continue;
			}
		}
		return children[i];
	}
	return SK_RID_ZERO;
}

static sk_rid_t ra_test_asset_by_name(sk_resource_assets_context_t* ctx, sk_rid_t node, const_chr_t name) {
	return ra_test_asset_by_name_ext(ctx, node, name, NULL);
}

static sk_uuid_t ra_test_parse_uuid(const char* s) {
	sk_uuid_t uuid = SK_UUID_ZERO;
	for (u32 i = 0u; i < 16u; ++i) {
		unsigned char c = (unsigned char)s[i];
		u32 v = (c >= (unsigned char)'0' && c <= (unsigned char)'9') ? (u32)(c - (unsigned char)'0') : (u32)(c - (unsigned char)'a' + 10u);
		uuid.lo = (uuid.lo << 4u) | v;
	}
	for (u32 i = 0u; i < 16u; ++i) {
		unsigned char c = (unsigned char)s[16u + i];
		u32 v = (c >= (unsigned char)'0' && c <= (unsigned char)'9') ? (u32)(c - (unsigned char)'0') : (u32)(c - (unsigned char)'a' + 10u);
		uuid.hi = (uuid.hi << 4u) | v;
	}
	return uuid;
}

SK_TEST(resource_assets_engine_context_lifecycle) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = sk_resource_assets_api()->create(repository, app, sk_app_api(), sk_allocator_default());
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_TRUE(SK_RID_EQ(sk_resource_assets_api()->get_package(ctx), SK_RID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(sk_resource_assets_api()->get_root_directory(ctx), SK_RID_ZERO));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_resource_assets_api()->get_assets(ctx, RA_TEST_MATERIAL_TYPE, NULL, 0u));
	sk_resource_assets_api()->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
}

SK_TEST(resource_assets_engine_handler_discovery) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = ra_test_context(repository, app);
	const sk_resource_assets_api_t* api = sk_resource_assets_api();

	TEST_ASSERT_EQUAL_PTR(&ra_test_handler, api->get_asset_handler_for_extension(ctx, ".testmat"));
	TEST_ASSERT_NULL(api->get_asset_handler_for_extension(ctx, ".nope"));
	TEST_ASSERT_EQUAL_PTR(&ra_test_handler, api->get_asset_handler_for_type(ctx, RA_TEST_MATERIAL_TYPE));
	TEST_ASSERT_NULL(api->get_asset_handler_for_type(ctx, SK_RESOURCE_ASSET_TYPE_ID));
	TEST_ASSERT_EQUAL_PTR(&ra_test_importer, api->get_importer(ctx, ".testimp"));
	TEST_ASSERT_EQUAL_PTR(&ra_test_generic_importer, api->get_importer(ctx, ".testgen"));
	TEST_ASSERT_NULL(api->get_importer(ctx, ".nope"));

	api->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
}

SK_TEST(resource_assets_engine_scan_package) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = ra_test_context(repository, app);
	const sk_resource_assets_api_t* api = sk_resource_assets_api();
	const sk_repository_api_t* repo = sk_repository_api();

	char root[SK_FS_PATH_MAX];
	char assets[SK_FS_PATH_MAX];
	ra_test_temp_paths(root, (u32)sizeof(root), assets, (u32)sizeof(assets));
	ra_test_remove_tree(root, assets);
	ra_test_make_tree(root, assets);

	sk_rid_t package = api->scan_package_from_directory(ctx, "TestPackage", root);
	TEST_ASSERT_TRUE(package.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(package, api->get_package(ctx)));
	{
		sk_resource_object_t pv = repo->read(repository, package);
		u32 fcount = 0u;
		const sk_rid_t* files = repo->get_subobject_list(pv, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &fcount);
		(void)files;
	}

	sk_rid_t root_node = api->get_root_directory(ctx);
	TEST_ASSERT_TRUE(root_node.id != 0u);

	{
		sk_resource_object_t package_view = repo->read(repository, package);
		TEST_ASSERT_EQUAL_STRING("TestPackage", repo->get_string(package_view, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
		sk_rid_t root_from_package = repo->get_subobject(package_view, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
		TEST_ASSERT_TRUE(SK_RID_EQ(root_from_package, root_node));
	}

	/* Root node's directory asset. */
	sk_resource_object_t root_node_view = repo->read(repository, root_node);
	sk_rid_t root_asset = repo->get_subobject(root_node_view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
	TEST_ASSERT_TRUE(root_asset.id != 0u);
	{
		sk_resource_object_t view = repo->read(repository, root_asset);
		TEST_ASSERT_EQUAL_STRING("TestPackage", repo->get_string(view, SK_RESOURCE_ASSET_FIELD_NAME));
		TEST_ASSERT_EQUAL_STRING("TestPackage:/", repo->get_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID));
		TEST_ASSERT_EQUAL_INT(1, repo->get_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
	}

	/* scene.material file asset under the root. */
	sk_rid_t scene = ra_test_asset_by_name(ctx, root_node, "scene");
	TEST_ASSERT_TRUE(scene.id != 0u);
	{
		sk_resource_object_t view = repo->read(repository, scene);
		TEST_ASSERT_EQUAL_STRING(".material", repo->get_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION));
		TEST_ASSERT_EQUAL_STRING("TestPackage:/scene.material", repo->get_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID));
		TEST_ASSERT_EQUAL_INT(0, repo->get_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
		TEST_ASSERT_TRUE(SK_RID_EQ(repo->get_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT), root_node));
	}
	TEST_ASSERT_EQUAL_STRING("TestPackage:/scene.material", api->get_path_id(ctx, scene));
	char expected_abs[SK_FS_PATH_MAX];
	ra_test_path(assets, "scene.material", expected_abs, (u32)sizeof(expected_abs));
	TEST_ASSERT_EQUAL_STRING(expected_abs, api->get_absolute_path(ctx, scene));

	/* shader.glsl under the root. */
	sk_rid_t shader = ra_test_asset_by_name(ctx, root_node, "shader");
	TEST_ASSERT_TRUE(shader.id != 0u);
	TEST_ASSERT_EQUAL_STRING("TestPackage:/shader.glsl", api->get_path_id(ctx, shader));

	/* Textures directory: directory asset + node + nested wood.png. */
	sk_rid_t textures_dir_asset = ra_test_asset_by_name(ctx, root_node, "Textures");
	TEST_ASSERT_TRUE(textures_dir_asset.id != 0u);
	{
		sk_resource_object_t view = repo->read(repository, textures_dir_asset);
		TEST_ASSERT_EQUAL_INT(1, repo->get_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
		TEST_ASSERT_EQUAL_STRING("TestPackage:/Textures", repo->get_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID));
	}

	sk_rid_t textures_node = repo->get_parent(repository, textures_dir_asset);
	TEST_ASSERT_TRUE(textures_node.id != 0u);
	TEST_ASSERT_EQUAL_INT(1, api->is_child_of(ctx, root_node, textures_node));

	sk_rid_t wood = ra_test_asset_by_name(ctx, textures_node, "wood");
	TEST_ASSERT_TRUE(wood.id != 0u);
	TEST_ASSERT_EQUAL_STRING("TestPackage:/Textures/wood.png", api->get_path_id(ctx, wood));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent_asset(ctx, wood), textures_node));

	/* Hidden and .buffer files are skipped. */
	TEST_ASSERT_TRUE(SK_RID_EQ(ra_test_asset_by_name(ctx, root_node, ".hidden"), SK_RID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(ra_test_asset_by_name(ctx, root_node, "ignore"), SK_RID_ZERO));

	/* get_absolute_path_from_path_id resolves against the current package. */
	char resolved[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, api->get_absolute_path_from_path_id(ctx, "TestPackage:/Textures/wood.png", resolved, (u32)sizeof(resolved)));
	ra_test_path(assets, "Textures/wood.png", expected_abs, (u32)sizeof(expected_abs));
	TEST_ASSERT_EQUAL_STRING(expected_abs, resolved);

	api->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
	ra_test_remove_tree(root, assets);
}

SK_TEST(resource_assets_engine_create_and_unique_names) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = ra_test_context(repository, app);
	const sk_resource_assets_api_t* api = sk_resource_assets_api();
	const sk_repository_api_t* repo = sk_repository_api();

	sk_rid_t package = api->scan_package_from_directory(ctx, "Game", "/nonexistent_scan_root");
	(void)package;
	sk_rid_t root_node = api->get_root_directory(ctx);
	TEST_ASSERT_TRUE(root_node.id != 0u);

	/* create_asset_directory. */
	sk_rid_t folder_asset = api->create_asset_directory(ctx, root_node, "Folder", NULL);
	TEST_ASSERT_TRUE(folder_asset.id != 0u);
	sk_rid_t folder_node = repo->get_parent(repository, folder_asset);
	TEST_ASSERT_TRUE(folder_node.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Game:/Folder", api->get_path_id(ctx, folder_asset));

	/* create_asset_file with unique naming on collision. */
	sk_rid_t a = api->create_asset_file(ctx, root_node, "a", ".txt", NULL);
	sk_rid_t b = api->create_asset_file(ctx, root_node, "a", ".txt", NULL);
	TEST_ASSERT_TRUE(a.id != 0u);
	TEST_ASSERT_TRUE(b.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Game:/a.txt", api->get_path_id(ctx, a));
	TEST_ASSERT_EQUAL_STRING("Game:/a (1).txt", api->get_path_id(ctx, b));

	/* Same name, different extension is allowed. */
	sk_rid_t c = api->create_asset_file(ctx, root_node, "a", ".md", NULL);
	TEST_ASSERT_TRUE(c.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Game:/a.md", api->get_path_id(ctx, c));

	/* create_asset via the handler. */
	ra_test_payload_repo = repository;
	sk_rid_t mat = api->create_asset(ctx, root_node, RA_TEST_MATERIAL_TYPE, "MyMaterial", NULL);
	TEST_ASSERT_TRUE(mat.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Game:/MyMaterial.testmat", api->get_path_id(ctx, mat));
	sk_resource_object_t mat_view = repo->read(repository, mat);
	sk_rid_t payload = repo->get_subobject(mat_view, SK_RESOURCE_ASSET_FIELD_OBJECT);
	TEST_ASSERT_TRUE(payload.id != 0u);
	TEST_ASSERT_EQUAL_PTR(repo->find_type(repository, RA_TEST_MATERIAL_TYPE), repo->resource_type(repository, payload));
	TEST_ASSERT_EQUAL_PTR(&ra_test_handler, api->get_asset_handler(ctx, mat));
	TEST_ASSERT_EQUAL_PTR(&ra_test_handler, api->get_asset_handler(ctx, payload));
	TEST_ASSERT_EQUAL_PTR(&ra_test_handler, api->get_asset_handler(ctx, sk_resource_assets_api()->get_asset_payload(ctx, mat)));

	/* create_asset with no handler for the type fails cleanly. */
	TEST_ASSERT_TRUE(SK_RID_EQ(api->create_asset(ctx, root_node, SK_RESOURCE_ASSET_TYPE_ID, "x", NULL), SK_RID_ZERO));

	/* get_asset_name / get_asset_full_name. */
	char name[256];
	TEST_ASSERT_TRUE(api->get_asset_name(ctx, mat, name, (u32)sizeof(name)));
	TEST_ASSERT_EQUAL_STRING("MyMaterial", name);
	TEST_ASSERT_TRUE(api->get_asset_full_name(ctx, mat, name, (u32)sizeof(name)));
	TEST_ASSERT_EQUAL_STRING("MyMaterial.testmat", name);

	api->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
}

SK_TEST(resource_assets_engine_move_asset) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = ra_test_context(repository, app);
	const sk_resource_assets_api_t* api = sk_resource_assets_api();
	const sk_repository_api_t* repo = sk_repository_api();

	sk_rid_t package = api->scan_package_from_directory(ctx, "Game", "/nonexistent_scan_root");
	(void)package;
	sk_rid_t root_node = api->get_root_directory(ctx);
	TEST_ASSERT_TRUE(root_node.id != 0u);

	sk_rid_t folder_asset = api->create_asset_directory(ctx, root_node, "Folder", NULL);
	sk_rid_t folder_node = repo->get_parent(repository, folder_asset);

	/* Move a file into a folder that already has a colliding name. */
	sk_rid_t moved = api->create_asset_file(ctx, root_node, "doc", ".txt", NULL);
	sk_rid_t existing = api->create_asset_file(ctx, folder_node, "doc", ".txt", NULL);
	TEST_ASSERT_TRUE(moved.id != 0u);
	TEST_ASSERT_TRUE(existing.id != 0u);

	api->move_asset(ctx, folder_node, moved, NULL);
	char dbg[256];
	sk_resource_assets_api()->create_unique_asset_name(ctx, folder_node, "doc", ".txt", 0, dbg, (u32)sizeof(dbg));
	TEST_ASSERT_EQUAL_STRING("Game:/Folder/doc (1).txt", api->get_path_id(ctx, moved));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent_asset(ctx, moved), folder_node));
	TEST_ASSERT_TRUE(SK_RID_EQ(ra_test_asset_by_name(ctx, root_node, "doc"), SK_RID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(ra_test_asset_by_name(ctx, folder_node, "doc (1)"), moved));

	/* Moving to the same parent is a no-op. */
	char before[SK_FS_PATH_MAX];
	snprintf(before, sizeof(before), "%s", api->get_path_id(ctx, moved));
	api->move_asset(ctx, folder_node, moved, NULL);
	TEST_ASSERT_EQUAL_STRING(before, api->get_path_id(ctx, moved));

	/* Move a directory subtree. */
	sk_rid_t sub_asset = api->create_asset_directory(ctx, folder_node, "Sub", NULL);
	sk_rid_t sub_node = repo->get_parent(repository, sub_asset);
	api->move_asset(ctx, root_node, sub_asset, NULL);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent_asset(ctx, sub_asset), root_node));
	TEST_ASSERT_EQUAL_INT(1, api->is_child_of(ctx, root_node, sub_node));
	TEST_ASSERT_EQUAL_INT(0, api->is_child_of(ctx, folder_node, sub_node));

	api->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
}

SK_TEST(resource_assets_engine_register_by_type) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = ra_test_context(repository, app);
	const sk_resource_assets_api_t* api = sk_resource_assets_api();

	sk_rid_t package = api->scan_package_from_directory(ctx, "Game", "/nonexistent_scan_root");
	(void)package;
	sk_rid_t root_node = api->get_root_directory(ctx);

	ra_test_payload_repo = repository;
	sk_rid_t m1 = api->create_asset(ctx, root_node, RA_TEST_MATERIAL_TYPE, "One", NULL);
	sk_rid_t m2 = api->create_asset(ctx, root_node, RA_TEST_MATERIAL_TYPE, "Two", NULL);
	sk_rid_t payload1 = api->get_asset_payload(ctx, m1);
	sk_rid_t payload2 = api->get_asset_payload(ctx, m2);
	TEST_ASSERT_TRUE(payload1.id != 0u);
	TEST_ASSERT_TRUE(payload2.id != 0u);

	sk_rid_t out[8];
	TEST_ASSERT_EQUAL_UINT32(2u, api->get_assets(ctx, RA_TEST_MATERIAL_TYPE, out, 8u));
	TEST_ASSERT_TRUE(SK_RID_EQ(out[0], payload1));
	TEST_ASSERT_TRUE(SK_RID_EQ(out[1], payload2));

	/* get_assets with a NULL out still returns the count. */
	TEST_ASSERT_EQUAL_UINT32(2u, api->get_assets(ctx, RA_TEST_MATERIAL_TYPE, NULL, 0u));

	api->unregister_asset_by_type(ctx, payload1);
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_assets(ctx, RA_TEST_MATERIAL_TYPE, out, 8u));
	TEST_ASSERT_TRUE(SK_RID_EQ(out[0], payload2));

	/* Unknown type id indexes nothing. */
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_assets(ctx, SK_RESOURCE_ASSET_TYPE_ID, out, 8u));

	/* find_asset_on_directory resolves by handler extension. */
	sk_rid_t found = api->find_asset_on_directory(ctx, root_node, RA_TEST_MATERIAL_TYPE, "Two");
	TEST_ASSERT_TRUE(SK_RID_EQ(found, payload2));

	api->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
}

SK_TEST(resource_assets_engine_import_dispatch) {
	sk_repository_t* repository = ra_test_repository();
	sk_app_context_t* app = ra_test_app_context();
	sk_resource_assets_context_t* ctx = ra_test_context(repository, app);
	const sk_resource_assets_api_t* api = sk_resource_assets_api();
	const sk_repository_api_t* repo = sk_repository_api();
	const sk_filesystem_api_t* fs = sk_filesystem_api();

	char root[SK_FS_PATH_MAX];
	char assets[SK_FS_PATH_MAX];
	ra_test_temp_paths(root, (u32)sizeof(root), assets, (u32)sizeof(assets));
	ra_test_remove_tree(root, assets);
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(root));
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(assets));

	sk_rid_t package = api->scan_package_from_directory(ctx, "Game", root);
	(void)package;
	sk_rid_t root_node = api->get_root_directory(ctx);
	TEST_ASSERT_TRUE(root_node.id != 0u);

	/* Direct dispatch: importer with import_asset != NULL is called. */
	char source[SK_FS_PATH_MAX];
	ra_test_path(assets, "model.testimp", source, (u32)sizeof(source));
	ra_test_write_file(source, "model data");
	ra_test_direct_import_count = 0;
	TEST_ASSERT_EQUAL_INT(0, api->import_asset(ctx, root_node, source, NULL));
	TEST_ASSERT_EQUAL_INT(1, ra_test_direct_import_count);

	/* Generic path: wrapper created, ingest declared sub-resource, cook created it. */
	char generic[SK_FS_PATH_MAX];
	ra_test_path(assets, "mesh.testgen", generic, (u32)sizeof(generic));
	ra_test_write_file(generic, "mesh data");
	TEST_ASSERT_EQUAL_INT(0, api->import_asset(ctx, root_node, generic, NULL));

	sk_rid_t wrapper_asset = ra_test_asset_by_name_ext(ctx, root_node, "mesh", ".testasset");
	TEST_ASSERT_TRUE(wrapper_asset.id != 0u);
	sk_resource_object_t wrapper_asset_view = repo->read(repository, wrapper_asset);
	sk_rid_t wrapper = repo->get_subobject(wrapper_asset_view, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET);
	TEST_ASSERT_TRUE(wrapper.id != 0u);

	sk_resource_object_t wrapper_view = repo->read(repository, wrapper);
	TEST_ASSERT_EQUAL_STRING(".testasset", repo->get_string(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION));
	TEST_ASSERT_EQUAL_UINT64(1u, repo->get_uint(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION));
	TEST_ASSERT_EQUAL_UINT64(9u, repo->get_uint(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE));
	TEST_ASSERT_NOT_NULL(repo->get_string(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH));

	u32 sub_count = 0u;
	const sk_rid_t* sub_entries = repo->get_subobject_list(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, &sub_count);
	TEST_ASSERT_EQUAL_UINT32(1u, sub_count);
	sk_resource_object_t sub_entry = repo->read(repository, sub_entries[0]);
	TEST_ASSERT_EQUAL_STRING("mesh", repo->get_string(sub_entry, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID));
	const_chr_t target = repo->get_string(sub_entry, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID);
	TEST_ASSERT_NOT_NULL(target);
	sk_rid_t cooked = repo->find_by_uuid(repository, ra_test_parse_uuid(target));
	TEST_ASSERT_TRUE(cooked.id != 0u);
	TEST_ASSERT_EQUAL_PTR(repo->find_type(repository, RA_TEST_MATERIAL_TYPE), repo->resource_type(repository, cooked));

	/* Dependencies recorded by ingest. */
	u32 dep_count = 0u;
	const sk_rid_t* deps = repo->get_subobject_list(wrapper_view, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, &dep_count);
	TEST_ASSERT_EQUAL_UINT32(1u, dep_count);
	sk_resource_object_t dep = repo->read(repository, deps[0]);
	TEST_ASSERT_EQUAL_STRING("deps/helper.txt", repo->get_string(dep, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH));
	TEST_ASSERT_EQUAL_UINT64(6u, repo->get_uint(dep, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE));

	/* Importing a directory imports every supported file inside. */
	char folder[SK_FS_PATH_MAX];
	ra_test_path(assets, "Batch", folder, (u32)sizeof(folder));
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(folder));
	char inner[SK_FS_PATH_MAX];
	ra_test_path(folder, "a.testimp", inner, (u32)sizeof(inner));
	ra_test_write_file(inner, "a");
	ra_test_path(folder, "b.unknown_ext", inner, (u32)sizeof(inner));
	ra_test_write_file(inner, "b");
	ra_test_direct_import_count = 0;
	TEST_ASSERT_EQUAL_INT(0, api->import_asset(ctx, root_node, folder, NULL));
	TEST_ASSERT_EQUAL_INT(1, ra_test_direct_import_count);

	api->destroy(ctx);
	sk_app_destroy(app);
	sk_repository_api()->destroy(repository);
	ra_test_remove_tree(root, assets);
}

#endif /* SK_TESTS */
