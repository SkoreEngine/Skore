#include "app.h"

#include "allocator.h"
#include "array.h"
#include "crash.h"
#include "filesystem.h"
#include "hashmap.h"
#include "logger.h"
#include "path.h"
#include "platform.h"
#include "profiler.h"

/* Engine scene world + physics: the app owns an ECS world (entities plugin)
 * and drives the jolt plugin's fixed-step update against it every frame.
 * Both are plugin public headers (plain C, core-only includes); the plugins
 * themselves are loaded at runtime from {app_folder}/plugins. */
#include "entities.h"
#include "jolt.h"

#include <stdio.h>
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

	/* Profiler table cached when sk-profiler registers (see load_plugin);
	 * drives init/shutdown and the per-frame begin/end delimiters. */
	const sk_profiler_api_t* profiler_api;

	/* Engine scene world + physics: the app creates one ECS world at
	 * bootstrap (entities plugin) and steps the jolt plugin against it every
	 * frame (see app_physics_startup / app_tick_engine_systems). The jolt
	 * table is cached for teardown; the frame loop re-resolves it so an
	 * unregistered (disabled) plugin stops driving physics immediately. */
	sk_world_t* world;
	const sk_entities_api_t* entities_api;
	const sk_jolt_api_t* jolt_api;
	i32 physics_started;
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
static sk_world_t* sk_app_scene_world_impl(sk_app_context_t* context);
static i32 sk_app_bootstrap_init(sk_app_context_t* context);
static void sk_app_bootstrap_shutdown(sk_app_context_t* context);
static void app_profiler_shutdown(sk_app_context_t* context);
static void app_physics_startup(sk_app_context_t* context);
static void app_physics_shutdown(sk_app_context_t* context);
static void app_main_loop_exit(sk_app_context_t* context);
static i32 app_tick_frame(sk_app_context_t* context);
static void app_tick_engine_systems(sk_app_context_t* context);

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
	sk_app_load_plugin_impl, sk_app_request_shutdown_impl, sk_app_delta_time_impl, sk_app_fps_impl,			sk_app_elapsed_time_impl, sk_app_scene_world_impl,
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

	/* Profiler teardown before the plugin libraries unload. */
	app_profiler_shutdown(context);

	/* Engine physics teardown (jolt world + scene world) before the plugin
	 * libraries unload. */
	app_physics_shutdown(context);

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

/* ---- profiler lifecycle (host-driven; plugin registers its table at load) ---- */

/**
 * Shut the profiler plugin down and drop the cached table.
 * Safe when the plugin never loaded (profiler_api == NULL).
 */
static void app_profiler_shutdown(sk_app_context_t* context) {
	if (context->profiler_api == NULL) {
		return;
	}
	if (context->log != NULL) {
		sk_log_debug(sk_logger_api(), context->log, "profiler shutdown");
	}
	context->profiler_api->shutdown();
	context->profiler_api = NULL;
}

/* ---- physics lifecycle (engine-owned; plugins register their tables at load) ---- */

/**
 * Start the engine physics subsystem: create the engine scene world (owned by
 * the app; hosts reach it via app_api->scene_world) and, when the jolt plugin
 * is registered, initialize the physics world with default settings so the
 * frame loop can drive the fixed-step update and write simulated poses back.
 * Safe when the entities / jolt plugins never loaded (fields stay NULL/0).
 */
static void app_physics_startup(sk_app_context_t* context) {
	context->entities_api = (const sk_entities_api_t*)sk_app_get_api_impl(context, SK_ENTITIES_API_TYPE_ID);
	if (context->entities_api == NULL || context->entities_api->world_create == NULL) {
		return;
	}

	context->world = context->entities_api->world_create();
	if (context->world == NULL) {
		return;
	}

	context->jolt_api = (const sk_jolt_api_t*)sk_app_get_api_impl(context, SK_JOLT_API_TYPE_ID);
	if (context->jolt_api != NULL && context->jolt_api->init != NULL && context->jolt_api->init(NULL) == 0) {
		context->physics_started = 1;
		if (context->log != NULL) {
			sk_log_info(sk_logger_api(), context->log, "physics initialized (jolt)");
		}
	}
}

/**
 * Tear the engine physics subsystem down: shut the jolt world down (when the
 * app initialized it) and destroy the engine scene world — both before the
 * plugin libraries unload. Safe when never started / already stopped.
 */
static void app_physics_shutdown(sk_app_context_t* context) {
	if (context->physics_started != 0) {
		if (context->jolt_api != NULL && context->jolt_api->shutdown != NULL) {
			if (context->log != NULL) {
				sk_log_debug(sk_logger_api(), context->log, "physics shutdown (jolt)");
			}
			context->jolt_api->shutdown();
		}
		context->physics_started = 0;
	}
	if (context->world != NULL) {
		if (context->entities_api != NULL && context->entities_api->world_destroy != NULL) {
			context->entities_api->world_destroy(context->world);
		}
		context->world = NULL;
	}
	context->jolt_api = NULL;
	context->entities_api = NULL;
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

	/* Engine physics: scene world + jolt world (when the plugins are present;
	 * the frame loop steps them, see app_tick_engine_systems). */
	app_physics_startup(context);

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

	/* Profiler teardown must run before the plugin libraries unload. */
	app_profiler_shutdown(context);

	/* Engine physics teardown (jolt world + scene world) before the plugin
	 * libraries unload. */
	app_physics_shutdown(context);

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

	/* Redirect the plugin's static-linked logger module to the host table so
	 * host-registered sinks (file, editor console, …) receive plugin sk_log_*.
	 * Symbol is optional: older / stripped plugins keep a private stdout sink. */
	{
		void_ptr_t bind_raw = plat->lib_symbol(lib, "sk_logger_bind_api");
		if (bind_raw != NULL) {
			typedef void (*sk_logger_bind_api_fn)(const sk_logger_api_t* api);
			SK_PTR_TO_FN(sk_logger_bind_api_fn, bind_raw)(logger_api);
		}
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

static sk_world_t* sk_app_scene_world_impl(sk_app_context_t* context) {
	return context->world;
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

/**
 * Main-loop exit bookkeeping shared by sk_app_tick / app_tick_frame: log the
 * exit and clear the loop-started flag (request_shutdown is sticky).
 */
static void app_main_loop_exit(sk_app_context_t* context) {
	if (context->loop_started != 0) {
		if (context->log != NULL) {
			sk_log_info(sk_logger_api(), context->log, "main loop exit (elapsed %.3f s)", context->elapsed_time);
		}
		context->loop_started = 0;
	}
}

/* ---- engine frame systems (physics) ---- */

/**
 * Engine frame systems: drive the physics plugin's fixed-step update against
 * the engine scene world and write the simulated poses back onto the owning
 * transform / rigid-body state components (step_world does sync → fixed steps
 * → write-back, including the kinematic / teleported reverse path). The jolt
 * table is resolved fresh from the registry every frame: when the plugin is
 * not loaded (or its API is unregistered — plugin disabled) the loop leaves
 * entity transforms untouched. No-op without an engine scene world.
 */
static void app_tick_engine_systems(sk_app_context_t* context) {
	if (context->world == NULL) {
		return;
	}
	const sk_jolt_api_t* jolt = (const sk_jolt_api_t*)sk_app_get_api_impl(context, SK_JOLT_API_TYPE_ID);
	if (jolt == NULL || jolt->step_world == NULL) {
		return;
	}
	{
		SK_PROFILE_CPU_ZONE(context->profiler_api, "physics step");
		jolt->step_world(context->world, (f32)context->delta_time);
	}
}

/**
 * One frame of main-loop work once the frame delta is set: profiler frame
 * delimiters bracket the frame phases (engine systems: the jolt physics
 * fixed-step update + write-back) and the post-frame shutdown check runs
 * after end_frame. The loop-entered timing baseline is established by the
 * caller (sk_app_tick or the test frame driver).
 */
static i32 app_tick_frame(sk_app_context_t* context) {
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
		/* Frame phases / systems land here, between begin/end. */
		app_tick_engine_systems(context);
	}

	if (context->profiler_api != NULL) {
		context->profiler_api->end_frame();
	}

	if (context->shutdown_requested != 0) {
		app_main_loop_exit(context);
		return 0;
	}
	return 1;
}

i32 sk_app_tick(sk_app_context_t* context) {
	if (context->initialized == 0) {
		return 0;
	}

	/* Do not clear shutdown_requested: a prior request_shutdown() must make
	 * the host loop exit immediately (tests / hosts). */
	if (context->shutdown_requested != 0) {
		app_main_loop_exit(context);
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
	return app_tick_frame(context);
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
#include "jolt_components.h"
#include "resource_serialize.h"

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

/* The app auto-loads plugins in sorted filename order (sk-entities before
 * sk-jolt), so the jolt plugin entry point registers the physics ECS
 * components with the entities API at load. Verifies the components appear in
 * the ECS component registry exactly like the entities plugin's own types. */
SK_TEST(app_init_auto_loads_jolt_plugin) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)sk_app_api()->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL_MESSAGE(ecs, "expected sk-entities auto-loaded from app_folder/plugins");

	sk_component_info_t info;
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("rigid_body_config", info.name);
	TEST_ASSERT_EQUAL_UINT32((u32)sizeof(sk_rigid_body_config_t), info.size);

	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("rigid_body_state", info.name);

	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_BOX_COLLIDER_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("box_collider", info.name);
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID, &info));
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_TRANSFORM_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("transform", info.name);
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("character_config", info.name);
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_CHARACTER_STATE_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("character_state", info.name);
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

/* ---- profiler plugin ---- */

SK_TEST(app_init_auto_loads_profiler_plugin) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_profiler_api_t* prof = (const sk_profiler_api_t*)sk_app_api()->get_api(ctx, SK_PROFILER_API_TYPE_ID);
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
	sk_app_destroy(ctx);
}

SK_TEST(app_tick_delivers_profiler_frames) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_profiler_api_t* prof = (const sk_profiler_api_t*)sk_app_api()->get_api(ctx, SK_PROFILER_API_TYPE_ID);
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
	sk_app_destroy(ctx);
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
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
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

/* ---- physics components (jolt plugin) ---- */

/* Host-side JSON round-trip for the physics component payload types. The
 * jolt plugin deliberately does not link resource_serialize (its *_to_file
 * helpers pull sk_filesystem_api, which only exists in sk-app), so the
 * serialize/deserialize identity of the component payloads is verified here
 * where sk_filesystem_api resolves. */

static sk_repository_t* app_jolt_components_repo(void) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_jolt_component_types_register(repo));
	return repo;
}

/* serialize -> destroy -> deserialize -> re-serialize; JSON must be identical. */
static void app_jolt_components_assert_double_serialize_identity(sk_repository_t* repo, sk_rid_t rid) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	char* json1 = NULL;
	u32 size1 = 0u;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json1, &size1));
	TEST_ASSERT_NOT_NULL(json1);
	TEST_ASSERT_TRUE(size1 > 0u);

	sk_uuid_t uuid = api->resource_uuid(repo, rid);
	api->destroy_resource(repo, rid, NULL);

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_make(json1, size1), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		sk_uuid_t got = api->resource_uuid(repo, loaded);
		TEST_ASSERT_EQUAL_UINT64(uuid.lo, got.lo);
		TEST_ASSERT_EQUAL_UINT64(uuid.hi, got.hi);
	}

	char* json2 = NULL;
	u32 size2 = 0u;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, loaded, a, &json2, &size2));
	TEST_ASSERT_NOT_NULL(json2);
	TEST_ASSERT_EQUAL_UINT32(size1, size2);
	TEST_ASSERT_EQUAL_MEMORY(json1, json2, size1);

	a->free(a->instance, json1);
	a->free(a->instance, json2);
}

SK_TEST(jolt_components_serialize_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = app_jolt_components_repo();

	/* Rigid body config: every cold field round-trips through JSON. */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "RigidBodyConfigResource");
		TEST_ASSERT_NOT_NULL(type);
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_enum(w, SK_RIGID_BODY_CONFIG_FIELD_MOTION_TYPE, SK_JOLT_MOTION_TYPE_KINEMATIC));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_MASS, 4.0));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_FRICTION, 0.5));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_RESTITUTION, 0.1));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_LINEAR_DAMPING, 0.01));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_ANGULAR_DAMPING, 0.02));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_GRAVITY_FACTOR, 1.5));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RIGID_BODY_CONFIG_FIELD_OBJECT_LAYER, SK_JOLT_OBJECT_LAYER_NON_MOVING));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RIGID_BODY_CONFIG_FIELD_FLAGS, SK_RIGID_BODY_FLAG_ALLOW_SLEEPING));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}

	/* Rigid body state: hot velocities (vec3 wire form). */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "RigidBodyStateResource");
		TEST_ASSERT_NOT_NULL(type);
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_RIGID_BODY_STATE_FIELD_LINEAR_VELOCITY, sk_vec3(10.0f, -5.5f, 0.0f)));
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_RIGID_BODY_STATE_FIELD_ANGULAR_VELOCITY, sk_vec3(0.0f, 0.0f, 2.25f)));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}

	/* Box / sphere / capsule colliders. */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "BoxColliderResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_BOX_COLLIDER_FIELD_HALF_EXTENT, sk_vec3(2.0f, 1.0f, 0.5f)));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "SphereColliderResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SPHERE_COLLIDER_FIELD_RADIUS, 1.25));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "CapsuleColliderResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CAPSULE_COLLIDER_FIELD_HALF_HEIGHT, 1.0));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CAPSULE_COLLIDER_FIELD_RADIUS, 0.4));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "CharacterConfigResource");
		TEST_ASSERT_NOT_NULL(type);
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CHARACTER_CONFIG_FIELD_RADIUS, 0.35));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CHARACTER_CONFIG_FIELD_HEIGHT, 1.9));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CHARACTER_CONFIG_FIELD_MAX_SLOPE_ANGLE, 0.7));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CHARACTER_CONFIG_FIELD_STEP_HEIGHT, 0.35));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CHARACTER_CONFIG_FIELD_MASS, 80.0));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_CHARACTER_CONFIG_FIELD_OBJECT_LAYER, SK_JOLT_OBJECT_LAYER_MOVING));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "CharacterStateResource");
		TEST_ASSERT_NOT_NULL(type);
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_CHARACTER_STATE_FIELD_VELOCITY, sk_vec3(1.0f, 0.0f, -0.5f)));
		TEST_ASSERT_EQUAL_INT(0, api->set_enum(w, SK_CHARACTER_STATE_FIELD_GROUND_STATE, SK_JOLT_GROUND_STATE_GROUNDED));
		api->commit(w, NULL);
		app_jolt_components_assert_double_serialize_identity(repo, rid);
	}

	api->destroy(repo);
}

/* Host-side ECS ↔ Jolt sync (APX-307). Plugin-local tests cover the same
 * paths when sk-entities is already registered; this always runs after
 * sorted auto-load so both plugins are live. */
SK_TEST(jolt_ecs_sync_world_gravity_kinematic_remove_velocity) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* host = sk_app_api();
	const sk_jolt_api_t* jolt = (const sk_jolt_api_t*)host->get_api(ctx, SK_JOLT_API_TYPE_ID);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)host->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(jolt);
	TEST_ASSERT_NOT_NULL(ecs);
	TEST_ASSERT_NOT_NULL(jolt->sync_world);
	TEST_ASSERT_NOT_NULL(jolt->write_back);
	TEST_ASSERT_NOT_NULL(jolt->step_world);

	TEST_ASSERT_EQUAL_INT32(0, jolt->init(NULL));
	sk_world_t* world = ecs->world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t ids[] = {SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, SK_TRANSFORM_COMPONENT_TYPE_ID, SK_BOX_COLLIDER_COMPONENT_TYPE_ID};

	/* Static floor at y=-1 (top at y=0) and a dynamic box dropped from y=5. */
	sk_entity_t floor = ecs->world_spawn(world, ids, 4u);
	sk_entity_t box = ecs->world_spawn(world, ids, 4u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(floor));
	TEST_ASSERT_TRUE(sk_entity_is_valid(box));

	sk_rigid_body_config_t* floor_cfg = (sk_rigid_body_config_t*)ecs->world_component(world, floor, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	floor_cfg->motion_type = SK_JOLT_MOTION_TYPE_STATIC;
	floor_cfg->object_layer = SK_JOLT_OBJECT_LAYER_NON_MOVING;
	floor_cfg->friction = 0.2f;
	((sk_box_collider_t*)ecs->world_component(world, floor, SK_BOX_COLLIDER_COMPONENT_TYPE_ID))->half_extent = sk_vec3(10.0f, 1.0f, 10.0f);
	((sk_transform_t*)ecs->world_component(world, floor, SK_TRANSFORM_COMPONENT_TYPE_ID))->position = sk_vec3(0.0f, -1.0f, 0.0f);
	((sk_transform_t*)ecs->world_component(world, floor, SK_TRANSFORM_COMPONENT_TYPE_ID))->rotation = sk_quat_identity();

	sk_rigid_body_config_t* box_cfg = (sk_rigid_body_config_t*)ecs->world_component(world, box, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	box_cfg->motion_type = SK_JOLT_MOTION_TYPE_DYNAMIC;
	box_cfg->mass = 1.0f;
	box_cfg->friction = 0.2f;
	box_cfg->linear_damping = 0.05f;
	box_cfg->angular_damping = 0.05f;
	box_cfg->gravity_factor = 1.0f;
	box_cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
	box_cfg->flags = SK_RIGID_BODY_FLAG_ALLOW_SLEEPING;
	((sk_box_collider_t*)ecs->world_component(world, box, SK_BOX_COLLIDER_COMPONENT_TYPE_ID))->half_extent = sk_vec3(0.5f, 0.5f, 0.5f);
	((sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->position = sk_vec3(0.0f, 5.0f, 0.0f);
	((sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->rotation = sk_quat_identity();

	i32 i;
	i32 settled = 0;
	for (i = 0; i < 60; ++i) {
		jolt->step_world(world, 1.0f / 60.0f);
	}
	TEST_ASSERT_TRUE(((sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.y < 4.0f);

	for (i = 0; i < 1800; ++i) {
		jolt->step_world(world, 1.0f / 60.0f);
		const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
		const sk_rigid_body_state_t* st = (const sk_rigid_body_state_t*)ecs->world_component(world, box, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
		const f32 v2 = st->linear_velocity.x * st->linear_velocity.x + st->linear_velocity.y * st->linear_velocity.y + st->linear_velocity.z * st->linear_velocity.z;
		if (xf->position.y > 0.4f && xf->position.y < 0.6f && v2 < 0.05f * 0.05f) {
			settled = 1;
			break;
		}
	}
	TEST_ASSERT_TRUE(settled);

	/* Kinematic follows its transform. */
	sk_entity_t kin = ecs->world_spawn(world, ids, 4u);
	sk_rigid_body_config_t* kin_cfg = (sk_rigid_body_config_t*)ecs->world_component(world, kin, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	kin_cfg->motion_type = SK_JOLT_MOTION_TYPE_KINEMATIC;
	kin_cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
	kin_cfg->gravity_factor = 1.0f;
	((sk_box_collider_t*)ecs->world_component(world, kin, SK_BOX_COLLIDER_COMPONENT_TYPE_ID))->half_extent = sk_vec3(0.5f, 0.5f, 0.5f);
	sk_transform_t* kin_xf = (sk_transform_t*)ecs->world_component(world, kin, SK_TRANSFORM_COMPONENT_TYPE_ID);
	kin_xf->position = sk_vec3(8.0f, 3.0f, 0.0f);
	kin_xf->rotation = sk_quat_identity();
	for (i = 0; i < 30; ++i) {
		jolt->step_world(world, 1.0f / 60.0f);
	}
	kin_xf = (sk_transform_t*)ecs->world_component(world, kin, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 8.0f, kin_xf->position.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 3.0f, kin_xf->position.y);
	kin_xf->position = sk_vec3(8.0f, 7.0f, 1.0f);
	for (i = 0; i < 15; ++i) {
		jolt->step_world(world, 1.0f / 60.0f);
	}
	kin_xf = (sk_transform_t*)ecs->world_component(world, kin, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 7.0f, kin_xf->position.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 1.0f, kin_xf->position.z);

	/* Removing the collider removes the Jolt body (ray no longer hits). */
	sk_entity_t probe = ecs->world_spawn(world, ids, 4u);
	sk_rigid_body_config_t* probe_cfg = (sk_rigid_body_config_t*)ecs->world_component(world, probe, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	probe_cfg->motion_type = SK_JOLT_MOTION_TYPE_STATIC;
	probe_cfg->object_layer = SK_JOLT_OBJECT_LAYER_NON_MOVING;
	((sk_box_collider_t*)ecs->world_component(world, probe, SK_BOX_COLLIDER_COMPONENT_TYPE_ID))->half_extent = sk_vec3(0.5f, 0.5f, 0.5f);
	sk_transform_t* probe_xf = (sk_transform_t*)ecs->world_component(world, probe, SK_TRANSFORM_COMPONENT_TYPE_ID);
	probe_xf->position = sk_vec3(20.0f, 0.0f, -5.0f);
	probe_xf->rotation = sk_quat_identity();
	jolt->sync_world(world);
	{
		sk_jolt_query_hit_t hit;
		sk_jolt_vec3_t origin = {20.0f, 0.0f, 0.0f};
		sk_jolt_vec3_t toward = {0.0f, 0.0f, -1.0f};
		TEST_ASSERT_EQUAL_INT32(0, jolt->ray_cast(&origin, &toward, 10.0f, SK_JOLT_OBJECT_LAYER_MOVING, &hit));
		TEST_ASSERT_NOT_NULL(hit.body);
		TEST_ASSERT_EQUAL_INT32(0, ecs->world_remove_component(world, probe, SK_BOX_COLLIDER_COMPONENT_TYPE_ID));
		jolt->sync_world(world);
		TEST_ASSERT_NULL(((sk_rigid_body_config_t*)ecs->world_component(world, probe, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID))->body);
		TEST_ASSERT_EQUAL_INT32(1, jolt->ray_cast(&origin, &toward, 10.0f, SK_JOLT_OBJECT_LAYER_MOVING, &hit));
	}

	/* Velocities authored on the hot component are applied and read back. */
	jolt->shutdown();
	sk_jolt_settings_t settings;
	jolt->settings_defaults(&settings);
	settings.gravity[0] = 0.0f;
	settings.gravity[1] = 0.0f;
	settings.gravity[2] = 0.0f;
	TEST_ASSERT_EQUAL_INT32(0, jolt->init(&settings));
	sk_world_t* world2 = ecs->world_create();
	sk_entity_t flyer = ecs->world_spawn(world2, ids, 4u);
	sk_rigid_body_config_t* flyer_cfg = (sk_rigid_body_config_t*)ecs->world_component(world2, flyer, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	flyer_cfg->motion_type = SK_JOLT_MOTION_TYPE_DYNAMIC;
	flyer_cfg->mass = 1.0f;
	flyer_cfg->gravity_factor = 0.0f;
	flyer_cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
	((sk_box_collider_t*)ecs->world_component(world2, flyer, SK_BOX_COLLIDER_COMPONENT_TYPE_ID))->half_extent = sk_vec3(0.5f, 0.5f, 0.5f);
	((sk_transform_t*)ecs->world_component(world2, flyer, SK_TRANSFORM_COMPONENT_TYPE_ID))->rotation = sk_quat_identity();
	sk_rigid_body_state_t* flyer_st = (sk_rigid_body_state_t*)ecs->world_component(world2, flyer, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
	flyer_st->linear_velocity = sk_vec3(3.0f, 0.0f, 0.0f);
	jolt->step_world(world2, 1.0f / 60.0f);
	flyer_st = (sk_rigid_body_state_t*)ecs->world_component(world2, flyer, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
	TEST_ASSERT_TRUE(flyer_st->linear_velocity.x > 1.5f);
	TEST_ASSERT_TRUE(((sk_transform_t*)ecs->world_component(world2, flyer, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.x > 0.0f);

	ecs->world_destroy(world2);
	ecs->world_destroy(world);
	jolt->shutdown();
	sk_app_destroy(ctx);
}

/* ---- engine loop → jolt physics (APX-323) ---- */

/* Deterministic frame driver for engine-loop tests: like sk_app_tick but with
 * an explicit frame delta instead of the wall clock (bootstrap_tick_timing is
 * skipped; timing state is left to the caller). The shutdown pre-check and the
 * frame body (profiler delimiters, engine systems, post-frame shutdown check)
 * are identical to sk_app_tick. */
static i32 app_test_tick_delta(sk_app_context_t* context, f64 delta) {
	if (context->initialized == 0) {
		return 0;
	}
	if (context->shutdown_requested != 0) {
		return 0;
	}
	context->loop_started = 1;
	context->delta_time = delta;
	return app_tick_frame(context);
}

/* Bitwise f32 equality (true when a value was not written at all). memcmp over
 * unsigned-char views: comparing float object representation directly trips
 * bugprone-suspicious-memory-comparison and float == trips -Wfloat-equal. */
static i32 app_test_f32_bit_equal(f32 a, f32 b) {
	const u8* pa = (const u8*)&a;
	const u8* pb = (const u8*)&b;
	return memcmp(pa, pb, sizeof(a)) == 0;
}

/* Bitwise pose equality (true when no write touched the transform). Same
 * rationale as app_test_f32_bit_equal. */
static i32 app_test_pose_bit_equal(const sk_transform_t* a, const sk_transform_t* b) {
	const u8* pa = (const u8*)a;
	const u8* pb = (const u8*)b;
	return memcmp(pa, pb, sizeof(*a)) == 0;
}

/* Spawn a box rigid-body entity (config + state + transform + box collider)
 * into @p world. Defaults: mass 1, friction 0.2, no restitution, damping
 * 0.05, sleeping allowed, layer by motion type (NON_MOVING / MOVING). */
static sk_entity_t app_test_spawn_box(sk_world_t* world, const sk_entities_api_t* ecs, sk_jolt_motion_type_t motion, const sk_vec3_t* position, const sk_vec3_t* half_extent,
									  f32 gravity_factor) {
	const sk_type_id_t ids[] = {SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, SK_TRANSFORM_COMPONENT_TYPE_ID, SK_BOX_COLLIDER_COMPONENT_TYPE_ID};
	sk_entity_t entity = ecs->world_spawn(world, ids, 4u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));

	sk_rigid_body_config_t* cfg = (sk_rigid_body_config_t*)ecs->world_component(world, entity, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(cfg);
	cfg->motion_type = motion;
	cfg->mass = 1.0f;
	cfg->friction = 0.2f;
	cfg->restitution = 0.0f;
	cfg->linear_damping = 0.05f;
	cfg->angular_damping = 0.05f;
	cfg->gravity_factor = gravity_factor;
	cfg->object_layer = (motion == SK_JOLT_MOTION_TYPE_STATIC) ? SK_JOLT_OBJECT_LAYER_NON_MOVING : SK_JOLT_OBJECT_LAYER_MOVING;
	cfg->flags = SK_RIGID_BODY_FLAG_ALLOW_SLEEPING;

	sk_box_collider_t* box = (sk_box_collider_t*)ecs->world_component(world, entity, SK_BOX_COLLIDER_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(box);
	box->half_extent = *half_extent;

	sk_transform_t* xf = (sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(xf);
	xf->position = *position;
	xf->rotation = sk_quat_identity();
	return entity;
}

/* Drive up to @p frames frames of @p dt and report whether the box settled by
 * the end (resting on the floor, near-zero velocity). */
static i32 app_test_box_settled(sk_app_context_t* ctx, sk_world_t* world, const sk_entities_api_t* ecs, sk_entity_t box, i32 frames, f64 dt) {
	i32 i;
	for (i = 0; i < frames; ++i) {
		TEST_ASSERT_TRUE(app_test_tick_delta(ctx, dt) != 0);
		const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
		const sk_rigid_body_state_t* st = (const sk_rigid_body_state_t*)ecs->world_component(world, box, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
		const f32 v2 = st->linear_velocity.x * st->linear_velocity.x + st->linear_velocity.y * st->linear_velocity.y + st->linear_velocity.z * st->linear_velocity.z;
		if (xf->position.y > 0.4f && xf->position.y < 0.6f && v2 < 0.05f * 0.05f) {
			return 1;
		}
	}
	return 0;
}

/* The engine frame loop drives the jolt fixed-step update against the scene
 * world and writes simulated poses back: a dynamic box over a static floor
 * falls and settles with no manual step_world calls. Also proves partial
 * frames (delta shorter than the fixed timestep, no completed step) never
 * touch the transform — at dt = step/3 one step completes every third frame
 * and the other two frames must leave the pose byte-identical. */
SK_TEST(app_engine_loop_drives_jolt_fixed_step_and_writeback) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	const sk_jolt_api_t* jolt = (const sk_jolt_api_t*)api->get_api(ctx, SK_JOLT_API_TYPE_ID);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(jolt);
	TEST_ASSERT_NOT_NULL(ecs);
	TEST_ASSERT_NOT_NULL(api->scene_world);

	/* The engine owns the scene world; the frame loop steps it. */
	sk_world_t* world = api->scene_world(ctx);
	TEST_ASSERT_NOT_NULL(world);

	/* Static floor (top at y=0) + dynamic box dropped from y=5. */
	const sk_vec3_t floor_half = sk_vec3(10.0f, 1.0f, 10.0f);
	const sk_vec3_t floor_pos = sk_vec3(0.0f, -1.0f, 0.0f);
	TEST_ASSERT_TRUE(sk_entity_is_valid(app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_STATIC, &floor_pos, &floor_half, 1.0f)));
	const sk_vec3_t box_half = sk_vec3(0.5f, 0.5f, 0.5f);
	const sk_vec3_t box_pos = sk_vec3(0.0f, 5.0f, 0.0f);
	sk_entity_t box = app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_DYNAMIC, &box_pos, &box_half, 1.0f);
	TEST_ASSERT_TRUE(sk_entity_is_valid(box));

	const f64 step = 1.0 / 60.0;

	/* Partial frames must not touch the transform (no jitter from partial
	 * steps): byte-identical pose on the two non-step frames of each three. */
	{
		sk_transform_t prev = *(const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
		i32 i;
		for (i = 0; i < 12; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, step / 3.0) != 0);
			const sk_transform_t* cur = (const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
			if (i % 3 == 2) {
				TEST_ASSERT_TRUE(cur->position.y < prev.position.y); /* completed step: fell */
			} else {
				TEST_ASSERT_TRUE(app_test_pose_bit_equal(&prev, cur)); /* partial frame: no write */
			}
			prev = *cur;
		}
	}

	/* The engine loop stepped physics and wrote pose + velocity back. */
	sk_transform_t* xf = (sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
	sk_rigid_body_state_t* st = (sk_rigid_body_state_t*)ecs->world_component(world, box, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(xf);
	TEST_ASSERT_NOT_NULL(st);
	TEST_ASSERT_TRUE(xf->position.y < 5.0f);
	TEST_ASSERT_TRUE(st->linear_velocity.y < 0.0f);

	/* 2 s at 60 Hz: the box keeps falling toward the floor. */
	{
		i32 i;
		for (i = 0; i < 120; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)step) != 0);
		}
	}
	xf = (sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_TRUE(xf->position.y < 4.0f);

	/* Settle: rest on the floor (0.5-half box on top of the y=0 floor →
	 * center y ≈ 0.5) with near-zero velocity. */
	TEST_ASSERT_TRUE(app_test_box_settled(ctx, world, ecs, box, 600, (f64)step));

	/* Long idle run: the pose stays settled (stable, no drift). */
	{
		/* Rest phase: let the contact solver finish pushing the landed box out
		 * of its discrete-collision penetration before sampling the pose. */
		i32 rest;
		for (rest = 0; rest < 300; ++rest) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)step) != 0);
		}
		const f32 y_settled = ((const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.y;
		i32 i;
		for (i = 0; i < 300; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)step) != 0);
		}
		xf = (sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
		TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, y_settled, xf->position.y);
		TEST_ASSERT_TRUE(xf->position.y > 0.4f && xf->position.y < 0.6f);
	}

	/* Reverse path through the engine loop: a kinematic body follows its
	 * authored transform, and teleporting the transform mid-run is picked up
	 * by the next frame (the Jolt body is re-placed, not written over). */
	{
		const sk_vec3_t kin_pos = sk_vec3(8.0f, 3.0f, 0.0f);
		sk_entity_t kin = app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_KINEMATIC, &kin_pos, &box_half, 1.0f);
		TEST_ASSERT_TRUE(sk_entity_is_valid(kin));
		i32 i;
		for (i = 0; i < 30; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)step) != 0);
		}
		const sk_transform_t* kin_xf = (const sk_transform_t*)ecs->world_component(world, kin, SK_TRANSFORM_COMPONENT_TYPE_ID);
		TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 8.0f, kin_xf->position.x);
		TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 3.0f, kin_xf->position.y);

		/* Teleport the authored pose; the next frames drive it into Jolt. */
		sk_transform_t* kin_xf_mut = (sk_transform_t*)ecs->world_component(world, kin, SK_TRANSFORM_COMPONENT_TYPE_ID);
		kin_xf_mut->position = sk_vec3(8.0f, 7.0f, 1.0f);
		for (i = 0; i < 15; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)step) != 0);
		}
		kin_xf = (const sk_transform_t*)ecs->world_component(world, kin, SK_TRANSFORM_COMPONENT_TYPE_ID);
		TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 7.0f, kin_xf->position.y);
		TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 1.0f, kin_xf->position.z);
	}

	sk_app_destroy(ctx);
}

/* The fixed-step sync is stable across variable frame times: a zero-gravity
 * flyer with a known velocity moves exactly v * step per completed step and
 * is byte-identical on partial frames, under a mixed pattern of frame deltas
 * (halves, fulls, doubles of the fixed timestep); the total equals the
 * fixed-step count. A settled box then stays put (within contact tolerance)
 * under the same mixed pattern, still never changing on partial frames. */
SK_TEST(app_engine_loop_variable_frame_times_no_partial_step_jitter) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	const sk_jolt_api_t* jolt = (const sk_jolt_api_t*)api->get_api(ctx, SK_JOLT_API_TYPE_ID);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(jolt);
	TEST_ASSERT_NOT_NULL(ecs);
	sk_world_t* world = api->scene_world(ctx);
	TEST_ASSERT_NOT_NULL(world);

	/* Static floor (top at y=0) shared by both phases. */
	{
		const sk_vec3_t floor_half = sk_vec3(10.0f, 1.0f, 10.0f);
		const sk_vec3_t floor_pos = sk_vec3(0.0f, -1.0f, 0.0f);
		TEST_ASSERT_TRUE(sk_entity_is_valid(app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_STATIC, &floor_pos, &floor_half, 1.0f)));
	}

	/* Zero-gravity flyer: uniform motion makes every completed step an exact
	 * v * step displacement and every partial frame provably motionless. */
	const sk_vec3_t box_half = sk_vec3(0.5f, 0.5f, 0.5f);
	const sk_vec3_t flyer_pos = sk_vec3(0.0f, 1.0f, 0.0f);
	sk_entity_t flyer = app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_DYNAMIC, &flyer_pos, &box_half, 0.0f);
	TEST_ASSERT_TRUE(sk_entity_is_valid(flyer));
	sk_rigid_body_config_t* flyer_cfg = (sk_rigid_body_config_t*)ecs->world_component(world, flyer, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	flyer_cfg->linear_damping = 0.0f;
	flyer_cfg->angular_damping = 0.0f;
	((sk_rigid_body_state_t*)ecs->world_component(world, flyer, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID))->linear_velocity = sk_vec3(1.0f, 0.0f, 0.0f);

	const f32 step = 1.0f / 60.0f;
	const f32 k_eps = 1.0e-4f;
	/* Mixed frame deltas: half, half, full, double, half, full (of step). */
	const f32 deltas[6] = {step / 2.0f, step / 2.0f, step, 2.0f * step, step / 2.0f, step};
	f32 acc = 0.0f;
	f32 prev_flyer_x = 0.0f;
	u64 total_steps = 0u;
	i32 i;
	for (i = 0; i < 600; ++i) {
		const f32 dt = deltas[i % 6];
		TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)dt) != 0);
		acc += dt;
		u32 drained = 0u;
		while (acc + k_eps >= step) {
			acc -= step;
			++drained;
		}
		total_steps += drained;
		const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, flyer, SK_TRANSFORM_COMPONENT_TYPE_ID);
		if (drained == 0u) {
			/* Partial frame: the transform must be byte-identical (no
			 * partial-step write, no jitter). */
			TEST_ASSERT_TRUE(app_test_f32_bit_equal(prev_flyer_x, xf->position.x));
		} else {
			/* Completed step(s): moved by exactly v * step * drained. */
			const f32 expected = 1.0f * step * (f32)drained;
			TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, prev_flyer_x + expected, xf->position.x);
		}
		prev_flyer_x = xf->position.x;
	}
	/* The whole run equals the fixed-step total: no time lost or doubled. */
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, (f32)total_steps * step, ((const sk_transform_t*)ecs->world_component(world, flyer, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.x);

	/* A settled box under the same mixed pattern: stays put (contact
	 * tolerance) and never changes on partial frames. (The flyer is despawned
	 * first so the falling box never collides with it.) */
	{
		TEST_ASSERT_EQUAL_INT32(0, ecs->world_despawn(world, flyer));
		const sk_vec3_t box_pos = sk_vec3(0.0f, 5.0f, 0.0f);
		sk_entity_t box = app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_DYNAMIC, &box_pos, &box_half, 1.0f);
		TEST_ASSERT_TRUE(sk_entity_is_valid(box));
		TEST_ASSERT_TRUE(app_test_box_settled(ctx, world, ecs, box, 720, (f64)step));

		/* Let the contact solver finish pushing the landed box out of its
		 * discrete-collision penetration before sampling the resting pose
		 * (a 5 m drop lands ~6 cm deep at discrete quality; the solver
		 * corrects over the next seconds). */
		{
			i32 rest;
			for (rest = 0; rest < 300; ++rest) {
				TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)step) != 0);
			}
		}
		const f32 y_settled = ((const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.y;
		f32 last_x = ((const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.x;
		f32 last_y = y_settled;
		f32 settled_acc = 0.0f;
		for (i = 0; i < 240; ++i) {
			const f32 dt = deltas[i % 6];
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, (f64)dt) != 0);
			settled_acc += dt;
			u32 drained = 0u;
			while (settled_acc + k_eps >= step) {
				settled_acc -= step;
				++drained;
			}
			const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);
			TEST_ASSERT_FLOAT_WITHIN(5.0e-4f, y_settled, xf->position.y);
			if (drained == 0u) {
				/* No completed step → no write-back → byte-identical pose. */
				TEST_ASSERT_TRUE(app_test_f32_bit_equal(last_x, xf->position.x));
				TEST_ASSERT_TRUE(app_test_f32_bit_equal(last_y, xf->position.y));
			} else {
				/* Contact-solved frame: stays within the settled band. */
				TEST_ASSERT_FLOAT_WITHIN(5.0e-4f, last_y, xf->position.y);
			}
			last_x = xf->position.x;
			last_y = xf->position.y;
		}
	}

	sk_app_destroy(ctx);
}

/* Disabling the plugin (unregistering its API table) leaves entity
 * transforms untouched: with the jolt API absent from the registry the engine
 * loop stops driving physics, no body is created, and the spawn pose stays
 * byte-identical across frames. Re-registering resumes the engine loop. */
SK_TEST(app_engine_loop_disabled_physics_leaves_transforms_untouched) {
	sk_app_context_t* ctx = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(ctx);
	const sk_app_api_t* api = sk_app_api();
	const sk_jolt_api_t* jolt = (const sk_jolt_api_t*)api->get_api(ctx, SK_JOLT_API_TYPE_ID);
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(jolt);
	TEST_ASSERT_NOT_NULL(ecs);
	sk_world_t* world = api->scene_world(ctx);
	TEST_ASSERT_NOT_NULL(world);

	const sk_vec3_t box_half = sk_vec3(0.5f, 0.5f, 0.5f);
	const sk_vec3_t box_pos = sk_vec3(0.0f, 5.0f, 0.0f);
	sk_entity_t box = app_test_spawn_box(world, ecs, SK_JOLT_MOTION_TYPE_DYNAMIC, &box_pos, &box_half, 1.0f);
	TEST_ASSERT_TRUE(sk_entity_is_valid(box));
	const sk_transform_t spawn_pose = *(const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID);

	/* Disable: drop the jolt API from the registry. */
	api->set_api(ctx, SK_JOLT_API_TYPE_ID, NULL);
	{
		i32 i;
		for (i = 0; i < 60; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, 1.0 / 30.0) != 0);
		}
	}
	/* Transform untouched (byte-identical), no body was ever created. */
	TEST_ASSERT_TRUE(app_test_pose_bit_equal(&spawn_pose, (const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID)));
	TEST_ASSERT_NULL(((sk_rigid_body_config_t*)ecs->world_component(world, box, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID))->body);

	/* Re-enable: the engine loop picks the plugin back up and the box falls. */
	api->set_api(ctx, SK_JOLT_API_TYPE_ID, (const_ptr_t)jolt);
	{
		i32 i;
		for (i = 0; i < 60; ++i) {
			TEST_ASSERT_TRUE(app_test_tick_delta(ctx, 1.0 / 60.0) != 0);
		}
	}
	TEST_ASSERT_TRUE(((const sk_transform_t*)ecs->world_component(world, box, SK_TRANSFORM_COMPONENT_TYPE_ID))->position.y < 5.0f);
	TEST_ASSERT_NOT_NULL(((sk_rigid_body_config_t*)ecs->world_component(world, box, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID))->body);

	sk_app_destroy(ctx);
}

#endif /* SK_TESTS */
