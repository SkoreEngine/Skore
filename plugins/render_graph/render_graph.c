/**
 * @file render_graph.c
 * @brief sk-render-graph plugin: fn table, frame memory, build-phase declare.
 *
 * Frame memory (APX-150):
 *   - Linear/bump frame arena: preallocated block, reset (not freed) at begin.
 *   - Persistent free-list pools: pass nodes + resource descriptors.
 *   - Persistent growable arrays: edge list + barrier list (count cleared per frame).
 * Growth (heap) is allowed only during setup or between frames (in_frame == 0).
 * Mid-frame exhaustion returns SK_RG_ERR_OUT_OF_SPACE — never silent malloc.
 *
 * Build phase (APX-151):
 *   - begin → create/import resources, add_pass, pass_read/write, end/execute.
 *   - Dep lists + imported texture handle arrays live in the frame arena.
 *   - Producer→consumer edges are recorded into the edge array pool.
 */

#include "render_graph.h"

#include "allocator.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Defaults                                                            */
/* ------------------------------------------------------------------ */

#define SK_RG_DEFAULT_FRAME_ARENA_BYTES (64ull * 1024ull)
#define SK_RG_DEFAULT_PASS_CAPACITY 64u
#define SK_RG_DEFAULT_RESOURCE_CAPACITY 128u
#define SK_RG_DEFAULT_EDGE_CAPACITY 256u
#define SK_RG_DEFAULT_BARRIER_CAPACITY 256u

/** Default alignment for unaligned arena requests (pointer-sized). */
#define SK_RG_ARENA_DEFAULT_ALIGN ((u64)sizeof(void_ptr_t))

/** Sentinel for "no writer / no resource index". */
#define SK_RG_INVALID_INDEX ((u32)0xffffffffu)

/* ------------------------------------------------------------------ */
/* Internal element types                                              */
/* ------------------------------------------------------------------ */

/** One pass→resource dependency (arena-linked list on the pass). */
typedef struct sk_rg_dep_node_t {
	const_chr_t resource_name;
	sk_rg_access_t access;
	u32 usage_flags;
	i32 is_resolve;
	u32 resource_index;
	struct sk_rg_dep_node_t* next;
} sk_rg_dep_node_t;

/**
 * Pass node storage (free-list pool element). Public sk_rg_pass_t is this layout.
 * Freelist link overlays the first pointer-sized field when the slot is free.
 */
struct sk_rg_pass_t {
	sk_render_graph_t* graph;
	const_chr_t name;
	sk_rg_pass_type_t type;
	i32 stage;
	u32 index;
	u32 dep_count;
	sk_rg_dep_node_t* deps_head;
	sk_rg_dep_node_t* deps_tail;
	sk_rg_record_fn record_fn;
	void_ptr_t record_user;
	sk_rg_resize_fn resize_fn;
	void_ptr_t resize_user;
	sk_rg_constants_fn constants_fn;
	void_ptr_t constants_user;
	u32 constants_size;
	u32 constants_stage_mask;
	sk_pipeline_t pipeline;
	u32 dispatch_x;
	u32 dispatch_y;
	u32 dispatch_z;
	sk_buffer_t dispatch_indirect;
	u32 generation; /* matches graph frame_generation while live */
};

/** Resource descriptor slot (free-list pool element). */
typedef struct sk_rg_resource_node_t {
	const_chr_t name;
	sk_rg_resource_kind_t kind;
	u32 flags;
	u32 usage;
	u32 index;
	u32 last_writer_pass;
	u32 last_reader_pass;
	union {
		sk_rg_texture_desc_t texture;
		sk_rg_buffer_desc_t buffer;
		sk_rg_view_desc_t view;
		struct {
			sk_texture_t* textures; /* arena-backed copy */
			u32 count;
			sk_resource_state_t state;
		} imported;
		struct {
			void_ptr_t data; /* arena-backed blob */
			u64 size;
		} instance;
	} u;
} sk_rg_resource_node_t;

/** Topology edge (from pass index → to pass index). */
typedef struct sk_rg_edge_t {
	u32 from;
	u32 to;
} sk_rg_edge_t;

/** Barrier list entry (placeholder until barrier builder lands). */
typedef struct sk_rg_barrier_entry_t {
	u32 resource_index;
	u32 old_state;
	u32 new_state;
	u32 flags;
} sk_rg_barrier_entry_t;

/* ------------------------------------------------------------------ */
/* Frame arena (linear bump)                                           */
/* ------------------------------------------------------------------ */

typedef struct sk_rg_frame_arena_t {
	u8* base;
	u64 capacity;
	u64 offset;
	u64 high_water;
} sk_rg_frame_arena_t;

/** Sub-arena / scratch marker: restore offset on pop (capacity retained). */
typedef struct sk_rg_arena_marker_t {
	u64 offset;
} sk_rg_arena_marker_t;

/* ------------------------------------------------------------------ */
/* Object free-list pool                                               */
/* ------------------------------------------------------------------ */

typedef struct sk_rg_object_pool_t {
	u8* storage;
	void_ptr_t free_head;
	u32 elem_size;
	u32 capacity;
	u32 live;
	u32 high_water;
} sk_rg_object_pool_t;

/* ------------------------------------------------------------------ */
/* Persistent array (count reset per frame; capacity retained)         */
/* ------------------------------------------------------------------ */

typedef struct sk_rg_array_pool_t {
	u8* data;
	u32 elem_size;
	u32 count;
	u32 capacity;
	u32 high_water;
} sk_rg_array_pool_t;

/* ------------------------------------------------------------------ */
/* Graph memory root                                                   */
/* ------------------------------------------------------------------ */

typedef struct sk_rg_memory_t {
	const sk_allocator_t* heap;
	sk_rg_frame_arena_t arena;
	sk_rg_object_pool_t passes;
	sk_rg_object_pool_t resources;
	sk_rg_array_pool_t edges;
	sk_rg_array_pool_t barriers;
	u32 growth_events;
	u32 heap_alloc_count;
	i32 in_frame;
	i32 last_error;
} sk_rg_memory_t;

struct sk_render_graph_t {
	sk_render_device_t device;
	sk_rg_memory_t memory;
	sk_rg_extent_t output_size;
	u32 topology_build_count;
	u32 current_output_index;
	u32 frame_generation;
	void_ptr_t scene;
	const_chr_t color_output;
	const_chr_t depth_output;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static u64 rg_align_up(u64 value, u64 alignment) {
	/* alignment is power-of-two (caller contract for non-default). */
	return (value + (alignment - 1ull)) & ~(alignment - 1ull);
}

static i32 rg_is_pow2(u64 alignment) {
	return alignment != 0ull && (alignment & (alignment - 1ull)) == 0ull;
}

static void rg_memory_set_error(sk_rg_memory_t* mem, i32 err) {
	mem->last_error = err;
}

static void_ptr_t rg_heap_alloc(sk_rg_memory_t* mem, size_t size) {
	void_ptr_t p = mem->heap->alloc(mem->heap->instance, size);
	if (p != NULL) {
		mem->heap_alloc_count += 1u;
	}
	return p;
}

static void_ptr_t rg_heap_realloc(sk_rg_memory_t* mem, void_ptr_t ptr, size_t size) {
	void_ptr_t p = mem->heap->realloc(mem->heap->instance, ptr, size);
	if (p != NULL) {
		mem->heap_alloc_count += 1u;
	}
	return p;
}

static void rg_heap_free(sk_rg_memory_t* mem, void_ptr_t ptr) {
	if (ptr != NULL) {
		mem->heap->free(mem->heap->instance, ptr);
	}
}

static i32 rg_name_eq(const_chr_t a, const_chr_t b) {
	if (a == b) {
		return 1;
	}
	if (a == NULL || b == NULL) {
		return 0;
	}
	return strcmp(a, b) == 0;
}

static i32 rg_name_empty(const_chr_t name) {
	return name == NULL || name[0] == '\0';
}

/* ------------------------------------------------------------------ */
/* Frame arena                                                         */
/* ------------------------------------------------------------------ */

static i32 rg_arena_init(sk_rg_memory_t* mem, u64 capacity) {
	sk_rg_frame_arena_t* a = &mem->arena;
	a->base = NULL;
	a->capacity = 0ull;
	a->offset = 0ull;
	a->high_water = 0ull;
	if (capacity == 0ull) {
		return SK_RG_OK;
	}
	a->base = (u8*)rg_heap_alloc(mem, capacity);
	if (a->base == NULL) {
		return SK_RG_ERR_OOM;
	}
	a->capacity = capacity;
	return SK_RG_OK;
}

static void rg_arena_shutdown(sk_rg_memory_t* mem) {
	sk_rg_frame_arena_t* a = &mem->arena;
	rg_heap_free(mem, a->base);
	a->base = NULL;
	a->capacity = 0ull;
	a->offset = 0ull;
	a->high_water = 0ull;
}

/**
 * Bump-allocate @p size bytes aligned to @p alignment (power of two, or 0 → default).
 * Never heap-allocates. On failure sets last_error and returns NULL.
 */
static void_ptr_t rg_arena_alloc(sk_rg_memory_t* mem, u64 size, u64 alignment) {
	sk_rg_frame_arena_t* a = &mem->arena;
	u64 align = alignment == 0ull ? SK_RG_ARENA_DEFAULT_ALIGN : alignment;
	u64 aligned_off;
	u64 end;

	if (!rg_is_pow2(align)) {
		rg_memory_set_error(mem, SK_RG_ERR_OUT_OF_SPACE);
		return NULL;
	}
	if (size == 0ull) {
		/* Zero-size: return a unique non-null if possible, else error. */
		aligned_off = rg_align_up(a->offset, align);
		if (aligned_off > a->capacity) {
			rg_memory_set_error(mem, SK_RG_ERR_OUT_OF_SPACE);
			return NULL;
		}
		a->offset = aligned_off;
		if (a->offset > a->high_water) {
			a->high_water = a->offset;
		}
		return a->base != NULL ? (void_ptr_t)(a->base + aligned_off) : NULL;
	}

	aligned_off = rg_align_up(a->offset, align);
	end = aligned_off + size;
	if (end < aligned_off || end > a->capacity) {
		rg_memory_set_error(mem, SK_RG_ERR_OUT_OF_SPACE);
		return NULL;
	}
	a->offset = end;
	if (a->offset > a->high_water) {
		a->high_water = a->offset;
	}
	rg_memory_set_error(mem, SK_RG_OK);
	return (void_ptr_t)(a->base + aligned_off);
}

/** Reset offset to 0; retain capacity and high-water mark. */
static void rg_arena_reset(sk_rg_frame_arena_t* a) {
	a->offset = 0ull;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static sk_rg_arena_marker_t rg_arena_push(const sk_rg_frame_arena_t* a) {
	sk_rg_arena_marker_t m;
	m.offset = a->offset;
	return m;
}

/** Roll back to marker (scratch/sub-arena). Does not shrink high-water. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static void rg_arena_pop(sk_rg_frame_arena_t* a, sk_rg_arena_marker_t marker) {
	if (marker.offset <= a->offset) {
		a->offset = marker.offset;
	}
}

/**
 * Grow frame arena capacity (only when !in_frame). New block is larger or equal;
 * existing used bytes are copied; offset preserved.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static i32 rg_arena_grow(sk_rg_memory_t* mem, u64 new_capacity) {
	sk_rg_frame_arena_t* a = &mem->arena;
	u8* next;

	if (mem->in_frame) {
		rg_memory_set_error(mem, SK_RG_ERR_GROW_BLOCKED);
		return SK_RG_ERR_GROW_BLOCKED;
	}
	if (new_capacity <= a->capacity) {
		return SK_RG_OK;
	}
	next = (u8*)rg_heap_realloc(mem, a->base, new_capacity);
	if (next == NULL) {
		rg_memory_set_error(mem, SK_RG_ERR_OOM);
		return SK_RG_ERR_OOM;
	}
	a->base = next;
	a->capacity = new_capacity;
	mem->growth_events += 1u;
	rg_memory_set_error(mem, SK_RG_OK);
	return SK_RG_OK;
}

/* ------------------------------------------------------------------ */
/* Object free-list pool                                               */
/* ------------------------------------------------------------------ */

static void rg_object_pool_link_all(sk_rg_object_pool_t* pool) {
	u32 i;
	pool->free_head = NULL;
	for (i = pool->capacity; i > 0u; --i) {
		u8* slot = pool->storage + (u64)(i - 1u) * (u64)pool->elem_size;
		void_ptr_t* next_field = (void_ptr_t*)(void_ptr_t)slot;
		*next_field = pool->free_head;
		pool->free_head = (void_ptr_t)slot;
	}
	pool->live = 0u;
}

static i32 rg_object_pool_init(sk_rg_memory_t* mem, sk_rg_object_pool_t* pool, u32 elem_size, u32 capacity) {
	pool->storage = NULL;
	pool->free_head = NULL;
	pool->elem_size = elem_size;
	pool->capacity = 0u;
	pool->live = 0u;
	pool->high_water = 0u;

	if (elem_size < (u32)sizeof(void_ptr_t)) {
		return SK_RG_ERR_OOM;
	}
	if (capacity == 0u) {
		return SK_RG_OK;
	}

	pool->storage = (u8*)rg_heap_alloc(mem, (size_t)capacity * (size_t)elem_size);
	if (pool->storage == NULL) {
		return SK_RG_ERR_OOM;
	}
	pool->capacity = capacity;
	rg_object_pool_link_all(pool);
	return SK_RG_OK;
}

static void rg_object_pool_shutdown(sk_rg_memory_t* mem, sk_rg_object_pool_t* pool) {
	rg_heap_free(mem, pool->storage);
	pool->storage = NULL;
	pool->free_head = NULL;
	pool->capacity = 0u;
	pool->live = 0u;
	pool->high_water = 0u;
}

static void_ptr_t rg_object_pool_acquire(sk_rg_memory_t* mem, sk_rg_object_pool_t* pool) {
	void_ptr_t slot;
	void_ptr_t* next_field;

	if (pool->free_head == NULL) {
		rg_memory_set_error(mem, SK_RG_ERR_OUT_OF_SPACE);
		return NULL;
	}
	slot = pool->free_head;
	next_field = (void_ptr_t*)slot;
	pool->free_head = *next_field;
	memset(slot, 0, pool->elem_size);
	pool->live += 1u;
	if (pool->live > pool->high_water) {
		pool->high_water = pool->live;
	}
	rg_memory_set_error(mem, SK_RG_OK);
	return slot;
}

/** Return all slots to the free-list (begin-of-frame). */
static void rg_object_pool_reset(sk_rg_object_pool_t* pool) {
	if (pool->storage == NULL || pool->capacity == 0u) {
		pool->live = 0u;
		pool->free_head = NULL;
		return;
	}
	rg_object_pool_link_all(pool);
}

/**
 * Index of a live pool slot acquired in FIFO free-list order (slot 0 first).
 * Returns SK_RG_INVALID_INDEX if ptr is outside the pool.
 */
static u32 rg_object_pool_index(const sk_rg_object_pool_t* pool, const void* ptr) {
	uintptr_t base;
	uintptr_t p;
	uintptr_t delta;
	if (pool->storage == NULL || ptr == NULL) {
		return SK_RG_INVALID_INDEX;
	}
	base = (uintptr_t)pool->storage;
	p = (uintptr_t)ptr;
	if (p < base) {
		return SK_RG_INVALID_INDEX;
	}
	delta = p - base;
	if (delta % (uintptr_t)pool->elem_size != 0u) {
		return SK_RG_INVALID_INDEX;
	}
	delta /= (uintptr_t)pool->elem_size;
	if (delta >= (uintptr_t)pool->capacity) {
		return SK_RG_INVALID_INDEX;
	}
	return (u32)delta;
}

static void_ptr_t rg_object_pool_at(const sk_rg_object_pool_t* pool, u32 index) {
	if (index >= pool->live || pool->storage == NULL) {
		return NULL;
	}
	return (void_ptr_t)(pool->storage + (u64)index * (u64)pool->elem_size);
}

/**
 * Grow object pool (only when !in_frame). Live objects are discarded — growth is
 * for setup / between-frame reconfiguration before the next acquire sequence.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static i32 rg_object_pool_grow(sk_rg_memory_t* mem, sk_rg_object_pool_t* pool, u32 new_capacity) {
	u8* next;

	if (mem->in_frame) {
		rg_memory_set_error(mem, SK_RG_ERR_GROW_BLOCKED);
		return SK_RG_ERR_GROW_BLOCKED;
	}
	if (new_capacity <= pool->capacity) {
		return SK_RG_OK;
	}
	next = (u8*)rg_heap_realloc(mem, pool->storage, (size_t)new_capacity * (size_t)pool->elem_size);
	if (next == NULL) {
		rg_memory_set_error(mem, SK_RG_ERR_OOM);
		return SK_RG_ERR_OOM;
	}
	pool->storage = next;
	pool->capacity = new_capacity;
	rg_object_pool_link_all(pool);
	mem->growth_events += 1u;
	rg_memory_set_error(mem, SK_RG_OK);
	return SK_RG_OK;
}

/* ------------------------------------------------------------------ */
/* Array pool                                                          */
/* ------------------------------------------------------------------ */

static i32 rg_array_pool_init(sk_rg_memory_t* mem, sk_rg_array_pool_t* arr, u32 elem_size, u32 capacity) {
	arr->data = NULL;
	arr->elem_size = elem_size;
	arr->count = 0u;
	arr->capacity = 0u;
	arr->high_water = 0u;
	if (capacity == 0u) {
		return SK_RG_OK;
	}
	arr->data = (u8*)rg_heap_alloc(mem, (size_t)capacity * (size_t)elem_size);
	if (arr->data == NULL) {
		return SK_RG_ERR_OOM;
	}
	arr->capacity = capacity;
	return SK_RG_OK;
}

static void rg_array_pool_shutdown(sk_rg_memory_t* mem, sk_rg_array_pool_t* arr) {
	rg_heap_free(mem, arr->data);
	arr->data = NULL;
	arr->count = 0u;
	arr->capacity = 0u;
	arr->high_water = 0u;
}

static void rg_array_pool_clear(sk_rg_array_pool_t* arr) {
	arr->count = 0u;
}

static i32 rg_array_pool_push(sk_rg_memory_t* mem, sk_rg_array_pool_t* arr, const void* elem) {
	u8* dst;
	if (arr->count >= arr->capacity) {
		rg_memory_set_error(mem, SK_RG_ERR_OUT_OF_SPACE);
		return SK_RG_ERR_OUT_OF_SPACE;
	}
	dst = arr->data + (u64)arr->count * (u64)arr->elem_size;
	memcpy(dst, elem, arr->elem_size);
	arr->count += 1u;
	if (arr->count > arr->high_water) {
		arr->high_water = arr->count;
	}
	rg_memory_set_error(mem, SK_RG_OK);
	return SK_RG_OK;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static i32 rg_array_pool_grow(sk_rg_memory_t* mem, sk_rg_array_pool_t* arr, u32 new_capacity) {
	u8* next;
	if (mem->in_frame) {
		rg_memory_set_error(mem, SK_RG_ERR_GROW_BLOCKED);
		return SK_RG_ERR_GROW_BLOCKED;
	}
	if (new_capacity <= arr->capacity) {
		return SK_RG_OK;
	}
	next = (u8*)rg_heap_realloc(mem, arr->data, (size_t)new_capacity * (size_t)arr->elem_size);
	if (next == NULL) {
		rg_memory_set_error(mem, SK_RG_ERR_OOM);
		return SK_RG_ERR_OOM;
	}
	arr->data = next;
	arr->capacity = new_capacity;
	mem->growth_events += 1u;
	rg_memory_set_error(mem, SK_RG_OK);
	return SK_RG_OK;
}

static void_ptr_t rg_array_pool_at(const sk_rg_array_pool_t* arr, u32 index) {
	if (index >= arr->count || arr->data == NULL) {
		return NULL;
	}
	return (void_ptr_t)(arr->data + (u64)index * (u64)arr->elem_size);
}

/* ------------------------------------------------------------------ */
/* Memory root lifecycle                                               */
/* ------------------------------------------------------------------ */

static void rg_memory_config_apply_defaults(sk_rg_memory_config_t* cfg) {
	if (cfg->frame_arena_bytes == 0ull) {
		cfg->frame_arena_bytes = SK_RG_DEFAULT_FRAME_ARENA_BYTES;
	}
	if (cfg->pass_capacity == 0u) {
		cfg->pass_capacity = SK_RG_DEFAULT_PASS_CAPACITY;
	}
	if (cfg->resource_capacity == 0u) {
		cfg->resource_capacity = SK_RG_DEFAULT_RESOURCE_CAPACITY;
	}
	if (cfg->edge_capacity == 0u) {
		cfg->edge_capacity = SK_RG_DEFAULT_EDGE_CAPACITY;
	}
	if (cfg->barrier_capacity == 0u) {
		cfg->barrier_capacity = SK_RG_DEFAULT_BARRIER_CAPACITY;
	}
}

static i32 rg_memory_init(sk_rg_memory_t* mem, const sk_rg_memory_config_t* config) {
	sk_rg_memory_config_t cfg;
	i32 rc;

	memset(mem, 0, sizeof(*mem));
	mem->heap = sk_allocator_default();

	cfg = *config;
	rg_memory_config_apply_defaults(&cfg);

	rc = rg_arena_init(mem, cfg.frame_arena_bytes);
	if (rc != SK_RG_OK) {
		return rc;
	}
	rc = rg_object_pool_init(mem, &mem->passes, (u32)sizeof(struct sk_rg_pass_t), cfg.pass_capacity);
	if (rc != SK_RG_OK) {
		rg_arena_shutdown(mem);
		return rc;
	}
	rc = rg_object_pool_init(mem, &mem->resources, (u32)sizeof(sk_rg_resource_node_t), cfg.resource_capacity);
	if (rc != SK_RG_OK) {
		rg_object_pool_shutdown(mem, &mem->passes);
		rg_arena_shutdown(mem);
		return rc;
	}
	rc = rg_array_pool_init(mem, &mem->edges, (u32)sizeof(sk_rg_edge_t), cfg.edge_capacity);
	if (rc != SK_RG_OK) {
		rg_object_pool_shutdown(mem, &mem->resources);
		rg_object_pool_shutdown(mem, &mem->passes);
		rg_arena_shutdown(mem);
		return rc;
	}
	rc = rg_array_pool_init(mem, &mem->barriers, (u32)sizeof(sk_rg_barrier_entry_t), cfg.barrier_capacity);
	if (rc != SK_RG_OK) {
		rg_array_pool_shutdown(mem, &mem->edges);
		rg_object_pool_shutdown(mem, &mem->resources);
		rg_object_pool_shutdown(mem, &mem->passes);
		rg_arena_shutdown(mem);
		return rc;
	}
	return SK_RG_OK;
}

static void rg_memory_shutdown(sk_rg_memory_t* mem) {
	rg_array_pool_shutdown(mem, &mem->barriers);
	rg_array_pool_shutdown(mem, &mem->edges);
	rg_object_pool_shutdown(mem, &mem->resources);
	rg_object_pool_shutdown(mem, &mem->passes);
	rg_arena_shutdown(mem);
	mem->in_frame = 0;
	mem->last_error = SK_RG_OK;
}

/** begin-of-frame: reset arena + pools/arrays; mark in_frame. */
static void rg_memory_begin_frame(sk_rg_memory_t* mem) {
	rg_arena_reset(&mem->arena);
	rg_object_pool_reset(&mem->passes);
	rg_object_pool_reset(&mem->resources);
	rg_array_pool_clear(&mem->edges);
	rg_array_pool_clear(&mem->barriers);
	mem->in_frame = 1;
	mem->last_error = SK_RG_OK;
}

/** End of build/execute: allow growth again until next begin. */
static void rg_memory_end_frame(sk_rg_memory_t* mem) {
	mem->in_frame = 0;
}

static void rg_memory_fill_stats(const sk_rg_memory_t* mem, sk_rg_memory_stats_t* out) {
	out->frame_arena_capacity = mem->arena.capacity;
	out->frame_arena_used = mem->arena.offset;
	out->frame_arena_high_water = mem->arena.high_water;
	out->pass_capacity = mem->passes.capacity;
	out->pass_live = mem->passes.live;
	out->pass_high_water = mem->passes.high_water;
	out->resource_capacity = mem->resources.capacity;
	out->resource_live = mem->resources.live;
	out->resource_high_water = mem->resources.high_water;
	out->edge_capacity = mem->edges.capacity;
	out->edge_count = mem->edges.count;
	out->edge_high_water = mem->edges.high_water;
	out->barrier_capacity = mem->barriers.capacity;
	out->barrier_count = mem->barriers.count;
	out->barrier_high_water = mem->barriers.high_water;
	out->growth_events = mem->growth_events;
	out->heap_alloc_count = mem->heap_alloc_count;
	out->in_frame = mem->in_frame;
	out->last_error = mem->last_error;
}

/* ------------------------------------------------------------------ */
/* Build-phase helpers                                                 */
/* ------------------------------------------------------------------ */

static sk_rg_pass_t* rg_pass_at(const sk_render_graph_t* g, u32 index) {
	return (sk_rg_pass_t*)rg_object_pool_at(&g->memory.passes, index);
}

static sk_rg_resource_node_t* rg_resource_at(const sk_render_graph_t* g, u32 index) {
	return (sk_rg_resource_node_t*)rg_object_pool_at(&g->memory.resources, index);
}

static sk_rg_resource_node_t* rg_find_resource(const sk_render_graph_t* g, const_chr_t name) {
	u32 i;
	if (rg_name_empty(name)) {
		return NULL;
	}
	for (i = 0u; i < g->memory.resources.live; ++i) {
		sk_rg_resource_node_t* r = rg_resource_at(g, i);
		if (r != NULL && rg_name_eq(r->name, name)) {
			return r;
		}
	}
	return NULL;
}

static sk_rg_pass_t* rg_find_pass_by_name(const sk_render_graph_t* g, const_chr_t name) {
	u32 i;
	if (rg_name_empty(name)) {
		return NULL;
	}
	for (i = 0u; i < g->memory.passes.live; ++i) {
		sk_rg_pass_t* p = rg_pass_at(g, i);
		if (p != NULL && rg_name_eq(p->name, name)) {
			return p;
		}
	}
	return NULL;
}

static i32 rg_imported_state_is_read_only(sk_resource_state_t state) {
	return state == SK_RESOURCE_STATE_SHADER_READ || state == SK_RESOURCE_STATE_DEPTH_STENCIL_READ || state == SK_RESOURCE_STATE_COPY_SOURCE || state == SK_RESOURCE_STATE_PRESENT;
}

static u32 rg_infer_usage(sk_rg_pass_type_t type, sk_rg_access_t access, i32 is_resolve) {
	u32 usage = SK_RESOURCE_USAGE_NONE;
	if (is_resolve) {
		return SK_RESOURCE_USAGE_RENDER_TARGET | SK_RESOURCE_USAGE_COPY_DEST;
	}
	if (access == SK_RG_ACCESS_READ || access == SK_RG_ACCESS_READ_WRITE) {
		if (type == SK_RG_PASS_TRANSFER) {
			usage |= SK_RESOURCE_USAGE_COPY_SOURCE;
		} else {
			usage |= SK_RESOURCE_USAGE_SHADER_RESOURCE;
		}
	}
	if (access == SK_RG_ACCESS_WRITE || access == SK_RG_ACCESS_READ_WRITE) {
		if (type == SK_RG_PASS_GRAPHICS) {
			usage |= SK_RESOURCE_USAGE_RENDER_TARGET;
		} else if (type == SK_RG_PASS_TRANSFER) {
			usage |= SK_RESOURCE_USAGE_COPY_DEST;
		} else {
			usage |= SK_RESOURCE_USAGE_UNORDERED_ACCESS;
		}
	}
	return usage;
}

static i32 rg_edge_exists(const sk_render_graph_t* g, u32 from, u32 to) {
	u32 i;
	for (i = 0u; i < g->memory.edges.count; ++i) {
		const sk_rg_edge_t* e = (const sk_rg_edge_t*)rg_array_pool_at(&g->memory.edges, i);
		if (e != NULL && e->from == from && e->to == to) {
			return 1;
		}
	}
	return 0;
}

static i32 rg_add_edge(sk_render_graph_t* g, u32 from, u32 to) {
	sk_rg_edge_t edge;
	if (from == to || from == SK_RG_INVALID_INDEX || to == SK_RG_INVALID_INDEX) {
		return SK_RG_OK;
	}
	if (rg_edge_exists(g, from, to)) {
		return SK_RG_OK;
	}
	edge.from = from;
	edge.to = to;
	return rg_array_pool_push(&g->memory, &g->memory.edges, &edge);
}

/**
 * Acquire a resource slot after validating frame + name uniqueness.
 * On failure sets last_error and returns NULL.
 */
static sk_rg_resource_node_t* rg_declare_resource(sk_render_graph_t* g, const_chr_t name) {
	sk_rg_resource_node_t* node;

	if (!g->memory.in_frame) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_NOT_IN_FRAME);
		return NULL;
	}
	if (rg_name_empty(name)) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
		return NULL;
	}
	if (rg_find_resource(g, name) != NULL) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_DUPLICATE_NAME);
		return NULL;
	}

	node = (sk_rg_resource_node_t*)rg_object_pool_acquire(&g->memory, &g->memory.resources);
	if (node == NULL) {
		return NULL;
	}
	node->name = name;
	node->index = rg_object_pool_index(&g->memory.resources, node);
	node->last_writer_pass = SK_RG_INVALID_INDEX;
	node->last_reader_pass = SK_RG_INVALID_INDEX;
	node->flags = SK_RG_RESOURCE_FLAG_NONE;
	node->usage = 0u;
	return node;
}

static i32 rg_pass_is_live(const sk_rg_pass_t* p) {
	if (p == NULL || p->graph == NULL) {
		return 0;
	}
	if (!p->graph->memory.in_frame) {
		return 0;
	}
	if (p->generation != p->graph->frame_generation) {
		return 0;
	}
	return 1;
}

static void rg_pass_add_dep(sk_rg_pass_t* p, const_chr_t name, sk_rg_access_t access, i32 is_resolve) {
	sk_render_graph_t* g;
	sk_rg_resource_node_t* res;
	sk_rg_dep_node_t* dep;
	u32 usage;
	i32 writes;

	if (!rg_pass_is_live(p)) {
		/* Stale pass or dependency outside pass/frame scope. */
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	g = p->graph;

	if (rg_name_empty(name)) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
		return;
	}

	res = rg_find_resource(g, name);
	if (res == NULL) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_UNKNOWN_RESOURCE);
		return;
	}

	writes = (access == SK_RG_ACCESS_WRITE || access == SK_RG_ACCESS_READ_WRITE || is_resolve);
	if (writes && (res->flags & SK_RG_RESOURCE_FLAG_READ_ONLY) != 0u) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_WRITE_READ_ONLY);
		return;
	}

	dep = (sk_rg_dep_node_t*)rg_arena_alloc(&g->memory, sizeof(sk_rg_dep_node_t), sizeof(void_ptr_t));
	if (dep == NULL) {
		/* last_error already OUT_OF_SPACE */
		return;
	}

	usage = rg_infer_usage(p->type, access, is_resolve);
	if (res->kind == SK_RG_RESOURCE_TEXTURE && res->u.texture.usage != 0u) {
		/* Caller-specified desc usage is OR'd with inferred bits later. */
	}
	if (res->kind == SK_RG_RESOURCE_BUFFER && res->u.buffer.usage != 0u) {
		/* same */
	}

	dep->resource_name = name;
	dep->access = access;
	dep->usage_flags = usage;
	dep->is_resolve = is_resolve;
	dep->resource_index = res->index;
	dep->next = NULL;

	if (p->deps_tail != NULL) {
		p->deps_tail->next = dep;
	} else {
		p->deps_head = dep;
	}
	p->deps_tail = dep;
	p->dep_count += 1u;

	res->usage |= usage;
	if (res->kind == SK_RG_RESOURCE_TEXTURE) {
		res->u.texture.usage |= usage;
	} else if (res->kind == SK_RG_RESOURCE_BUFFER) {
		res->u.buffer.usage |= usage;
	}

	/* Edges: writer→this for writes and reads; reader→writer when write after read. */
	if (writes) {
		if (res->last_writer_pass != SK_RG_INVALID_INDEX) {
			if (rg_add_edge(g, res->last_writer_pass, p->index) != SK_RG_OK) {
				return;
			}
		}
		if (res->last_reader_pass != SK_RG_INVALID_INDEX && res->last_reader_pass != res->last_writer_pass) {
			if (rg_add_edge(g, res->last_reader_pass, p->index) != SK_RG_OK) {
				return;
			}
		}
		res->last_writer_pass = p->index;
	} else {
		if (res->last_writer_pass != SK_RG_INVALID_INDEX) {
			if (rg_add_edge(g, res->last_writer_pass, p->index) != SK_RG_OK) {
				return;
			}
		}
		res->last_reader_pass = p->index;
	}

	rg_memory_set_error(&g->memory, SK_RG_OK);
}

/* ------------------------------------------------------------------ */
/* Module API implementations                                          */
/* ------------------------------------------------------------------ */

static i32 render_graph_init_impl(void) {
	return 0;
}

static void render_graph_shutdown_impl(void) {}

static sk_render_graph_t* render_graph_create_with_config_impl(sk_render_device_t device, const sk_rg_memory_config_t* config) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* graph;
	const sk_allocator_t* heap = sk_allocator_default();
	i32 rc;

	memset(&cfg, 0, sizeof(cfg));
	if (config != NULL) {
		cfg = *config;
	}

	graph = (sk_render_graph_t*)heap->alloc(heap->instance, sizeof(sk_render_graph_t));
	if (graph == NULL) {
		return NULL;
	}
	memset(graph, 0, sizeof(*graph));
	graph->device = device;

	rc = rg_memory_init(&graph->memory, &cfg);
	if (rc != SK_RG_OK) {
		heap->free(heap->instance, graph);
		return NULL;
	}
	/* create allocates the graph object + pool blocks; count graph object too. */
	graph->memory.heap_alloc_count += 1u;
	return graph;
}

static sk_render_graph_t* render_graph_create_impl(sk_render_device_t device) {
	return render_graph_create_with_config_impl(device, NULL);
}

static void render_graph_destroy_impl(sk_render_graph_t* graph) {
	const sk_allocator_t* heap;
	if (graph == NULL) {
		return;
	}
	heap = graph->memory.heap;
	rg_memory_shutdown(&graph->memory);
	heap->free(heap->instance, graph);
}

static void render_graph_create_texture_impl(sk_render_graph_t* g, const_chr_t name, const sk_rg_texture_desc_t* desc) {
	sk_rg_resource_node_t* node;
	if (desc == NULL) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
		return;
	}
	node = rg_declare_resource(g, name);
	if (node == NULL) {
		return;
	}
	node->kind = SK_RG_RESOURCE_TEXTURE;
	node->u.texture = *desc;
	node->usage = desc->usage;
}

static void render_graph_create_buffer_impl(sk_render_graph_t* g, const_chr_t name, const sk_rg_buffer_desc_t* desc) {
	sk_rg_resource_node_t* node;
	if (desc == NULL) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
		return;
	}
	node = rg_declare_resource(g, name);
	if (node == NULL) {
		return;
	}
	node->kind = SK_RG_RESOURCE_BUFFER;
	node->u.buffer = *desc;
	node->usage = desc->usage;
}

static void render_graph_create_view_impl(sk_render_graph_t* g, const_chr_t name, const sk_rg_view_desc_t* desc) {
	sk_rg_resource_node_t* node;
	if (desc == NULL) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
		return;
	}
	node = rg_declare_resource(g, name);
	if (node == NULL) {
		return;
	}
	node->kind = SK_RG_RESOURCE_VIEW;
	node->u.view = *desc;
}

static void render_graph_import_textures_impl(sk_render_graph_t* g, const_chr_t name, const sk_texture_t* textures, u32 count, sk_resource_state_t state) {
	sk_rg_resource_node_t* node;
	sk_texture_t* copy;
	u32 i;

	node = rg_declare_resource(g, name);
	if (node == NULL) {
		return;
	}
	node->kind = SK_RG_RESOURCE_IMPORTED;
	node->flags |= SK_RG_RESOURCE_FLAG_IMPORTED;
	if (rg_imported_state_is_read_only(state)) {
		node->flags |= SK_RG_RESOURCE_FLAG_READ_ONLY;
	}
	node->u.imported.count = count;
	node->u.imported.state = state;
	node->u.imported.textures = NULL;

	if (count > 0u) {
		if (textures == NULL) {
			rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
			/* Resource already acquired; leave empty import. */
			return;
		}
		copy = (sk_texture_t*)rg_arena_alloc(&g->memory, (u64)count * sizeof(sk_texture_t), sizeof(void_ptr_t));
		if (copy == NULL) {
			return;
		}
		for (i = 0u; i < count; ++i) {
			copy[i] = textures[i];
		}
		node->u.imported.textures = copy;
	}
	rg_memory_set_error(&g->memory, SK_RG_OK);
}

static void_ptr_t render_graph_create_instance_impl(sk_render_graph_t* g, const_chr_t name, u64 size) {
	sk_rg_resource_node_t* node;
	void_ptr_t data;

	node = rg_declare_resource(g, name);
	if (node == NULL) {
		return NULL;
	}
	node->kind = SK_RG_RESOURCE_INSTANCE;
	data = NULL;
	if (size > 0ull) {
		data = rg_arena_alloc(&g->memory, size, sizeof(void_ptr_t));
		if (data == NULL) {
			return NULL;
		}
		memset(data, 0, size);
	}
	node->u.instance.data = data;
	node->u.instance.size = size;
	rg_memory_set_error(&g->memory, SK_RG_OK);
	return data;
}

static void_ptr_t render_graph_get_instance_impl(sk_render_graph_t* g, const_chr_t name) {
	sk_rg_resource_node_t* node = rg_find_resource(g, name);
	if (node == NULL || node->kind != SK_RG_RESOURCE_INSTANCE) {
		return NULL;
	}
	return node->u.instance.data;
}

static sk_rg_pass_t* render_graph_add_pass_impl(sk_render_graph_t* g, const_chr_t name, sk_rg_pass_type_t type) {
	sk_rg_pass_t* pass;

	if (!g->memory.in_frame) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_NOT_IN_FRAME);
		return NULL;
	}
	if (rg_name_empty(name)) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_INVALID_ARGUMENT);
		return NULL;
	}
	if (rg_find_pass_by_name(g, name) != NULL) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_DUPLICATE_NAME);
		return NULL;
	}

	pass = (sk_rg_pass_t*)rg_object_pool_acquire(&g->memory, &g->memory.passes);
	if (pass == NULL) {
		return NULL;
	}
	pass->graph = g;
	pass->name = name;
	pass->type = type;
	pass->stage = 0;
	pass->index = rg_object_pool_index(&g->memory.passes, pass);
	pass->dep_count = 0u;
	pass->deps_head = NULL;
	pass->deps_tail = NULL;
	pass->generation = g->frame_generation;
	return pass;
}

static void render_graph_pass_read_impl(sk_rg_pass_t* p, const_chr_t name) {
	rg_pass_add_dep(p, name, SK_RG_ACCESS_READ, 0);
}

static void render_graph_pass_write_impl(sk_rg_pass_t* p, const_chr_t name) {
	rg_pass_add_dep(p, name, SK_RG_ACCESS_WRITE, 0);
}

static void render_graph_pass_read_write_impl(sk_rg_pass_t* p, const_chr_t name) {
	rg_pass_add_dep(p, name, SK_RG_ACCESS_READ_WRITE, 0);
}

static void render_graph_pass_resolve_impl(sk_rg_pass_t* p, const_chr_t name) {
	rg_pass_add_dep(p, name, SK_RG_ACCESS_WRITE, 1);
}

static void render_graph_pass_stage_impl(sk_rg_pass_t* p, i32 stage) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->stage = stage;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_set_pipeline_impl(sk_rg_pass_t* p, sk_pipeline_t pipeline) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->pipeline = pipeline;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_set_descriptor_set_impl(sk_rg_pass_t* p, u32 set, sk_descriptor_set_t ds) {
	(void)p;
	(void)set;
	(void)ds;
	/* Descriptor binding storage lands with execute. */
}

static void render_graph_pass_set_record_impl(sk_rg_pass_t* p, sk_rg_record_fn fn, void_ptr_t user) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->record_fn = fn;
	p->record_user = user;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_set_resize_impl(sk_rg_pass_t* p, sk_rg_resize_fn fn, void_ptr_t user) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->resize_fn = fn;
	p->resize_user = user;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_set_constants_impl(sk_rg_pass_t* p, u32 size, u32 stage_mask, sk_rg_constants_fn fn, void_ptr_t user) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->constants_size = size;
	p->constants_stage_mask = stage_mask;
	p->constants_fn = fn;
	p->constants_user = user;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_dispatch_impl(sk_rg_pass_t* p, u32 x, u32 y, u32 z) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->dispatch_x = x;
	p->dispatch_y = y;
	p->dispatch_z = z;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_dispatch_indirect_impl(sk_rg_pass_t* p, sk_buffer_t indirect) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->dispatch_indirect = indirect;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static void render_graph_pass_trace_rays_impl(sk_rg_pass_t* p, u32 w, u32 h, u32 d) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	p->dispatch_x = w;
	p->dispatch_y = h;
	p->dispatch_z = d;
	rg_memory_set_error(&p->graph->memory, SK_RG_OK);
}

static sk_texture_t render_graph_get_texture_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_texture_t_zero();
}

static sk_texture_t render_graph_get_prev_texture_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_texture_t_zero();
}

static sk_texture_view_t render_graph_get_texture_view_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_texture_view_t_zero();
}

static sk_buffer_t render_graph_get_buffer_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_buffer_t_zero();
}

static sk_buffer_t render_graph_get_prev_buffer_impl(const sk_render_graph_t* g, const_chr_t name) {
	(void)g;
	(void)name;
	return sk_buffer_t_zero();
}

static void render_graph_set_color_output_impl(sk_render_graph_t* g, const_chr_t name) {
	sk_rg_resource_node_t* res;
	g->color_output = name;
	res = rg_find_resource(g, name);
	if (res != NULL) {
		res->flags |= SK_RG_RESOURCE_FLAG_COLOR_OUTPUT;
	}
	rg_memory_set_error(&g->memory, SK_RG_OK);
}

static void render_graph_set_depth_output_impl(sk_render_graph_t* g, const_chr_t name) {
	sk_rg_resource_node_t* res;
	g->depth_output = name;
	res = rg_find_resource(g, name);
	if (res != NULL) {
		res->flags |= SK_RG_RESOURCE_FLAG_DEPTH_OUTPUT;
	}
	rg_memory_set_error(&g->memory, SK_RG_OK);
}

static void render_graph_set_output_size_impl(sk_render_graph_t* g, sk_rg_extent_t size) {
	g->output_size = size;
}

static sk_rg_extent_t render_graph_get_output_size_impl(const sk_render_graph_t* g) {
	if (g == NULL) {
		sk_rg_extent_t zero = {0u, 0u};
		return zero;
	}
	return g->output_size;
}

static void render_graph_set_current_output_index_impl(sk_render_graph_t* g, u32 index) {
	g->current_output_index = index;
}

static void render_graph_begin_impl(sk_render_graph_t* g, void_ptr_t scene) {
	g->scene = scene;
	g->frame_generation += 1u;
	if (g->frame_generation == 0u) {
		g->frame_generation = 1u;
	}
	g->color_output = NULL;
	g->depth_output = NULL;
	rg_memory_begin_frame(&g->memory);
}

static void render_graph_end_impl(sk_render_graph_t* g) {
	rg_memory_end_frame(&g->memory);
}

static void render_graph_execute_impl(sk_render_graph_t* g, sk_command_buffer_t cmd) {
	(void)cmd;
	/* Topology / barriers / record land in later tasks. End frame so growth is allowed again. */
	rg_memory_end_frame(&g->memory);
}

static u32 render_graph_topology_build_count_impl(const sk_render_graph_t* g) {
	if (g == NULL) {
		return 0u;
	}
	return g->topology_build_count;
}

static void render_graph_get_memory_stats_impl(const sk_render_graph_t* g, sk_rg_memory_stats_t* out) {
	rg_memory_fill_stats(&g->memory, out);
}

static i32 render_graph_get_last_error_impl(const sk_render_graph_t* g) {
	if (g == NULL) {
		return SK_RG_OK;
	}
	return g->memory.last_error;
}

static u32 render_graph_get_pass_count_impl(const sk_render_graph_t* g) {
	if (g == NULL) {
		return 0u;
	}
	return g->memory.passes.live;
}

static i32 render_graph_get_pass_info_impl(const sk_render_graph_t* g, u32 index, sk_rg_pass_info_t* out) {
	const sk_rg_pass_t* pass;
	if (g == NULL || out == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	pass = rg_pass_at(g, index);
	if (pass == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));
	out->name = pass->name;
	out->type = pass->type;
	out->stage = pass->stage;
	out->index = pass->index;
	out->dep_count = pass->dep_count;
	out->record_fn = pass->record_fn;
	out->record_user = pass->record_user;
	return SK_RG_OK;
}

static u32 render_graph_get_resource_count_impl(const sk_render_graph_t* g) {
	if (g == NULL) {
		return 0u;
	}
	return g->memory.resources.live;
}

static i32 render_graph_get_resource_info_impl(const sk_render_graph_t* g, u32 index, sk_rg_resource_info_t* out) {
	const sk_rg_resource_node_t* res;
	if (g == NULL || out == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	res = rg_resource_at(g, index);
	if (res == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));
	out->name = res->name;
	out->kind = res->kind;
	out->flags = res->flags;
	out->index = res->index;
	out->usage = res->usage;
	out->last_writer_pass = res->last_writer_pass;
	if (res->kind == SK_RG_RESOURCE_TEXTURE) {
		out->texture = res->u.texture;
	} else if (res->kind == SK_RG_RESOURCE_BUFFER) {
		out->buffer = res->u.buffer;
	} else if (res->kind == SK_RG_RESOURCE_IMPORTED) {
		out->imported_count = res->u.imported.count;
		out->imported_state = res->u.imported.state;
	}
	return SK_RG_OK;
}

static i32 render_graph_get_pass_dep_info_impl(const sk_render_graph_t* g, u32 pass_index, u32 dep_index, sk_rg_dep_info_t* out) {
	const sk_rg_pass_t* pass;
	const sk_rg_dep_node_t* dep;
	u32 i;

	if (g == NULL || out == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	pass = rg_pass_at(g, pass_index);
	if (pass == NULL || dep_index >= pass->dep_count) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	dep = pass->deps_head;
	for (i = 0u; i < dep_index && dep != NULL; ++i) {
		dep = dep->next;
	}
	if (dep == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	memset(out, 0, sizeof(*out));
	out->resource_name = dep->resource_name;
	out->access = dep->access;
	out->usage_flags = dep->usage_flags;
	out->is_resolve = dep->is_resolve;
	out->resource_index = dep->resource_index;
	return SK_RG_OK;
}

static u32 render_graph_get_edge_count_impl(const sk_render_graph_t* g) {
	if (g == NULL) {
		return 0u;
	}
	return g->memory.edges.count;
}

static i32 render_graph_get_edge_info_impl(const sk_render_graph_t* g, u32 index, sk_rg_edge_info_t* out) {
	const sk_rg_edge_t* edge;
	if (g == NULL || out == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	edge = (const sk_rg_edge_t*)rg_array_pool_at(&g->memory.edges, index);
	if (edge == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	out->from_pass = edge->from;
	out->to_pass = edge->to;
	return SK_RG_OK;
}

/* ------------------------------------------------------------------ */
/* Static function table                                               */
/* ------------------------------------------------------------------ */

static const sk_render_graph_api_t render_graph_api = {
	render_graph_init_impl,
	render_graph_shutdown_impl,
	render_graph_create_impl,
	render_graph_create_with_config_impl,
	render_graph_destroy_impl,
	render_graph_create_texture_impl,
	render_graph_create_buffer_impl,
	render_graph_create_view_impl,
	render_graph_import_textures_impl,
	render_graph_create_instance_impl,
	render_graph_get_instance_impl,
	render_graph_add_pass_impl,
	render_graph_pass_read_impl,
	render_graph_pass_write_impl,
	render_graph_pass_read_write_impl,
	render_graph_pass_resolve_impl,
	render_graph_pass_stage_impl,
	render_graph_pass_set_pipeline_impl,
	render_graph_pass_set_descriptor_set_impl,
	render_graph_pass_set_record_impl,
	render_graph_pass_set_resize_impl,
	render_graph_pass_set_constants_impl,
	render_graph_pass_dispatch_impl,
	render_graph_pass_dispatch_indirect_impl,
	render_graph_pass_trace_rays_impl,
	render_graph_get_texture_impl,
	render_graph_get_prev_texture_impl,
	render_graph_get_texture_view_impl,
	render_graph_get_buffer_impl,
	render_graph_get_prev_buffer_impl,
	render_graph_set_color_output_impl,
	render_graph_set_depth_output_impl,
	render_graph_set_output_size_impl,
	render_graph_get_output_size_impl,
	render_graph_set_current_output_index_impl,
	render_graph_begin_impl,
	render_graph_end_impl,
	render_graph_execute_impl,
	render_graph_topology_build_count_impl,
	render_graph_get_memory_stats_impl,
	render_graph_get_last_error_impl,
	render_graph_get_pass_count_impl,
	render_graph_get_pass_info_impl,
	render_graph_get_resource_count_impl,
	render_graph_get_resource_info_impl,
	render_graph_get_pass_dep_info_impl,
	render_graph_get_edge_count_impl,
	render_graph_get_edge_info_impl,
};

void sk_render_graph_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_RENDER_GRAPH_API_TYPE_ID, (const_ptr_t)&render_graph_api);
}

#ifdef SK_TESTS
#include "test.h"

/* ------------------------------------------------------------------ */
/* Helpers for tests                                                   */
/* ------------------------------------------------------------------ */

static sk_rg_texture_desc_t rg_test_tex_desc(void) {
	sk_rg_texture_desc_t d;
	memset(&d, 0, sizeof(d));
	d.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
	d.extent.width = 128u;
	d.extent.height = 128u;
	d.extent.depth = 1u;
	d.scale_x = 1.0f;
	d.scale_y = 1.0f;
	d.array_layers = 1u;
	d.samples = 1u;
	d.mip_levels = 1u;
	return d;
}

static sk_rg_buffer_desc_t rg_test_buf_desc(u64 size) {
	sk_rg_buffer_desc_t d;
	memset(&d, 0, sizeof(d));
	d.size = size;
	return d;
}

static void rg_test_record_fn(sk_rg_pass_t* pass, void_ptr_t scene, sk_command_buffer_t cmd, void_ptr_t user) {
	(void)pass;
	(void)scene;
	(void)cmd;
	(void)user;
}

/* File-scope so &cfg is not a local address (avoids cppcheck returnDanglingLifetime FP:
 * create_with_config copies *config and returns a heap graph, not a pointer into cfg). */
static const sk_rg_memory_config_t rg_test_default_cfg = {
	.frame_arena_bytes = 32ull * 1024ull,
	.pass_capacity = 32u,
	.resource_capacity = 64u,
	.edge_capacity = 128u,
	.barrier_capacity = 64u,
};

static sk_render_graph_t* rg_test_create_graph(void) {
	return render_graph_api.create_with_config(sk_render_device_t_zero(), &rg_test_default_cfg);
}

static i32 rg_test_has_edge(const sk_render_graph_t* g, u32 from, u32 to) {
	u32 i;
	for (i = 0u; i < render_graph_api.get_edge_count(g); ++i) {
		sk_rg_edge_info_t e;
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_edge_info(g, i, &e));
		if (e.from_pass == from && e.to_pass == to) {
			return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* API surface smoke                                                   */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_api_table_is_complete) {
	TEST_ASSERT_NOT_NULL(render_graph_api.init);
	TEST_ASSERT_NOT_NULL(render_graph_api.shutdown);
	TEST_ASSERT_NOT_NULL(render_graph_api.create);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_with_config);
	TEST_ASSERT_NOT_NULL(render_graph_api.destroy);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_texture);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_buffer);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_view);
	TEST_ASSERT_NOT_NULL(render_graph_api.import_textures);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_instance);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_instance);
	TEST_ASSERT_NOT_NULL(render_graph_api.add_pass);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_read);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_write);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_read_write);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_resolve);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_stage);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_pipeline);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_descriptor_set);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_record);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_resize);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_constants);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_dispatch);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_dispatch_indirect);
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_trace_rays);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_texture);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_prev_texture);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_texture_view);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_buffer);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_prev_buffer);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_color_output);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_depth_output);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_output_size);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_output_size);
	TEST_ASSERT_NOT_NULL(render_graph_api.set_current_output_index);
	TEST_ASSERT_NOT_NULL(render_graph_api.begin);
	TEST_ASSERT_NOT_NULL(render_graph_api.end);
	TEST_ASSERT_NOT_NULL(render_graph_api.execute);
	TEST_ASSERT_NOT_NULL(render_graph_api.topology_build_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_memory_stats);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_last_error);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_pass_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_pass_info);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_resource_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_resource_info);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_pass_dep_info);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_edge_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_edge_info);
}

SK_TEST(render_graph_api_type_id_nonzero) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_RENDER_GRAPH_API_TYPE_ID, SK_TYPE_ID_ZERO));
}

SK_TEST(render_graph_create_destroy_defaults) {
	sk_render_graph_t* g;
	sk_rg_memory_stats_t stats;

	TEST_ASSERT_EQUAL_INT(0, render_graph_api.init());
	g = render_graph_api.create(sk_render_device_t_zero());
	TEST_ASSERT_NOT_NULL(g);

	memset(&stats, 0, sizeof(stats));
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT64(SK_RG_DEFAULT_FRAME_ARENA_BYTES, stats.frame_arena_capacity);
	TEST_ASSERT_EQUAL_UINT32(SK_RG_DEFAULT_PASS_CAPACITY, stats.pass_capacity);
	TEST_ASSERT_EQUAL_UINT32(SK_RG_DEFAULT_RESOURCE_CAPACITY, stats.resource_capacity);
	TEST_ASSERT_EQUAL_UINT32(SK_RG_DEFAULT_EDGE_CAPACITY, stats.edge_capacity);
	TEST_ASSERT_EQUAL_UINT32(SK_RG_DEFAULT_BARRIER_CAPACITY, stats.barrier_capacity);
	TEST_ASSERT_TRUE(stats.heap_alloc_count >= 1u);
	TEST_ASSERT_EQUAL_INT(0, stats.in_frame);

	render_graph_api.destroy(g);
	render_graph_api.shutdown();
}

/* ------------------------------------------------------------------ */
/* Arena: alignment                                                    */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_arena_alignment) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	void_ptr_t p1;
	void_ptr_t p2;
	void_ptr_t p16;
	uintptr_t a1;
	uintptr_t a2;
	uintptr_t a16;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 4096ull;
	cfg.pass_capacity = 4u;
	cfg.resource_capacity = 4u;
	cfg.edge_capacity = 4u;
	cfg.barrier_capacity = 4u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	p1 = rg_arena_alloc(&g->memory, 1ull, 1ull);
	p2 = rg_arena_alloc(&g->memory, 1ull, 8ull);
	p16 = rg_arena_alloc(&g->memory, 32ull, 16ull);
	TEST_ASSERT_NOT_NULL(p1);
	TEST_ASSERT_NOT_NULL(p2);
	TEST_ASSERT_NOT_NULL(p16);

	a1 = (uintptr_t)p1;
	a2 = (uintptr_t)p2;
	a16 = (uintptr_t)p16;
	TEST_ASSERT_EQUAL_UINT64(0ull, (u64)(a1 % 1u));
	TEST_ASSERT_EQUAL_UINT64(0ull, (u64)(a2 % 8u));
	TEST_ASSERT_EQUAL_UINT64(0ull, (u64)(a16 % 16u));
	TEST_ASSERT_TRUE(a2 >= a1);
	TEST_ASSERT_TRUE(a16 >= a2);

	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Arena: reset semantics                                              */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_arena_reset_semantics) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_memory_stats_t stats;
	void_ptr_t p;
	u64 used_after_alloc;
	u64 high_after_alloc;
	u32 heap_before_second_begin;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 1024ull;
	cfg.pass_capacity = 4u;
	cfg.resource_capacity = 4u;
	cfg.edge_capacity = 4u;
	cfg.barrier_capacity = 4u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);

	render_graph_api.begin(g, NULL);
	p = rg_arena_alloc(&g->memory, 200ull, 8ull);
	TEST_ASSERT_NOT_NULL(p);
	render_graph_api.get_memory_stats(g, &stats);
	used_after_alloc = stats.frame_arena_used;
	high_after_alloc = stats.frame_arena_high_water;
	TEST_ASSERT_TRUE(used_after_alloc >= 200ull);
	TEST_ASSERT_EQUAL_UINT64(used_after_alloc, high_after_alloc);
	heap_before_second_begin = stats.heap_alloc_count;

	/* begin resets offset; capacity and high-water retained; no free/realloc. */
	render_graph_api.begin(g, NULL);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT64(0ull, stats.frame_arena_used);
	TEST_ASSERT_EQUAL_UINT64(high_after_alloc, stats.frame_arena_high_water);
	TEST_ASSERT_EQUAL_UINT64(1024ull, stats.frame_arena_capacity);
	TEST_ASSERT_EQUAL_UINT32(heap_before_second_begin, stats.heap_alloc_count);

	/* Same region can be reused after reset. */
	p = rg_arena_alloc(&g->memory, 200ull, 8ull);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_EQUAL_UINT32(heap_before_second_begin, g->memory.heap_alloc_count);

	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Arena: high-water tracking                                          */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_arena_high_water_tracking) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_memory_stats_t stats;
	u64 hw1;
	u64 hw2;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 2048ull;
	cfg.pass_capacity = 4u;
	cfg.resource_capacity = 4u;
	cfg.edge_capacity = 4u;
	cfg.barrier_capacity = 4u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	TEST_ASSERT_NOT_NULL(rg_arena_alloc(&g->memory, 100ull, 1ull));
	render_graph_api.get_memory_stats(g, &stats);
	hw1 = stats.frame_arena_high_water;
	TEST_ASSERT_TRUE(hw1 >= 100ull);

	TEST_ASSERT_NOT_NULL(rg_arena_alloc(&g->memory, 300ull, 1ull));
	render_graph_api.get_memory_stats(g, &stats);
	hw2 = stats.frame_arena_high_water;
	TEST_ASSERT_TRUE(hw2 > hw1);

	/* Reset does not lower high-water. */
	rg_arena_reset(&g->memory.arena);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT64(0ull, stats.frame_arena_used);
	TEST_ASSERT_EQUAL_UINT64(hw2, stats.frame_arena_high_water);

	/* Smaller alloc after reset keeps previous high-water. */
	TEST_ASSERT_NOT_NULL(rg_arena_alloc(&g->memory, 50ull, 1ull));
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT64(hw2, stats.frame_arena_high_water);

	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Arena: sub-arena / marker rollback                                  */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_arena_sub_arena_rollback) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_arena_marker_t mark;
	sk_rg_memory_stats_t stats;
	u64 used_before_scratch;
	void_ptr_t permanent;
	void_ptr_t scratch;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 2048ull;
	cfg.pass_capacity = 4u;
	cfg.resource_capacity = 4u;
	cfg.edge_capacity = 4u;
	cfg.barrier_capacity = 4u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	permanent = rg_arena_alloc(&g->memory, 64ull, 8ull);
	TEST_ASSERT_NOT_NULL(permanent);
	render_graph_api.get_memory_stats(g, &stats);
	used_before_scratch = stats.frame_arena_used;

	mark = rg_arena_push(&g->memory.arena);
	scratch = rg_arena_alloc(&g->memory, 400ull, 16ull);
	TEST_ASSERT_NOT_NULL(scratch);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_TRUE(stats.frame_arena_used > used_before_scratch);

	rg_arena_pop(&g->memory.arena, mark);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT64(used_before_scratch, stats.frame_arena_used);
	/* High-water retains scratch peak. */
	TEST_ASSERT_TRUE(stats.frame_arena_high_water > used_before_scratch);

	/* After rollback, next alloc reuses scratch region. */
	scratch = rg_arena_alloc(&g->memory, 16ull, 8ull);
	TEST_ASSERT_NOT_NULL(scratch);

	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Capacity growth outside a frame                                     */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_capacity_growth_outside_frame) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_memory_stats_t stats;
	u32 growth_before;
	u32 heap_before;
	i32 rc;
	sk_rg_edge_t edge;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 512ull;
	cfg.pass_capacity = 2u;
	cfg.resource_capacity = 2u;
	cfg.edge_capacity = 2u;
	cfg.barrier_capacity = 2u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);

	/* Not in frame: growth is allowed. */
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_INT(0, stats.in_frame);
	growth_before = stats.growth_events;
	heap_before = stats.heap_alloc_count;

	rc = rg_arena_grow(&g->memory, 2048ull);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rc);
	rc = rg_object_pool_grow(&g->memory, &g->memory.passes, 8u);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rc);
	rc = rg_array_pool_grow(&g->memory, &g->memory.edges, 16u);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rc);

	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT64(2048ull, stats.frame_arena_capacity);
	TEST_ASSERT_EQUAL_UINT32(8u, stats.pass_capacity);
	TEST_ASSERT_EQUAL_UINT32(16u, stats.edge_capacity);
	TEST_ASSERT_TRUE(stats.growth_events >= growth_before + 3u);
	TEST_ASSERT_TRUE(stats.heap_alloc_count > heap_before);

	/* Steady-state: after grow, frame work does not allocate. */
	heap_before = stats.heap_alloc_count;
	render_graph_api.begin(g, NULL);
	TEST_ASSERT_NOT_NULL(render_graph_api.add_pass(g, "a", SK_RG_PASS_COMPUTE));
	edge.from = 0u;
	edge.to = 1u;
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg_array_pool_push(&g->memory, &g->memory.edges, &edge));
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(heap_before, stats.heap_alloc_count);
	render_graph_api.execute(g, sk_command_buffer_t_zero());

	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Out-of-space is a defined error (no silent malloc)                  */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_out_of_space_is_defined_error) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_memory_stats_t stats;
	u32 heap_at_begin;
	void_ptr_t p;
	sk_rg_pass_t* pass;
	sk_rg_edge_t edge;
	u32 i;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 64ull;
	cfg.pass_capacity = 2u;
	cfg.resource_capacity = 2u;
	cfg.edge_capacity = 2u;
	cfg.barrier_capacity = 2u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.get_memory_stats(g, &stats);
	heap_at_begin = stats.heap_alloc_count;

	/* Arena exhaustion. */
	p = rg_arena_alloc(&g->memory, 32ull, 1ull);
	TEST_ASSERT_NOT_NULL(p);
	p = rg_arena_alloc(&g->memory, 64ull, 1ull);
	TEST_ASSERT_NULL(p);
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_OUT_OF_SPACE, g->memory.last_error);
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, g->memory.heap_alloc_count);

	/* Pass pool exhaustion. */
	TEST_ASSERT_NOT_NULL(render_graph_api.add_pass(g, "p0", SK_RG_PASS_GRAPHICS));
	TEST_ASSERT_NOT_NULL(render_graph_api.add_pass(g, "p1", SK_RG_PASS_GRAPHICS));
	pass = render_graph_api.add_pass(g, "p2", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NULL(pass);
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_OUT_OF_SPACE, g->memory.last_error);
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, g->memory.heap_alloc_count);

	/* Edge array exhaustion. */
	edge.from = 0u;
	edge.to = 1u;
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg_array_pool_push(&g->memory, &g->memory.edges, &edge));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg_array_pool_push(&g->memory, &g->memory.edges, &edge));
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_OUT_OF_SPACE, rg_array_pool_push(&g->memory, &g->memory.edges, &edge));
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, g->memory.heap_alloc_count);

	/* Growth blocked mid-frame. */
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_GROW_BLOCKED, rg_arena_grow(&g->memory, 4096ull));
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_GROW_BLOCKED, rg_object_pool_grow(&g->memory, &g->memory.passes, 64u));
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_GROW_BLOCKED, rg_array_pool_grow(&g->memory, &g->memory.edges, 64u));
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, g->memory.heap_alloc_count);

	/* After execute, growth works again; remembered capacities used next frame. */
	render_graph_api.execute(g, sk_command_buffer_t_zero());
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg_array_pool_grow(&g->memory, &g->memory.edges, 8u));

	render_graph_api.begin(g, NULL);
	for (i = 0u; i < 8u; ++i) {
		edge.from = i;
		edge.to = i + 1u;
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg_array_pool_push(&g->memory, &g->memory.edges, &edge));
	}
	/* Still no mid-frame heap growth for the push path. */
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(8u, stats.edge_count);
	TEST_ASSERT_EQUAL_UINT32(8u, stats.edge_capacity);

	render_graph_api.destroy(g);
}

SK_TEST(render_graph_begin_resets_pools_and_marks_in_frame) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_memory_stats_t stats;
	sk_rg_edge_t edge;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 256ull;
	cfg.pass_capacity = 8u;
	cfg.resource_capacity = 8u;
	cfg.edge_capacity = 8u;
	cfg.barrier_capacity = 8u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);

	render_graph_api.begin(g, NULL);
	TEST_ASSERT_NOT_NULL(render_graph_api.add_pass(g, "x", SK_RG_PASS_TRANSFER));
	TEST_ASSERT_NOT_NULL(rg_object_pool_acquire(&g->memory, &g->memory.resources));
	edge.from = 0u;
	edge.to = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, rg_array_pool_push(&g->memory, &g->memory.edges, &edge));
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_INT(1, stats.in_frame);
	TEST_ASSERT_EQUAL_UINT32(1u, stats.pass_live);
	TEST_ASSERT_EQUAL_UINT32(1u, stats.resource_live);
	TEST_ASSERT_EQUAL_UINT32(1u, stats.edge_count);

	render_graph_api.begin(g, NULL);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_INT(1, stats.in_frame);
	TEST_ASSERT_EQUAL_UINT32(0u, stats.pass_live);
	TEST_ASSERT_EQUAL_UINT32(0u, stats.resource_live);
	TEST_ASSERT_EQUAL_UINT32(0u, stats.edge_count);
	/* Capacities remembered. */
	TEST_ASSERT_EQUAL_UINT32(8u, stats.pass_capacity);
	TEST_ASSERT_EQUAL_UINT32(8u, stats.edge_capacity);

	render_graph_api.destroy(g);
}

SK_TEST(render_graph_stub_accessors_return_zero) {
	TEST_ASSERT_FALSE(sk_texture_t_is_valid(render_graph_api.get_texture(NULL, "color")));
	TEST_ASSERT_FALSE(sk_buffer_t_is_valid(render_graph_api.get_buffer(NULL, "buf")));
	TEST_ASSERT_EQUAL_UINT32(0u, render_graph_api.topology_build_count(NULL));
	{
		const sk_rg_extent_t extent = render_graph_api.get_output_size(NULL);
		TEST_ASSERT_EQUAL_UINT32(0u, extent.width);
		TEST_ASSERT_EQUAL_UINT32(0u, extent.height);
	}
}

/* ------------------------------------------------------------------ */
/* Build phase: representative graphs                                  */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_build_single_pass) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* pass;
	sk_rg_pass_info_t pi;
	sk_rg_resource_info_t ri;
	sk_rg_dep_info_t di;
	i32 marker = 42;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	render_graph_api.create_texture(g, "color", &tex);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));

	pass = render_graph_api.add_pass(g, "main", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(pass);
	render_graph_api.pass_write(pass, "color");
	render_graph_api.pass_set_record(pass, rg_test_record_fn, &marker);
	render_graph_api.pass_stage(pass, 100);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));

	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_resource_count(g));
	TEST_ASSERT_EQUAL_UINT32(0u, render_graph_api.get_edge_count(g));

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, 0u, &pi));
	TEST_ASSERT_EQUAL_STRING("main", pi.name);
	TEST_ASSERT_EQUAL_INT(SK_RG_PASS_GRAPHICS, pi.type);
	TEST_ASSERT_EQUAL_INT(100, pi.stage);
	TEST_ASSERT_EQUAL_UINT32(1u, pi.dep_count);
	TEST_ASSERT_EQUAL_PTR(rg_test_record_fn, pi.record_fn);
	TEST_ASSERT_EQUAL_PTR(&marker, pi.record_user);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_info(g, 0u, &ri));
	TEST_ASSERT_EQUAL_STRING("color", ri.name);
	TEST_ASSERT_EQUAL_INT(SK_RG_RESOURCE_TEXTURE, ri.kind);
	TEST_ASSERT_EQUAL_UINT32(0u, ri.last_writer_pass);
	TEST_ASSERT_TRUE((ri.usage & SK_RESOURCE_USAGE_RENDER_TARGET) != 0u);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_dep_info(g, 0u, 0u, &di));
	TEST_ASSERT_EQUAL_STRING("color", di.resource_name);
	TEST_ASSERT_EQUAL_INT(SK_RG_ACCESS_WRITE, di.access);
	TEST_ASSERT_EQUAL_UINT32(0u, di.resource_index);

	render_graph_api.end(g);
	TEST_ASSERT_EQUAL_INT(0, g->memory.in_frame);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_build_linear_chain) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	render_graph_api.create_texture(g, "r0", &tex);
	render_graph_api.create_texture(g, "r1", &tex);
	render_graph_api.create_texture(g, "r2", &tex);

	a = render_graph_api.add_pass(g, "pass_a", SK_RG_PASS_COMPUTE);
	b = render_graph_api.add_pass(g, "pass_b", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "pass_c", SK_RG_PASS_COMPUTE);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_NOT_NULL(c);

	render_graph_api.pass_write(a, "r0");
	render_graph_api.pass_read(b, "r0");
	render_graph_api.pass_write(b, "r1");
	render_graph_api.pass_read(c, "r1");
	render_graph_api.pass_write(c, "r2");

	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_resource_count(g));
	TEST_ASSERT_TRUE(rg_test_has_edge(g, 0u, 1u));
	TEST_ASSERT_TRUE(rg_test_has_edge(g, 1u, 2u));
	TEST_ASSERT_FALSE(rg_test_has_edge(g, 0u, 2u));

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_build_diamond) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;
	sk_rg_pass_t* d;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	render_graph_api.create_texture(g, "shared", &tex);
	render_graph_api.create_texture(g, "left", &tex);
	render_graph_api.create_texture(g, "right", &tex);
	render_graph_api.create_texture(g, "merged", &tex);

	a = render_graph_api.add_pass(g, "root", SK_RG_PASS_GRAPHICS);
	b = render_graph_api.add_pass(g, "branch_l", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "branch_r", SK_RG_PASS_COMPUTE);
	d = render_graph_api.add_pass(g, "merge", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_NOT_NULL(c);
	TEST_ASSERT_NOT_NULL(d);

	render_graph_api.pass_write(a, "shared");
	render_graph_api.pass_read(b, "shared");
	render_graph_api.pass_write(b, "left");
	render_graph_api.pass_read(c, "shared");
	render_graph_api.pass_write(c, "right");
	render_graph_api.pass_read(d, "left");
	render_graph_api.pass_read(d, "right");
	render_graph_api.pass_write(d, "merged");

	TEST_ASSERT_TRUE(rg_test_has_edge(g, 0u, 1u)); /* root → branch_l */
	TEST_ASSERT_TRUE(rg_test_has_edge(g, 0u, 2u)); /* root → branch_r */
	TEST_ASSERT_TRUE(rg_test_has_edge(g, 1u, 3u)); /* branch_l → merge */
	TEST_ASSERT_TRUE(rg_test_has_edge(g, 2u, 3u)); /* branch_r → merge */
	TEST_ASSERT_EQUAL_UINT32(4u, render_graph_api.get_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(4u, render_graph_api.get_edge_count(g));

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_build_multiple_writers) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;
	sk_rg_resource_info_t ri;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	render_graph_api.create_texture(g, "target", &tex);
	a = render_graph_api.add_pass(g, "w0", SK_RG_PASS_COMPUTE);
	b = render_graph_api.add_pass(g, "w1", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "w2", SK_RG_PASS_COMPUTE);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_NOT_NULL(c);

	render_graph_api.pass_write(a, "target");
	render_graph_api.pass_write(b, "target");
	render_graph_api.pass_write(c, "target");

	TEST_ASSERT_TRUE(rg_test_has_edge(g, 0u, 1u));
	TEST_ASSERT_TRUE(rg_test_has_edge(g, 1u, 2u));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_info(g, 0u, &ri));
	TEST_ASSERT_EQUAL_UINT32(2u, ri.last_writer_pass);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_build_imported_backbuffer) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_texture_t images[2];
	sk_rg_pass_t* pass;
	sk_rg_resource_info_t ri;
	sk_rg_dep_info_t di;

	images[0] = sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)0x1111u);
	images[1] = sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)0x2222u);

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	/* Writable import (color attachment) for backbuffer write. */
	render_graph_api.import_textures(g, "backbuffer", images, 2u, SK_RESOURCE_STATE_COLOR_ATTACHMENT);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));

	pass = render_graph_api.add_pass(g, "present_pass", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(pass);
	render_graph_api.pass_write(pass, "backbuffer");
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));
	render_graph_api.set_color_output(g, "backbuffer");
	render_graph_api.set_current_output_index(g, 1u);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_info(g, 0u, &ri));
	TEST_ASSERT_EQUAL_STRING("backbuffer", ri.name);
	TEST_ASSERT_EQUAL_INT(SK_RG_RESOURCE_IMPORTED, ri.kind);
	TEST_ASSERT_TRUE((ri.flags & SK_RG_RESOURCE_FLAG_IMPORTED) != 0u);
	TEST_ASSERT_TRUE((ri.flags & SK_RG_RESOURCE_FLAG_COLOR_OUTPUT) != 0u);
	TEST_ASSERT_EQUAL_UINT32(2u, ri.imported_count);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_STATE_COLOR_ATTACHMENT, ri.imported_state);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_dep_info(g, 0u, 0u, &di));
	TEST_ASSERT_EQUAL_STRING("backbuffer", di.resource_name);
	TEST_ASSERT_EQUAL_INT(SK_RG_ACCESS_WRITE, di.access);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Build phase: validation errors                                      */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_validate_duplicate_resource_name) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "dup", &tex);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));
	render_graph_api.create_texture(g, "dup", &tex);
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_DUPLICATE_NAME, render_graph_api.get_last_error(g));
	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_resource_count(g));
	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_validate_write_imported_read_only) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_texture_t img = sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)0xabcdu);
	sk_rg_pass_t* pass;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.import_textures(g, "ro_bb", &img, 1u, SK_RESOURCE_STATE_PRESENT);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));

	pass = render_graph_api.add_pass(g, "bad_write", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(pass);
	render_graph_api.pass_write(pass, "ro_bb");
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_WRITE_READ_ONLY, render_graph_api.get_last_error(g));
	TEST_ASSERT_EQUAL_UINT32(0u, pass->dep_count);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_validate_outside_frame_and_pass_scope) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* pass;
	sk_rg_pass_t* stale;

	TEST_ASSERT_NOT_NULL(g);

	/* Resource declare outside begin..end. */
	render_graph_api.create_texture(g, "too_early", &tex);
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_NOT_IN_FRAME, render_graph_api.get_last_error(g));
	TEST_ASSERT_NULL(render_graph_api.add_pass(g, "p", SK_RG_PASS_COMPUTE));
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_NOT_IN_FRAME, render_graph_api.get_last_error(g));

	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "color", &tex);
	pass = render_graph_api.add_pass(g, "ok", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(pass);
	stale = pass;
	render_graph_api.pass_write(pass, "color");
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));
	render_graph_api.end(g);

	/* Dependency on pass after end (outside pass/frame scope). */
	render_graph_api.pass_read(stale, "color");
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_INVALID_PASS, render_graph_api.get_last_error(g));

	render_graph_api.destroy(g);
}

SK_TEST(render_graph_validate_capacity_exceeded) {
	sk_rg_memory_config_t cfg;
	sk_render_graph_t* g;
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* pass;

	memset(&cfg, 0, sizeof(cfg));
	cfg.frame_arena_bytes = 4096ull;
	cfg.pass_capacity = 1u;
	cfg.resource_capacity = 1u;
	cfg.edge_capacity = 1u;
	cfg.barrier_capacity = 1u;

	g = render_graph_api.create_with_config(sk_render_device_t_zero(), &cfg);
	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);

	render_graph_api.create_texture(g, "r0", &tex);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));
	render_graph_api.create_texture(g, "r1", &tex);
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_OUT_OF_SPACE, render_graph_api.get_last_error(g));

	pass = render_graph_api.add_pass(g, "p0", SK_RG_PASS_COMPUTE);
	TEST_ASSERT_NOT_NULL(pass);
	TEST_ASSERT_NULL(render_graph_api.add_pass(g, "p1", SK_RG_PASS_COMPUTE));
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_OUT_OF_SPACE, render_graph_api.get_last_error(g));

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_validate_unknown_resource_dep) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_pass_t* pass;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	pass = render_graph_api.add_pass(g, "p", SK_RG_PASS_COMPUTE);
	TEST_ASSERT_NOT_NULL(pass);
	render_graph_api.pass_read(pass, "missing");
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_UNKNOWN_RESOURCE, render_graph_api.get_last_error(g));
	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

/* ------------------------------------------------------------------ */
/* Build phase: zero heap allocations                                  */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_build_phase_zero_heap_allocs) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_memory_stats_t stats;
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_buffer_desc_t buf = rg_test_buf_desc(256ull);
	sk_texture_t images[2];
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;
	sk_rg_pass_t* d;
	u32 heap_at_begin;
	i32 user = 7;

	images[0] = sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)0x10u);
	images[1] = sk_texture_t_from_ptr((void_ptr_t)(uintptr_t)0x20u);

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.get_memory_stats(g, &stats);
	/* Warm create is allowed to allocate; capture after create. */
	heap_at_begin = stats.heap_alloc_count;

	render_graph_api.begin(g, NULL);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, stats.heap_alloc_count);

	render_graph_api.create_texture(g, "shared", &tex);
	render_graph_api.create_texture(g, "left", &tex);
	render_graph_api.create_texture(g, "right", &tex);
	render_graph_api.create_buffer(g, "scratch", &buf);
	render_graph_api.import_textures(g, "backbuffer", images, 2u, SK_RESOURCE_STATE_COLOR_ATTACHMENT);
	TEST_ASSERT_NOT_NULL(render_graph_api.create_instance(g, "blackboard", 64ull));

	a = render_graph_api.add_pass(g, "root", SK_RG_PASS_GRAPHICS);
	b = render_graph_api.add_pass(g, "left_p", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "right_p", SK_RG_PASS_COMPUTE);
	d = render_graph_api.add_pass(g, "merge", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_NOT_NULL(c);
	TEST_ASSERT_NOT_NULL(d);

	render_graph_api.pass_write(a, "shared");
	render_graph_api.pass_set_record(a, rg_test_record_fn, &user);
	render_graph_api.pass_read(b, "shared");
	render_graph_api.pass_write(b, "left");
	render_graph_api.pass_read_write(b, "scratch");
	render_graph_api.pass_read(c, "shared");
	render_graph_api.pass_write(c, "right");
	render_graph_api.pass_read(d, "left");
	render_graph_api.pass_read(d, "right");
	render_graph_api.pass_write(d, "backbuffer");
	render_graph_api.set_color_output(g, "backbuffer");

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));
	TEST_ASSERT_EQUAL_UINT32(4u, render_graph_api.get_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(6u, render_graph_api.get_resource_count(g));
	TEST_ASSERT_TRUE(render_graph_api.get_edge_count(g) >= 4u);

	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, stats.heap_alloc_count);
	TEST_ASSERT_EQUAL_INT(1, stats.in_frame);

	render_graph_api.end(g);
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(heap_at_begin, stats.heap_alloc_count);
	TEST_ASSERT_EQUAL_INT(0, stats.in_frame);

	render_graph_api.destroy(g);
}

SK_TEST(render_graph_buffer_and_view_declare) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_buffer_desc_t buf = rg_test_buf_desc(1024ull);
	sk_rg_view_desc_t view;
	sk_rg_resource_info_t ri;

	memset(&view, 0, sizeof(view));
	view.texture_name = "color";
	view.view_type = SK_TEXTURE_VIEW_TYPE_2D;
	view.mip_level_count = 1u;
	view.array_layer_count = 1u;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "color", &tex);
	render_graph_api.create_buffer(g, "ubo", &buf);
	render_graph_api.create_view(g, "color_view", &view);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_last_error(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_resource_count(g));

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_info(g, 1u, &ri));
	TEST_ASSERT_EQUAL_INT(SK_RG_RESOURCE_BUFFER, ri.kind);
	TEST_ASSERT_EQUAL_UINT64(1024ull, ri.buffer.size);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_info(g, 2u, &ri));
	TEST_ASSERT_EQUAL_INT(SK_RG_RESOURCE_VIEW, ri.kind);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

#endif /* SK_TESTS */
