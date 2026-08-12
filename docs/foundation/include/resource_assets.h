#pragma once

/**
 * @file resource_assets.h
 * @brief Contract delta for APX-278 (apply onto today's `core/resource_assets.h`
 *        and `core/resource_asset_builtins.h`).
 *
 * DELETED:
 *   - `SK_API const sk_resource_assets_api_t* sk_resource_assets_api(void);`
 *   - `void sk_resource_asset_builtins_bind_repository(sk_repository_t*);`
 *     (the former `builtins_repository` static)
 *
 * ADDED:
 *   - `SK_RESOURCE_ASSETS_API_TYPE_ID`
 *   - `sk_repository_t* repository` + `const sk_repository_api_t* repo_api`
 *     on ingest/cook contexts (filled by the engine at call time)
 *   - `repository` + `repo_api` parameters on handler create/load/save/
 *     reloaded/after_move (see below)
 *
 * UNCHANGED: the rest of `sk_resource_assets_api_t` (create still takes
 * `repository`, `app_context`, `app_api`, `allocator`). `create` **must**
 * store `repository` and `app_api->repository_api(app_context)` on the
 * assets context; builtins read those, never a file-scope pointer.
 *
 * Obtain the table:
 *
 * @code
 * const sk_resource_assets_api_t* assets_api = app_api->resource_assets_api(app_ctx);
 * sk_resource_assets_context_t* assets =
 *     assets_api->create(repository, app_ctx, app_api, allocator);
 * @endcode
 *
 * Type registration after this contract (both take the table; no process getter):
 *
 * @code
 * i32 sk_resource_assets_register_types(sk_repository_t* repository,
 *                                       const sk_repository_api_t* repo_api);
 * i32 sk_resource_asset_builtins_register_types(sk_repository_t* repository,
 *                                               const sk_repository_api_t* repo_api);
 * void sk_resource_asset_builtins_register_impls(sk_app_context_t* context,
 *                                                const sk_app_api_t* app_api);
 * @endcode
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_resource_assets_api_t sk_resource_assets_api_t;
typedef struct sk_repository_t sk_repository_t;
typedef struct sk_repository_api_t sk_repository_api_t;

/** Type id for app-context registration of `sk_resource_assets_api_t`. */
#define SK_RESOURCE_ASSETS_API_TYPE_ID SK_TYPE_ID("sk.resource_assets_api", 0xcceb767bc7a323b7ULL, 0x2d273f5d983e8243ULL)

/*
 * Ingest / cook context additions (append to the existing structs):
 *
 *   sk_repository_t* repository;            -- borrowed; set by the engine
 *   const sk_repository_api_t* repo_api;    -- borrowed; set by the engine
 *
 * Handler callback signature replacements (today's user_data-first form
 * plus repository + repo_api immediately after user_data):
 *
 *   sk_rid_t (*load)(void_ptr_t user_data, sk_repository_t* repository,
 *                    const sk_repository_api_t* repo_api,
 *                    sk_rid_t asset, const_chr_t absolute_path);
 *   void (*save)(void_ptr_t user_data, sk_repository_t* repository,
 *                const sk_repository_api_t* repo_api,
 *                sk_rid_t object, const_chr_t absolute_path);
 *   sk_rid_t (*create)(void_ptr_t user_data, sk_repository_t* repository,
 *                      const sk_repository_api_t* repo_api,
 *                      sk_uuid_t uuid, sk_undo_redo_scope_t* scope);
 *   void (*reloaded)(void_ptr_t user_data, sk_repository_t* repository,
 *                    const sk_repository_api_t* repo_api,
 *                    sk_rid_t asset, const_chr_t absolute_path);
 *   void (*after_move)(void_ptr_t user_data, sk_repository_t* repository,
 *                      const sk_repository_api_t* repo_api,
 *                      sk_rid_t asset, const_chr_t old_absolute_path,
 *                      const_chr_t new_absolute_path);
 *
 * Matching free-function dispatchers in resource_assets.h take the same
 * extra arguments. Engine `create` / `import_asset` / `scan` fill them from
 * the assets context. `user_data` stays whatever the handler registered
 * (builtins keep NULL).
 */

#ifdef __cplusplus
}
#endif
