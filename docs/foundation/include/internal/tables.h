#pragma once

/**
 * @file internal/tables.h
 * @brief Foundation-internal table install hooks (APX-278).
 *
 * NOT a public header. Not `SK_API`. Not callable by plugins or hosts.
 * Implementing tasks copy this to `foundation/internal/tables.h`.
 *
 * Each module TU keeps its `static const sk_*_api_t` table. The matching
 * `sk_*_install` includes `foundation/internal/app_context.h` and writes
 * **only** the cached pointer on the context. Install functions do **not**
 * call `set_api`.
 * `sk_app_create` calls `sk_foundation_bind_tables` (installs + `set_api`).
 * `sk_app_startup` also calls `sk_platform_install` then `set_api`
 * (platform is not bound on registry-only create).
 */

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Write `ctx->logger_api = &logger_api` only. Defined in `logger.c`.
 * Does not create a logger context. Does not call `set_api`.
 */
void sk_logger_install(sk_app_context_t* ctx);

/**
 * Write `ctx->filesystem_api = &filesystem_api` only.
 * Defined in `filesystem_unix.c` / `filesystem_win32.c` (same TU as the
 * backend `static const` table). Does not create a filesystem context.
 * Does not call `set_api`.
 */
void sk_filesystem_install(sk_app_context_t* ctx);

/**
 * Write `ctx->platform = &platform_api` only.
 * Defined in `platform_unix.c` / `platform_win32.c`.
 * Called from `sk_app_startup` only (not `sk_app_create`). Does not call `set_api`.
 * `sk_platform_init` is deleted; this is the only platform hook.
 */
void sk_platform_install(sk_app_context_t* ctx);

/**
 * Write `ctx->repository_api = &repository_api` only. Defined in `repository.c`.
 * Does not call `set_api`.
 */
void sk_repository_install(sk_app_context_t* ctx);

/**
 * Write `ctx->resource_assets_api = &resource_assets_api` only.
 * Defined in `resource_assets.c`. Does not call `set_api`.
 */
void sk_resource_assets_install(sk_app_context_t* ctx);

/**
 * Calls logger / filesystem / repository / resource_assets install, then
 * `set_api` for each of those four type ids. This is the **only** `set_api`
 * caller for those tables. Does **not** call `sk_platform_install`.
 * Implemented in `app.c`.
 *
 * @param ctx Context from `sk_app_create` after maps are initialized (must not be NULL).
 */
void sk_foundation_bind_tables(sk_app_context_t* ctx);

#ifdef __cplusplus
}
#endif
