#pragma once

/**
 * @file repository.h
 * @brief Contract delta for APX-278 (apply onto today's `core/repository.h`).
 *
 * UNCHANGED (copy from `core/repository.h` as-is when creating
 * `foundation/repository.h`):
 *   - all types (`sk_repository_t`, `sk_resource_type_t`, `sk_rid_t`,
 *     `sk_resource_object_t`, field descriptors, undo/redo scope, …)
 *   - the full `sk_repository_api_t` function-pointer layout
 *     (create / destroy / register_type / read / write / commit / …)
 *
 * DELETED:
 *   - `SK_API const sk_repository_api_t* sk_repository_api(void);`
 *
 * ADDED: type id + obtain path (below). There is no free-function accessor.
 *
 * Obtain the immutable table from the host app API:
 *
 * @code
 * const sk_repository_api_t* repo_api = app_api->repository_api(app_ctx);
 * sk_repository_t* repo = repo_api->create(allocator);
 * @endcode
 *
 * `sk_repository_t` remains instance-owned. Only the **function table pointer**
 * was process-wide; it now lives on the app context (cached at create/startup/init).
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_repository_api_t sk_repository_api_t;

/**
 * Type id for app-context registration of `sk_repository_api_t`.
 * Halves are MD5("sk.repository_api"). CMake embed must still list `foundation/`.
 */
#define SK_REPOSITORY_API_TYPE_ID SK_TYPE_ID("sk.repository_api", 0x529d5721e95fa579ULL, 0x5d7928cd14815381ULL)

/*
 * DELETED — do not restore:
 *
 *   SK_API const sk_repository_api_t* sk_repository_api(void);
 *
 * The implementing TU (`foundation/repository.c`) keeps
 * `static const sk_repository_api_t repository_api = { ... };`
 * and `sk_app_create` caches `&repository_api` on the context. Callers use
 * `app_api->repository_api(ctx)` or
 * `app_api->get_api(ctx, SK_REPOSITORY_API_TYPE_ID)`.
 */

#ifdef __cplusplus
}
#endif
