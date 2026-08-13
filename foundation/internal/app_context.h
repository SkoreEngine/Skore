#pragma once

/**
 * @file internal/app_context.h
 * @brief One definition of `sk_app_context_t` for foundation TUs only.
 *
 * Not a public header. Plugins and hosts must not include this.
 */

#include "app.h"
#include "allocator.h"
#include "array.h"
#include "hashmap.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sk_profiler_api_t sk_profiler_api_t;

typedef SK_ARRAY(sk_shared_lib_t) plugin_lib_array_t;
typedef SK_HASH_MAP(sk_type_id_t, void_ptr_t) sk_app_api_map_t;
typedef SK_ARRAY(void_ptr_t) sk_app_impl_list_t;
typedef SK_HASH_MAP(sk_type_id_t, sk_app_impl_list_t) sk_app_impl_map_t;

struct sk_app_context_t {
	sk_app_api_map_t apis;
	sk_app_impl_map_t impls;

	const sk_allocator_t* allocator;

	/* Cached immutable tables (set at create; always non-NULL after success). */
	const sk_logger_api_t* logger_api;
	const sk_filesystem_api_t* filesystem_api;
	const sk_repository_api_t* repository_api;
	const sk_resource_assets_api_t* resource_assets_api;

	/* Created by startup/init; NULL after sk_app_create. */
	sk_logger_context_t* logger_ctx;
	sk_logger_t* log;
	sk_filesystem_context_t* fs_ctx;
	const sk_platform_api_t* platform;

	i32 initialized; /* 1 after successful bootstrap_init */
	i32 shutdown_requested;
	i32 loop_started;
	i32 crash_owned; /* 1 if this context called sk_crash_install */
	i32 argc;		 /* borrowed from sk_app_init; grouped with flags */
	u8 _pad0[4];	 /* pointer-align argv (LP64 / LLP64) */
	char** argv;	 /* borrowed; unused by bootstrap besides storage */
	f64 start_seconds;
	f64 last_frame_seconds;
	f64 delta_time;
	f64 fps;
	f64 elapsed_time;
	plugin_lib_array_t plugins;
	const sk_profiler_api_t* profiler_api;
};

#ifdef __cplusplus
}
#endif
