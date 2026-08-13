#pragma once

/**
 * @file app.h
 * @brief App context + host lifecycle (foundation).
 *
 * Types and the `sk_app_api_t` table live in `sk-foundation`. The context
 * owns the typed API registry, multi-impl lists, and (when bootstrapped)
 * logger context, filesystem context, platform table cache, timing, and
 * loaded plugin handles.
 *
 * There is no `sk_app_api(void)`. Hosts take the table from `sk_app_boot_t`
 * returned by `sk_app_create` / `sk_app_startup` / `sk_app_init`. Plugins
 * receive the same table as the second argument of `sk_plugin_entry_point`.
 *
 * Teardown is `sk_app_shutdown` only (`sk_app_destroy` is deleted).
 *
 * Main-thread ownership of the context. Not worker-safe.
 */

#include "common.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque app instance.
 * Layout is internal (`foundation/internal/app_context.h`) so all foundation
 * TUs share one definition. Public code never field-accesses the struct.
 *
 * A context from `sk_app_init` owns runtime state (timing, loaded plugins,
 * logger context, filesystem context, host "app" logger, crash-install flag).
 * A context from `sk_app_create` is registry-only (no logger/fs/crash/plugins).
 * A context from `sk_app_startup` has platform + logger + filesystem but no
 * plugin scan and does not install the crash handler.
 */
typedef struct sk_app_context_t sk_app_context_t;

/* Forward declarations so this header does not pull every module. */
typedef struct sk_logger_context_t sk_logger_context_t;
typedef struct sk_logger_api_t sk_logger_api_t;
typedef struct sk_logger_t sk_logger_t;
typedef struct sk_filesystem_context_t sk_filesystem_context_t;
typedef struct sk_filesystem_api_t sk_filesystem_api_t;
typedef struct sk_platform_api_t sk_platform_api_t;
typedef struct sk_repository_api_t sk_repository_api_t;
typedef struct sk_resource_assets_api_t sk_resource_assets_api_t;
typedef struct sk_world_t sk_world_t; /* engine ECS scene world (entities plugin) */
typedef struct sk_entities_api_t sk_entities_api_t;
typedef struct sk_jolt_api_t sk_jolt_api_t;

/**
 * Host / plugin module API table for the app.
 *
 * The table object itself is immutable (`static const` in the implementing
 * TU). Runtime state lives on the `sk_app_context_t*` passed into each entry.
 * One table pointer is handed to every plugin; do not copy the struct.
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
	 * @return Registered pointer, or NULL if missing.
	 */
	void_ptr_t (*get_api)(sk_app_context_t* context, sk_type_id_t type_id);

	/**
	 * Register an implementation pointer for @p type_id.
	 * Unlike set_api (which stores a single pointer per type), implementations
	 * accumulate as a list: repeated add_impl calls append entries per type_id.
	 * Adding the same pointer twice records it twice; use remove_impl to drop
	 * an exact match. The context owns the list storage; @p pointer is stored
	 * as an opaque address and never dereferenced here.
	 * @param context App context (must not be NULL).
	 * @param type_id Type id of the implementation surface.
	 * @param pointer Pointer to the implementation (must not be NULL).
	 */
	void (*add_impl)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer);

	/**
	 * Remove the first implementation entry under @p type_id whose stored
	 * pointer equals @p pointer (exact address match). No-op when @p pointer
	 * is not registered or @p type_id has no implementations.
	 * @param context App context (must not be NULL).
	 * @param type_id Type id of the implementation surface.
	 * @param pointer Pointer previously passed to add_impl.
	 */
	void (*remove_impl)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer);

	/**
	 * Number of implementations registered under @p type_id.
	 * @param context App context (must not be NULL).
	 * @param type_id Type id of the implementation surface.
	 * @return Implementation count, or 0 when the type_id is missing/empty.
	 */
	u32 (*impl_count)(sk_app_context_t* context, sk_type_id_t type_id);

	/**
	 * Copy up to @p out_cap implementation pointers registered under @p type_id
	 * into @p out and return the total count. When @p out is NULL or
	 * @p out_cap is 0, only the count is returned (nothing is written).
	 * Caller owns no storage: entries are copies of internal pointers and stay
	 * valid until removed (or the context is shut down). The order is
	 * insertion order, but swap-remove may reorder after a remove_impl.
	 * @param context App context (must not be NULL).
	 * @param type_id Type id of the implementation surface.
	 * @param out Destination buffer, or NULL to query the count only.
	 * @param out_cap Capacity of @p out in elements.
	 * @return Total implementation count for @p type_id (may exceed @p out_cap).
	 */
	u32 (*get_all_impls)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t* out, u32 out_cap);

	/**
	 * Load a plugin shared library from @p path and call `sk_plugin_entry_point`.
	 * Does not search directories; the caller supplies an exact path.
	 * The library stays loaded until `sk_app_shutdown`.
	 * Does **not** bind a plugin-local logger (there is no `sk_logger_bind_api`).
	 *
	 * @param context App context from `sk_app_init` (must not be NULL; must be bootstrapped).
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

	/**
	 * Host logger context owned by @p context (sinks live here).
	 * NULL on a registry-only context from `sk_app_create`.
	 *
	 * Sinks live on this object. Plugins obtain it from the table given at
	 * load time, not from a process-wide accessor.
	 *
	 * @param context App context (must not be NULL).
	 * @return Logger context, or NULL if this context never started the logger.
	 */
	sk_logger_context_t* (*logger_context)(sk_app_context_t* context);

	/**
	 * Immutable logger function table. Non-NULL after any successful
	 * `sk_app_create` / `sk_app_startup` / `sk_app_init` (the table is
	 * process-lifetime const data; it still requires a logger context to use).
	 *
	 * @param context App context (must not be NULL).
	 * @return Non-NULL pointer to the static `sk_logger_api_t`.
	 */
	const sk_logger_api_t* (*logger_api)(sk_app_context_t* context);

	/**
	 * Named "app" logger created at startup. NULL on registry-only contexts
	 * and after the logger has been torn down during shutdown.
	 *
	 * @param context App context (must not be NULL).
	 * @return Host logger, or NULL.
	 */
	sk_logger_t* (*app_logger)(sk_app_context_t* context);

	/**
	 * Filesystem context owned by @p context (`temp_override`, mmap view list).
	 * NULL on a registry-only context from `sk_app_create`.
	 *
	 * @param context App context (must not be NULL).
	 * @return Filesystem context, or NULL.
	 */
	sk_filesystem_context_t* (*filesystem_context)(sk_app_context_t* context);

	/**
	 * Immutable filesystem function table (OS backend).
	 *
	 * @param context App context (must not be NULL).
	 * @return Non-NULL pointer to the static `sk_filesystem_api_t`.
	 */
	const sk_filesystem_api_t* (*filesystem_api)(sk_app_context_t* context);

	/**
	 * Immutable platform function table (lib_open / clocks). Cached on
	 * startup via `sk_platform_install` + `set_api`; NULL only on a
	 * registry-only context that never ran startup.
	 *
	 * @param context App context (must not be NULL).
	 * @return Platform table, or NULL on registry-only contexts.
	 */
	const sk_platform_api_t* (*platform_api)(sk_app_context_t* context);

	/**
	 * Immutable repository function table.
	 *
	 * @param context App context (must not be NULL).
	 * @return Non-NULL pointer to the static `sk_repository_api_t`.
	 */
	const sk_repository_api_t* (*repository_api)(sk_app_context_t* context);

	/**
	 * Immutable resource-assets engine function table.
	 *
	 * @param context App context (must not be NULL).
	 * @return Non-NULL pointer to the static `sk_resource_assets_api_t`.
	 */
	const sk_resource_assets_api_t* (*resource_assets_api)(sk_app_context_t* context);

	/**
	 * The engine's ECS scene world (owned by the app context).
	 *
	 * Created at bootstrap when the entities plugin is loaded; destroyed at
	 * app shutdown. The engine's frame loop (sk_app_tick) drives the jolt
	 * physics plugin's fixed-step update against this world every frame and
	 * writes the simulated poses back onto the owning transform / rigid-body
	 * state components, so hosts spawn scene entities (see sk_entities_api_t)
	 * into this world and let the engine step them. Returns NULL when the
	 * entities plugin is not loaded (or before bootstrap).
	 *
	 * @param context App context from sk_app_init (must not be NULL).
	 * @return The engine scene world, or NULL when unavailable.
	 */
	sk_world_t* (*scene_world)(sk_app_context_t* context);
} sk_app_api_t;

/**
 * Result of `sk_app_create` / `sk_app_startup` / `sk_app_init`.
 *
 * On success both fields are non-NULL. On failure both fields are NULL
 * (`sk_app_boot_failed()`). Never mix (non-NULL, NULL) or (NULL, non-NULL).
 *
 * @p api points at the process-lifetime immutable table; it remains valid
 * after `sk_app_shutdown`. @p context is owned by the caller until
 * `sk_app_shutdown`.
 */
typedef struct sk_app_boot_t {
	sk_app_context_t* context;
	const sk_app_api_t* api;
} sk_app_boot_t;

/**
 * Failed boot result (`context == NULL`, `api == NULL`).
 * C++-safe (no compound literal). Equivalent to `{NULL, NULL}`.
 */
SK_FINLINE sk_app_boot_t sk_app_boot_failed(void) {
	sk_app_boot_t boot;
	boot.context = NULL;
	boot.api = NULL;
	return boot;
}

/**
 * Create an empty app context (API registry only).
 * No logger, filesystem context, platform registration, plugins, or crash handler.
 * `boot.api` still points at the immutable app table so tests can use
 * `set_api` / `get_api` / `add_impl` without `sk_app_api()`.
 *
 * @return `{context, api}` on success, `sk_app_boot_failed()` on allocation failure.
 */
sk_app_boot_t sk_app_create(void);

/**
 * Create a context with platform + logger context + filesystem context
 * registered. Does not scan plugins and does not install the crash handler.
 * Caller owns `boot.context` (`sk_app_shutdown`).
 *
 * @return `{context, api}` on success, `sk_app_boot_failed()` on failure.
 */
sk_app_boot_t sk_app_startup(void);

/**
 * Application process entry used by player / editor (and tests).
 * `sk_app_startup` + plugin auto-load from `{app_folder}/plugins` + crash
 * handler install (best-effort; a failed crash install does not fail init).
 * Does not enter the main loop; call `sk_app_tick` or `sk_app_run` after.
 *
 * @param argc Argument count from main.
 * @param argv Argument vector from main (borrowed; may be NULL when argc is 0).
 *             Must outlive the context if stored/read later. Currently unused
 *             by bootstrap besides being stored as borrowed pointers.
 * @return `{context, api}` on success, `sk_app_boot_failed()` on failure.
 */
sk_app_boot_t sk_app_init(int argc, char* argv[]);

/**
 * Single teardown for every context from create / startup / init.
 * Unloads plugins, shuts down the profiler cache, destroys the named "app"
 * logger, destroys the logger context, destroys the filesystem context,
 * frees registries, and (if this context installed it) drops one crash-handler
 * refcount. Caller must not use @p context after.
 *
 * @param context Context to shut down (must not be NULL).
 */
void sk_app_shutdown(sk_app_context_t* context);

/**
 * Process one application frame (timing update; future phases/systems).
 * Requires a prior successful `sk_app_init` on @p context. Hosts drive the loop:
 *
 *   while (sk_app_tick(context)) { }
 *
 * @param context App context from `sk_app_init` (must not be NULL).
 * @return Non-zero while the app should keep running; 0 when shutdown was
 *         requested (or when called before bootstrap).
 */
i32 sk_app_tick(sk_app_context_t* context);

/**
 * Run the main loop until `app_api->request_shutdown(context)`.
 * Convenience wrapper around `while (sk_app_tick(context)) {}`.
 * Requires a prior successful `sk_app_init` on @p context.
 *
 * @param context App context from `sk_app_init` (must not be NULL).
 * @return 0 on clean exit, non-zero on failure (not bootstrapped).
 */
i32 sk_app_run(sk_app_context_t* context);

#ifdef SK_TESTS
/**
 * Immutable filesystem table obtained via a temporary `sk_app_create`.
 * Tests without a live context use this instead of a process-wide accessor.
 */
SK_FINLINE const sk_filesystem_api_t* sk_test_filesystem_table(void) {
	static const sk_filesystem_api_t* cached = NULL;
	if (cached == NULL) {
		sk_app_boot_t boot = sk_app_create();
		cached = boot.api->filesystem_api(boot.context);
		sk_app_shutdown(boot.context);
	}
	return cached;
}

/**
 * Immutable repository table obtained via a temporary `sk_app_create`.
 * Tests without a live context use this instead of a process-wide accessor.
 */
SK_FINLINE const sk_repository_api_t* sk_test_repository_table(void) {
	static const sk_repository_api_t* cached = NULL;
	if (cached == NULL) {
		sk_app_boot_t boot = sk_app_create();
		cached = boot.api->repository_api(boot.context);
		sk_app_shutdown(boot.context);
	}
	return cached;
}

/**
 * Immutable platform table obtained via a temporary `sk_app_startup`.
 * Tests without a live context use this instead of a process-wide accessor.
 */
SK_FINLINE const sk_platform_api_t* sk_test_platform_table(void) {
	static const sk_platform_api_t* cached = NULL;
	if (cached == NULL) {
		sk_app_boot_t boot = sk_app_startup();
		cached = boot.api->platform_api(boot.context);
		sk_app_shutdown(boot.context);
	}
	return cached;
}
#endif

#ifdef __cplusplus
}
#endif
