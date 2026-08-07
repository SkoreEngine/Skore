#pragma once

/**
 * @file app.h
 * @brief App context: typed API registry shared by host and plugins.
 *
 * Types and the sk_app_api_t table shape live in sk-core. The context storage
 * and set_api/get_api implementations live in sk-app (created by sk_app_init).
 * Plugins do not link sk-app; they receive the context (and use the API table
 * passed at entry) to register/lookup module APIs by sk_type_id_t.
 *
 * Rule: free-function / module-API *implementations* for surfaces declared
 * here belong in sk-app, not in sk-core (see AGENTS.md).
 *
 * Runtime (main loop, timing state, plugin handles) and process lifecycle
 * (sk_app_init / sk_app_tick / sk_app_run) are implemented in sk-app (app/app.c).
 * Public entry points for plugins sit on sk_app_api_t; every table entry that
 * needs runtime state takes sk_app_context_t* — no process-global context.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque app instance: owns the type_id → API pointer registry.
 * A context from sk_app_init also owns runtime state (timing, loaded plugins,
 * host "app" logger). Independent contexts from sk_app_create are registry-only.
 */
typedef struct sk_app_context_t sk_app_context_t;

/**
 * Global module API table for the app (one process-wide surface of entry points).
 * Host fills this; plugins call through the table (no static link to sk-app).
 * Runtime state lives on the sk_app_context_t passed into each entry — not in
 * a process-global variable.
 */
typedef struct sk_app_api_t {
	/**
     * Register or replace an API pointer under @p type_id.
     * @param context App context (must not be NULL).
     * @param type_id Type id of the API surface being registered.
     * @param pointer Pointer to the API table / object (may be NULL to unregister).
     *                Typically a const module table; stored as an opaque address.
     */
	void (*set_api)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer);

	/**
     * Look up a previously registered API pointer.
     * @param context App context (must not be NULL).
     * @param type_id Type id of the API surface.
     * @return Registered pointer, or NULL if missing / bad args.
     */
	void_ptr_t (*get_api)(sk_app_context_t* context, sk_type_id_t type_id);

	/**
     * Load a plugin shared library from @p path and call sk_plugin_entry_point.
     * Does not search directories; the caller supplies an exact path.
     * The library stays loaded until the context is destroyed (or plugins unloaded).
     *
     * @param context App context from sk_app_init (must not be NULL; must be bootstrapped).
     * @param path Filesystem path to the plugin (UTF-8). Must not be NULL.
     * @return 0 on success, non-zero on failure (open, symbol, or entry error).
     */
	i32 (*load_plugin)(sk_app_context_t* context, const_chr_t path);

	/**
     * Request the main loop to exit after the current frame (or immediately
     * if the loop has not started yet).
     *
     * @param context App context (must not be NULL).
     */
	void (*request_shutdown)(sk_app_context_t* context);

	/**
     * Seconds between the previous frame and the current one.
     * Valid after the main loop has begun updating; 0 before the first tick.
     *
     * @param context App context (must not be NULL).
     * @return Delta time in seconds (non-negative).
     */
	f64 (*delta_time)(sk_app_context_t* context);

	/**
     * Approximate frames per second derived from the last delta time
     * (1 / delta_time when delta_time > 0, otherwise 0).
     *
     * @param context App context (must not be NULL).
     * @return FPS estimate.
     */
	f64 (*fps)(sk_app_context_t* context);

	/**
     * Seconds since bootstrap init (wall-clock monotonic).
     *
     * @param context App context (must not be NULL).
     * @return Elapsed time in seconds (non-negative).
     */
	f64 (*elapsed_time)(sk_app_context_t* context);
} sk_app_api_t;

/**
 * Create an empty app context (API registry).
 * Implemented in sk-app; link sk-app (or an executable that does).
 *
 * @return New context, or NULL on allocation failure.
 */
sk_app_context_t* sk_app_create(void);

/**
 * Destroy a context: unload plugins, destroy the host logger if present, free
 * the registry. Caller must not use @p context after.
 *
 * @param context Context to destroy (must not be NULL).
 */
void sk_app_destroy(sk_app_context_t* context);

/**
 * Process-wide app API function table (registry, plugins, timing, shutdown).
 * Always points at valid functions; safe to use after sk_app_create / sk_app_init.
 * Table entries that touch runtime state take an explicit sk_app_context_t*.
 *
 * @return Non-NULL pointer to the static sk_app_api_t table.
 */
const sk_app_api_t* sk_app_api(void);

/**
 * Create an app context with platform + host logger registered.
 * Does not bootstrap plugins/timing. Caller owns the returned context
 * (destroy with sk_app_destroy). Implemented in sk-app.
 *
 * @return New context, or NULL on failure.
 */
sk_app_context_t* sk_app_startup(void);

/**
 * Application process entry used by player / editor (and tests).
 * Lives in sk-app so sk-core stays free of app-process lifecycle and API
 * implementations (see AGENTS.md: core declares; app implements).
 * Creates a new app context (API registry + runtime), initializes timing and
 * auto-loads {app_folder}/plugins. Does not enter the main loop; call
 * sk_app_tick(context) (or sk_app_run(context)) after a successful init.
 * Caller owns the returned context and must sk_app_destroy it when done.
 *
 * @param argc argument count from main
 * @param argv argument vector from main (may be NULL when argc is 0)
 * @return Bootstrapped context, or NULL on failure
 */
sk_app_context_t* sk_app_init(int argc, char* argv[]);

/**
 * Process one application frame (timing update; future phases/systems).
 * Requires a prior successful sk_app_init on @p context. Hosts drive the loop:
 *
 *   while (sk_app_tick(context)) { }
 *
 * @param context App context from sk_app_init (must not be NULL).
 * @return Non-zero while the app should keep running; 0 when shutdown was
 *         requested (or when called before bootstrap).
 */
i32 sk_app_tick(sk_app_context_t* context);

/**
 * Run the main loop until app_api->request_shutdown(context).
 * Convenience wrapper around while (sk_app_tick(context)) { }.
 * Requires a prior successful sk_app_init on @p context.
 *
 * @param context App context from sk_app_init (must not be NULL).
 * @return 0 on clean exit, non-zero on failure
 */
i32 sk_app_run(sk_app_context_t* context);

#ifdef __cplusplus
}
#endif
