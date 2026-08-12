#pragma once

/**
 * @file profiler.h
 * @brief Hierarchical CPU/GPU profiler plugin (v2 C port of main's Skore::Profiler).
 *
 * The plugin registers a static sk_profiler_api_t on the app context from
 * sk_plugin_entry_point (via sk_profiler_init). Hosts obtain the table only
 * through the app registry, e.g.:
 *
 *   const sk_profiler_api_t* prof =
 *       (const sk_profiler_api_t*)app_api->get_api(ctx, SK_PROFILER_API_TYPE_ID);
 *
 * Lifecycle (host wiring lives in sk-foundation):
 *   - prof->init(dev) after plugins are loaded; pass a render device handle to
 *     attach GPU timestamp query pools, or sk_render_device_t_zero() for a
 *     CPU-only session. Re-call later with a real device to attach GPU pools
 *     (idempotent; previous pools are destroyed).
 *   - prof->begin_frame() / prof->end_frame() bracket each frame from the
 *     engine main loop so the triple buffers rotate exactly once per frame and
 *     per-frame data is delimited correctly.
 *   - prof->shutdown() before the plugins are unloaded.
 *
 * Thread model: main-thread only. All table entries and instrumentation
 * macros must run on the same thread (matching the v2 default ownership);
 * there is no TLS and no locking.
 *
 * Recording is OFF by default: prof->set_active(false) (the initial state)
 * makes every sample/frame call an early-out. UI or tools turn recording on
 * with set_active(true). Sample names are copied into fixed 63+NUL buffers;
 * at most SK_PROFILER_MAX_SAMPLES zones per frame are recorded (further
 * begins are silently dropped).
 *
 * Zones carry an optional category label and color: pass NULL (or "") for
 * uncategorized and 0 for the automatic palette color derived from the name
 * hash (FNV-1a). The category/color of the first zone that creates an
 * aggregated task entry win; later same-name zones merge into that entry.
 *
 * dump_report(path) writes a human-readable text snapshot of the last built
 * frame (CPU/GPU task trees with nesting, per-frame totals, rolling
 * min/max/avg, call counts and percentage-of-frame, plus a cumulative
 * summary across all recorded frames; times in milliseconds) to a file.
 * dump_report_json(path) emits the same per-frame + cumulative aggregation
 * as a versioned JSON document for later tooling, and log_report() prints
 * the human-readable form through the engine logger (console/log form).
 * All three are safe to call at any time — an inactive or empty profiler
 * yields a valid header-only report.
 *
 * --- Instrumentation macros -------------------------------------------------
 *
 * The macros are the user-facing instrumentation surface. They call through
 * the sk_profiler_api_t table, so every call site passes the resolved table
 * pointer (obtained once from the app registry) plus the zone name:
 *
 *   SK_PROFILE_CPU_ZONE(prof, "Update");
 *   SK_PROFILE_GPU_ZONE(prof, "TLAS Build", cmd);
 *
 * Scoped-zone macros work from plain C. The canonical usage is the statement
 * form above: on GCC/Clang the __attribute__((cleanup)) helper closes the
 * sample when the enclosing block exits (including early returns), which is
 * also how engine call sites (main loop, ECS, render graph) use them.
 * Elsewhere (MSVC) a one-shot for-loop guard provides the same one-liner,
 * but there the zone covers only the next statement — write the zone as
 * `SK_PROFILE_CPU_ZONE(prof, "Update") { ...body... }` or use the explicit
 * begin/end pair (break/return inside the body skip the end). The plain
 * zone macros are the name-only convenience (category NULL, color 0 = auto);
 * the _EX variants pass an explicit category label and ABGR color (0 = auto).
 * The documented fallback for any environment is the explicit pair:
 *
 *   SK_PROFILE_BEGIN_CPU_SAMPLE(prof, "Update", NULL, 0u);
 *   ...work...
 *   SK_PROFILE_END_CPU_SAMPLE(prof);
 *
 * Compile-time switch: define SK_PROFILER_ENABLED (CMake option
 * SK_ENABLE_PROFILER, default OFF) to compile the real macro bodies. Without
 * it every SK_PROFILE_* macro reduces to a no-op that still evaluates its
 * arguments (so call-site variables stay "used"), and no translation unit
 * references the profiler plugin at all. Enabled builds require sk-profiler
 * to be present in the plugins folder at runtime.
 *
 * Limitations shared with main: one zone per source line (the generated
 * variable names use __LINE__), names longer than 63 chars and categories
 * longer than 31 chars are truncated, and at most 256 zones per frame are
 * recorded.
 */

#include "app.h"
#include "common.h"
#include "render_device.h" /* sk_command_buffer_t (GPU zones) / sk_render_device_t (init) */

#include <stddef.h> /* NULL in the inline zone helpers */

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_profiler_api_t in the app registry. */
#define SK_PROFILER_API_TYPE_ID SK_TYPE_ID("sk.profiler_api", 0x05b3ca0d0e4a2db8ULL, 0xf8b666f8a06446b3ULL)

/** Fixed caps: samples per frame buffer, frame buffers, name storage. */
enum {
	SK_PROFILER_MAX_SAMPLES = 256u,
	SK_PROFILER_BUFFER_COUNT = 3u,
	SK_PROFILER_NAME_CAP = 64u,
	SK_PROFILER_CATEGORY_CAP = 32u,
};

/**
 * One aggregated task: same-name samples in a read frame are summed into one
 * entry, and per-entry rolling min/max/avg accumulate across frames. Times
 * are seconds. @p depth is the nest depth of the sample that opened the task.
 */
typedef struct sk_profiler_task_entry_t {
	f64 cpu_time;
	f64 gpu_time;
	f64 cpu_min;
	f64 cpu_max;
	f64 cpu_avg;
	f64 gpu_min;
	f64 gpu_max;
	f64 gpu_avg;
	u32 cpu_count;
	u32 gpu_count;
	u32 color; /* explicit ABGR-ish color, or palette index from the name hash when 0 was passed */
	/* Per-frame call counts (same-name samples merged into this entry in the
	 * last built frame). Reset each frame build; cumulative presence is
	 * cpu_count / gpu_count. */
	u32 cpu_calls;
	u32 gpu_calls;
	char name[SK_PROFILER_NAME_CAP];
	char category[SK_PROFILER_CATEGORY_CAP]; /* zone category label; "" when uncategorized */
	i32 depth;
	bool has_gpu;
	bool present; /* seen in the last built frame */
} sk_profiler_task_entry_t;

/** Rolling frame-time statistics (current/min/max/avg over @p count frames). */
typedef struct sk_profiler_frame_stats_t {
	f64 current;
	f64 min;
	f64 max;
	f64 avg;
	u32 count;
} sk_profiler_frame_stats_t;

/**
 * Global profiler module API (one table per process after plugin load).
 * All entries are main-thread only. Entries are guaranteed non-NULL.
 */
typedef struct sk_profiler_api_t {
	/**
	 * Initialize the profiler. Idempotent: any previous GPU query pools are
	 * destroyed first, so hosts may re-call init once a render device exists.
	 * @param dev Render device for GPU timestamp pools, or
	 *            sk_render_device_t_zero() for a CPU-only session.
	 * @return 0 on success; non-zero if GPU setup failed (CPU sampling still
	 *         works afterwards).
	 */
	i32 (*init)(sk_render_device_t dev);

	/** Destroy GPU query pools. Safe when init was never called / CPU-only. */
	void (*shutdown)(void);

	/**
	 * Advance the triple buffers: builds the task list from the frame two
	 * frames ago (GPU timestamps are then guaranteed complete) and clears the
	 * write buffer for the new frame. No-op while inactive.
	 */
	void (*begin_frame)(void);

	/** Frame delimiter counterpart (reserved; no-op today). */
	void (*end_frame)(void);

	/**
	 * Open a nested CPU sample.
	 * @param name Zone name, copied into a fixed 63+NUL buffer.
	 * @param category Optional category label, copied into a 31+NUL buffer;
	 *                 NULL or "" means uncategorized.
	 * @param color Optional explicit ABGR-ish task color; 0 selects the
	 *              automatic palette color derived from the name hash.
	 */
	void (*begin_cpu_sample)(const_chr_t name, const_chr_t category, u32 color);

	/** Close the most recently opened CPU sample (@p name is not needed). */
	void (*end_cpu_sample)(void);

	/**
	 * Open a nested GPU sample: CPU wall stamps + timestamp query pair on
	 * @p cmd when GPU pools are attached (init with a device), otherwise a
	 * CPU-only sample flagged has_gpu=false.
	 * @param name Zone name (copied, 63+NUL).
	 * @param category Optional category label (NULL/"" = uncategorized).
	 * @param color Optional explicit ABGR-ish task color; 0 = auto palette.
	 * @param cmd Command buffer the timestamp query pair is recorded on.
	 */
	void (*begin_gpu_sample)(const_chr_t name, const_chr_t category, u32 color, sk_command_buffer_t cmd);

	/** Close the most recently opened GPU sample on @p cmd. */
	void (*end_gpu_sample)(sk_command_buffer_t cmd);

	/**
	 * Task array for the last built CPU frame. @p out or @p count may be NULL
	 * to query only the other; the array stays valid until the next
	 * begin_frame / reset_stats / set_active(false).
	 */
	void (*get_cpu_tasks)(const sk_profiler_task_entry_t** out, u32* count);

	/** Same as get_cpu_tasks for the GPU task list. */
	void (*get_gpu_tasks)(const sk_profiler_task_entry_t** out, u32* count);

	/** Rolling CPU frame stats (wall-clock delta between begin_frames). */
	sk_profiler_frame_stats_t (*get_cpu_frame_stats)(void);

	/** Rolling GPU frame stats (sum of depth-0 GPU task times per read frame). */
	sk_profiler_frame_stats_t (*get_gpu_frame_stats)(void);

	/**
	 * Write a human-readable text report to @p path: the last built frame's
	 * CPU/GPU task trees (nesting by depth, per-frame total, rolling
	 * min/max/avg, per-frame call count, percentage of the frame) plus a
	 * cumulative summary across all recorded frames (total, average,
	 * share of the average frame). Times in milliseconds. Safe to call at
	 * any time: an inactive or empty profiler produces a valid header-only
	 * report.
	 * @param path Output file path (must not be NULL).
	 * @return 0 on success; non-zero if @p path is NULL or the file could
	 *         not be opened.
	 */
	i32 (*dump_report)(const_chr_t path);

	/**
	 * Machine-readable counterpart of dump_report: the same per-frame and
	 * cumulative aggregation as a versioned JSON document (frame stats and
	 * task arrays carrying name/category/depth/color, call counts, times,
	 * and percentages) suitable for later tooling. Same safety contract as
	 * dump_report (valid header-only document when inactive/empty).
	 * @param path Output file path (must not be NULL).
	 * @return 0 on success; non-zero if @p path is NULL or the file could
	 *         not be opened.
	 */
	i32 (*dump_report_json)(const_chr_t path);

	/**
	 * Emit the human-readable report (same content as dump_report) through
	 * the engine logger — the console/log form of the reporting surface.
	 * Uses the process logger (sk_logger_api) with a "profiler" logger; a
	 * no-op when the logger is unavailable.
	 * @return 0 on success; non-zero if the logger could not be used.
	 */
	i32 (*log_report)(void);

	/** Clear per-task and per-frame rolling statistics (keeps task names). */
	void (*reset_stats)(void);

	/** Master recording switch. false clears tasks + frame stats and stops
	 *  all sampling until re-enabled. */
	void (*set_active)(bool active);

	/** Current recording state. */
	bool (*is_active)(void);
} sk_profiler_api_t;

/**
 * Register the static sk_profiler_api_t on the app context.
 * Called from sk_plugin_entry_point; also caches the context/API table used
 * for later registry lookups (platform clock, render device for GPU pools).
 * Requires the platform API to be registered already (hosts register it
 * before plugin load; see sk_app_startup).
 * @param context App context (must not be NULL).
 * @param app_api App module table (must not be NULL).
 */
void sk_profiler_init(sk_app_context_t* context, const sk_app_api_t* app_api);

/* ------------------------------------------------------------------ */
/* Instrumentation macros (compile-time switch: SK_PROFILER_ENABLED)   */
/* ------------------------------------------------------------------ */

/* Two-level paste so __LINE__ expands before concatenation (a direct
 * x##__LINE__ in a macro body pastes the literal token). Defined outside the
 * enabled/disabled branches: both compile-time modes name their loop/zone
 * variables this way. */
#define SK_PROFILER_CONCAT_INNER(a, b) a##b
#define SK_PROFILER_CONCAT(a, b) SK_PROFILER_CONCAT_INNER(a, b)

#if defined(SK_PROFILER_ENABLED)

/* Zone helper types/functions (macro backing; static per TU, zero cost when
 * unused). The cleanup attribute takes the end function's address, so these
 * must not be SK_FINLINE (always_inline). */

/**
 * CPU scoped zone handle (opened at declaration, closed at block exit).
 * @p open doubles as the portable one-shot for-loop guard: it stays 1 while
 * the zone is live so the loop body runs even when the profiler table is
 * absent (api == NULL, zone is a no-op).
 */
typedef struct sk_profiler_cpu_zone_t {
	const sk_profiler_api_t* api;
	i32 open;
} sk_profiler_cpu_zone_t;

/** GPU scoped zone handle (keeps the command buffer for the end timestamp). */
typedef struct sk_profiler_gpu_zone_t {
	const sk_profiler_api_t* api;
	sk_command_buffer_t cmd;
	i32 open;
} sk_profiler_gpu_zone_t;

/* Zones tolerate a NULL table (profiler plugin not loaded): begin no-ops and
 * returns a closed-loop-safe handle so engine call sites can instrument
 * unconditionally. */
static inline sk_profiler_cpu_zone_t sk_profiler_cpu_zone_begin(const sk_profiler_api_t* api, const_chr_t name, const_chr_t category, u32 color) {
	sk_profiler_cpu_zone_t zone;
	zone.api = api;
	zone.open = 1;
	if (api != NULL) {
		api->begin_cpu_sample(name, category, color);
	}
	return zone;
}

static inline void sk_profiler_cpu_zone_end(sk_profiler_cpu_zone_t* zone) {
	if (zone->api != NULL) {
		zone->api->end_cpu_sample();
	}
	zone->open = 0;
}

static inline sk_profiler_gpu_zone_t sk_profiler_gpu_zone_begin(const sk_profiler_api_t* api, const_chr_t name, const_chr_t category, u32 color, sk_command_buffer_t cmd) {
	sk_profiler_gpu_zone_t zone;
	zone.api = api;
	zone.cmd = cmd;
	zone.open = 1;
	if (api != NULL) {
		api->begin_gpu_sample(name, category, color, cmd);
	}
	return zone;
}

static inline void sk_profiler_gpu_zone_end(sk_profiler_gpu_zone_t* zone) {
	if (zone->api != NULL) {
		zone->api->end_gpu_sample(zone->cmd);
	}
	zone->open = 0;
}

/* Two-level paste so __LINE__ expands before concatenation (a direct
 * x##__LINE__ in a macro body pastes the literal token). */
#define SK_PROFILER_CONCAT_INNER(a, b) a##b
#define SK_PROFILER_CONCAT(a, b) SK_PROFILER_CONCAT_INNER(a, b)

#if defined(__GNUC__) || defined(__clang__)

/**
 * Scoped CPU zone (GCC/Clang): opens the sample at the declaration and closes
 * it when the enclosing block exits — including early returns. Use as a
 * statement; one zone per source line (__LINE__-named variable). Category is
 * NULL and color is 0 (auto) — see SK_PROFILE_CPU_ZONE_EX for explicit values.
 */
#define SK_PROFILE_CPU_ZONE(api, name)                                        \
	sk_profiler_cpu_zone_t SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__) \
		__attribute__((cleanup(sk_profiler_cpu_zone_end))) = sk_profiler_cpu_zone_begin((api), (name), NULL, 0u)

/** Scoped CPU zone with an explicit category label and ABGR color (0 = auto). */
#define SK_PROFILE_CPU_ZONE_EX(api, name, category, color)                    \
	sk_profiler_cpu_zone_t SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__) \
		__attribute__((cleanup(sk_profiler_cpu_zone_end))) = sk_profiler_cpu_zone_begin((api), (name), (category), (color))

/**
 * Scoped GPU zone (GCC/Clang): same scoping as SK_PROFILE_CPU_ZONE with a
 * timestamp query pair recorded on @p cmd. Category NULL, color 0 (auto) —
 * see SK_PROFILE_GPU_ZONE_EX for explicit values.
 */
#define SK_PROFILE_GPU_ZONE(api, name, cmd)                                   \
	sk_profiler_gpu_zone_t SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__) \
		__attribute__((cleanup(sk_profiler_gpu_zone_end))) = sk_profiler_gpu_zone_begin((api), (name), NULL, 0u, (cmd))

/** Scoped GPU zone with an explicit category label and ABGR color (0 = auto). */
#define SK_PROFILE_GPU_ZONE_EX(api, name, category, color, cmd)               \
	sk_profiler_gpu_zone_t SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__) \
		__attribute__((cleanup(sk_profiler_gpu_zone_end))) = sk_profiler_gpu_zone_begin((api), (name), (category), (color), (cmd))

#else /* !__GNUC__ && !__clang__ (e.g. MSVC): portable one-shot for-loop guard */

/**
 * Scoped CPU zone (portable fallback): one-shot for-loop that opens the
 * sample, runs the body once, and closes the sample afterwards. `continue`
 * inside the body is safe; `break`/`return` skip the end — use the explicit
 * begin/end form there. Use as a statement; one zone per source line.
 * Category NULL, color 0 (auto) — see SK_PROFILE_CPU_ZONE_EX for explicit
 * values.
 */
#define SK_PROFILE_CPU_ZONE(api, name)                                                                                                    \
	for (sk_profiler_cpu_zone_t SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__) = sk_profiler_cpu_zone_begin((api), (name), NULL, 0u); \
		 SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__).open != 0; (sk_profiler_cpu_zone_end(&SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__))))

/** Scoped CPU zone with an explicit category label and ABGR color (0 = auto). */
#define SK_PROFILE_CPU_ZONE_EX(api, name, category, color)                                                                                           \
	for (sk_profiler_cpu_zone_t SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__) = sk_profiler_cpu_zone_begin((api), (name), (category), (color)); \
		 SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__).open != 0; (sk_profiler_cpu_zone_end(&SK_PROFILER_CONCAT(sk_profile_cpu_zone_, __LINE__))))

/** Scoped GPU zone (portable fallback; same semantics as the CPU form). */
#define SK_PROFILE_GPU_ZONE(api, name, cmd)                                                                                                      \
	for (sk_profiler_gpu_zone_t SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__) = sk_profiler_gpu_zone_begin((api), (name), NULL, 0u, (cmd)); \
		 SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__).open != 0; (sk_profiler_gpu_zone_end(&SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__))))

/** Scoped GPU zone with an explicit category label and ABGR color (0 = auto). */
#define SK_PROFILE_GPU_ZONE_EX(api, name, category, color, cmd)                                                                                             \
	for (sk_profiler_gpu_zone_t SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__) = sk_profiler_gpu_zone_begin((api), (name), (category), (color), (cmd)); \
		 SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__).open != 0; (sk_profiler_gpu_zone_end(&SK_PROFILER_CONCAT(sk_profile_gpu_zone_, __LINE__))))

#endif /* __GNUC__ || __clang__ */

/**
 * Explicit begin/end instrumentation (documented fallback + programmatic
 * zones). All forms tolerate a NULL table (profiler plugin not loaded):
 * arguments are still evaluated so call-site variables stay used, and the
 * table call is skipped.
 */
/* Argument-discard idiom: each operand is cast to void separately (the
 * canonical "evaluate and discard" form). A single comma expression would
 * trip -Wunused-value on newer GCC/Clang when call sites pass literals. */
#define SK_PROFILE_BEGIN_CPU_SAMPLE(api, name, category, color) \
	((void)(name), (void)(category), (void)(color), (void)((api) != NULL ? ((api)->begin_cpu_sample((name), (category), (color)), 0) : 0))
#define SK_PROFILE_END_CPU_SAMPLE(api) ((void)((api) != NULL ? ((api)->end_cpu_sample(), 0) : 0))
#define SK_PROFILE_BEGIN_GPU_SAMPLE(api, name, category, color, cmd) \
	((void)(name), (void)(category), (void)(color), (void)(cmd), (void)((api) != NULL ? ((api)->begin_gpu_sample((name), (category), (color), (cmd)), 0) : 0))
#define SK_PROFILE_END_GPU_SAMPLE(api, cmd) ((void)(cmd), (void)((api) != NULL ? ((api)->end_gpu_sample((cmd)), 0) : 0))
#define SK_PROFILE_BEGIN_FRAME(api) ((void)((api) != NULL ? ((api)->begin_frame(), 0) : 0))
#define SK_PROFILE_END_FRAME(api) ((void)((api) != NULL ? ((api)->end_frame(), 0) : 0))

#else /* !SK_PROFILER_ENABLED: every macro is a no-op (arguments still
       * evaluated so call-site variables stay used). The canonical usage is
       * the statement form (`SK_PROFILE_CPU_ZONE(api, "x");`) — on
       * GCC/Clang the cleanup attribute scopes the zone to the enclosing
       * block; here it reduces to an evaluated expression statement. */

#define SK_PROFILE_CPU_ZONE(api, name) ((void)(api), (void)(name))
#define SK_PROFILE_CPU_ZONE_EX(api, name, category, color) ((void)(api), (void)(name), (void)(category), (void)(color))
#define SK_PROFILE_GPU_ZONE(api, name, cmd) ((void)(api), (void)(name), (void)(cmd))
#define SK_PROFILE_GPU_ZONE_EX(api, name, category, color, cmd) ((void)(api), (void)(name), (void)(category), (void)(color), (void)(cmd))

#define SK_PROFILE_BEGIN_CPU_SAMPLE(api, name, category, color) ((void)(api), (void)(name), (void)(category), (void)(color))
#define SK_PROFILE_END_CPU_SAMPLE(api) ((void)(api))
#define SK_PROFILE_BEGIN_GPU_SAMPLE(api, name, category, color, cmd) ((void)(api), (void)(name), (void)(category), (void)(color), (void)(cmd))
#define SK_PROFILE_END_GPU_SAMPLE(api, cmd) ((void)(api), (void)(cmd))
#define SK_PROFILE_BEGIN_FRAME(api) ((void)(api))
#define SK_PROFILE_END_FRAME(api) ((void)(api))

#endif /* SK_PROFILER_ENABLED */

#ifdef __cplusplus
}
#endif
