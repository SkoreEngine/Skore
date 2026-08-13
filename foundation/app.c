#include "app.h"

#include "allocator.h"
#include "array.h"
#include "crash.h"
#include "filesystem.h"
#include "hashmap.h"
#include "internal/app_context.h"
#include "internal/tables.h"
#include "logger.h"
#include "path.h"
#include "platform.h"
#include "plugin.h"
#include "profiler.h"
#include "repository.h"
#include "resource_assets.h"

#include <stdio.h>
#include <string.h>

/* ---- app logger (named "app"; lifetime tied to context) ---- */

static void app_logger_shutdown(sk_app_context_t* context) {
	if (context->log == NULL) {
		return;
	}
	if (context->logger_api != NULL) {
		context->logger_api->destroy_logger(context->logger_ctx, context->log);
	}
	context->log = NULL;
}

/**
 * Create the "app" logger. Safe to call again after app_logger_shutdown.
 * @return 0 on success, non-zero if create_logger failed.
 */
static i32 app_logger_startup(sk_app_context_t* context) {
	app_logger_shutdown(context);

	context->logger_ctx = sk_logger_context_create(context->allocator);
	if (context->logger_ctx == NULL) {
		return -1;
	}

	context->log = context->logger_api->create_logger(context->logger_ctx, "app");
	if (context->log == NULL) {
		sk_logger_context_destroy(context->logger_ctx);
		context->logger_ctx = NULL;
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
static void app_profiler_shutdown(sk_app_context_t* context);

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

static sk_logger_context_t* sk_app_logger_context_impl(sk_app_context_t* context) {
	return context->logger_ctx;
}

static const sk_logger_api_t* sk_app_logger_api_impl(sk_app_context_t* context) {
	return context->logger_api;
}

static sk_logger_t* sk_app_app_logger_impl(sk_app_context_t* context) {
	return context->log;
}

static sk_filesystem_context_t* sk_app_filesystem_context_impl(sk_app_context_t* context) {
	return context->fs_ctx;
}

static const sk_filesystem_api_t* sk_app_filesystem_api_impl(sk_app_context_t* context) {
	return context->filesystem_api;
}

static const sk_platform_api_t* sk_app_platform_api_impl(sk_app_context_t* context) {
	return context->platform;
}

static const sk_repository_api_t* sk_app_repository_api_impl(sk_app_context_t* context) {
	return context->repository_api;
}

static const sk_resource_assets_api_t* sk_app_resource_assets_api_impl(sk_app_context_t* context) {
	return context->resource_assets_api;
}

static const sk_app_api_t app_api = {
	sk_app_set_api_impl,		sk_app_get_api_impl,		sk_app_add_impl_impl,		  sk_app_remove_impl_impl,		   sk_app_impl_count_impl,
	sk_app_get_all_impls_impl,	sk_app_load_plugin_impl,	sk_app_request_shutdown_impl, sk_app_delta_time_impl,		   sk_app_fps_impl,
	sk_app_elapsed_time_impl,	sk_app_logger_context_impl, sk_app_logger_api_impl,		  sk_app_app_logger_impl,		   sk_app_filesystem_context_impl,
	sk_app_filesystem_api_impl, sk_app_platform_api_impl,	sk_app_repository_api_impl,	  sk_app_resource_assets_api_impl,
};

void sk_foundation_bind_tables(sk_app_context_t* ctx) {
	sk_logger_install(ctx);
	sk_filesystem_install(ctx);
	sk_repository_install(ctx);
	sk_resource_assets_install(ctx);
	app_api.set_api(ctx, SK_LOGGER_API_TYPE_ID, ctx->logger_api);
	app_api.set_api(ctx, SK_FILESYSTEM_API_TYPE_ID, ctx->filesystem_api);
	app_api.set_api(ctx, SK_REPOSITORY_API_TYPE_ID, ctx->repository_api);
	app_api.set_api(ctx, SK_RESOURCE_ASSETS_API_TYPE_ID, ctx->resource_assets_api);
}

static sk_app_boot_t sk_app_boot_ok(sk_app_context_t* context) {
	sk_app_boot_t boot;
	boot.context = context;
	boot.api = &app_api;
	return boot;
}

/* ---- context create / shutdown / API table ---- */

sk_app_boot_t sk_app_create(void) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_app_context_t* context = (sk_app_context_t*)alloc->alloc(alloc->instance, sizeof(sk_app_context_t));
	if (context == NULL) {
		return sk_app_boot_failed();
	}
	memset(context, 0, sizeof(*context));
	context->allocator = alloc;

	if (sk_hash_map_init(&context->apis, alloc, NULL, NULL) != 0) {
		alloc->free(alloc->instance, context);
		return sk_app_boot_failed();
	}
	if (sk_hash_map_init(&context->impls, alloc, NULL, NULL) != 0) {
		sk_hash_map_free(&context->apis);
		alloc->free(alloc->instance, context);
		return sk_app_boot_failed();
	}
	sk_foundation_bind_tables(context);
	return sk_app_boot_ok(context);
}

void sk_app_shutdown(sk_app_context_t* context) {
	const sk_allocator_t* alloc;

	if (context->crash_owned != 0) {
		sk_crash_uninstall();
		context->crash_owned = 0;
	}

	/* Profiler teardown before the plugin libraries unload. */
	app_profiler_shutdown(context);

	/* Unload plugins before free; process re-entry is explicit shutdown + new init. */
	if (context->plugins.items != NULL) {
		const sk_platform_api_t* plat = context->platform;
		if (plat == NULL) {
			plat = (const sk_platform_api_t*)sk_app_get_api_impl(context, SK_PLATFORM_API_TYPE_ID);
		}
		if (plat != NULL) {
			for (u32 i = 0u; i < context->plugins.count; i++) {
				plat->lib_close(context->plugins.items[i]);
			}
		}
		sk_array_free(&context->plugins);
	}
	app_logger_shutdown(context);
	if (context->logger_ctx != NULL) {
		sk_logger_context_destroy(context->logger_ctx);
		context->logger_ctx = NULL;
	}
	if (context->fs_ctx != NULL) {
		sk_filesystem_context_destroy(context->fs_ctx);
		context->fs_ctx = NULL;
	}

	/* Free every implementation list before the impl map itself. */
	const sk_hash_map_t* impl_map = &context->impls._hm;
	for (u32 slot = 0u; slot < impl_map->capacity; slot++) {
		if (sk_hash_map_slot_occupied_(impl_map, slot) != 0) {
			sk_array_free((sk_app_impl_list_t*)sk_hash_map_value_at_(&context->impls._hm, slot));
		}
	}
	sk_hash_map_free(&context->impls);

	sk_hash_map_free(&context->apis);

	alloc = context->allocator != NULL ? context->allocator : sk_allocator_default();
	alloc->free(alloc->instance, context);
}

sk_app_boot_t sk_app_startup(void) {
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* context = boot.context;
	if (context == NULL) {
		return sk_app_boot_failed();
	}

	sk_platform_install(context);
	app_api.set_api(context, SK_PLATFORM_API_TYPE_ID, context->platform);

	if (app_logger_startup(context) != 0) {
		sk_app_shutdown(context);
		return sk_app_boot_failed();
	}

	context->fs_ctx = sk_filesystem_context_create(context->allocator);
	if (context->fs_ctx == NULL) {
		sk_app_shutdown(context);
		return sk_app_boot_failed();
	}

	sk_log_info(context->logger_api, context->log, "app startup complete");
	return boot;
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

/* ---- profiler lifecycle (host-driven; plugin registers its table at load) ---- */

/**
 * Shut the profiler plugin down and drop the cached table.
 * Safe when the plugin never loaded (profiler_api == NULL).
 */
static void app_profiler_shutdown(sk_app_context_t* context) {
	if (context->profiler_api == NULL) {
		return;
	}
	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_debug(context->logger_api, context->log, "profiler shutdown");
	}
	context->profiler_api->shutdown();
	context->profiler_api = NULL;
}

static void unload_plugins(sk_app_context_t* context) {
	u32 count = context->plugins.count;
	if (count == 0u) {
		sk_array_free(&context->plugins);
		return;
	}

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "unloading %u plugin(s)", count);
	}

	const sk_platform_api_t* plat = app_platform_api(context);
	for (u32 i = 0u; i < count; i++) {
		plat->lib_close(context->plugins.items[i]);
	}
	sk_array_free(&context->plugins);
}

/* ---- auto-load shared libraries from {app_folder}/plugins ---- */

/* Filename collection for deterministic load order (see below). */
typedef struct sk_plugin_file_t {
	char name[SK_FS_PATH_MAX];
} sk_plugin_file_t;

enum { SK_PLUGIN_FILE_CAP = 64u };

static void sort_plugin_files(sk_plugin_file_t* files, u32 count) {
	/* Insertion sort by filename (strcmp). Small lists; keeps the load order
	 * deterministic across filesystems (readdir order is not). */
	for (u32 i = 1u; i < count; i++) {
		sk_plugin_file_t key = files[i];
		i32 j = (i32)i - 1;
		while (j >= 0 && strcmp(files[j].name, key.name) > 0) {
			files[j + 1] = files[j];
			j--;
		}
		files[j + 1] = key;
	}
}

/**
 * Scan @p dir for shared libraries and load each via sk_app_load_plugin_impl.
 * Missing files / bad plugins are skipped (best-effort). Always returns 0.
 *
 * Files are loaded in sorted filename order: plugin entry points register
 * tables on the app registry, and instrumented plugins (sk-entities,
 * sk-render-graph) cache the sk-profiler table when they register — a
 * deterministic order (sk-profiler before sk-render-graph) makes that
 * resolution reliable on every filesystem.
 */
static i32 load_plugins_from_directory(sk_app_context_t* context, const_chr_t dir) {
	char full_path[SK_FS_PATH_MAX];
	u32 attempted = 0u;
	u32 loaded = 0u;
	sk_plugin_file_t files[SK_PLUGIN_FILE_CAP];
	u32 file_count = 0u;

	if (dir[0] == '\0') {
		return 0;
	}

	const sk_filesystem_api_t* fs = context->filesystem_api;
	const sk_platform_api_t* plat = app_platform_api(context);
	sk_directory_iterator_t it = fs->open_directory(dir);
	if (it == NULL) {
		if (context->log != NULL && context->logger_api != NULL) {
			sk_log_warn(context->logger_api, context->log, "could not open plugins directory: %s", dir);
		}
		return 0;
	}

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "scanning plugins directory: %s", dir);
	}

	/* Collect candidate filenames straight into the array (next_directory is
	 * an out-param writer; no intermediate buffer needed). */
	while (file_count < SK_PLUGIN_FILE_CAP && fs->next_directory(it, files[file_count].name, (u32)sizeof(files[file_count].name)) == 0) {
		if (!sk_is_shared_library_filename(files[file_count].name)) {
			continue; /* same slot is overwritten by the next entry */
		}
		file_count++;
	}
	fs->close_directory(it);
	sort_plugin_files(files, file_count);

	for (u32 i = 0u; i < file_count; i++) {
		if (sk_path_join(sk_str_view_cstr(dir), sk_str_view_cstr(files[i].name), full_path, (u32)sizeof(full_path)) < 0) {
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

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "plugins directory scan done: loaded %u of %u", loaded, attempted);
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

	const sk_filesystem_api_t* fs = context->filesystem_api;

	base[0] = '\0';
	/* Prefer the directory of the running executable; fall back to process cwd. */
	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		if (context->log != NULL && context->logger_api != NULL) {
			sk_log_warn(context->logger_api, context->log, "plugin auto-load skipped: could not resolve app or cwd folder");
		}
		return;
	}

	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		if (context->log != NULL && context->logger_api != NULL) {
			sk_log_warn(context->logger_api, context->log, "plugin auto-load skipped: path join failed");
		}
		return;
	}

	if (fs->get_file_status(plugins_dir) != SK_FILE_STATUS_DIRECTORY) {
		if (context->log != NULL && context->logger_api != NULL) {
			sk_log_debug(context->logger_api, context->log, "no plugins directory at %s", plugins_dir);
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

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "bootstrap init");
	}

	/* Auto-load every shared library under {app_folder}/plugins. */
	load_plugins_auto(context);

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "bootstrap ready (%u plugin(s) loaded)", context->plugins.count);
	}

	return 0;
}

static void sk_app_bootstrap_shutdown(sk_app_context_t* context) {
	if (context->initialized == 0 && context->plugins.items == NULL) {
		return;
	}

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "bootstrap shutdown");
	}

	/* Profiler teardown must run before the plugin libraries unload. */
	app_profiler_shutdown(context);
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
		if (context->log != NULL && context->logger_api != NULL) {
			sk_log_error(context->logger_api, context->log, "load_plugin before bootstrap: %s", path);
		}
		return -1;
	}

	const sk_logger_api_t* logger_api = context->logger_api;
	if (context->log != NULL && logger_api != NULL) {
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
	i32 rc = entry(context, &app_api);
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

	/* Profiler lifecycle: cache its table and run init right after the entry
	 * point registers it. init(sk_render_device_t_zero()) is CPU-only; the
	 * host re-calls init(dev) once a render device exists to attach GPU
	 * timestamp pools (idempotent). */
	if (context->profiler_api == NULL) {
		context->profiler_api = (const sk_profiler_api_t*)sk_app_get_api_impl(context, SK_PROFILER_API_TYPE_ID);
		if (context->profiler_api != NULL) {
			if (context->log != NULL) {
				sk_log_debug(logger_api, context->log, "profiler init");
			}
			(void)context->profiler_api->init(sk_render_device_t_zero());
		}
	}

	if (context->log != NULL) {
		sk_log_info(logger_api, context->log, "plugin loaded: %s", path);
	}
	return 0;
}

static void sk_app_request_shutdown_impl(sk_app_context_t* context) {
	if (context->log != NULL && context->logger_api != NULL && context->shutdown_requested == 0) {
		sk_log_info(context->logger_api, context->log, "shutdown requested");
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

sk_app_boot_t sk_app_init(int argc, char* argv[]) {
	sk_app_boot_t boot = sk_app_startup();
	sk_app_context_t* context = boot.context;

	if (context == NULL) {
		return sk_app_boot_failed();
	}
	context->argc = argc;
	context->argv = argv;
	if (sk_app_bootstrap_init(context) != 0) {
		if (context->log != NULL && context->logger_api != NULL) {
			sk_log_error(context->logger_api, context->log, "bootstrap init failed");
		}
		sk_app_shutdown(context);
		return sk_app_boot_failed();
	}
	/* Fatal-fault reporting: print a stacktrace to stderr, then die with the
	 * platform's normal termination. Best-effort: a failed install must not
	 * prevent the app from starting. Embedders opt out via sk_crash_uninstall. */
	if (sk_crash_install() == 0) {
		context->crash_owned = 1;
	} else if (context->log != NULL && context->logger_api != NULL) {
		sk_log_warn(context->logger_api, context->log, "crash handler install failed");
	}

	if (context->log != NULL && context->logger_api != NULL) {
		sk_log_info(context->logger_api, context->log, "app init complete");
	}
	return boot;
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
				sk_log_info(context->logger_api, context->log, "main loop exit (elapsed %.3f s)", context->elapsed_time);
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
			sk_log_info(context->logger_api, context->log, "main loop enter");
		}
	}

	bootstrap_tick_timing(context);

	/* Profiler frame delimiters: begin/end bracket the whole tick (timing
	 * update and frame work) so the triple buffers rotate exactly once per
	 * tick and every main-loop phase is sampled inside the frame. */
	if (context->profiler_api != NULL) {
		context->profiler_api->begin_frame();
	}

	/* Main-loop phase instrumentation (sk-profiler; no-ops when the plugin
	 * is absent or SK_ENABLE_PROFILER is off). Zones close before end_frame
	 * below (cleanup runs at block exit) so end stamps stay inside the
	 * frame. */
	{
		SK_PROFILE_CPU_ZONE(context->profiler_api, "app tick");
		SK_PROFILE_CPU_ZONE(context->profiler_api, "tick timing");
		bootstrap_tick_timing(context);
		/* Future: frame phases / systems land here, between begin/end. */
	}

	if (context->profiler_api != NULL) {
		context->profiler_api->end_frame();
	}

	if (context->shutdown_requested != 0) {
		if (context->log != NULL) {
			sk_log_info(context->logger_api, context->log, "main loop exit (elapsed %.3f s)", context->elapsed_time);
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
#include "resource_assets_types.h"

#include <stdio.h>
#include <string.h>

/* ---- helpers ---- */

static void test_make_fs_paths(char* dir, u32 dir_cap, char* file_a, u32 a_cap, char* file_b, u32 b_cap) {
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
	(void)api->remove(file_a);
	(void)api->remove(file_b);
	(void)api->remove(dir);
}

static i32 test_plugin_path(const_chr_t plugin_filename, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
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
	const sk_platform_api_t* api = sk_test_platform_table();
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_NOT_NULL(api->lib_open);
	TEST_ASSERT_NOT_NULL(api->lib_symbol);
	TEST_ASSERT_NOT_NULL(api->lib_close);
	TEST_ASSERT_NOT_NULL(api->lib_error);
	TEST_ASSERT_NOT_NULL(api->monotonic_seconds);
}

SK_TEST(platform_lib_open_missing_file_fails) {
	const sk_platform_api_t* api = sk_test_platform_table();
	sk_shared_lib_t lib = api->lib_open("skore_definitely_missing_plugin_xyz.so");
	TEST_ASSERT_NULL(lib);
	TEST_ASSERT_NOT_NULL(api->lib_error());
	TEST_ASSERT_TRUE(api->lib_error()[0] != '\0');
}

SK_TEST(platform_lib_error_never_null) {
	TEST_ASSERT_NOT_NULL(sk_test_platform_table()->lib_error());
}

SK_TEST(platform_monotonic_seconds_advances) {
	const sk_platform_api_t* api = sk_test_platform_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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

SK_TEST(filesystem_path_queries) {
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_NOT_FOUND, api->get_file_status(""));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_NOT_FOUND, api->get_file_status("skore_definitely_missing_fs_xyz_99"));
	TEST_ASSERT_EQUAL_UINT64(0ull, api->get_path_size("skore_definitely_missing_fs_xyz_99"));
	TEST_ASSERT_EQUAL_UINT64(0ull, api->get_file_id(""));
}

SK_TEST(filesystem_create_write_read_remove) {
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
	TEST_ASSERT_NULL(api->open_file("", SK_FILE_ACCESS_READ));
	TEST_ASSERT_NULL(api->open_file("skore_missing_open_xyz", SK_FILE_ACCESS_READ));
}

SK_TEST(filesystem_file_mapping_roundtrip) {
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	const sk_filesystem_api_t* api = sk_test_filesystem_table();
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
	sk_app_boot_t boot = sk_app_init(1, argv);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_shutdown(ctx);
}

SK_TEST(app_init_accepts_null_argv_with_zero_argc) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	sk_app_shutdown(ctx);
}

SK_TEST(app_init_creates_context_and_api_table) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_NULL(boot.api);
	TEST_ASSERT_NOT_NULL(boot.api->set_api);
	TEST_ASSERT_NOT_NULL(boot.api->get_api);
	TEST_ASSERT_NOT_NULL(boot.api->add_impl);
	TEST_ASSERT_NOT_NULL(boot.api->remove_impl);
	TEST_ASSERT_NOT_NULL(boot.api->impl_count);
	TEST_ASSERT_NOT_NULL(boot.api->get_all_impls);
	TEST_ASSERT_NOT_NULL(boot.api->load_plugin);
	TEST_ASSERT_NOT_NULL(boot.api->request_shutdown);
	TEST_ASSERT_NOT_NULL(boot.api->delta_time);
	TEST_ASSERT_NOT_NULL(boot.api->fps);
	TEST_ASSERT_NOT_NULL(boot.api->elapsed_time);
	TEST_ASSERT_NOT_NULL(boot.api->logger_context);
	TEST_ASSERT_NOT_NULL(boot.api->logger_api);
	TEST_ASSERT_NOT_NULL(boot.api->app_logger);
	TEST_ASSERT_NOT_NULL(boot.api->filesystem_context);
	TEST_ASSERT_NOT_NULL(boot.api->filesystem_api);
	TEST_ASSERT_NOT_NULL(boot.api->platform_api);
	TEST_ASSERT_NOT_NULL(boot.api->repository_api);
	TEST_ASSERT_NOT_NULL(boot.api->resource_assets_api);
	TEST_ASSERT_NOT_NULL(boot.api->logger_context(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->logger_api(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->app_logger(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->filesystem_context(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->filesystem_api(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->platform_api(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->repository_api(ctx));
	TEST_ASSERT_NOT_NULL(boot.api->resource_assets_api(ctx));
	sk_app_shutdown(ctx);
}

SK_TEST(app_init_registers_platform_api) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	const sk_platform_api_t* plat = (const sk_platform_api_t*)api->get_api(ctx, SK_PLATFORM_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(plat);
	TEST_ASSERT_EQUAL_PTR(api->platform_api(ctx), plat);
	sk_app_shutdown(ctx);
}

SK_TEST(app_init_registers_logger_api) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;

	const sk_logger_api_t* logger_api = (const sk_logger_api_t*)api->get_api(ctx, SK_LOGGER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(logger_api, "logger API must be registered on app startup");
	TEST_ASSERT_EQUAL_PTR(api->logger_api(ctx), logger_api);
	TEST_ASSERT_NOT_NULL(logger_api->create_logger);
	TEST_ASSERT_NOT_NULL(logger_api->destroy_logger);
	TEST_ASSERT_NOT_NULL(logger_api->message);

	sk_logger_context_t* log_ctx = api->logger_context(ctx);
	TEST_ASSERT_NOT_NULL(log_ctx);
	sk_logger_t* log = logger_api->create_logger(log_ctx, "integration-test");
	TEST_ASSERT_NOT_NULL(log);
	TEST_ASSERT_EQUAL_STRING("integration-test", sk_logger_name(log));
	sk_log_info(logger_api, log, "logger registry smoke");
	logger_api->destroy_logger(log_ctx, log);
	sk_app_shutdown(ctx);
}

SK_TEST(app_set_get_api_roundtrip) {
	static int dummy_api = 42;
	sk_type_id_t id = SK_TYPE_ID("test.dummy_api", 0x1111111111111111ULL, 0x2222222222222222ULL);
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	boot.api->set_api(ctx, id, &dummy_api);
	TEST_ASSERT_EQUAL_PTR(&dummy_api, boot.api->get_api(ctx, id));
	TEST_ASSERT_EQUAL_INT(42, *(int*)boot.api->get_api(ctx, id));
	sk_app_shutdown(ctx);
}

SK_TEST(app_get_api_missing_returns_null) {
	sk_type_id_t id = SK_TYPE_ID("test.missing_api", 0xAAAAAAAAAAAAAAAAULL, 0xBBBBBBBBBBBBBBBBULL);
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NULL(boot.api->get_api(ctx, id));
	sk_app_shutdown(ctx);
}

SK_TEST(app_create_destroy_independent_context) {
	static char marker = 'x';
	sk_type_id_t id = SK_TYPE_ID("test.local_api", 0x0101010101010101ULL, 0x0202020202020202ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	boot.api->set_api(ctx, id, &marker);
	TEST_ASSERT_EQUAL_PTR(&marker, boot.api->get_api(ctx, id));
	sk_app_shutdown(ctx);
}

/* ---- multi-implementation registry ---- */

SK_TEST(app_impl_add_count_roundtrip) {
	static char a = 'a', b = 'b', c = 'c';
	sk_type_id_t id = SK_TYPE_ID("test.multi_impl", 0x1111111111111111ULL, 0x3333333333333333ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;

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
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_get_all_truncates_to_buffer) {
	static char a = 'a', b = 'b', c = 'c';
	sk_type_id_t id = SK_TYPE_ID("test.multi_trunc", 0x1111111111111111ULL, 0x4444444444444444ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &b);
	api->add_impl(ctx, id, &c);

	const_ptr_t out[2];
	TEST_ASSERT_EQUAL_UINT32(3u, api->get_all_impls(ctx, id, out, 2u));
	TEST_ASSERT_EQUAL_PTR(&a, out[0]);
	TEST_ASSERT_EQUAL_PTR(&b, out[1]);
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_get_all_count_only) {
	static char a = 'a';
	sk_type_id_t id = SK_TYPE_ID("test.multi_count_only", 0x1111111111111111ULL, 0x5555555555555555ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	api->add_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_all_impls(ctx, id, NULL, 0u));
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_all_impls(ctx, id, NULL, 4u));
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_missing_type_returns_zero) {
	sk_type_id_t id = SK_TYPE_ID("test.multi_missing", 0xAAAAAAAAAAAAAAAAULL, 0xCCCCCCCCCCCCCCCCULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	TEST_ASSERT_EQUAL_UINT32(0u, api->impl_count(ctx, id));
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_all_impls(ctx, id, NULL, 0u));
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_remove_by_pointer) {
	static char a = 'a', b = 'b', c = 'c';
	sk_type_id_t id = SK_TYPE_ID("test.multi_remove", 0x1111111111111111ULL, 0x6666666666666666ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &b);
	api->add_impl(ctx, id, &c);

	api->remove_impl(ctx, id, &b);
	TEST_ASSERT_EQUAL_UINT32(2u, api->impl_count(ctx, id));
	const_ptr_t out[8];
	TEST_ASSERT_EQUAL_UINT32(2u, api->get_all_impls(ctx, id, out, 8u));
	TEST_ASSERT_EQUAL_PTR(&a, out[0]);
	TEST_ASSERT_EQUAL_PTR(&c, out[1]);
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_remove_does_not_touch_other_types) {
	static char a = 'a', b = 'b';
	static char other_x = 'x', other_y = 'y';
	sk_type_id_t id = SK_TYPE_ID("test.multi_remove_a", 0x1111111111111111ULL, 0x1234123412341234ULL);
	sk_type_id_t other = SK_TYPE_ID("test.multi_remove_b", 0x5555555555555555ULL, 0x5678567856785678ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
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
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_remove_missing_is_noop) {
	static char a = 'a', b = 'b';
	sk_type_id_t id = SK_TYPE_ID("test.multi_remove_missing", 0x1111111111111111ULL, 0x7777777777777777ULL);
	sk_type_id_t missing = SK_TYPE_ID("test.multi_no_type", 0xBBBBBBBBBBBBBBBBULL, 0xDDDDDDDDDDDDDDDDULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	api->add_impl(ctx, id, &a);
	api->remove_impl(ctx, id, &b);
	api->remove_impl(ctx, missing, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_duplicate_pointer_allowed) {
	static char a = 'a';
	sk_type_id_t id = SK_TYPE_ID("test.multi_dup", 0x1111111111111111ULL, 0x9999999999999999ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	api->add_impl(ctx, id, &a);
	api->add_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(2u, api->impl_count(ctx, id));
	api->remove_impl(ctx, id, &a);
	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	sk_app_shutdown(ctx);
}

SK_TEST(app_impl_independent_of_set_get_api) {
	static char impl = 'i';
	static int api_val = 7;
	sk_type_id_t id = SK_TYPE_ID("test.multi_indep", 0x1111111111111111ULL, 0x8888888888888888ULL);
	sk_app_boot_t boot = sk_app_create();
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	api->add_impl(ctx, id, &impl);
	api->set_api(ctx, id, &api_val);

	TEST_ASSERT_EQUAL_UINT32(1u, api->impl_count(ctx, id));
	TEST_ASSERT_EQUAL_PTR(&api_val, api->get_api(ctx, id));
	const_ptr_t out[4];
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_all_impls(ctx, id, out, 4u));
	TEST_ASSERT_EQUAL_PTR(&impl, out[0]);
	sk_app_shutdown(ctx);
}

/* No process-global context: each sk_app_init returns a caller-owned instance. */
SK_TEST(app_init_returns_independent_contexts) {
	sk_app_boot_t first_boot = sk_app_init(0, NULL);
	sk_app_context_t* first = first_boot.context;
	TEST_ASSERT_NOT_NULL(first);
	TEST_ASSERT_TRUE(first_boot.api->elapsed_time(first) >= 0.0);

	sk_app_boot_t second_boot = sk_app_init(0, NULL);

	sk_app_context_t* second = second_boot.context;
	TEST_ASSERT_NOT_NULL(second);
	TEST_ASSERT_TRUE(first != second);
	TEST_ASSERT_NOT_NULL(second_boot.api->get_api(second, SK_PLATFORM_API_TYPE_ID));
	TEST_ASSERT_NOT_NULL(second_boot.api->get_api(second, SK_LOGGER_API_TYPE_ID));
	TEST_ASSERT_TRUE(sk_app_tick(second) != 0);

	sk_app_shutdown(first);
	sk_app_shutdown(second);
}

static void isolation_sink_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	(void)level;
	(void)logger_name;
	(void)message;
	*((u32*)user_data) += 1u;
}

SK_TEST(app_two_contexts_isolate_logger_sinks_and_repository) {
	sk_app_boot_t boot_a = sk_app_startup();
	sk_app_boot_t boot_b = sk_app_startup();
	TEST_ASSERT_NOT_NULL(boot_a.context);
	TEST_ASSERT_NOT_NULL(boot_b.context);
	TEST_ASSERT_TRUE(boot_a.context != boot_b.context);

	sk_logger_context_t* log_ctx_a = boot_a.api->logger_context(boot_a.context);
	sk_logger_context_t* log_ctx_b = boot_b.api->logger_context(boot_b.context);
	TEST_ASSERT_NOT_NULL(log_ctx_a);
	TEST_ASSERT_NOT_NULL(log_ctx_b);
	TEST_ASSERT_TRUE(log_ctx_a != log_ctx_b);

	u32 hits_a = 0u;
	u32 hits_b = 0u;
	sk_log_sink_t sink_a;
	sk_log_sink_t sink_b;
	sink_a.user_data = &hits_a;
	sink_a.print = isolation_sink_print;
	sink_b.user_data = &hits_b;
	sink_b.print = isolation_sink_print;

	const sk_logger_api_t* logger_a = boot_a.api->logger_api(boot_a.context);
	const sk_logger_api_t* logger_b = boot_b.api->logger_api(boot_b.context);
	TEST_ASSERT_EQUAL_INT(0, logger_a->add_sink(log_ctx_a, &sink_a));
	TEST_ASSERT_EQUAL_INT(0, logger_b->add_sink(log_ctx_b, &sink_b));

	sk_log_info(logger_a, boot_a.api->app_logger(boot_a.context), "only-a");
	TEST_ASSERT_EQUAL_UINT32(1u, hits_a);
	TEST_ASSERT_EQUAL_UINT32(0u, hits_b);

	sk_log_info(logger_b, boot_b.api->app_logger(boot_b.context), "only-b");
	TEST_ASSERT_EQUAL_UINT32(1u, hits_a);
	TEST_ASSERT_EQUAL_UINT32(1u, hits_b);

	const sk_repository_api_t* repo_api = boot_a.api->repository_api(boot_a.context);
	sk_repository_t* repo_a = repo_api->create(sk_allocator_default());
	sk_repository_t* repo_b = repo_api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo_a);
	TEST_ASSERT_NOT_NULL(repo_b);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo_a, repo_api));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo_b, repo_api));

	const sk_resource_type_t* type_a = repo_api->find_type(repo_a, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID);
	const sk_resource_type_t* type_b = repo_api->find_type(repo_b, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(type_a);
	TEST_ASSERT_NOT_NULL(type_b);

	sk_rid_t rid_a = repo_api->create_resource(repo_a, type_a, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid_a.id != 0u);
	u64 count_a = repo_api->resource_count(repo_a);
	TEST_ASSERT_TRUE(count_a >= 1u);
	TEST_ASSERT_EQUAL_UINT64(0u, repo_api->resource_count(repo_b));

	sk_rid_t rid_b = repo_api->create_resource(repo_b, type_b, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid_b.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(count_a, repo_api->resource_count(repo_a));
	TEST_ASSERT_TRUE(repo_api->resource_count(repo_b) >= 1u);

	repo_api->destroy(repo_a);
	repo_api->destroy(repo_b);
	sk_app_shutdown(boot_a.context);
	sk_app_shutdown(boot_b.context);
}

SK_TEST(app_api_exposes_bootstrap_surface) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_NULL(boot.api->load_plugin);
	TEST_ASSERT_NOT_NULL(boot.api->request_shutdown);
	TEST_ASSERT_NOT_NULL(boot.api->delta_time);
	TEST_ASSERT_NOT_NULL(boot.api->fps);
	TEST_ASSERT_NOT_NULL(boot.api->elapsed_time);
	sk_app_shutdown(ctx);
}

SK_TEST(app_request_shutdown_exits_main_loop) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	boot.api->request_shutdown(ctx);
	TEST_ASSERT_EQUAL_INT32(0, sk_app_run(ctx));
	sk_app_shutdown(ctx);
}

SK_TEST(app_tick_returns_zero_after_shutdown) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_TRUE(sk_app_tick(ctx) != 0);
	boot.api->request_shutdown(ctx);
	TEST_ASSERT_EQUAL_INT32(0, sk_app_tick(ctx));
	sk_app_shutdown(ctx);
}

SK_TEST(app_tick_host_loop_exits_on_pre_shutdown) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	boot.api->request_shutdown(ctx);
	i32 frames = 0;
	while (sk_app_tick(ctx)) {
		frames++;
	}
	TEST_ASSERT_EQUAL_INT32(0, frames);
	sk_app_shutdown(ctx);
}

SK_TEST(app_timing_api_after_init) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	TEST_ASSERT_TRUE(api->delta_time(ctx) >= 0.0);
	TEST_ASSERT_TRUE(api->fps(ctx) >= 0.0);
	TEST_ASSERT_TRUE(api->elapsed_time(ctx) >= 0.0);
	sk_app_shutdown(ctx);
}

SK_TEST(app_load_plugin_missing_file_fails) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_NOT_EQUAL_INT32(0, boot.api->load_plugin(ctx, ""));
	TEST_ASSERT_NOT_EQUAL_INT32(0, boot.api->load_plugin(ctx, "skore_definitely_missing_plugin_xyz.so"));
	sk_app_shutdown(ctx);
}

SK_TEST(app_elapsed_time_advances) {
	f64 a, b;
	volatile i32 spin;
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	a = api->elapsed_time(ctx);
	for (spin = 0; spin < 100000; spin++) {
	}
	b = api->elapsed_time(ctx);
	TEST_ASSERT_TRUE(b >= a);
	sk_app_shutdown(ctx);
}

SK_TEST(app_init_auto_loads_plugins_folder) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)boot.api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(win_api, "expected sk-platform-window auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(win_api->init);
	TEST_ASSERT_NOT_NULL(win_api->create_window);
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	/* Already auto-loaded at init; load_plugin again should still succeed (re-entry). */
	TEST_ASSERT_EQUAL_INT32(0, boot.api->load_plugin(ctx, path));
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_test_platform_table();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, boot.api));
	plat->lib_close(lib);
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_test_platform_table();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, boot.api));
	const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)boot.api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(win_api);
	TEST_ASSERT_NOT_NULL(win_api->init);
	TEST_ASSERT_NOT_NULL(win_api->create_window);
	TEST_ASSERT_NULL(win_api->create_window("t", 0u, 100u, SK_WINDOW_FLAG_NONE));
	plat->lib_close(lib);
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	TEST_ASSERT_EQUAL_INT32(0, boot.api->load_plugin(ctx, path));
	const sk_platform_window_api_t* win_api = (const sk_platform_window_api_t*)boot.api->get_api(ctx, SK_PLATFORM_WINDOW_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(win_api);
	sk_app_shutdown(ctx);
}

/* ---- entities plugin ---- */

SK_TEST(app_init_auto_loads_entities_plugin) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)boot.api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(ecs, "expected sk-entities auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(ecs->register_component);
	TEST_ASSERT_NOT_NULL(ecs->component_info);
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_test_platform_table();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, boot.api));
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)boot.api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(ecs);
	TEST_ASSERT_NOT_NULL(ecs->register_component);
	TEST_ASSERT_NOT_NULL(ecs->component_info);
	plat->lib_close(lib);
	sk_app_shutdown(ctx);
}

/* ---- profiler plugin ---- */

SK_TEST(app_init_auto_loads_profiler_plugin) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_profiler_api_t* prof = (const sk_profiler_api_t*)boot.api->get_api(ctx, SK_PROFILER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(prof, "expected sk-profiler auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(prof->init);
	TEST_ASSERT_NOT_NULL(prof->shutdown);
	TEST_ASSERT_NOT_NULL(prof->begin_frame);
	TEST_ASSERT_NOT_NULL(prof->end_frame);
	TEST_ASSERT_NOT_NULL(prof->begin_cpu_sample);
	TEST_ASSERT_NOT_NULL(prof->end_cpu_sample);
	TEST_ASSERT_NOT_NULL(prof->begin_gpu_sample);
	TEST_ASSERT_NOT_NULL(prof->end_gpu_sample);
	TEST_ASSERT_NOT_NULL(prof->get_cpu_tasks);
	TEST_ASSERT_NOT_NULL(prof->get_gpu_tasks);
	TEST_ASSERT_NOT_NULL(prof->get_cpu_frame_stats);
	TEST_ASSERT_NOT_NULL(prof->get_gpu_frame_stats);
	TEST_ASSERT_NOT_NULL(prof->reset_stats);
	TEST_ASSERT_NOT_NULL(prof->set_active);
	TEST_ASSERT_NOT_NULL(prof->is_active);
	sk_app_shutdown(ctx);
}

SK_TEST(app_tick_delivers_profiler_frames) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_profiler_api_t* prof = (const sk_profiler_api_t*)boot.api->get_api(ctx, SK_PROFILER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(prof);

	/* Each tick must begin/end a profiler frame: with recording on, the CPU
	 * frame stats accumulate per tick (first tick is the baseline). */
	prof->set_active(false);
	prof->set_active(true);
	for (i32 i = 0; i < 3; i++) {
		TEST_ASSERT_TRUE(sk_app_tick(ctx) != 0);
	}
	sk_profiler_frame_stats_t stats = prof->get_cpu_frame_stats();
	TEST_ASSERT_TRUE(stats.count >= 1u);
	TEST_ASSERT_TRUE(stats.current >= 0.0);
	prof->set_active(false);
	sk_app_shutdown(ctx);
}

/* End-to-end profiler reporting evidence: drive the real main loop plus hot
 * subsystems (ECS, render graph) between profiler frame delimiters, then
 * dump the text, JSON, and console/log forms and verify the aggregation
 * (nesting, call counts, times, % of frame, cumulative summary) is present
 * with non-zero, plausible timings. Zone-presence assertions only apply to
 * SK_ENABLE_PROFILER builds — without the compile-time switch the macros
 * compile away and no samples exist (by design). */

typedef struct app_profiler_ecs_workload_t {
	const sk_entities_api_t* ecs;
	sk_query_t* query;
	f64 checksum;
} app_profiler_ecs_workload_t;

static void app_profiler_ecs_system(sk_world_t* world, f32 delta_time, void_ptr_t user_data) {
	app_profiler_ecs_workload_t* wl = (app_profiler_ecs_workload_t*)user_data;
	(void)world;
	(void)delta_time;
	/* Real query iteration over every position component (hot ECS path). */
	SK_ECS_QUERY_FOREACH(wl->ecs, wl->query, it) {
		const f32* pos = (const f32*)wl->ecs->query_iter_field(&it, 1u, 0u);
		const u32 elem_stride = it.strides[1u] / (u32)sizeof(f32);
		for (u32 row = 0u; row < it.count; row++) {
			wl->checksum += (f64)pos[row * elem_stride] + (f64)pos[row * elem_stride + 1u] + (f64)pos[row * elem_stride + 2u];
		}
	}
}

SK_TEST(app_profiler_report_end_to_end) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = boot.api;
	const sk_profiler_api_t* prof = (const sk_profiler_api_t*)api->get_api(ctx, SK_PROFILER_API_TYPE_ID);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	const sk_render_graph_api_t* rg = sk_render_graph_api_from_app(ctx, api);
	TEST_ASSERT_NOT_NULL(prof);
	TEST_ASSERT_NOT_NULL(ecs);
	TEST_ASSERT_NOT_NULL(rg);

	/* ECS workload: position components, a query, and a scheduler system. */
	const sk_type_id_t pos_id = SK_TYPE_ID("app.test.position", 0x1337c0de1337c0deULL, 0x0ddba11c0ddba11cULL);
	TEST_ASSERT_EQUAL_INT32(0, ecs->register_component(pos_id, 3u * (u32)sizeof(f32), 4u, "position"));

	sk_world_t* world = ecs->world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t reads[1] = {pos_id};
	sk_query_desc_t qdesc;
	memset(&qdesc, 0, sizeof(qdesc));
	qdesc.required = reads;
	qdesc.required_count = 1u;
	sk_query_t* query = ecs->world_query_create(world, &qdesc);
	TEST_ASSERT_NOT_NULL(query);

	app_profiler_ecs_workload_t workload;
	memset(&workload, 0, sizeof(workload));
	workload.ecs = ecs;
	workload.query = query;

	sk_system_desc_t sdesc;
	memset(&sdesc, 0, sizeof(sdesc));
	sdesc.callback = app_profiler_ecs_system;
	sdesc.reads = reads;
	sdesc.read_count = 1u;
	sdesc.name = "iterate positions";
	sdesc.user_data = &workload;

	sk_scheduler_t* scheduler = ecs->scheduler_create();
	TEST_ASSERT_NOT_NULL(scheduler);
	sk_system_t* system = ecs->system_create(&sdesc);
	TEST_ASSERT_NOT_NULL(system);
	TEST_ASSERT_EQUAL_INT32(0, ecs->scheduler_add(scheduler, system));
	TEST_ASSERT_EQUAL_INT32(0, ecs->scheduler_build(scheduler));

	/* Render-graph workload: a small headless graph (zero device/cmd, the
	 * same unit-test mode the plugin's own execute tests use). */
	TEST_ASSERT_EQUAL_INT(0, rg->init());
	sk_render_graph_t* graph = rg->create(sk_render_device_t_zero());
	TEST_ASSERT_NOT_NULL(graph);
	rg->set_output_size(graph, (sk_rg_extent_t){512u, 512u});
	sk_rg_texture_desc_t tex;
	memset(&tex, 0, sizeof(tex));
	tex.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	tex.extent.width = 256u;
	tex.extent.height = 256u;
	tex.extent.depth = 1u;
	tex.scale_x = 1.0f;
	tex.scale_y = 1.0f;
	tex.array_layers = 1u;
	tex.samples = 1u;
	tex.mip_levels = 1u;
	/* Per-frame declare phase runs inside begin/execute below (passes and
	 * resource declarations are frame-scoped in the render graph). */

	/* Recording on; alternate real main-loop ticks with engine work frames
	 * so the last built frame (dumped below) shows the hot subsystems and
	 * the cumulative summary covers the main-loop phases too. */
	prof->set_active(false);
	prof->set_active(true);

	const u32 spawn_per_frame = 100u;
	sk_entity_t spawned[100];
	bool all_valid = true;
	for (i32 frame = 0; frame < 40; frame++) {
		TEST_ASSERT_TRUE(sk_app_tick(ctx) != 0);

		prof->begin_frame();
		for (u32 k = 0u; k < spawn_per_frame; k++) {
			spawned[k] = ecs->world_spawn(world, &pos_id, 1u);
			all_valid = all_valid && sk_entity_is_valid(spawned[k]) != 0;
		}
		TEST_ASSERT_EQUAL_INT32(0, ecs->scheduler_run(scheduler, world, 0.016f));
		rg->begin(graph, NULL);
		rg->create_texture(graph, "Scene", &tex);
		sk_rg_pass_t* pass = rg->add_pass(graph, "Lighting", SK_RG_PASS_COMPUTE);
		TEST_ASSERT_NOT_NULL(pass);
		rg->pass_write(pass, "Scene");
		rg->pass_set_side_effects(pass, 1);
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg->compile(graph));
		rg->execute(graph, sk_command_buffer_t_zero());
		for (u32 k = 0u; k < spawn_per_frame; k++) {
			TEST_ASSERT_EQUAL_INT32(0, ecs->world_despawn(world, spawned[k]));
		}
		prof->end_frame();
	}
	TEST_ASSERT_TRUE(all_valid);
	TEST_ASSERT_TRUE(workload.checksum >= 0.0);

	/* Dump all three report forms (files land in {build}/bin, the test cwd). */
	const char* report_path = "sk_profiler_end_to_end.txt";
	const char* json_path = "sk_profiler_end_to_end.json";
	TEST_ASSERT_EQUAL_INT(0, prof->dump_report(report_path));
	TEST_ASSERT_EQUAL_INT(0, prof->dump_report_json(json_path));
	TEST_ASSERT_EQUAL_INT(0, prof->log_report());

	/* Human-readable form: aggregation headers + instrumented zones. */
	FILE* f = fopen(report_path, "r");
	TEST_ASSERT_NOT_NULL(f);
	bool found_frame_stats = false;
	bool found_cumulative = false;
	bool found_pct = false;
#if defined(SK_PROFILER_ENABLED)
	bool found_app_tick = false;
	bool found_tick_timing = false;
	bool found_ecs_spawn = false;
	bool found_ecs_despawn = false;
	bool found_ecs_scheduler = false;
	bool found_rg_begin = false;
	bool found_rg_compile = false;
	bool found_rg_execute = false;
#endif
	char line[512];
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strstr(line, "CPU frame:") != NULL || strstr(line, "GPU frame:") != NULL) {
			found_frame_stats = true;
		}
		if (strstr(line, "Cumulative") != NULL) {
			found_cumulative = true;
		}
		if (strstr(line, "% frame") != NULL || strstr(line, "% of avg frame") != NULL) {
			found_pct = true;
		}
#if defined(SK_PROFILER_ENABLED)
		if (strstr(line, "app tick") != NULL) {
			found_app_tick = true;
		}
		if (strstr(line, "tick timing") != NULL) {
			found_tick_timing = true;
		}
		if (strstr(line, "ecs spawn") != NULL) {
			found_ecs_spawn = true;
		}
		if (strstr(line, "ecs despawn") != NULL) {
			found_ecs_despawn = true;
		}
		if (strstr(line, "ecs scheduler run") != NULL) {
			found_ecs_scheduler = true;
		}
		if (strstr(line, "rg begin") != NULL) {
			found_rg_begin = true;
		}
		if (strstr(line, "rg compile") != NULL) {
			found_rg_compile = true;
		}
		if (strstr(line, "rg execute") != NULL) {
			found_rg_execute = true;
		}
#endif
	}
	fclose(f);
	TEST_ASSERT_TRUE(found_frame_stats);
	TEST_ASSERT_TRUE(found_cumulative);
	TEST_ASSERT_TRUE(found_pct);
#if defined(SK_PROFILER_ENABLED)
	TEST_ASSERT_TRUE(found_app_tick);
	TEST_ASSERT_TRUE(found_tick_timing);
	TEST_ASSERT_TRUE(found_ecs_spawn);
	TEST_ASSERT_TRUE(found_ecs_despawn);
	TEST_ASSERT_TRUE(found_ecs_scheduler);
	TEST_ASSERT_TRUE(found_rg_begin);
	TEST_ASSERT_TRUE(found_rg_compile);
	TEST_ASSERT_TRUE(found_rg_execute);
#endif

	/* Machine-readable form: versioned JSON with per-task aggregation. */
	f = fopen(json_path, "r");
	TEST_ASSERT_NOT_NULL(f);
	bool found_json_format = false;
	bool found_json_task = false;
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strstr(line, "\"format\": \"skore.profiler.report\"") != NULL) {
			found_json_format = true;
		}
		if (strstr(line, "\"frame_pct\"") != NULL && strstr(line, "\"cumulative_pct\"") != NULL) {
			found_json_task = true;
		}
	}
	fclose(f);
	TEST_ASSERT_TRUE(found_json_format);
	TEST_ASSERT_TRUE(found_json_task);

	/* Non-zero, plausible timings: only checkable when instrumentation is
	 * compiled in (without SK_ENABLE_PROFILER the macros compile away). */
#if defined(SK_PROFILER_ENABLED)
	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	prof->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_TRUE(count > 0u);
	bool any_present_nonzero = false;
	i32 app_tick_idx = -1;
	i32 tick_timing_idx = -1;
	for (u32 i = 0u; i < count; i++) {
		if (tasks[i].present && tasks[i].cpu_time > 0.0) {
			any_present_nonzero = true;
		}
		if (strcmp(tasks[i].name, "app tick") == 0) {
			app_tick_idx = (i32)i;
		}
		if (strcmp(tasks[i].name, "tick timing") == 0) {
			tick_timing_idx = (i32)i;
		}
	}
	TEST_ASSERT_TRUE(any_present_nonzero);
	/* Nesting: the main-loop phase tree is app tick -> tick timing. */
	TEST_ASSERT_TRUE(app_tick_idx >= 0);
	TEST_ASSERT_TRUE(tick_timing_idx >= 0);
	TEST_ASSERT_EQUAL_INT32(0, tasks[app_tick_idx].depth);
	TEST_ASSERT_EQUAL_INT32(1, tasks[tick_timing_idx].depth);
	TEST_ASSERT_TRUE(tasks[app_tick_idx].cpu_count >= 1u);
	TEST_ASSERT_TRUE(tasks[tick_timing_idx].cpu_count >= 1u);
#endif

	(void)remove(report_path);
	(void)remove(json_path);

	prof->set_active(false);
	ecs->system_destroy(system);
	ecs->scheduler_destroy(scheduler);
	ecs->world_destroy(world);
	rg->destroy(graph);
	rg->shutdown();
	sk_app_shutdown(ctx);
}

/* ---- dxc_compiler plugin ---- */

SK_TEST(app_init_auto_loads_dxc_compiler_plugin) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_dxc_compiler_api_t* dxc = (const sk_dxc_compiler_api_t*)boot.api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(dxc, "expected sk-dxc-compiler auto-loaded from app_folder/plugins");
	TEST_ASSERT_NOT_NULL(dxc->init);
	TEST_ASSERT_NOT_NULL(dxc->shutdown);
	TEST_ASSERT_NOT_NULL(dxc->compile);
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_test_platform_table();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, boot.api));
	const sk_dxc_compiler_api_t* dxc = (const sk_dxc_compiler_api_t*)boot.api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(dxc);
	TEST_ASSERT_NOT_NULL(dxc->init);
	TEST_ASSERT_NOT_NULL(dxc->compile);
	plat->lib_close(lib);
	sk_app_shutdown(ctx);
}

/* ---- APX-284: host and plugin share one logger context ---- */

typedef struct plugin_log_capture_t {
	u32 hits;
	char last[256];
	char last_logger[SK_LOGGER_NAME_MAX];
} plugin_log_capture_t;

static void plugin_log_capture_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	plugin_log_capture_t* cap = (plugin_log_capture_t*)user_data;
	size_t n;

	(void)level;
	cap->hits += 1u;
	if (logger_name != NULL) {
		n = strlen(logger_name);
		if (n >= sizeof(cap->last_logger)) {
			n = sizeof(cap->last_logger) - 1u;
		}
		memcpy(cap->last_logger, logger_name, n);
		cap->last_logger[n] = '\0';
	}
	if (message == NULL) {
		return;
	}
	n = strlen(message);
	if (n >= sizeof(cap->last)) {
		n = sizeof(cap->last) - 1u;
	}
	memcpy(cap->last, message, n);
	cap->last[n] = '\0';
}

/* Regression: plugin log emissions must land on the HOST's logger context.
 * Plugins statically link their own copy of sk-foundation; before the context
 * refactor each DSO carried a private static sink array, so host-registered
 * sinks never saw plugin output. Now the plugin caches app_api->logger_context
 * at entry and writes into the host-owned sink list. Load a real plugin, add a
 * host sink, force the plugin to emit, and assert the host sink received it. */
SK_TEST(plugin_logs_reach_host_sink) {
	char path[SK_FS_PATH_MAX];
#if defined(_WIN32)
	const_chr_t name = "sk-dxc-compiler.dll";
#elif defined(__APPLE__)
	const_chr_t name = "sk-dxc-compiler.dylib";
#else
	const_chr_t name = "sk-dxc-compiler.so";
#endif
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	const sk_logger_api_t* logger_api;
	sk_logger_context_t* log_ctx;
	plugin_log_capture_t capture;
	sk_log_sink_t sink;
	const sk_dxc_compiler_api_t* dxc;

	TEST_ASSERT_NOT_NULL(ctx);
	logger_api = boot.api->logger_api(ctx);
	log_ctx = boot.api->logger_context(ctx);
	TEST_ASSERT_NOT_NULL(logger_api);
	TEST_ASSERT_NOT_NULL(log_ctx);

	/* Host registers its own sink on the shared logger context (stdout muted
	 * so the deliberately-failing compile below does not spam the test log). */
	memset(&capture, 0, sizeof(capture));
	sink.user_data = &capture;
	sink.print = plugin_log_capture_print;
	TEST_ASSERT_EQUAL_INT(0, logger_api->remove_sink(log_ctx, sk_logger_stdout_sink()));
	TEST_ASSERT_EQUAL_INT(0, logger_api->add_sink(log_ctx, &sink));

	/* sk_app_init auto-loads the plugin DLLs; the dxc entry point cached
	 * app_api->logger_context and created its "dxc-compiler" logger on the
	 * host context at load time. Prove it is loaded (and load it explicitly
	 * if the folder scan was skipped, e.g. a custom plugins dir). */
	dxc = (const sk_dxc_compiler_api_t*)boot.api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
	if (dxc == NULL) {
		TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
		TEST_ASSERT_EQUAL_INT32(0, boot.api->load_plugin(ctx, path));
		dxc = (const sk_dxc_compiler_api_t*)boot.api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID);
	}
	TEST_ASSERT_NOT_NULL(dxc);
	TEST_ASSERT_NOT_NULL(dxc->init);
	TEST_ASSERT_NOT_NULL(dxc->compile);

	/* Emit from the plugin. Whether or not the vendored DXC runtime is
	 * present, an error is surfaced through the plugin's logger: the runtime
	 * load failure, or the HLSL diagnostics from the broken shader. Both go
	 * through the host's logger context — that is the shared-state proof. */
	(void)dxc->init();
	{
		char log_buf[128];
		u8 spirv[256];
		u32 spirv_size = 0u;
		const_chr_t broken = "void mainVS() { float x = ; }\n";
		(void)dxc->compile("mainVS", "vs_6_8", broken, (u32)strlen(broken), spirv, (u32)sizeof(spirv), &spirv_size, log_buf, (u32)sizeof(log_buf));
	}
	dxc->shutdown();

	/* The host sink received the plugin's emission, and it arrived on the
	 * plugin's own logger ("dxc-compiler", created by the plugin against the
	 * host logger context at entry) — one shared logger context, not a
	 * separate per-DLL static sink array. */
	TEST_ASSERT_TRUE(capture.hits >= 1u);
	TEST_ASSERT_EQUAL_STRING("dxc-compiler", capture.last_logger);
	TEST_ASSERT_NOT_NULL(strstr(capture.last, "dxc-compiler:"));

	TEST_ASSERT_EQUAL_INT(0, logger_api->remove_sink(log_ctx, &sink));
	TEST_ASSERT_EQUAL_INT(0, logger_api->add_sink(log_ctx, sk_logger_stdout_sink()));
	sk_app_shutdown(ctx);
}

/* ---- render_graph plugin (C++ main call-site migration) ---- */

SK_TEST(app_init_auto_loads_render_graph_plugin) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_render_graph_api_t* rg = sk_render_graph_api_from_app(ctx, boot.api);
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
	sk_app_shutdown(ctx);
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
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* ctx = boot.context;
	TEST_ASSERT_NOT_NULL(ctx);
	TEST_ASSERT_EQUAL_INT32(0, test_plugin_path(name, path, (u32)sizeof(path)));
	const sk_platform_api_t* plat = sk_test_platform_table();
	sk_shared_lib_t lib = plat->lib_open(path);
	TEST_ASSERT_NOT_NULL(lib);
	void_ptr_t raw = plat->lib_symbol(lib, "sk_plugin_entry_point");
	TEST_ASSERT_NOT_NULL(raw);
	TEST_ASSERT_EQUAL_INT(0, (SK_PTR_TO_FN(entry_fn, raw))(ctx, boot.api));
	const sk_render_graph_api_t* rg = sk_render_graph_api_from_app(ctx, boot.api);
	TEST_ASSERT_NOT_NULL(rg);
	TEST_ASSERT_NOT_NULL(rg->create);
	TEST_ASSERT_NOT_NULL(rg->begin);
	TEST_ASSERT_NOT_NULL(rg->execute);
	plat->lib_close(lib);
	sk_app_shutdown(ctx);
}

#endif /* SK_TESTS */
