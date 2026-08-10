#include "app.h"

#include "allocator.h"
#include "array.h"
#include "crash.h"
#include "filesystem.h"
#include "hashmap.h"
#include "logger.h"
#include "path.h"
#include "platform.h"

#include <string.h>

/* Platform backend: register static table on the app context (not in core). */
void sk_platform_init(sk_app_context_t* context, const sk_app_api_t* app_api);

/* ---- types ---- */

typedef int (*sk_plugin_entry_point_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

typedef SK_ARRAY(sk_shared_lib_t) plugin_lib_array_t;

/* Typed registry: sk_type_id_t → opaque API pointer. */
typedef SK_HASH_MAP(sk_type_id_t, void_ptr_t) sk_app_api_map_t;

/* Multi-implementation registry: sk_type_id_t → list of opaque pointers. */
typedef SK_ARRAY(void_ptr_t) sk_app_impl_list_t;
typedef SK_HASH_MAP(sk_type_id_t, sk_app_impl_list_t) sk_app_impl_map_t;

/*
 * App context: API registry plus process runtime when used as the
 * sk_app_init instance. Independent contexts from sk_app_create leave
 * runtime fields zeroed (registry only).
 */
struct sk_app_context_t {
	sk_app_api_map_t apis;
	sk_app_impl_map_t impls;

	/* Runtime (timing, plugins, main-loop flags, host logger). */
	i32 initialized;
	i32 shutdown_requested;
	i32 loop_started;
	f64 start_seconds;
	f64 last_frame_seconds;
	f64 delta_time;
	f64 fps;
	f64 elapsed_time;
	plugin_lib_array_t plugins;
	sk_logger_t* log;
	const sk_platform_api_t* platform;
};

/* ---- app logger (named "app"; lifetime tied to context) ---- */

static void app_logger_shutdown(sk_app_context_t* context) {
	if (context->log == NULL) {
		return;
	}
	sk_logger_api()->destroy_logger(context->log);
	context->log = NULL;
}

/**
 * Create the "app" logger and register sk_logger_api on the context.
 * Safe to call again after app_logger_shutdown.
 * @return 0 on success, non-zero if create_logger failed.
 */
static i32 app_logger_startup(sk_app_context_t* context, const sk_app_api_t* api) {
	const sk_logger_api_t* logger_api = sk_logger_api();

	app_logger_shutdown(context);

	/* Host logger API: plugins may also call sk_logger_api() via static core. */
	api->set_api(context, SK_LOGGER_API_TYPE_ID, logger_api);

	context->log = logger_api->create_logger("app");
	if (context->log == NULL) {
		return -1;
	}
	return 0;
}

/* ---- forward decls (static impls wired into sk_app_api_t) ---- */

static i32 sk_app_load_plugin_impl(sk_app_context_t* context, const_chr_t path);
static void sk_app_request_shutdown_impl(sk_app_context_t* context);
static f64 sk_app_delta_time_impl(sk_app_context_t* context);
static f64 sk_app_fps_impl(sk_app_context_t* context);
static f64 sk_app_elapsed_time_impl(sk_app_context_t* context);
static i32 sk_app_bootstrap_init(sk_app_context_t* context);
static void sk_app_bootstrap_shutdown(sk_app_context_t* context);

/* ---- registry backends ---- */

static void sk_app_set_api_impl(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer) {
	/* Hash map rejects NULL values; treat NULL as unregister. */
	if (pointer == NULL) {
		(void)sk_hash_map_remove(&context->apis, type_id);
		return;
	}
	/* Registry stores opaque addresses; API tables remain immutable after register. */
	(void)sk_hash_map_put(&context->apis, type_id, SK_CONST_CAST(void_ptr_t, pointer));
}

static void_ptr_t sk_app_get_api_impl(sk_app_context_t* context, sk_type_id_t type_id) {
	void_ptr_t out = NULL;

	if (sk_hash_map_get(&context->apis, type_id, &out) != 0) {
		return NULL;
	}
	return out;
}

/* ---- multi-implementation registry backends ---- */

/** Implementation list for @p type_id, or NULL when the type_id is unknown. */
static sk_app_impl_list_t* impl_list_for(sk_app_context_t* context, sk_type_id_t type_id) {
	return (sk_app_impl_list_t*)sk_hash_map_get_ptr(&context->impls, type_id);
}

static void sk_app_add_impl_impl(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer) {
	sk_app_impl_list_t* list = impl_list_for(context, type_id);
	if (list == NULL) {
		sk_app_impl_list_t fresh;
		sk_array_init(&fresh, sk_allocator_default());
		if (sk_hash_map_put(&context->impls, type_id, fresh) != 0) {
			return;
		}
		list = impl_list_for(context, type_id);
	}
	/* Duplicates are allowed: each add records another entry. */
	(void)sk_array_push(list, SK_CONST_CAST(void_ptr_t, pointer));
}

static void sk_app_remove_impl_impl(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer) {
	sk_app_impl_list_t* list = impl_list_for(context, type_id);
	if (list == NULL) {
		return;
	}
	for (u32 i = 0u; i < list->count; i++) {
		if ((const_ptr_t)list->items[i] == pointer) {
			sk_array_swap_remove(list, i);
			return;
		}
	}
}

static u32 sk_app_impl_count_impl(sk_app_context_t* context, sk_type_id_t type_id) {
	sk_app_impl_list_t* list = impl_list_for(context, type_id);
	return (list == NULL) ? 0u : list->count;
}

static u32 sk_app_get_all_impls_impl(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t* out, u32 out_cap) {
	sk_app_impl_list_t* list = impl_list_for(context, type_id);
	if (list == NULL) {
		return 0u;
	}
	if (out != NULL) {
		u32 n = (out_cap < list->count) ? out_cap : list->count;
		for (u32 i = 0u; i < n; i++) {
			out[i] = (const_ptr_t)list->items[i];
		}
	}
	return list->count;
}

static const sk_app_api_t app_api = {
	sk_app_set_api_impl,	 sk_app_get_api_impl,		   sk_app_add_impl_impl,   sk_app_remove_impl_impl, sk_app_impl_count_impl,	  sk_app_get_all_impls_impl,
	sk_app_load_plugin_impl, sk_app_request_shutdown_impl, sk_app_delta_time_impl, sk_app_fps_impl,			sk_app_elapsed_time_impl,
};

/* ---- context create / destroy / API table ---- */

sk_app_context_t* sk_app_create(void) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_app_context_t* context = (sk_app_context_t*)alloc->alloc(alloc->instance, sizeof(sk_app_context_t));
	if (context == NULL) {
		return NULL;
	}
	memset(context, 0, sizeof(*context));

	if (sk_hash_map_init(&context->apis, alloc, NULL, NULL) != 0) {
		alloc->free(alloc->instance, context);
		return NULL;
	}
	if (sk_hash_map_init(&context->impls, alloc, NULL, NULL) != 0) {
		sk_hash_map_free(&context->apis);
		alloc->free(alloc->instance, context);
		return NULL;
	}
	return context;
}

void sk_app_destroy(sk_app_context_t* context) {
	/* Restore pre-app crash handling; safe when never installed (registry-only
	 * contexts) and idempotent across repeated destroys. */
	sk_crash_uninstall();

	/* Unload plugins before free; process re-entry is explicit destroy + new init. */
	if (context->plugins.items != NULL) {
		const sk_platform_api_t* plat = (const sk_platform_api_t*)sk_app_get_api_impl(context, SK_PLATFORM_API_TYPE_ID);
		if (plat != NULL) {
			for (u32 i = 0u; i < context->plugins.count; i++) {
				plat->lib_close(context->plugins.items[i]);
			}
		}
		sk_array_free(&context->plugins);
	}
	app_logger_shutdown(context);

	/* Free every implementation list before the impl map itself. */
	const sk_hash_map_t* impl_map = &context->impls._hm;
	for (u32 slot = 0u; slot < impl_map->capacity; slot++) {
		if (sk_hash_map_slot_occupied_(impl_map, slot) != 0) {
			sk_array_free((sk_app_impl_list_t*)sk_hash_map_value_at_(&context->impls._hm, slot));
		}
	}
	sk_hash_map_free(&context->impls);

	sk_hash_map_free(&context->apis);

	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, context);
}

const sk_app_api_t* sk_app_api(void) {
	return &app_api;
}

sk_app_context_t* sk_app_startup(void) {
	sk_app_context_t* context = sk_app_create();
	if (context == NULL) {
		return NULL;
	}

	/* Host platform API: register static table for app_api->get_api lookup. */
	sk_platform_init(context, &app_api);

	/* Cache the platform table on the context; registered above, valid for the
	 * context lifetime. Independent contexts (sk_app_create) keep it NULL. */
	context->platform = (const sk_platform_api_t*)sk_app_get_api_impl(context, SK_PLATFORM_API_TYPE_ID);

	/* Named "app" logger + SK_LOGGER_API_TYPE_ID on the context registry. */
	if (app_logger_startup(context, &app_api) != 0) {
		sk_app_destroy(context);
		return NULL;
	}

	sk_log_info(sk_logger_api(), context->log, "app startup complete");
	return context;
}

/* ---- timing via platform monotonic clock ---- */

/** Platform API from the app registry (valid after sk_app_startup). */
static const sk_platform_api_t* app_platform_api(sk_app_context_t* context) {
	return context->platform;
}

static f64 monotonic_seconds(sk_app_context_t* context) {
	return app_platform_api(context)->monotonic_seconds();
}

static void bootstrap_reset_timing(sk_app_context_t* context) {
	f64 now = monotonic_seconds(context);

	context->start_seconds = now;
	context->last_frame_seconds = now;
	context->delta_time = 0.0;
	context->fps = 0.0;
	context->elapsed_time = 0.0;
}

static void bootstrap_tick_timing(sk_app_context_t* context) {
	f64 now = monotonic_seconds(context);
	f64 dt = now - context->last_frame_seconds;

	if (dt < 0.0) {
		dt = 0.0;
	}

	context->delta_time = dt;
	context->last_frame_seconds = now;
	context->elapsed_time = now - context->start_seconds;
	if (context->elapsed_time < 0.0) {
		context->elapsed_time = 0.0;
	}
	context->fps = (dt > 0.0) ? (1.0 / dt) : 0.0;
}

static void unload_plugins(sk_app_context_t* context) {
	u32 count = context->plugins.count;
	if (count == 0u) {
		sk_array_free(&context->plugins);
		return;
	}

	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "unloading %u plugin(s)", count);
	}

	const sk_platform_api_t* plat = app_platform_api(context);
	for (u32 i = 0u; i < count; i++) {
		plat->lib_close(context->plugins.items[i]);
	}
	sk_array_free(&context->plugins);
}

/* ---- auto-load shared libraries from {app_folder}/plugins ---- */

/**
 * Scan @p dir for shared libraries and load each via sk_app_load_plugin_impl.
 * Missing files / bad plugins are skipped (best-effort). Always returns 0.
 */
static i32 load_plugins_from_directory(sk_app_context_t* context, const_chr_t dir) {
	char name[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	u32 attempted = 0u;
	u32 loaded = 0u;

	if (dir[0] == '\0') {
		return 0;
	}

	const sk_filesystem_api_t* fs = sk_filesystem_api();
	const sk_platform_api_t* plat = app_platform_api(context);
	sk_directory_iterator_t it = fs->open_directory(dir);
	if (it == NULL) {
		if (context->log != NULL) {
			sk_log_warn(sk_logger_api(), context->log, "could not open plugins directory: %s", dir);
		}
		return 0;
	}

	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "scanning plugins directory: %s", dir);
	}

	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		if (!sk_is_shared_library_filename(name)) {
			continue;
		}
		if (sk_path_join(sk_str_view_cstr(dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path)) < 0) {
			continue;
		}
		/* Skip shared libraries that are not plugins (e.g. the vendored DXC
		 * runtime copied into the plugins dir by sk_copy_dxc_shared_library);
		 * only real plugins export sk_plugin_entry_point. */
		sk_shared_lib_t lib = plat->lib_open(full_path);
		if (lib == NULL) {
			continue;
		}
		void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
		plat->lib_close(lib);
		if (raw == NULL) {
			continue;
		}
		attempted += 1u;
		/* Best-effort: a single bad plugin must not abort the rest. */
		if (sk_app_load_plugin_impl(context, full_path) == 0) {
			loaded += 1u;
		}
	}

	fs->close_directory(it);
	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "plugins directory scan done: loaded %u of %u", loaded, attempted);
	}
	return 0;
}

/**
 * Resolve {app_folder}/plugins (fall back to {cwd}/plugins) and load every
 * shared library found there. Missing directory is not an error.
 */
static void load_plugins_auto(sk_app_context_t* context) {
	char base[SK_FS_PATH_MAX];
	char plugins_dir[SK_FS_PATH_MAX];

	const sk_filesystem_api_t* fs = sk_filesystem_api();

	base[0] = '\0';
	/* Prefer the directory of the running executable; fall back to process cwd. */
	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		if (context->log != NULL) {
			sk_log_warn(sk_logger_api(), context->log, "plugin auto-load skipped: could not resolve app or cwd folder");
		}
		return;
	}

	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		if (context->log != NULL) {
			sk_log_warn(sk_logger_api(), context->log, "plugin auto-load skipped: path join failed");
		}
		return;
	}

	if (fs->get_file_status(plugins_dir) != SK_FILE_STATUS_DIRECTORY) {
		if (context->log != NULL) {
			sk_log_debug(sk_logger_api(), context->log, "no plugins directory at %s", plugins_dir);
		}
		return;
	}

	(void)load_plugins_from_directory(context, plugins_dir);
}

static i32 sk_app_bootstrap_init(sk_app_context_t* context) {
	sk_app_bootstrap_shutdown(context);

	/* Clear runtime fields; keep registry, platform, and logger. */
	context->initialized = 0;
	context->shutdown_requested = 0;
	context->loop_started = 0;
	context->start_seconds = 0.0;
	context->last_frame_seconds = 0.0;
	context->delta_time = 0.0;
	context->fps = 0.0;
	context->elapsed_time = 0.0;
	sk_array_init(&context->plugins, sk_allocator_default());
	bootstrap_reset_timing(context);
	context->initialized = 1;
	context->shutdown_requested = 0;

	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "bootstrap init");
	}

	/* Auto-load every shared library under {app_folder}/plugins. */
	load_plugins_auto(context);

	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "bootstrap ready (%u plugin(s) loaded)", context->plugins.count);
	}

	return 0;
}

static void sk_app_bootstrap_shutdown(sk_app_context_t* context) {
	if (context->initialized == 0 && context->plugins.items == NULL) {
		return;
	}

	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "bootstrap shutdown");
	}

	unload_plugins(context);
	context->initialized = 0;
	context->shutdown_requested = 0;
	context->loop_started = 0;
	context->start_seconds = 0.0;
	context->last_frame_seconds = 0.0;
	context->delta_time = 0.0;
	context->fps = 0.0;
	context->elapsed_time = 0.0;
}

static i32 sk_app_load_plugin_impl(sk_app_context_t* context, const_chr_t path) {
	if (context->initialized == 0) {
		if (context->log != NULL) {
			sk_log_error(sk_logger_api(), context->log, "load_plugin before bootstrap: %s", path);
		}
		return -1;
	}

	const sk_logger_api_t* logger_api = sk_logger_api();
	if (context->log != NULL) {
		sk_log_info(logger_api, context->log, "loading plugin: %s", path);
	}

	const sk_platform_api_t* plat = app_platform_api(context);
	sk_shared_lib_t lib = plat->lib_open(path);
	if (lib == NULL) {
		if (context->log != NULL) {
			sk_log_error(logger_api, context->log, "plugin open failed: %s (%s)", path, plat->lib_error());
		}
		return -1;
	}

	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	if (raw == NULL) {
		if (context->log != NULL) {
			sk_log_error(logger_api, context->log, "plugin missing sk_plugin_entry_point: %s (%s)", path, plat->lib_error());
		}
		plat->lib_close(lib);
		return -1;
	}

	sk_plugin_entry_point_fn entry = SK_PTR_TO_FN(sk_plugin_entry_point_fn, raw);
	i32 rc = entry(context, sk_app_api());
	if (rc != 0) {
		if (context->log != NULL) {
			sk_log_error(logger_api, context->log, "plugin entry failed (%d): %s", rc, path);
		}
		plat->lib_close(lib);
		return rc;
	}

	if (sk_array_push(&context->plugins, lib) != 0) {
		if (context->log != NULL) {
			sk_log_error(logger_api, context->log, "plugin list full / alloc failed: %s", path);
		}
		plat->lib_close(lib);
		return -1;
	}

	if (context->log != NULL) {
		sk_log_info(logger_api, context->log, "plugin loaded: %s", path);
	}
	return 0;
}

static void sk_app_request_shutdown_impl(sk_app_context_t* context) {
	if (context->log != NULL && context->shutdown_requested == 0) {
		sk_log_info(sk_logger_api(), context->log, "shutdown requested");
	}
	context->shutdown_requested = 1;
}

static f64 sk_app_delta_time_impl(sk_app_context_t* context) {
	return context->delta_time;
}

static f64 sk_app_fps_impl(sk_app_context_t* context) {
	return context->fps;
}

static f64 sk_app_elapsed_time_impl(sk_app_context_t* context) {
	if (context->initialized == 0) {
		return 0.0;
	}

	/* Live wall time since bootstrap init (works inside and outside the loop). */
	f64 now = monotonic_seconds(context);
	f64 elapsed = now - context->start_seconds;
	return (elapsed > 0.0) ? elapsed : 0.0;
}

/* ---- process lifecycle (public; declared in core/app.h) ---- */

sk_app_context_t* sk_app_init(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	sk_app_context_t* context = sk_app_startup();
	if (context == NULL) {
		return NULL;
	}
	if (sk_app_bootstrap_init(context) != 0) {
		if (context->log != NULL) {
			sk_log_error(sk_logger_api(), context->log, "bootstrap init failed");
		}
		sk_app_destroy(context);
		return NULL;
	}
	/* Fatal-fault reporting: print a stacktrace to stderr, then die with the
	 * platform's normal termination. Best-effort: a failed install must not
	 * prevent the app from starting. Embedders opt out via sk_crash_uninstall. */
	if (sk_crash_install() != 0 && context->log != NULL) {
		sk_log_warn(sk_logger_api(), context->log, "crash handler install failed");
	}

	if (context->log != NULL) {
		sk_log_info(sk_logger_api(), context->log, "app init complete");
	}
	return context;
}

i32 sk_app_tick(sk_app_context_t* context) {
	if (context->initialized == 0) {
		return 0;
	}

	/* Do not clear shutdown_requested: a prior request_shutdown() must make
	 * the host loop exit immediately (tests / hosts). */
	if (context->shutdown_requested != 0) {
		if (context->loop_started != 0) {
			if (context->log != NULL) {
				sk_log_info(sk_logger_api(), context->log, "main loop exit (elapsed %.3f s)", context->elapsed_time);
			}
			context->loop_started = 0;
		}
		return 0;
	}

	/* First tick of a loop: fresh timing baseline (init time is not frame 0). */
	if (context->loop_started == 0) {
		bootstrap_reset_timing(context);
		context->loop_started = 1;
		if (context->log != NULL) {
			sk_log_info(sk_logger_api(), context->log, "main loop enter");
		}
	}

	bootstrap_tick_timing(context);
	/* Future: frame phases / systems. Nothing else in empty bootstrap. */

	if (context->shutdown_requested != 0) {
		if (context->log != NULL) {
			sk_log_info(sk_logger_api(), context->log, "main loop exit (elapsed %.3f s)", context->elapsed_time);
		}
		context->loop_started = 0;
		return 0;
	}
	return 1;
}

i32 sk_app_run(sk_app_context_t* context) {
	if (context->initialized == 0) {
		return -1;
	}

	while (sk_app_tick(context)) {
	}

	return 0;
}

#ifdef SK_TESTS
#include "test.h"
#include "filesystem.h"
#include "logger.h"
#include "platform.h"
#include "platform_window.h"
#include "entities.h"
#include "dxc_compiler.h"
#include "render_graph.h"
#include "render_pipeline.h"

#include <stdio.h>
#include <string.h>

/* ---- helpers ---- */

static void test_make_fs_paths(char* dir, u32 dir_cap, char* file_a, u32 a_cap, char* file_b, u32 b_cap) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	char temp[SK_FS_PATH_MAX];

	TEST_ASSERT_EQUAL_INT32(0, api->temp_folder(temp, (u32)sizeof(temp)));
	int n = snprintf(dir, dir_cap, "%s/skore_fs_test_dir", temp);
	TEST_ASSERT_TRUE(n > 0 && (u32)n < dir_cap);
	n = snprintf(file_a, a_cap, "%s/skore_fs_test_a.bin", dir);
	TEST_ASSERT_TRUE(n > 0 && (u32)n < a_cap);
	n = snprintf(file_b, b_cap, "%s/skore_fs_test_b.bin", dir);
	TEST_ASSERT_TRUE(n > 0 && (u32)n < b_cap);
}

static void test_cleanup_fs_tree(const char* dir, const char* file_a, const char* file_b) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	(void)api->remove(file_a);
	(void)api->remove(file_b);
	(void)api->remove(dir);
}

static i32 test_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];
	char plugins[SK_FS_PATH_MAX];

	if (fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') {
		if (fs->current_dir(base, (u32)sizeof(base)) != 0) {
			return -1;
		}
	}
	i32 n = sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins, (u32)sizeof(plugins));
	if (n < 0) {
		return -1;
	}
	n = sk_path_join(sk_str_view_cstr(plugins), sk_str_view_cstr(plugin_filename), out, out_cap);
	return (n < 0) ? -1 : 0;
}

/* ---- platform ---- */

SK_TEST(platform_api_table_is_complete) {
	const sk_platform_api_t* api = sk_platform_api();
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_NOT_NULL(api->lib_open);
	TEST_ASSERT_NOT_NULL(api->lib_symbol);
	TEST_ASSERT_NOT_NULL(api->lib_close);
	TEST_ASSERT_NOT_NULL(api->lib_error);
	TEST_ASSERT_NOT_NULL(api->monotonic_seconds);
}

SK_TEST(platform_get_api_matches_static_table) {
	sk_platform_api_t out;
	memset(&out, 0, sizeof(out));
	sk_platform_get_api(&out);
	const sk_platform_api_t* def = sk_platform_api();
	TEST_ASSERT_EQUAL_PTR(def->lib_open, out.lib_open);
	TEST_ASSERT_EQUAL_PTR(def->lib_symbol, out.lib_symbol);
	TEST_ASSERT_EQUAL_PTR(def->lib_close, out.lib_close);
	TEST_ASSERT_EQUAL_PTR(def->lib_error, out.lib_error);
	TEST_ASSERT_EQUAL_PTR(def->monotonic_seconds, out.monotonic_seconds);
}

SK_TEST(platform_lib_open_missing_file_fails) {
	const sk_platform_api_t* api = sk_platform_api();
	sk_shared_lib_t lib = api->lib_open("skore_definitely_missing_plugin_xyz.so");
	TEST_ASSERT_NULL(lib);
	TEST_ASSERT_NOT_NULL(api->lib_error());
	TEST_ASSERT_TRUE(api->lib_error()[0] != '\0');
}

SK_TEST(platform_lib_error_never_null) {
	TEST_ASSERT_NOT_NULL(sk_platform_api()->lib_error());
}

SK_TEST(platform_monotonic_seconds_advances) {
	const sk_platform_api_t* api = sk_platform_api();
	f64 a = api->monotonic_seconds();
	f64 b = api->monotonic_seconds();
	TEST_ASSERT_TRUE(a >= 0.0);
	TEST_ASSERT_TRUE(b >= a);
}

SK_TEST(platform_type_id_nonzero) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_PLATFORM_API_TYPE_ID, SK_TYPE_ID_ZERO));
}

/* ---- filesystem ---- */

SK_TEST(filesystem_api_table_is_complete) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_NOT_NULL(api->setup_temp_folder);
	TEST_ASSERT_NOT_NULL(api->current_dir);
	TEST_ASSERT_NOT_NULL(api->documents_dir);
	TEST_ASSERT_NOT_NULL(api->app_folder);
	TEST_ASSERT_NOT_NULL(api->temp_folder);
	TEST_ASSERT_NOT_NULL(api->get_file_status);
	TEST_ASSERT_NOT_NULL(api->get_path_size);
	TEST_ASSERT_NOT_NULL(api->get_file_id);
	TEST_ASSERT_NOT_NULL(api->create_directory);
	TEST_ASSERT_NOT_NULL(api->remove);
	TEST_ASSERT_NOT_NULL(api->rename);
	TEST_ASSERT_NOT_NULL(api->copy_file);
	TEST_ASSERT_NOT_NULL(api->open_file);
	TEST_ASSERT_NOT_NULL(api->get_file_size);
	TEST_ASSERT_NOT_NULL(api->write_file);
	TEST_ASSERT_NOT_NULL(api->read_file);
	TEST_ASSERT_NOT_NULL(api->read_file_at);
	TEST_ASSERT_NOT_NULL(api->close_file);
	TEST_ASSERT_NOT_NULL(api->create_file_mapping);
	TEST_ASSERT_NOT_NULL(api->map_view_of_file);
	TEST_ASSERT_NOT_NULL(api->unmap_view_of_file);
	TEST_ASSERT_NOT_NULL(api->close_file_mapping);
	TEST_ASSERT_NOT_NULL(api->open_directory);
	TEST_ASSERT_NOT_NULL(api->next_directory);
	TEST_ASSERT_NOT_NULL(api->close_directory);
}

SK_TEST(filesystem_get_api_matches_static_table) {
	sk_filesystem_api_t out;
	const sk_filesystem_api_t* def = sk_filesystem_api();
	memset(&out, 0, sizeof(out));
	sk_filesystem_get_api(&out);
	TEST_ASSERT_EQUAL_PTR(def->open_file, out.open_file);
	TEST_ASSERT_EQUAL_PTR(def->close_file, out.close_file);
	TEST_ASSERT_EQUAL_PTR(def->get_file_status, out.get_file_status);
}

SK_TEST(filesystem_path_queries) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	char buf[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT32(0, api->current_dir(buf, (u32)sizeof(buf)));
	TEST_ASSERT_TRUE(buf[0] != '\0');
	TEST_ASSERT_EQUAL_INT32(0, api->temp_folder(buf, (u32)sizeof(buf)));
	TEST_ASSERT_TRUE(buf[0] != '\0');
	TEST_ASSERT_EQUAL_INT32(0, api->app_folder(buf, (u32)sizeof(buf)));
	TEST_ASSERT_TRUE(buf[0] != '\0');
	TEST_ASSERT_EQUAL_INT32(0, api->documents_dir(buf, (u32)sizeof(buf)));
	TEST_ASSERT_TRUE(buf[0] != '\0');
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->current_dir(buf, 0));
}

SK_TEST(filesystem_setup_temp_folder_override) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	char before[SK_FS_PATH_MAX];
	char after[SK_FS_PATH_MAX];
	char cwd[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT32(0, api->temp_folder(before, (u32)sizeof(before)));
	TEST_ASSERT_EQUAL_INT32(0, api->current_dir(cwd, (u32)sizeof(cwd)));
	api->setup_temp_folder(cwd);
	TEST_ASSERT_EQUAL_INT32(0, api->temp_folder(after, (u32)sizeof(after)));
	TEST_ASSERT_EQUAL_STRING(cwd, after);
	api->setup_temp_folder(NULL);
	TEST_ASSERT_EQUAL_INT32(0, api->temp_folder(after, (u32)sizeof(after)));
	TEST_ASSERT_EQUAL_STRING(before, after);
}

SK_TEST(filesystem_status_missing_and_null) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_NOT_FOUND, api->get_file_status(""));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_NOT_FOUND, api->get_file_status("skore_definitely_missing_fs_xyz_99"));
	TEST_ASSERT_EQUAL_UINT64(0ull, api->get_path_size("skore_definitely_missing_fs_xyz_99"));
	TEST_ASSERT_EQUAL_UINT64(0ull, api->get_file_id(""));
}

SK_TEST(filesystem_create_write_read_remove) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	char dir[SK_FS_PATH_MAX];
	char path_a[SK_FS_PATH_MAX];
	char path_b[SK_FS_PATH_MAX];
	const char payload[] = "hello-skore-fs";
	char read_buf[64];

	test_make_fs_paths(dir, (u32)sizeof(dir), path_a, (u32)sizeof(path_a), path_b, (u32)sizeof(path_b));
	test_cleanup_fs_tree(dir, path_a, path_b);

	TEST_ASSERT_EQUAL_INT32(0, api->create_directory(dir));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_DIRECTORY, api->get_file_status(dir));
	TEST_ASSERT_EQUAL_INT32(0, api->create_directory(dir));

	sk_file_handle_t file = api->open_file(path_a, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	u64 n = api->write_file(file, payload, sizeof(payload) - 1u);
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, n);
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, api->get_file_size(file));
	api->close_file(file);

	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, api->get_file_status(path_a));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, api->get_path_size(path_a));
	TEST_ASSERT_TRUE(api->get_file_id(path_a) != 0ull);

	file = api->open_file(path_a, SK_FILE_ACCESS_READ);
	TEST_ASSERT_NOT_NULL(file);
	memset(read_buf, 0, sizeof(read_buf));
	n = api->read_file(file, read_buf, sizeof(read_buf));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, n);
	TEST_ASSERT_EQUAL_MEMORY(payload, read_buf, (size_t)n);
	memset(read_buf, 0, sizeof(read_buf));
	n = api->read_file_at(file, read_buf, 5u, 6ull);
	TEST_ASSERT_EQUAL_UINT64(5ull, n);
	TEST_ASSERT_EQUAL_MEMORY("skore", read_buf, 5u);
	api->close_file(file);

	TEST_ASSERT_EQUAL_INT32(0, api->copy_file(path_a, path_b));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, api->get_file_status(path_b));
	TEST_ASSERT_EQUAL_INT32(0, api->remove(path_b));
	TEST_ASSERT_EQUAL_INT32(0, api->rename(path_a, path_b));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_NOT_FOUND, api->get_file_status(path_a));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, api->get_file_status(path_b));
	TEST_ASSERT_EQUAL_INT32(0, api->remove(path_b));
	TEST_ASSERT_EQUAL_INT32(0, api->remove(dir));
}

SK_TEST(filesystem_open_rejects_bad_args) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	TEST_ASSERT_NULL(api->open_file("", SK_FILE_ACCESS_READ));
	TEST_ASSERT_NULL(api->open_file("skore_missing_open_xyz", SK_FILE_ACCESS_READ));
}

SK_TEST(filesystem_file_mapping_roundtrip) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	char dir[SK_FS_PATH_MAX];
	char path_a[SK_FS_PATH_MAX];
	char path_b[SK_FS_PATH_MAX];
	void_ptr_t view;
	const char payload[] = "map-view-data";

	test_make_fs_paths(dir, (u32)sizeof(dir), path_a, (u32)sizeof(path_a), path_b, (u32)sizeof(path_b));
	test_cleanup_fs_tree(dir, path_a, path_b);
	TEST_ASSERT_EQUAL_INT32(0, api->create_directory(dir));
	sk_file_handle_t file = api->open_file(path_a, SK_FILE_ACCESS_READ_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	u64 n = api->write_file(file, payload, sizeof(payload) - 1u);
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, n);
	sk_file_handle_t mapping = api->create_file_mapping(file, SK_FILE_ACCESS_READ, 0ull);
	TEST_ASSERT_NOT_NULL(mapping);
	view = api->map_view_of_file(mapping);
	TEST_ASSERT_NOT_NULL(view);
	TEST_ASSERT_EQUAL_MEMORY(payload, view, sizeof(payload) - 1u);
	TEST_ASSERT_EQUAL_INT32(0, api->unmap_view_of_file(view));
	api->close_file_mapping(mapping);
	api->close_file(file);
	test_cleanup_fs_tree(dir, path_a, path_b);
}

SK_TEST(filesystem_directory_iterator_lists_entries) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	char dir[SK_FS_PATH_MAX];
	char path_a[SK_FS_PATH_MAX];
	char path_b[SK_FS_PATH_MAX];
	char name[SK_FS_PATH_MAX];
	i32 saw_a = 0, saw_b = 0, saw_dot = 0, saw_dotdot = 0;

	test_make_fs_paths(dir, (u32)sizeof(dir), path_a, (u32)sizeof(path_a), path_b, (u32)sizeof(path_b));
	test_cleanup_fs_tree(dir, path_a, path_b);
	TEST_ASSERT_EQUAL_INT32(0, api->create_directory(dir));
	sk_file_handle_t file = api->open_file(path_a, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	api->close_file(file);
	file = api->open_file(path_b, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	api->close_file(file);

	sk_directory_iterator_t it = api->open_directory(dir);
	TEST_ASSERT_NOT_NULL(it);
	while (api->next_directory(it, name, (u32)sizeof(name)) == 0) {
		if (strcmp(name, ".") == 0)
			saw_dot = 1;
		else if (strcmp(name, "..") == 0)
			saw_dotdot = 1;
		else if (strcmp(name, "skore_fs_test_a.bin") == 0)
			saw_a = 1;
		else if (strcmp(name, "skore_fs_test_b.bin") == 0)
			saw_b = 1;
	}
	api->close_directory(it);
	TEST_ASSERT_TRUE(saw_a);
	TEST_ASSERT_TRUE(saw_b);
	TEST_ASSERT_TRUE(saw_dot);
	TEST_ASSERT_TRUE(saw_dotdot);
	test_cleanup_fs_tree(dir, path_a, path_b);
}

SK_TEST(filesystem_open_directory_missing_fails) {
	const sk_filesystem_api_t* api = sk_filesystem_api();
	TEST_ASSERT_NULL(api->open_directory("skore_definitely_missing_dir_xyz_99"));
	TEST_ASSERT_NULL(api->open_directory(""));
}

SK_TEST(is_shared_library_filename) {
	TEST_ASSERT_EQUAL_INT32(0, sk_is_shared_library_filename(""));
	TEST_ASSERT_EQUAL_INT32(0, sk_is_shared_library_filename(".hidden"));
	TEST_ASSERT_EQUAL_INT32(0, sk_is_shared_library_filename("readme.txt"));
#if defined(_WIN32)
	TEST_ASSERT_EQUAL_INT32(1, sk_is_shared_library_filename("plugin.dll"));
	TEST_ASSERT_EQUAL_INT32(1, sk_is_shared_library_filename("Plugin.DLL"));
	TEST_ASSERT_EQUAL_INT32(0, sk_is_shared_library_filename("plugin.so"));
#elif defined(__APPLE__)
	TEST_ASSERT_EQUAL_INT32(1, sk_is_shared_library_filename("libplugin.dylib"));
#else
	TEST_ASSERT_EQUAL_INT32(1, sk_is_shared_library_filename("libplugin.so"));
	TEST_ASSERT_EQUAL_INT32(1, sk_is_shared_library_filename("libplugin.so.1"));
#endif
}

/* ---- app lifecycle / integration ---- */

SK_TEST(app_init_returns_success) {
	char arg0[] = "sk-tests";
	char* argv[] = {arg0, NULL};
	sk_app_context_t* ctx = sk_app_init(1, argv);
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_destroy(ctx);
}

SK_TEST(app_init_accepts_null_argv_with_zero_argc) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_destroy(ctx);
}

SK_TEST(app_init_creates_context_and_api_table) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_NULL(sk_app_api());
	TEST_ASSERT_NOT_NULL(sk_app_api()->set_api);
	TEST_ASSERT_NOT_NULL(sk_app_api()->get_api);
	TEST_ASSERT_NOT_NULL(sk_app_api()->add_impl);
	TEST_ASSERT_NOT_NULL(sk_app_api()->remove_impl);
	TEST_ASSERT_NOT_NULL(sk_app_api()->impl_count);
	TEST_ASSERT_NOT_NULL(sk_app_api()->get_all_impls);
	TEST_ASSERT_NOT_NULL(sk_app_api()->load_plugin);
	TEST_ASSERT_NOT_NULL(sk_app_api()->request_shutdown);
	TEST_ASSERT_NOT_NULL(sk_app_api()->delta_time);
	TEST_ASSERT_NOT_NULL(sk_app_api()->fps);
	TEST_ASSERT_NOT_NULL(sk_app_api()->elapsed_time);
	sk_app_destroy(ctx);
}

SK_TEST(app_init_registers_platform_api) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	const sk_platform_api_t* plat = (const sk_platform_api_t*)api->get_api(ctx, SK_PLATFORM_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(plat);
	TEST_ASSERT_EQUAL_PTR(sk_platform_api(), plat);
	sk_app_destroy(ctx);
}

SK_TEST(app_init_registers_logger_api) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();

	const sk_logger_api_t* logger_api = (const sk_logger_api_t*)api->get_api(ctx, SK_LOGGER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(logger_api, "logger API must be registered on app startup");
	TEST_ASSERT_EQUAL_PTR(sk_logger_api(), logger_api);
	TEST_ASSERT_NOT_NULL(logger_api->create_logger);
	TEST_ASSERT_NOT_NULL(logger_api->destroy_logger);
	TEST_ASSERT_NOT_NULL(logger_api->message);

	sk_logger_t* log = logger_api->create_logger("integration-test");
	TEST_ASSERT_NOT_NULL(log);
	TEST_ASSERT_EQUAL_STRING("integration-test", sk_logger_name(log));
	sk_log_info(logger_api, log, "logger registry smoke");
	logger_api->destroy_logger(log);
	sk_app_destroy(ctx);
}

SK_TEST(app_set_get_api_roundtrip) {
	static int dummy_api = 42;
	sk_type_id_t id = SK_TYPE_ID("test.dummy_api", 0x1111111111111111ULL, 0x2222222222222222ULL);
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_api()->set_api(ctx, id, &dummy_api);
	TEST_ASSERT_EQUAL_PTR(&dummy_api, sk_app_api()->get_api(ctx, id));
	TEST_ASSERT_EQUAL_INT(42, *(int*)sk_app_api()->get_api(ctx, id));
	sk_app_destroy(ctx);
}

SK_TEST(app_get_api_missing_returns_null) {
	sk_type_id_t id = SK_TYPE_ID("test.missing_api", 0xAAAAAAAAAAAAAAAAULL, 0xBBBBBBBBBBBBBBBBULL);
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NULL(sk_app_api()->get_api(ctx, id));
	sk_app_destroy(ctx);
}

SK_TEST(app_create_destroy_independent_context) {
	static char marker = 'x';
	sk_type_id_t id = SK_TYPE_ID("test.local_api", 0x0101010101010101ULL, 0x0202020202020202ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_api()->set_api(ctx, id, &marker);
	TEST_ASSERT_EQUAL_PTR(&marker, sk_app_api()->get_api(ctx, id));
	sk_app_destroy(ctx);
}

/* ---- multi-implementation registry ---- */

SK_TEST(app_impl_add_count_roundtrip) {
	static char a = 'a', b = 'b', c = 'c';
	sk_type_id_t id = SK_TYPE_ID("test.multi_impl", 0x1111111111111111ULL, 0x3333333333333333ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();

	TEST_ASSERT_EQUAL_UINT32(0u, api->impl_count(ctx, id));
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &b);
	api->add_impl(ctx, id, &c);
	TEST_ASSERT_EQUAL_UINT32(3u, api->impl_count(ctx, id));

	const_ptr_t out[8];
	TEST_ASSERT_EQUAL_UINT32(3u, api->get_all_impls(ctx, id, out, 8u));
	TEST_ASSERT_EQUAL_PTR(&a, out[0]);
	TEST_ASSERT_EQUAL_PTR(&b, out[1]);
	TEST_ASSERT_EQUAL_PTR(&c, out[2]);
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_get_all_truncates_to_buffer) {
	static char a = 'a', b = 'b', c = 'c';
	sk_type_id_t id = SK_TYPE_ID("test.multi_trunc", 0x1111111111111111ULL, 0x4444444444444444ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &b);
	api->add_impl(ctx, id, &c);

	const_ptr_t out[2];
	TEST_ASSERT_EQUAL_UINT32(3u, api->get_all_impls(ctx, id, out, 2u));
	TEST_ASSERT_EQUAL_PTR(&a, out[0]);
	TEST_ASSERT_EQUAL_PTR(&b, out[1]);
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_get_all_count_only) {
	static char a = 'a';
	sk_type_id_t id = SK_TYPE_ID("test.multi_count_only", 0x1111111111111111ULL, 0x5555555555555555ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_all_impls(ctx, id, NULL, 0u));
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_all_impls(ctx, id, NULL, 4u));
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_missing_type_returns_zero) {
	sk_type_id_t id = SK_TYPE_ID("test.multi_missing", 0xAAAAAAAAAAAAAAAAULL, 0xCCCCCCCCCCCCCCCCULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	TEST_ASSERT_EQUAL_UINT32(0u, api->impl_count(ctx, id));
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_all_impls(ctx, id, NULL, 0u));
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_remove_by_pointer) {
	static char a = 'a', b = 'b', c = 'c';
	sk_type_id_t id = SK_TYPE_ID("test.multi_remove", 0x1111111111111111ULL, 0x6666666666666666ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &b);
	api->add_impl(ctx, id, &c);

	api->remove_impl(ctx, id, &b);
	TEST_ASSERT_EQUAL_UINT32(2u, api->impl_count(ctx, id));
	const_ptr_t out[8];
	TEST_ASSERT_EQUAL_UINT32(2u, api->get_all_impls(ctx, id, out, 8u));
	TEST_ASSERT_EQUAL_PTR(&a, out[0]);
	TEST_ASSERT_EQUAL_PTR(&c, out[1]);
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_remove_does_not_touch_other_types) {
	static char a = 'a', b = 'b';
	static char other_x = 'x', other_y = 'y';
	sk_type_id_t id = SK_TYPE_ID("test.multi_remove_a", 0x1111111111111111ULL, 0x1234123412341234ULL);
	sk_type_id_t other = SK_TYPE_ID("test.multi_remove_b", 0x5555555555555555ULL, 0x5678567856785678ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &b);
	api->add_impl(ctx, other, &other_x);
	api->add_impl(ctx, other, &other_y);

	api->remove_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	TEST_ASSERT_EQUAL_UINT32(2u, api->impl_count(ctx, other));

	const_ptr_t out[4];
	TEST_ASSERT_EQUAL_UINT32(2u, api->get_all_impls(ctx, other, out, 4u));
	TEST_ASSERT_EQUAL_PTR(&other_x, out[0]);
	TEST_ASSERT_EQUAL_PTR(&other_y, out[1]);
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_remove_missing_is_noop) {
	static char a = 'a', b = 'b';
	sk_type_id_t id = SK_TYPE_ID("test.multi_remove_missing", 0x1111111111111111ULL, 0x7777777777777777ULL);
	sk_type_id_t missing = SK_TYPE_ID("test.multi_no_type", 0xBBBBBBBBBBBBBBBBULL, 0xDDDDDDDDDDDDDDDDULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &a);
	api->remove_impl(ctx, id, &b);
	api->remove_impl(ctx, missing, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_duplicate_pointer_allowed) {
	static char a = 'a';
	sk_type_id_t id = SK_TYPE_ID("test.multi_dup", 0x1111111111111111ULL, 0x9999999999999999ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(2u, api->impl_count(ctx, id));
	api->remove_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	sk_app_destroy(ctx);
}

SK_TEST(app_impl_independent_of_set_get_api) {
	static char impl = 'i';
	static int api_val = 7;
	sk_type_id_t id = SK_TYPE_ID("test.multi_indep", 0x1111111111111111ULL, 0x8888888888888888ULL);
	sk_app_context_t* ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	api->add_impl(ctx, id, &impl);
	api->set_api(ctx, id, &api_val);

	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	TEST_ASSERT_EQUAL_PTR(&api_val, api->get_api(ctx, id));
	const_ptr_t out[4];
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_all_impls(ctx, id, out, 4u));
	TEST_ASSERT_EQUAL_PTR(&impl, out[0]);
	sk_app_destroy(ctx);
}

/* No process-global context: each sk_app_init returns a caller-owned instance. */
SK_TEST(app_init_returns_independent_contexts) {
	sk_app_context_t* first = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(first);
	TEST_ASSERT_TRUE(sk_app_api()->elapsed_time(first) >= 0.0);

	sk_app_context_t* second = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(second);
	TEST_ASSERT_TRUE(first != second);
	TEST_ASSERT_NOT_NULL(sk_app_api()->get_api(second, SK_PLATFORM_API_TYPE_ID));
	TEST_ASSERT_NOT_NULL(sk_app_api()->get_api(second, SK_LOGGER_API_TYPE_ID));
	TEST_ASSERT_TRUE(sk_app_tick(second) != 0);

	sk_app_destroy(first);
	sk_app_destroy(second);
}

SK_TEST(app_api_exposes_bootstrap_surface) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_NULL(sk_app_api()->load_plugin);
	TEST_ASSERT_NOT_NULL(sk_app_api()->request_shutdown);
	TEST_ASSERT_NOT_NULL(sk_app_api()->delta_time);
	TEST_ASSERT_NOT_NULL(sk_app_api()->fps);
	TEST_ASSERT_NOT_NULL(sk_app_api()->elapsed_time);
	sk_app_destroy(ctx);
}

SK_TEST(app_request_shutdown_exits_main_loop) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_api()->request_shutdown(ctx);
	TEST_ASSERT_EQUAL_INT32(0, sk_app_run(ctx));
	sk_app_destroy(ctx);
}

SK_TEST(app_tick_returns_zero_after_shutdown) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_TRUE(sk_app_tick(ctx) != 0);
	sk_app_api()->request_shutdown(ctx);
	TEST_ASSERT_EQUAL_INT32(0, sk_app_tick(ctx));
	sk_app_destroy(ctx);
}

SK_TEST(app_tick_host_loop_exits_on_pre_shutdown) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_api()->request_shutdown(ctx);
	i32 frames = 0;
	while (sk_app_tick(ctx)) {
		frames++;
	}
	TEST_ASSERT_EQUAL_INT32(0, frames);
	sk_app_destroy(ctx);
}

SK_TEST(app_timing_api_after_init) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	TEST_ASSERT_TRUE(api->delta_time(ctx) >= 0.0);
	TEST_ASSERT_TRUE(api->fps(ctx) >= 0.0);
	TEST_ASSERT_TRUE(api->elapsed_time(ctx) >= 0.0);
	sk_app_destroy(ctx);
}

SK_TEST(app_load_plugin_missing_file_fails) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_EQUAL_INT32(0, sk_app_api()->load_plugin(ctx, ""));
	TEST_ASSERT_NOT_EQUAL_INT32(0, sk_app_api()->load_plugin(ctx, "skore_definitely_missing_plugin_xyz.so"));
	sk_app_destroy(ctx);
}

SK_TEST(app_elapsed_time_advances) {
	f64 a, b;
	volatile i32 spin;
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	a = api->elapsed_time(ctx);
	for (spin = 0; spin < 100000; spin++) {
	}
	b = api->elapsed_time(ctx);
	TEST_ASSERT_TRUE(b >= a);
	sk_app_destroy(ctx);
}

SK_TEST(app_init_auto_loads_plugins_folder) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)sk_app_api()->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(win_api, "expected sk-platform-window auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(win_api->init);
	TEST_ASSERT_NOT_NULL(win_api->create_window);
	sk_app_destroy(ctx);
}

SK_TEST(app_load_plugin_via_api) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-platform-window.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-platform-window.dylib";
#else
	const_chr_t name = "sk-platform-window.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	/* Already auto-loaded at init; load_plugin again should still succeed (re-entry). */
	TEST_ASSERT_EQUAL_INT32(0, sk_app_api()->load_plugin(ctx, path));
	sk_app_destroy(ctx);
}

SK_TEST(platform_window_plugin_entry_point_returns_zero) {
	char path[SK_FS_PATH_MAX];
	typedef int (*entry_fn)(sk_app_context_t*, const sk_app_api_t*);
#if defined(_WIN32)
	const_chr_t name = "sk-platform-window.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-platform-window.dylib";
#else
	const_chr_t name = "sk-platform-window.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_platform_api();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, sk_app_api()));
	plat->lib_close(lib);
	sk_app_destroy(ctx);
}

SK_TEST(platform_window_plugin_registers_api) {
	char path[SK_FS_PATH_MAX];
	typedef int (*entry_fn)(sk_app_context_t*, const sk_app_api_t*);
#if defined(_WIN32)
	const_chr_t name = "sk-platform-window.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-platform-window.dylib";
#else
	const_chr_t name = "sk-platform-window.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_platform_api();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, sk_app_api()));
	const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)sk_app_api()->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(win_api);
	TEST_ASSERT_NOT_NULL(win_api->init);
	TEST_ASSERT_NOT_NULL(win_api->create_window);
	TEST_ASSERT_NULL(win_api->create_window("t", 0u, 100u, SK_WINDOW_FLAG_NONE));
	plat->lib_close(lib);
	sk_app_destroy(ctx);
}

SK_TEST(platform_window_plugin_via_load_plugin) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-platform-window.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-platform-window.dylib";
#else
	const_chr_t name = "sk-platform-window.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	TEST_ASSERT_EQUAL_INT32(0, sk_app_api()->load_plugin(ctx, path));
	const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)sk_app_api()->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(win_api);
	sk_app_destroy(ctx);
}

/* ---- entities plugin ---- */

SK_TEST(app_init_auto_loads_entities_plugin) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)sk_app_api()->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(ecs, "expected sk-entities auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(ecs->register_component);
	TEST_ASSERT_NOT_NULL(ecs->component_info);
	sk_app_destroy(ctx);
}

SK_TEST(entities_plugin_registers_api) {
	char path[SK_FS_PATH_MAX];
	typedef int (*entry_fn)(sk_app_context_t*, const sk_app_api_t*);
#if defined(_WIN32)
	const_chr_t name = "sk-entities.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-entities.dylib";
#else
	const_chr_t name = "sk-entities.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_platform_api();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, sk_app_api()));
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)sk_app_api()->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(ecs);
	TEST_ASSERT_NOT_NULL(ecs->register_component);
	TEST_ASSERT_NOT_NULL(ecs->component_info);
	plat->lib_close(lib);
	sk_app_destroy(ctx);
}

/* ---- dxc_compiler plugin ---- */

SK_TEST(app_init_auto_loads_dxc_compiler_plugin) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_dxc_compiler_api_t* dxc = (const sk_dxc_compiler_api_t*)sk_app_api()->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(dxc, "expected sk-dxc-compiler auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(dxc->init);
	TEST_ASSERT_NOT_NULL(dxc->shutdown);
	TEST_ASSERT_NOT_NULL(dxc->compile);
	sk_app_destroy(ctx);
}

SK_TEST(dxc_compiler_plugin_registers_api) {
	char path[SK_FS_PATH_MAX];
	typedef int (*entry_fn)(sk_app_context_t*, const sk_app_api_t*);
#if defined(_WIN32)
	const_chr_t name = "sk-dxc-compiler.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-dxc-compiler.dylib";
#else
	const_chr_t name = "sk-dxc-compiler.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_platform_api();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, sk_app_api()));
	const sk_dxc_compiler_api_t* dxc = (const sk_dxc_compiler_api_t*)sk_app_api()->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(dxc);
	TEST_ASSERT_NOT_NULL(dxc->init);
	TEST_ASSERT_NOT_NULL(dxc->compile);
	plat->lib_close(lib);
	sk_app_destroy(ctx);
}

/* ---- render_graph plugin (C++ main call-site migration) ---- */

SK_TEST(app_init_auto_loads_render_graph_plugin) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_render_graph_api_t* rg = sk_render_graph_api_from_app(ctx, sk_app_api());
	TEST_ASSERT_NOT_NULL_MESSAGE(rg, "expected sk-render-graph auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(rg->init);
	TEST_ASSERT_NOT_NULL(rg->shutdown);
	TEST_ASSERT_NOT_NULL(rg->create);
	TEST_ASSERT_NOT_NULL(rg->destroy);
	TEST_ASSERT_NOT_NULL(rg->begin);
	TEST_ASSERT_NOT_NULL(rg->execute);
	TEST_ASSERT_NOT_NULL(rg->compile);
	TEST_ASSERT_NOT_NULL(rg->add_pass);
	TEST_ASSERT_NOT_NULL(rg->create_texture);
	sk_app_destroy(ctx);
}

SK_TEST(render_graph_plugin_registers_api) {
	char path[SK_FS_PATH_MAX];
	typedef int (*entry_fn)(sk_app_context_t*, const sk_app_api_t*);
#if defined(_WIN32)
	const_chr_t name = "sk-render-graph.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-render-graph.dylib";
#else
	const_chr_t name = "sk-render-graph.so";
#endif
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_platform_api();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, sk_app_api()));
	const sk_render_graph_api_t* rg = sk_render_graph_api_from_app(ctx, sk_app_api());
	TEST_ASSERT_NOT_NULL(rg);
	TEST_ASSERT_NOT_NULL(rg->create);
	TEST_ASSERT_NOT_NULL(rg->begin);
	TEST_ASSERT_NOT_NULL(rg->execute);
	plat->lib_close(lib);
	sk_app_destroy(ctx);
}

#endif /* SK_TESTS */
