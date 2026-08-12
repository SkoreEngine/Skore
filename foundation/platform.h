#pragma once

/**
 * @file platform.h
 * @brief Host platform module API (shared-library load / symbol resolve, clocks).
 *
 * Types, free-function declarations, and OS backends (Win32 / Unix) that
 * fill sk_platform_api_t live in sk-foundation. sk_app_init registers the
 * default table under SK_PLATFORM_API_TYPE_ID.
 *
 * Hosts obtain the table via the app registry after init:
 *
 *   const sk_platform_api_t* plat =
 *       (const sk_platform_api_t*)app_api->get_api(ctx, SK_PLATFORM_API_TYPE_ID);
 *
 * sk_platform_api() / sk_platform_get_api() expose the static table used for
 * registration (link sk-foundation). Prefer the app registry for production lookup.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_platform_api_t in the app registry. */
#define SK_PLATFORM_API_TYPE_ID SK_TYPE_ID("sk.platform_api", 0xc8034157c7aefdafULL, 0x446abbd73889fff6ULL)

/**
 * Opaque handle to a loaded shared library (DLL / .so / .dylib).
 * NULL means invalid / not loaded.
 */
typedef void_ptr_t sk_shared_lib_t;

/**
 * Global platform module API (one process-wide table).
 *
 * Shared-library load/resolve/unload, last-error text, and host clocks.
 * Registered on the app context by sk_platform_init (sk-foundation); look up with
 * app_api->get_api(ctx, SK_PLATFORM_API_TYPE_ID).
 */
typedef struct sk_platform_api_t {
	/**
     * Load a shared library from an absolute or relative filesystem path.
     *
     * @param path Path to the library file (UTF-8). Must not be NULL.
     * @return Library handle on success, or NULL on failure.
     *         Use lib_error() for a human-readable reason.
     */
	sk_shared_lib_t (*lib_open)(const_chr_t path);

	/**
     * Resolve a named export from a loaded library.
     *
     * @param lib  Handle from lib_open. Must not be NULL.
     * @param name Symbol name (e.g. "sk_plugin_entry_point"). Must not be NULL.
     * @return Pointer to the symbol, or NULL if not found / invalid args.
     */
	void_ptr_t (*lib_symbol)(sk_shared_lib_t lib, const_chr_t name);

	/**
     * Unload a shared library opened with lib_open.
     * Passing NULL is a no-op.
     *
     * @param lib Handle to close.
     */
	void (*lib_close)(sk_shared_lib_t lib);

	/**
     * Last platform error message for the calling thread (best-effort).
     * Valid until the next platform API call that may fail on this thread.
     * Never returns NULL (may return an empty string).
     */
	const_chr_t (*lib_error)(void);

	/**
     * Monotonic clock in seconds since an arbitrary epoch (not wall time).
     * Suitable for frame timing and deltas. Returns 0.0 if the OS query fails.
     *
     * @return Seconds as f64 (non-decreasing under normal conditions).
     */
	f64 (*monotonic_seconds)(void);
} sk_platform_api_t;

/**
 * Fill @p out with the default host platform API table.
 * Implemented in sk-foundation.
 *
 * @param out Destination table; NULL is a no-op.
 */
void sk_platform_get_api(sk_platform_api_t* out);

/**
 * Return a process-lifetime pointer to the default platform API table.
 * Same backend as sk_platform_get_api. Implemented in sk-foundation.
 *
 * @return Non-NULL pointer to a static sk_platform_api_t.
 */
const sk_platform_api_t* sk_platform_api(void);

#ifdef __cplusplus
}
#endif
