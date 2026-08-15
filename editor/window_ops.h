#pragma once

/**
 * @file window_ops.h
 * @brief Per-window public function tables (APX-365).
 *
 * Public window entry points (ProjectBrowserWindow.ClearSelection and the
 * rest) are never exported as free symbols. Each window class publishes one
 * process-lifetime struct of function pointers and registers it with
 * `app_api->add_impl` under that class's ops type id. Callers look the table
 * up and call through the pointers.
 *
 * Rules (also in docs/editor/window-table-pattern.md):
 *
 *  - Registration: once per app context at editor boot,
 *    `app_api->add_impl(ctx, OPS_TYPE_ID, &static_ops_table)`. The table is
 *    static / process-lifetime. Repeated register of the same pointer
 *    appends a duplicate; window register functions must be idempotent.
 *  - Lookup: `sk_editor_window_ops_lookup` returns the first impl for the
 *    ops type id (insertion order). Missing type → NULL. There is one table
 *    per window class, so first-impl is the table.
 *  - Lifetime: the table outlives every open window instance. Impl pointers
 *    stay valid until `remove_impl` or `sk_app_shutdown`. Do not hold a
 *    lookup result across shutdown.
 *  - Implementations stay `static` in the window's .c. The only published
 *    symbol is the register function (and this lookup helper).
 */

#include "app.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * First add_impl pointer registered under @p ops_type_id, or NULL when none.
 *
 * Contract: @p app_context and @p app_api are the live boot pair; @p ops_type_id
 * is a per-window-class ops type (e.g. SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID).
 */
const void* sk_editor_window_ops_lookup(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_type_id_t ops_type_id);

#ifdef __cplusplus
}
#endif
