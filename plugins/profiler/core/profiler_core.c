/**
 * @file profiler_core.c
 * @brief Engine-independent profiler core (see profiler_core.h).
 *
 * Layout:
 *  - The default clock has one platform backend per v2 target (Win32 QPC /
 *    POSIX CLOCK_MONOTONIC) plus a coarse fallback, isolated behind #ifdef
 *    blocks so the rest of the module is platform-neutral.
 *  - Every thread owns a sk_profiler_thread_ctx_t backed by pre-allocated
 *    storage (ring + open stack). The ctx structs themselves live in
 *    core-owned memory (thread_states, allocated at init); TLS only caches
 *    the pointer to this thread's ctx. That keeps the hot path lock-free
 *    and allocation-free, and it keeps the state readable for end_frame
 *    after a worker has exited (a TLS address would dangle: the owning
 *    thread's TLS block is reclaimed at thread exit). Worker registration
 *    is a single atomic fetch-add (buffers exist already), so it is
 *    lock-free too.
 *  - The per-thread ring holds the zones written since the last drain; the
 *    open zones form a contiguous LIFO segment inside it, so end_zone pops
 *    through a small open-stack. end_frame drains closed zones (chronological
 *    order) into the frame ring and compacts the surviving open zones to the
 *    front (their order is preserved, so the open stack needs no remap).
 *  - The frame ring keeps the most recent frame_zones zones of the frame
 *    (oldest overwritten and counted), then is compacted to a contiguous
 *    range for consumers. The task table merges same-name zones per frame
 *    and rolls min/max/avg across frames.
 *
 * All overflow paths are silent by default except the first occurrence per
 * thread/core, which is logged through the process logger ("profiler").
 */

#include "profiler_core.h"

#include "atomics.h"
#include "logger.h"

#include <stdarg.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <time.h>
#else
#include <time.h> /* coarse fallback */
#endif

/* ---- thread-local storage (one ctx per thread, zero-initialized) ---- */

#if defined(_MSC_VER)
#define SK_PROFILER_CORE_TLS __declspec(thread)
#else
#define SK_PROFILER_CORE_TLS __thread
#endif

/**
 * Per-thread zone storage. The ring holds every zone the thread wrote since
 * the last drain; open zones always occupy a contiguous segment at the tail
 * of the ring (begins append, ends pop LIFO), so end_zone resolves through
 * the open stack without scanning. All fields are owned by the owning thread
 * except the counters read by end_frame (atomic).
 */
typedef struct sk_profiler_thread_ctx_t {
	sk_profiler_core_t* core;		  /* owning core; NULL = not registered */
	sk_profiler_core_zone_t* records; /* ring slice: zones_per_thread records */
	u32* open_stack;				  /* ring indices of open zones, LIFO */
	u32 capacity;					  /* zones_per_thread; 0 = overflowed thread */
	u32 head;						  /* next ring write slot */
	u32 written;					  /* zones written since the last drain */
	u32 live;						  /* open zones currently in the ring */
	u32 pending_phantom;			  /* ends owed to dropped begins */
	u32 thread_index;				  /* 0 = recording thread */
	u32 dropped;					  /* atomic: begins dropped (buffer full) */
	u32 mismatched_ends;			  /* atomic: end without a matching begin */
	bool drop_warned;				  /* log the first drop once */
	u8 _pad[3];						  /* explicit padding */
} sk_profiler_thread_ctx_t;

/*
 * The ctx structs are stored in core-owned memory (core->thread_states,
 * pre-allocated at init); the TLS slot only caches this thread's pointer.
 * Publishing a TLS address for the recording thread to drain would dangle:
 * the owning thread's TLS block is reclaimed when the thread exits, so a
 * post-join drain would read stale/zeroed state (seen on macOS arm64).
 */
static SK_PROFILER_CORE_TLS sk_profiler_thread_ctx_t* thread_ctx; /* NULL = not registered */
static SK_PROFILER_CORE_TLS sk_profiler_core_t* thread_ctx_owner; /* core this thread resolved for */

/* ---- default clock (platform backends) ---- */

#if defined(_WIN32)

static f64 clock_now_default(void) {
	static LARGE_INTEGER freq = {0};
	LARGE_INTEGER now;

	/* Frequency query once; benign race on first parallel calls (same value). */
	if (freq.QuadPart == 0) {
		if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
			return 0.0;
		}
	}
	if (!QueryPerformanceCounter(&now)) {
		return 0.0;
	}
	return (f64)now.QuadPart / (f64)freq.QuadPart;
}

#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)

static f64 clock_now_default(void) {
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0.0;
	}
	return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1.0e-9;
}

#else /* unsupported platform: coarse fallback keeps every target building */

static f64 clock_now_default(void) {
	return (f64)time(NULL);
}

#endif

void sk_profiler_clock_default(sk_profiler_clock_t* out) {
	out->now = clock_now_default;
}

/* ---- config defaults ---- */

void sk_profiler_core_config_default(sk_profiler_core_config_t* out) {
	memset(out, 0, sizeof(*out));
	out->max_threads = 16u;
	out->zones_per_thread = 256u;
	out->frame_zones = 16u * 256u;
	out->max_tasks = 256u;
	out->allocator = NULL; /* sk_allocator_default() */
	out->clock.now = NULL; /* platform default clock */
}

/* ---- core state ---- */

struct sk_profiler_core_t {
	/* Config snapshot. */
	u32 max_threads;
	u32 zones_per_thread;
	u32 frame_zones_cap;
	u32 max_tasks;
	sk_profiler_clock_fn now;
	const sk_allocator_t* alloc;
	sk_logger_t* logger;

	/* Per-thread storage (pre-allocated at init; registration is lock-free).
	 * threads[] publishes each registering thread's ctx pointer (atomic); the
	 * ctx structs themselves live in thread_states (core-owned heap memory),
	 * so end_frame can drain them after a worker thread has exited. */
	void_ptr_t* threads;				   /* max_threads entries, NULL until registered */
	sk_profiler_thread_ctx_t* thread_states; /* max_threads ctx structs (heap) */
	sk_profiler_core_zone_t* thread_rings;	/* max_threads * zones_per_thread */
	u32* thread_stacks;				   /* max_threads * zones_per_thread */
	u32 next_thread_index;			   /* atomic; 0 = recording thread (init) */
	u32 thread_overflows;			   /* atomic: threads beyond max_threads */

	/* Last built frame (recording thread only). */
	sk_profiler_core_zone_t* frame_ring; /* frame_zones_cap entries (ring) */
	u32 frame_head;						 /* next frame-ring write slot */
	u32 frame_emitted;					 /* zones in the last built frame (<= cap) */
	u32 frame_overwrites;				 /* oldest records replaced by newer ones */
	f64 frame_start;					 /* clock at begin_frame */
	f64 frame_wall_time;				 /* wall time of the last built frame */

	/* Accumulation (recording thread only). */
	sk_profiler_core_task_t* tasks;
	u32 task_count;
	u32 dropped_tasks;
	bool task_overflow_warned;
	sk_profiler_core_frame_stats_t frame_stats;

	/* Lifecycle / switches. */
	bool active;
	bool initialized;
	bool thread_overflow_warned;
	u8 _pad;
};

static void core_log(const sk_profiler_core_t* core, sk_logger_type_t level, const_chr_t fmt, ...) {
	const sk_logger_api_t* logger_api = sk_logger_api();
	if (logger_api == NULL || core->logger == NULL) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	sk_log_messagev(logger_api, level, core->logger, fmt, args);
	va_end(args);
}

/* ---- name / category copy (fixed buffers, allocation-free) ---- */

static void copy_name(char dst[SK_PROFILER_CORE_NAME_CAP], const_chr_t name) {
	size_t len = strlen(name);
	if (len >= SK_PROFILER_CORE_NAME_CAP) {
		len = SK_PROFILER_CORE_NAME_CAP - 1u;
	}
	memcpy(dst, name, len);
	dst[len] = '\0';
}

static void copy_category(char dst[SK_PROFILER_CORE_CATEGORY_CAP], const_chr_t category) {
	if (category == NULL || category[0] == '\0') {
		dst[0] = '\0';
		return;
	}
	size_t len = strlen(category);
	if (len >= SK_PROFILER_CORE_CATEGORY_CAP) {
		len = SK_PROFILER_CORE_CATEGORY_CAP - 1u;
	}
	memcpy(dst, category, len);
	dst[len] = '\0';
}

/* ---- thread registration (cold path; lock-free) ---- */

/**
 * Resolve the calling thread's ctx for @p core, registering it on first use.
 * @return The thread ctx, or NULL when the thread cannot record (more
 *         distinct threads than max_threads; counted once).
 */
static sk_profiler_thread_ctx_t* acquire_thread_ctx(sk_profiler_core_t* core) {
	if (thread_ctx_owner == core) {
		return thread_ctx; /* registered, or overflow-remembered (NULL) */
	}

	u32 idx = sk_atomic_u32_fetch_add(&core->next_thread_index, 1u);
	thread_ctx_owner = core; /* remember the outcome for later calls */
	if (idx >= core->max_threads) {
		sk_atomic_u32_fetch_add(&core->thread_overflows, 1u);
		if (!core->thread_overflow_warned) {
			core->thread_overflow_warned = true;
			core_log(core, SK_LOGGER_TYPE_WARN, "thread buffer pool exhausted (%u threads); further threads cannot record zones", core->max_threads);
		}
		/* Remember the outcome so later calls on this thread stay silent. */
		thread_ctx = NULL;
		return NULL;
	}

	thread_ctx = &core->thread_states[idx];
	thread_ctx->core = core;
	thread_ctx->records = &core->thread_rings[(size_t)idx * (size_t)core->zones_per_thread];
	thread_ctx->open_stack = &core->thread_stacks[(size_t)idx * (size_t)core->zones_per_thread];
	thread_ctx->capacity = core->zones_per_thread;
	thread_ctx->head = 0u;
	thread_ctx->written = 0u;
	thread_ctx->live = 0u;
	thread_ctx->pending_phantom = 0u;
	thread_ctx->thread_index = idx;
	thread_ctx->dropped = 0u;
	thread_ctx->mismatched_ends = 0u;
	thread_ctx->drop_warned = false;
	/* Publish the fully initialized ctx; end_frame drains through this slot. */
	sk_atomic_ptr_store_ordered(&core->threads[idx], thread_ctx, SK_ATOMIC_ORDER_RELEASE);
	return thread_ctx;
}

/* ---- frame ring ---- */

/** Append one closed zone to the frame ring (keep newest; count overwrites). */
static void emit_zone(sk_profiler_core_t* core, const sk_profiler_core_zone_t* zone) {
	core->frame_ring[core->frame_head] = *zone;
	core->frame_head = (core->frame_head + 1u == core->frame_zones_cap) ? 0u : core->frame_head + 1u;
	if (core->frame_emitted < core->frame_zones_cap) {
		core->frame_emitted++;
	} else {
		core->frame_overwrites++;
	}
}

/**
 * Drain one thread's ring: emit closed zones (chronological order) into the
 * frame ring and compact the surviving open zones to the front of the thread
 * ring. Open zones keep their relative order, so the open stack is simply
 * [0 .. live) again afterwards.
 */
static void drain_thread(sk_profiler_core_t* core, sk_profiler_thread_ctx_t* ctx) {
	u32 written = ctx->written;
	u32 src = (ctx->head + ctx->capacity - written) % ctx->capacity;
	u32 dst = 0u;

	for (u32 k = 0u; k < written; k++) {
		u32 slot = (src + k) % ctx->capacity;
		sk_profiler_core_zone_t* zone = &ctx->records[slot];
		if (zone->open) {
			if (dst != slot) {
				ctx->records[dst] = *zone;
			}
			dst++;
		} else {
			emit_zone(core, zone);
		}
	}

	ctx->head = dst;
	ctx->written = dst; /* surviving open zones stay protected from reuse */
}

/** Reverse one half-open range of the frame ring in place. */
static void reverse_zone_range(sk_profiler_core_zone_t* zones, u32 begin, u32 end) {
	while (begin < end) {
		end--;
		sk_profiler_core_zone_t tmp = zones[begin];
		zones[begin] = zones[end];
		zones[end] = tmp;
		begin++;
	}
}

/** Move the frame ring's zone segment to the front so consumers get a
 *  contiguous, chronological array. Wrapped segments are rotated in place
 *  (three reversals; no temporary buffer, O(frame_zones) cold path). */
static void compact_frame_ring(sk_profiler_core_t* core) {
	u32 emitted = core->frame_emitted;
	u32 start = (core->frame_head + core->frame_zones_cap - emitted) % core->frame_zones_cap;

	if (start == 0u) {
		return; /* already contiguous at the front */
	}
	if (start + emitted <= core->frame_zones_cap) {
		memmove(core->frame_ring, &core->frame_ring[start], (size_t)emitted * sizeof(sk_profiler_core_zone_t));
	} else {
		/* Wrapped: the array holds [ring[start..cap) | ring[0..emitted-start));
		 * rotate it left by `start` to restore chronological order. */
		reverse_zone_range(core->frame_ring, 0u, emitted);
		reverse_zone_range(core->frame_ring, 0u, emitted - start);
		reverse_zone_range(core->frame_ring, emitted - start, emitted);
	}
	core->frame_head = emitted;
}

/* ---- task accumulation ---- */

static i32 find_task(const sk_profiler_core_t* core, const_chr_t name) {
	for (u32 i = 0u; i < core->task_count; i++) {
		if (strcmp(core->tasks[i].name, name) == 0) {
			return (i32)i;
		}
	}
	return -1;
}

static void accumulate_frame(sk_profiler_core_t* core) {
	u32 start = (core->frame_head + core->frame_zones_cap - core->frame_emitted) % core->frame_zones_cap;

	for (u32 k = 0u; k < core->frame_emitted; k++) {
		u32 slot = (start + k) % core->frame_zones_cap;
		const sk_profiler_core_zone_t* zone = &core->frame_ring[slot];
		f64 duration = zone->end - zone->start;

		i32 idx = find_task(core, zone->name);
		if (idx < 0) {
			if (core->task_count >= core->max_tasks) {
				core->dropped_tasks++;
				if (!core->task_overflow_warned) {
					core->task_overflow_warned = true;
					core_log(core, SK_LOGGER_TYPE_WARN, "task table full (%u entries); new zone names are dropped", core->max_tasks);
				}
				continue;
			}
			idx = (i32)core->task_count++;
			sk_profiler_core_task_t* created = &core->tasks[idx];
			memset(created, 0, sizeof(*created));
			copy_name(created->name, zone->name);
			copy_category(created->category, zone->category);
			created->color = zone->color;
			created->depth = zone->depth;
		}

		sk_profiler_core_task_t* task = &core->tasks[idx];
		task->present = true;
		task->total += duration;
		task->calls++;
	}

	/* Rolling min/max/avg across the frames each task was present. */
	for (u32 i = 0u; i < core->task_count; i++) {
		sk_profiler_core_task_t* task = &core->tasks[i];
		if (!task->present) {
			continue;
		}
		if (task->frames == 0u) {
			task->min = task->total;
			task->max = task->total;
			task->avg = task->total;
		} else {
			if (task->total < task->min) {
				task->min = task->total;
			}
			if (task->total > task->max) {
				task->max = task->total;
			}
			task->avg += (task->total - task->avg) / (f64)(task->frames + 1u);
		}
		task->frames++;
	}
}

static void accumulate_frame_stat(sk_profiler_core_frame_stats_t* stats, f64 value) {
	if (stats->count == 0u) {
		stats->min = value;
		stats->max = value;
		stats->avg = value;
	} else {
		if (value < stats->min) {
			stats->min = value;
		}
		if (value > stats->max) {
			stats->max = value;
		}
		stats->avg += (value - stats->avg) / (f64)(stats->count + 1u);
	}
	stats->current = value;
	stats->count++;
}

/* ---- lifecycle ---- */

sk_profiler_core_t* sk_profiler_core_create(const sk_profiler_core_config_t* config) {
	/* Resolve the allocator the same way init does, so the instance and its
	 * buffers share one owner. */
	const sk_allocator_t* alloc = (config != NULL && config->allocator != NULL) ? config->allocator : sk_allocator_default();
	sk_profiler_core_t* core = (sk_profiler_core_t*)alloc->alloc(alloc->instance, sizeof(*core));
	if (core == NULL) {
		return NULL;
	}
	if (sk_profiler_core_init(core, config) != 0) {
		alloc->free(alloc->instance, core);
		return NULL;
	}
	return core;
}

void sk_profiler_core_destroy(sk_profiler_core_t* core) {
	if (core == NULL) {
		return;
	}
	const sk_allocator_t* alloc = core->alloc;
	sk_profiler_core_shutdown(core);
	alloc->free(alloc->instance, core);
}

i32 sk_profiler_core_init(sk_profiler_core_t* core, const sk_profiler_core_config_t* config) {
	sk_profiler_core_config_t cfg;

	memset(core, 0, sizeof(*core));
	if (config != NULL) {
		cfg = *config;
	} else {
		sk_profiler_core_config_default(&cfg);
	}
	if (cfg.max_threads == 0u) {
		cfg.max_threads = 16u;
	}
	if (cfg.zones_per_thread == 0u) {
		cfg.zones_per_thread = 256u;
	}
	if (cfg.frame_zones == 0u) {
		cfg.frame_zones = cfg.max_threads * cfg.zones_per_thread;
	}
	if (cfg.max_tasks == 0u) {
		cfg.max_tasks = 256u;
	}

	core->max_threads = cfg.max_threads;
	core->zones_per_thread = cfg.zones_per_thread;
	core->frame_zones_cap = cfg.frame_zones;
	core->max_tasks = cfg.max_tasks;
	core->alloc = (cfg.allocator != NULL) ? cfg.allocator : sk_allocator_default();
	core->now = (cfg.clock.now != NULL) ? cfg.clock.now : clock_now_default;

	const sk_logger_api_t* logger_api = sk_logger_api();
	core->logger = (logger_api != NULL) ? logger_api->create_logger("profiler") : NULL;

	/* All buffers are pre-allocated here (known boundary); the hot path never
	 * allocates. Free everything on the first failure, then bail out. */
	size_t zone_size = sizeof(sk_profiler_core_zone_t);
	size_t task_size = sizeof(sk_profiler_core_task_t);
	size_t ring_count = (size_t)cfg.max_threads * (size_t)cfg.zones_per_thread;

	core->threads = (void_ptr_t*)core->alloc->alloc(core->alloc->instance, (size_t)cfg.max_threads * sizeof(void_ptr_t));
	core->thread_states = (sk_profiler_thread_ctx_t*)core->alloc->alloc(core->alloc->instance, (size_t)cfg.max_threads * sizeof(sk_profiler_thread_ctx_t));
	core->thread_rings = (sk_profiler_core_zone_t*)core->alloc->alloc(core->alloc->instance, ring_count * zone_size);
	core->thread_stacks = (u32*)core->alloc->alloc(core->alloc->instance, ring_count * sizeof(u32));
	core->frame_ring = (sk_profiler_core_zone_t*)core->alloc->alloc(core->alloc->instance, (size_t)cfg.frame_zones * zone_size);
	core->tasks = (sk_profiler_core_task_t*)core->alloc->alloc(core->alloc->instance, (size_t)cfg.max_tasks * task_size);
	if (core->threads == NULL || core->thread_states == NULL || core->thread_rings == NULL || core->thread_stacks == NULL || core->frame_ring == NULL || core->tasks == NULL) {
		/* initialized is still false: free the partial set directly. */
		const sk_allocator_t* alloc = core->alloc;
		alloc->free(alloc->instance, core->threads);
		alloc->free(alloc->instance, core->thread_states);
		alloc->free(alloc->instance, core->thread_rings);
		alloc->free(alloc->instance, core->thread_stacks);
		alloc->free(alloc->instance, core->frame_ring);
		alloc->free(alloc->instance, core->tasks);
		if (logger_api != NULL && core->logger != NULL) {
			logger_api->destroy_logger(core->logger);
		}
		memset(core, 0, sizeof(*core));
		return -1;
	}
	memset(core->threads, 0, (size_t)cfg.max_threads * sizeof(void_ptr_t));
	memset(core->thread_states, 0, (size_t)cfg.max_threads * sizeof(sk_profiler_thread_ctx_t));

	/* The calling thread becomes the recording thread (index 0). */
	sk_atomic_u32_init(&core->next_thread_index, 1u);
	thread_ctx = &core->thread_states[0];
	thread_ctx_owner = core;
	thread_ctx->core = core;
	thread_ctx->records = core->thread_rings;
	thread_ctx->open_stack = core->thread_stacks;
	thread_ctx->capacity = cfg.zones_per_thread;
	thread_ctx->head = 0u;
	thread_ctx->written = 0u;
	thread_ctx->live = 0u;
	thread_ctx->pending_phantom = 0u;
	thread_ctx->thread_index = 0u;
	thread_ctx->dropped = 0u;
	thread_ctx->mismatched_ends = 0u;
	thread_ctx->drop_warned = false;
	core->threads[0] = thread_ctx; /* plain store: same thread, not yet shared */

	core->active = false;
	core->initialized = true;

	core_log(core, SK_LOGGER_TYPE_INFO, "profiler core initialized (threads=%u, zones/thread=%u, frame zones=%u, tasks=%u)", cfg.max_threads, cfg.zones_per_thread, cfg.frame_zones,
			 cfg.max_tasks);
	return 0;
}

void sk_profiler_core_shutdown(sk_profiler_core_t* core) {
	if (core == NULL || !core->initialized) {
		return;
	}

	const sk_allocator_t* alloc = core->alloc;
	alloc->free(alloc->instance, core->threads);
	alloc->free(alloc->instance, core->thread_states);
	alloc->free(alloc->instance, core->thread_rings);
	alloc->free(alloc->instance, core->thread_stacks);
	alloc->free(alloc->instance, core->frame_ring);
	alloc->free(alloc->instance, core->tasks);

	const sk_logger_api_t* logger_api = sk_logger_api();
	if (logger_api != NULL && core->logger != NULL) {
		logger_api->destroy_logger(core->logger);
	}

	if (thread_ctx != NULL && thread_ctx->core == core) {
		thread_ctx = NULL; /* drop the TLS cache; the state memory is freed */
		thread_ctx_owner = NULL;
	}
	/* Keep the allocator pointer so destroy can free the instance after a
	 * shutdown; initialized is the live/not-live guard. */
	core->initialized = false;
	core->active = false;
}

/* ---- switches / stats ---- */

void sk_profiler_core_set_active(sk_profiler_core_t* core, bool active) {
	core->active = active;
	if (!active) {
		core->task_count = 0u;
		core->frame_emitted = 0u;
		core->frame_head = 0u;
		core->frame_wall_time = 0.0;
		memset(&core->frame_stats, 0, sizeof(core->frame_stats));
		/* The recording thread's own pending zones are discarded too. */
		if (thread_ctx != NULL && thread_ctx->core == core) {
			thread_ctx->head = 0u;
			thread_ctx->written = 0u;
			thread_ctx->live = 0u;
			thread_ctx->pending_phantom = 0u;
		}
	}
}

bool sk_profiler_core_is_active(const sk_profiler_core_t* core) {
	return core->active;
}

void sk_profiler_core_reset_stats(sk_profiler_core_t* core) {
	for (u32 i = 0u; i < core->task_count; i++) {
		sk_profiler_core_task_t* task = &core->tasks[i];
		task->min = 0.0;
		task->max = 0.0;
		task->avg = 0.0;
		task->frames = 0u;
	}
	memset(&core->frame_stats, 0, sizeof(core->frame_stats));
	core->frame_wall_time = 0.0;
}

/* ---- hot path (per-thread, lock-free, allocation-free) ---- */

void sk_profiler_core_begin_zone(sk_profiler_core_t* core, const_chr_t name, const_chr_t category, u32 color) {
	if (core == NULL || !core->active) {
		return;
	}

	sk_profiler_thread_ctx_t* ctx = acquire_thread_ctx(core);
	if (ctx == NULL || ctx->capacity == 0u) {
		return;
	}

	if (ctx->written >= ctx->capacity) {
		/* Buffer full: drop the newest begin; its end becomes a no-op. */
		sk_atomic_u32_fetch_add(&ctx->dropped, 1u);
		ctx->pending_phantom++;
		if (!ctx->drop_warned) {
			ctx->drop_warned = true;
			core_log(core, SK_LOGGER_TYPE_WARN, "thread %u zone buffer full (capacity %u): further zones this frame are dropped", ctx->thread_index, ctx->capacity);
		}
		return;
	}

	u32 slot = ctx->head;
	ctx->head = (ctx->head + 1u == ctx->capacity) ? 0u : ctx->head + 1u;
	ctx->written++;

	sk_profiler_core_zone_t* zone = &ctx->records[slot];
	copy_name(zone->name, name);
	copy_category(zone->category, category);
	zone->color = color;
	zone->thread_index = ctx->thread_index;
	zone->depth = (i32)ctx->live;
	zone->start = core->now();
	zone->end = 0.0;
	zone->open = true;
	ctx->open_stack[ctx->live] = slot; /* live == open stack depth */
	ctx->live++;
}

void sk_profiler_core_end_zone(sk_profiler_core_t* core) {
	if (core == NULL || !core->active) {
		return;
	}

	sk_profiler_thread_ctx_t* ctx = thread_ctx;
	if (ctx == NULL || ctx->core != core || ctx->capacity == 0u) {
		return; /* not registered with this core (or overflowed thread) */
	}

	if (ctx->live > 0u) {
		u32 slot = ctx->open_stack[ctx->live - 1u];
		ctx->live--;
		sk_profiler_core_zone_t* zone = &ctx->records[slot];
		zone->end = core->now();
		zone->open = false;
	} else if (ctx->pending_phantom > 0u) {
		ctx->pending_phantom--; /* end of a dropped begin */
	} else {
		sk_atomic_u32_fetch_add(&ctx->mismatched_ends, 1u);
	}
}

/* ---- frame delimiters (recording thread) ---- */

void sk_profiler_core_begin_frame(sk_profiler_core_t* core) {
	if (core == NULL || !core->active) {
		return;
	}
	core->frame_start = core->now();
}

void sk_profiler_core_end_frame(sk_profiler_core_t* core) {
	if (core == NULL || !core->active) {
		return;
	}

	/* Per-frame reset of the task table. */
	for (u32 i = 0u; i < core->task_count; i++) {
		core->tasks[i].present = false;
		core->tasks[i].total = 0.0;
		core->tasks[i].calls = 0u;
	}
	core->frame_head = 0u;
	core->frame_emitted = 0u;

	/* Drain every registered thread (recording thread first). */
	for (u32 i = 0u; i < core->max_threads; i++) {
		sk_profiler_thread_ctx_t* ctx = (sk_profiler_thread_ctx_t*)sk_atomic_ptr_load_ordered(&core->threads[i], SK_ATOMIC_ORDER_ACQUIRE);
		if (ctx != NULL) {
			drain_thread(core, ctx);
		}
	}

	accumulate_frame(core);

	f64 wall = core->now() - core->frame_start;
	accumulate_frame_stat(&core->frame_stats, wall);
	core->frame_wall_time = wall;

	compact_frame_ring(core);
}

/* ---- results ---- */

const sk_profiler_core_zone_t* sk_profiler_core_frame_zones(const sk_profiler_core_t* core, u32* count) {
	if (count != NULL) {
		*count = core->frame_emitted;
	}
	return core->frame_ring;
}

const sk_profiler_core_task_t* sk_profiler_core_tasks(const sk_profiler_core_t* core, u32* count) {
	if (count != NULL) {
		*count = core->task_count;
	}
	return core->tasks;
}

sk_profiler_core_frame_stats_t sk_profiler_core_frame_stats(const sk_profiler_core_t* core) {
	return core->frame_stats;
}

f64 sk_profiler_core_frame_wall_time(const sk_profiler_core_t* core) {
	return core->frame_wall_time;
}

u32 sk_profiler_core_dropped_zones(const sk_profiler_core_t* core) {
	u32 total = 0u;
	for (u32 i = 0u; i < core->max_threads; i++) {
		sk_profiler_thread_ctx_t* ctx = (sk_profiler_thread_ctx_t*)sk_atomic_ptr_load_ordered(&core->threads[i], SK_ATOMIC_ORDER_ACQUIRE);
		if (ctx != NULL) {
			total += sk_atomic_u32_load(&ctx->dropped);
		}
	}
	return total;
}

u32 sk_profiler_core_dropped_tasks(const sk_profiler_core_t* core) {
	return core->dropped_tasks;
}

u32 sk_profiler_core_frame_overwrites(const sk_profiler_core_t* core) {
	return core->frame_overwrites;
}

u32 sk_profiler_core_mismatched_ends(const sk_profiler_core_t* core) {
	u32 total = 0u;
	for (u32 i = 0u; i < core->max_threads; i++) {
		sk_profiler_thread_ctx_t* ctx = (sk_profiler_thread_ctx_t*)sk_atomic_ptr_load_ordered(&core->threads[i], SK_ATOMIC_ORDER_ACQUIRE);
		if (ctx != NULL) {
			total += sk_atomic_u32_load(&ctx->mismatched_ends);
		}
	}
	return total;
}

u32 sk_profiler_core_thread_overflows(const sk_profiler_core_t* core) {
	return sk_atomic_u32_load(&core->thread_overflows);
}
