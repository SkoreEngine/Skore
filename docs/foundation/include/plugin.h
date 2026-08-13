#pragma once

/**
 * @file plugin.h
 * @brief Host ↔ plugin load contract (entry + optional test export).
 *
 * Every SHARED plugin exports `sk_plugin_entry_point`. The host resolves that
 * symbol by name after `lib_open` and calls it with the **host** app context
 * and the **host** `sk_app_api_t` table. Plugins statically link
 * `sk-foundation` for types and pure utilities; they must **not** call
 * `sk_app_init` / `sk_app_startup` / `sk_app_shutdown` and must **not** use
 * any `sk_*_api()` accessor (those accessors do not exist).
 *
 * Shared engine state lives on `sk_app_context_t`. Plugins obtain logger /
 * filesystem / platform / repository / resource-assets tables only through
 * `app_api->*` accessors (or `get_api` by type id).
 */

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Typedef the host uses after `lib_symbol(..., "sk_plugin_entry_point")`.
 * Identical to the exported function's signature.
 *
 * @param context Host app context (must not be NULL; the plugin shares it).
 * @param app_api Host app API table from `sk_app_boot_t.api` (must not be NULL).
 * @return 0 on success, non-zero to abort the load (host unloads the library).
 */
typedef int (*sk_plugin_entry_point_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

/**
 * Plugin load entry. Resolved via `sk_platform_api_t::lib_symbol` after
 * `lib_open`. Register module tables with `app_api->set_api` /
 * `app_api->add_impl`. Do not start threads or touch other plugins' binaries.
 *
 * @param context Host app context (must not be NULL).
 * @param app_api Host app API table (must not be NULL).
 * @return 0 on success, non-zero on failure.
 */
SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef SK_TESTS
#include "test.h"

/**
 * Plugin-local test entry. Compiled only when `SK_TESTS` is set (non-Release).
 * Host skips the symbol when missing (Release). Not part of the production ABI.
 *
 * @param out Optional aggregate report; may be NULL.
 * @return 0 if failed == 0, non-zero if any test failed.
 */
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out);
#endif /* SK_TESTS */

#ifdef __cplusplus
}
#endif
