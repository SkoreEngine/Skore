#pragma once

/**
 * @file profiler_core.internal.h
 * @brief Engine-independent profiler core: high-resolution monotonic clock,
 *        per-thread zone storage (thread-local buffers, lock-free hot path),
 *        bounded ring buffers with a counted overflow policy, nested zone
 *        depth tracking, and per-zone accumulation (call count, total time,
 *        rolling min/max/average).
 *
 * This module is the engine-independent layer behind the frozen public
 * contract (sk_profiler_api_t in profiler.h). It has no dependency on the
 * app context, the plugin registry, or the render device: it only uses
 * sk-foundation utilities (common.h types, the engine allocator, the process
 * logger). The plugin layer (profiler.c) owns the registry wiring, GPU
 * timestamp queries and the report formats; this module owns timing, zone
 * capture and accumulation.
 *
 * Thread model:
 *  - Zone capture (begin_zone / end_zone) is per-thread: every thread keeps a
 *    thread-local bounded buffer, so the hot path takes no locks and never
 *    allocates. Worker threads self-register on first use through a lock-free
 *    atomic index assignment; all buffers are pre-allocated at init.
 *  - Frame delimiters (begin_frame / end_frame) run on the recording thread
 *    (usually the main thread, which is pinned to thread index 0 at init).
 *    end_frame drains every registered thread buffer and accumulates the
 *    frame's zones into the per-zone rolling stats.
 *  - A zone that is still open at end_frame belongs to the frame in which it
 *    ends: it stays in its thread buffer until it closes.
 *  - Because the per-thread storage is process-global TLS, only one core may
 *    be active at a time (matching the frozen header's one-table-per-process
 *    model). Do not call into a core after shutdown.
 *
 * Overflow policy (bounded, non-crashing; every drop is counted and the
 * first occurrence per thread/core is logged):
 *  - A thread buffer holds at most config.zones_per_thread zones per frame;
 *    further begins in that frame are dropped (the matching end is a no-op;
 *    nesting depth stays consistent).
 *  - The frame zone ring keeps the most recent config.frame_zones closed
 *    zones; older records are overwritten and counted.
 *  - The task table holds at most config.max_tasks zone names; zones whose
 *    name cannot be added are skipped and counted.
 *  - end_zone without a matching begin_zone is a no-op and is counted.
 *
 * Times are seconds on the configured monotonic clock. Zone durations are
 * end - start; the rolling stats accumulate the per-frame total of every
 * same-name zone (merged like the frozen header's task entries).
 */

#include "allocator.h"
#include "common.h"
#include "logger.h"

#include <stdbool.h> /* bool / true / false (not pulled in by common.h) */

#ifdef __cplusplus
extern "C" {
#endif

/** Fixed name/category storage caps; match the frozen sk_profiler_api_t
 *  contract (SK_PROFILER_NAME_CAP 64 / SK_PROFILER_CATEGORY_CAP 32) so the
 *  core can back it 1:1. Names are truncated to 63+NUL / 31+NUL. */
enum {
	SK_PROFILER_CORE_NAME_CAP = 64u,
	SK_PROFILER_CORE_CATEGORY_CAP = 32u,
};

/** Monotonic clock callback: seconds since an arbitrary epoch (never wall
 *  time), non-decreasing under normal conditions; 0.0 signals a failed query. */
typedef f64 (*sk_profiler_clock_fn)(void);

/**
 * Clock abstraction for the platforms the v2 engine targets: Windows
 * (QueryPerformanceCounter) and Linux/macOS (clock_gettime CLOCK_MONOTONIC),
 * with a coarse fallback for anything else. Configs may override @p now to
 * inject a deterministic clock (tests, host-supplied timing).
 */
typedef struct sk_profiler_clock_t {
	sk_profiler_clock_fn now; /* NULL selects the platform default clock */
} sk_profiler_clock_t;

/**
 * Fill @p out with the platform default high-resolution monotonic clock.
 * @param out Destination clock; must not be NULL.
 */
void sk_profiler_clock_default(sk_profiler_clock_t* out);

/**
 * Core configuration. Any field left 0 selects the documented default, so
 * callers may zero the struct and override only what they need.
 */
typedef struct sk_profiler_core_config_t {
	/** Per-thread buffers to pre-allocate (default 16; thread index 0 is the
	 *  recording thread, workers register in order). */
	u32 max_threads;
	/** Ring capacity per thread: max zones one thread may record per frame
	 *  before begins are dropped (default 256, matching the frozen header's
	 *  SK_PROFILER_MAX_SAMPLES). */
	u32 zones_per_thread;
	/** Ring capacity of the last-built-frame zone list (default
	 *  max_threads * zones_per_thread; overflow keeps the newest zones). */
	u32 frame_zones;
	/** Accumulation table cap: distinct zone names tracked (default 256,
	 *  matching the frozen header's task cap). */
	u32 max_tasks;
	/** Allocator for all buffers (allocated at init only); NULL selects
	 *  sk_allocator_default(). */
	const sk_allocator_t* allocator;
	/** Clock used for all stamps; now == NULL selects the platform default. */
	sk_profiler_clock_t clock;
	/** Optional host logger table (from app_api->logger_api). NULL skips logs. */
	const sk_logger_api_t* logger_api;
	/** Optional host logger context. Required with logger_api to create the core logger. */
	sk_logger_context_t* logger_ctx;
} sk_profiler_core_config_t;

/** Fill @p out with the documented defaults (0 fields are still treated as
 *  defaults by init). @param out Destination config; must not be NULL. */
void sk_profiler_core_config_default(sk_profiler_core_config_t* out);

/** Opaque core instance (one active per process; see file comment). */
typedef struct sk_profiler_core_t sk_profiler_core_t;

/**
 * Allocate and initialize a core: pre-allocates every buffer (per-thread
 * rings and open stacks, frame ring, task table) through the configured
 * allocator and pins the calling thread as the recording thread (index 0).
 * Recording starts inactive; call set_active(true) to capture zones.
 * @param config Optional config (NULL or zeroed fields select defaults).
 * @return New core on success, NULL on allocation failure (nothing leaks).
 */
sk_profiler_core_t* sk_profiler_core_create(const sk_profiler_core_config_t* config);

/** Shutdown and free a core from sk_profiler_core_create. NULL is a no-op.
 *  After destroy the core must not be used again (see file comment). */
void sk_profiler_core_destroy(sk_profiler_core_t* core);

/**
 * One closed zone of the last built frame (also the per-thread ring record).
 * @p start / @p end are seconds on the core clock; duration = end - start.
 * Open records carry open == true and are never exposed in the frame list.
 */
typedef struct sk_profiler_core_zone_t {
	char name[SK_PROFILER_CORE_NAME_CAP];
	char category[SK_PROFILER_CORE_CATEGORY_CAP];
	f64 start;
	f64 end;
	bool open;		  /* false once the zone is closed (drain emits only closed zones) */
	u8 _pad0[3];	  /* explicit padding: keep the struct hole-free */
	u32 color;		  /* explicit color passed at begin (0 = none given) */
	u32 thread_index; /* 0 = recording thread; workers in registration order */
	i32 depth;		  /* nest depth at begin (0 = root) */
} sk_profiler_core_zone_t;

/**
 * Per-zone accumulation (CPU side of the frozen header's
 * sk_profiler_task_entry_t; GPU query handling stays in the plugin layer).
 * Same-name zones merge: @p total and @p calls cover the last built frame,
 * @p min/max/avg/@p frames roll across frames.
 */
typedef struct sk_profiler_core_task_t {
	char name[SK_PROFILER_CORE_NAME_CAP];
	char category[SK_PROFILER_CORE_CATEGORY_CAP];
	f64 total;	  /* sum of this task's zone durations in the last built frame */
	f64 min;	  /* rolling min of per-frame totals */
	f64 max;	  /* rolling max of per-frame totals */
	f64 avg;	  /* rolling average of per-frame totals */
	u32 calls;	  /* call count in the last built frame */
	u32 frames;	  /* frames in which the task was present (rolling count) */
	u32 color;	  /* color of the first zone that created the entry */
	i32 depth;	  /* depth of the first zone that created the entry */
	bool present; /* seen in the last built frame */
	u8 _pad[7];	  /* explicit padding: keep the struct hole-free (rounds to 152 = 8-aligned) */
} sk_profiler_core_task_t;

/** Rolling frame statistics (wall time of built frames). */
typedef struct sk_profiler_core_frame_stats_t {
	f64 current; /* wall time of the last built frame */
	f64 min;
	f64 max;
	f64 avg;
	u32 count; /* built frames accumulated */
} sk_profiler_core_frame_stats_t;

/**
 * Initialize a caller-provided core (used by sk_profiler_core_create).
 * @param core   Destination; must not be NULL.
 * @param config Optional config (NULL or zeroed fields select defaults).
 * @return 0 on success, non-zero on allocation failure (nothing leaks).
 */
i32 sk_profiler_core_init(sk_profiler_core_t* core, const sk_profiler_core_config_t* config);

/** Release every buffer of @p core. NULL / double shutdown are no-ops. */
void sk_profiler_core_shutdown(sk_profiler_core_t* core);

/** Master recording switch. false clears the task table, frame list and frame
 *  stats and stops all sampling until re-enabled (zones recorded while
 *  inactive are ignored). Zones left open on other threads at the moment of
 *  deactivation stay pinned in their thread buffer until they end — they are
 *  only observable if recording resumes and a frame is built afterwards. */
void sk_profiler_core_set_active(sk_profiler_core_t* core, bool active);

/** Current recording state (false until set_active(true)). */
bool sk_profiler_core_is_active(const sk_profiler_core_t* core);

/**
 * Open a nested zone on the calling thread. Hot path: thread-local buffer,
 * no locking, no allocation. @p name is copied (63+NUL) so it may be
 * transient; @p category NULL or "" means uncategorized; @p color is stored
 * as given (0 = none; the plugin layer resolves auto palette colors).
 * When the thread buffer is full the zone is dropped (counted; its matching
 * end_zone is a no-op). Safe from any thread.
 */
void sk_profiler_core_begin_zone(sk_profiler_core_t* core, const_chr_t name, const_chr_t category, u32 color);

/**
 * Close the most recently opened zone on the calling thread. A no-op when no
 * zone is open on this thread (counted when the thread is registered; drops
 * from a full buffer are consumed silently). Safe from any thread.
 */
void sk_profiler_core_end_zone(sk_profiler_core_t* core);

/** Open the next frame on the recording thread (stamps the frame start).
 *  No-op while inactive. */
void sk_profiler_core_begin_frame(sk_profiler_core_t* core);

/**
 * Close the frame on the recording thread: drains every registered thread
 * buffer (closed zones only), accumulates the frame into the task table and
 * rolling stats, and records the frame wall time. Zones left open stay in
 * their thread buffer and land in the frame where they end. No-op while
 * inactive.
 */
void sk_profiler_core_end_frame(sk_profiler_core_t* core);

/**
 * Zones of the last built frame in chronological order (recording thread's
 * zones first, then workers in registration order). Valid until the next
 * end_frame / reset / deactivation. The list keeps at most
 * config.frame_zones zones; overflow keeps the newest (counted).
 * @param count Out-param for the zone count; may be NULL.
 * @return Ring base pointer (never NULL once initialized).
 */
const sk_profiler_core_zone_t* sk_profiler_core_frame_zones(const sk_profiler_core_t* core, u32* count);

/**
 * Accumulated per-zone tasks. Valid until the next end_frame / reset /
 * deactivation; entries never seen in a frame keep present == false and
 * total == 0.
 * @param count Out-param for the task count; may be NULL.
 * @return Task table base pointer (never NULL once initialized).
 */
const sk_profiler_core_task_t* sk_profiler_core_tasks(const sk_profiler_core_t* core, u32* count);

/** Rolling frame wall-time statistics (last built frame as current). */
sk_profiler_core_frame_stats_t sk_profiler_core_frame_stats(const sk_profiler_core_t* core);

/** Wall time (seconds) of the last built frame. */
f64 sk_profiler_core_frame_wall_time(const sk_profiler_core_t* core);

/** Zones dropped because a thread buffer was full (sum across threads;
 *  best-effort snapshot while threads are still running). */
u32 sk_profiler_core_dropped_zones(const sk_profiler_core_t* core);

/** Zones skipped because the task table was full. */
u32 sk_profiler_core_dropped_tasks(const sk_profiler_core_t* core);

/** Oldest frame-list records overwritten by newer zones of the same frame. */
u32 sk_profiler_core_frame_overwrites(const sk_profiler_core_t* core);

/** end_zone calls without a matching begin_zone (registered threads only). */
u32 sk_profiler_core_mismatched_ends(const sk_profiler_core_t* core);

/** Threads that could not register (more distinct threads than max_threads). */
u32 sk_profiler_core_thread_overflows(const sk_profiler_core_t* core);

/** Clear per-task and per-frame rolling statistics (keeps task names and the
 *  last built frame's per-frame data, like the frozen header's reset_stats). */
void sk_profiler_core_reset_stats(sk_profiler_core_t* core);

#ifdef __cplusplus
}
#endif
