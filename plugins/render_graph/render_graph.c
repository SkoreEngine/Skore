/**
 * @file render_graph.c
 * @brief sk-render-graph plugin: fn table, frame memory, build, compile.
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
 *
 * Compile phase (APX-152):
 *   - Topological order (stage tie-break), cycle → SK_RG_ERR_CYCLE.
 *   - Cull passes whose outputs are never consumed (unless SIDE_EFFECTS).
 *   - Per-resource first/last use in compiled order; alias pack for transients.
 *   - Compile scratch + results live in pre-sized persistent tables (reused).
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
	u32 flags; /* sk_rg_pass_flag_bit_t */
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

/** Per-resource lifetime slot (compile result; index = resource index). */
typedef struct sk_rg_lifetime_slot_t {
	u32 first_use;
	u32 last_use;
	i32 used;
	i32 first_use_is_write_only;
	i32 first_pass_writes;
	i32 first_pass_reads;
} sk_rg_lifetime_slot_t;

/** Alias-eligible resource + placement (compile result). */
typedef struct sk_rg_alias_slot_t {
	u32 resource_index;
	u32 first_use;
	u32 last_use;
	u64 size;
	u64 alignment;
	u32 memory_type_bits;
	u32 bucket;
	u64 offset;
} sk_rg_alias_slot_t;

/** Alias heap bucket (compile result). */
typedef struct sk_rg_alias_bucket_slot_t {
	u64 size;
	u64 alignment;
	u32 memory_type_bits;
} sk_rg_alias_bucket_slot_t;

/**
 * Compile working sets + results. All tables sized at graph create from
 * pass/resource capacities and reused every frame (no mid-compile heap).
 */
typedef struct sk_rg_compile_state_t {
	/* results */
	u32* order; /* [pass_capacity] declaration indices in exec order */
	u32 order_count;
	u32* culled; /* [pass_capacity] declaration indices culled */
	u32 culled_count;
	sk_rg_lifetime_slot_t* lifetimes; /* [resource_capacity] */
	sk_rg_alias_slot_t* aliases;	  /* [resource_capacity] eligible set */
	u32 alias_count;
	sk_rg_alias_bucket_slot_t* buckets; /* [resource_capacity] */
	u32 bucket_count;
	u64 standalone_bytes;
	u64 aliased_bytes;
	i32 compiled;

	/* scratch (reused each compile) */
	u32* indegree;	  /* [pass_capacity] */
	u8* needed;		  /* [pass_capacity] */
	u8* emitted;	  /* [pass_capacity] */
	u32* topo_order;  /* [pass_capacity] full topo before cull */
	u32* alias_order; /* [resource_capacity] sort keys for packer */
	u32 pass_capacity;
	u32 resource_capacity;
} sk_rg_compile_state_t;

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
	sk_rg_compile_state_t compile;
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
/* Compile state (pre-sized; reused across frames)                     */
/* ------------------------------------------------------------------ */

static void rg_compile_clear_results(sk_rg_compile_state_t* c) {
	c->order_count = 0u;
	c->culled_count = 0u;
	c->alias_count = 0u;
	c->bucket_count = 0u;
	c->standalone_bytes = 0ull;
	c->aliased_bytes = 0ull;
	c->compiled = 0;
}

static i32 rg_compile_state_init(sk_rg_memory_t* mem, sk_rg_compile_state_t* c, u32 pass_cap, u32 resource_cap) {
	memset(c, 0, sizeof(*c));
	c->pass_capacity = pass_cap;
	c->resource_capacity = resource_cap;

	if (pass_cap > 0u) {
		c->order = (u32*)rg_heap_alloc(mem, (size_t)pass_cap * sizeof(u32));
		c->culled = (u32*)rg_heap_alloc(mem, (size_t)pass_cap * sizeof(u32));
		c->indegree = (u32*)rg_heap_alloc(mem, (size_t)pass_cap * sizeof(u32));
		c->needed = (u8*)rg_heap_alloc(mem, (size_t)pass_cap * sizeof(u8));
		c->emitted = (u8*)rg_heap_alloc(mem, (size_t)pass_cap * sizeof(u8));
		c->topo_order = (u32*)rg_heap_alloc(mem, (size_t)pass_cap * sizeof(u32));
		if (c->order == NULL || c->culled == NULL || c->indegree == NULL || c->needed == NULL || c->emitted == NULL || c->topo_order == NULL) {
			return SK_RG_ERR_OOM;
		}
	}
	if (resource_cap > 0u) {
		c->lifetimes = (sk_rg_lifetime_slot_t*)rg_heap_alloc(mem, (size_t)resource_cap * sizeof(sk_rg_lifetime_slot_t));
		c->aliases = (sk_rg_alias_slot_t*)rg_heap_alloc(mem, (size_t)resource_cap * sizeof(sk_rg_alias_slot_t));
		c->buckets = (sk_rg_alias_bucket_slot_t*)rg_heap_alloc(mem, (size_t)resource_cap * sizeof(sk_rg_alias_bucket_slot_t));
		c->alias_order = (u32*)rg_heap_alloc(mem, (size_t)resource_cap * sizeof(u32));
		if (c->lifetimes == NULL || c->aliases == NULL || c->buckets == NULL || c->alias_order == NULL) {
			return SK_RG_ERR_OOM;
		}
	}
	rg_compile_clear_results(c);
	return SK_RG_OK;
}

static void rg_compile_state_shutdown(sk_rg_memory_t* mem, sk_rg_compile_state_t* c) {
	rg_heap_free(mem, c->order);
	rg_heap_free(mem, c->culled);
	rg_heap_free(mem, c->indegree);
	rg_heap_free(mem, c->needed);
	rg_heap_free(mem, c->emitted);
	rg_heap_free(mem, c->topo_order);
	rg_heap_free(mem, c->lifetimes);
	rg_heap_free(mem, c->aliases);
	rg_heap_free(mem, c->buckets);
	rg_heap_free(mem, c->alias_order);
	memset(c, 0, sizeof(*c));
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
	rg_memory_config_apply_defaults(&cfg);

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
	rc = rg_compile_state_init(&graph->memory, &graph->compile, cfg.pass_capacity, cfg.resource_capacity);
	if (rc != SK_RG_OK) {
		rg_compile_state_shutdown(&graph->memory, &graph->compile);
		rg_memory_shutdown(&graph->memory);
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
	rg_compile_state_shutdown(&graph->memory, &graph->compile);
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
	pass->flags = SK_RG_PASS_FLAG_NONE;
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

static void render_graph_pass_set_side_effects_impl(sk_rg_pass_t* p, i32 enabled) {
	if (!rg_pass_is_live(p)) {
		if (p != NULL && p->graph != NULL) {
			rg_memory_set_error(&p->graph->memory, SK_RG_ERR_INVALID_PASS);
		}
		return;
	}
	if (enabled) {
		p->flags |= SK_RG_PASS_FLAG_SIDE_EFFECTS;
	} else {
		p->flags &= ~(u32)SK_RG_PASS_FLAG_SIDE_EFFECTS;
	}
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
	rg_compile_clear_results(&g->compile);
	rg_memory_begin_frame(&g->memory);
}

static void render_graph_end_impl(sk_render_graph_t* g) {
	rg_memory_end_frame(&g->memory);
}

/* Forward declaration — defined with the compile phase. */
static i32 render_graph_compile_impl(sk_render_graph_t* g);

static void render_graph_execute_impl(sk_render_graph_t* g, sk_command_buffer_t cmd) {
	(void)cmd;
	/* Compile if not yet done this frame; barriers / record land later. */
	if (!g->compile.compiled) {
		(void)render_graph_compile_impl(g);
	}
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
	out->flags = pass->flags;
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
/* Compile phase (APX-152)                                             */
/* ------------------------------------------------------------------ */

/** Rough bytes-per-pixel for alias size estimates (compile has no RHI yet). */
static u32 rg_format_bytes_per_pixel(sk_pixel_format_t format) {
	if (format == SK_PIXEL_FORMAT_R8_UNORM || format == SK_PIXEL_FORMAT_R8_SNORM || format == SK_PIXEL_FORMAT_R8_UINT || format == SK_PIXEL_FORMAT_R8_SINT ||
		format == SK_PIXEL_FORMAT_R8_SRGB) {
		return 1u;
	}
	if (format == SK_PIXEL_FORMAT_R16_UNORM || format == SK_PIXEL_FORMAT_R16_SNORM || format == SK_PIXEL_FORMAT_R16_UINT || format == SK_PIXEL_FORMAT_R16_SINT ||
		format == SK_PIXEL_FORMAT_R16_FLOAT || format == SK_PIXEL_FORMAT_RG8_UNORM || format == SK_PIXEL_FORMAT_RG8_SNORM || format == SK_PIXEL_FORMAT_RG8_UINT ||
		format == SK_PIXEL_FORMAT_RG8_SINT || format == SK_PIXEL_FORMAT_RG8_SRGB || format == SK_PIXEL_FORMAT_D16_UNORM) {
		return 2u;
	}
	if (format == SK_PIXEL_FORMAT_RG32_UINT || format == SK_PIXEL_FORMAT_RG32_SINT || format == SK_PIXEL_FORMAT_RG32_FLOAT || format == SK_PIXEL_FORMAT_RGBA16_UNORM ||
		format == SK_PIXEL_FORMAT_RGBA16_SNORM || format == SK_PIXEL_FORMAT_RGBA16_UINT || format == SK_PIXEL_FORMAT_RGBA16_SINT || format == SK_PIXEL_FORMAT_RGBA16_FLOAT ||
		format == SK_PIXEL_FORMAT_D32_FLOAT_S8_UINT || format == SK_PIXEL_FORMAT_RGB16_UNORM || format == SK_PIXEL_FORMAT_RGB16_SNORM || format == SK_PIXEL_FORMAT_RGB16_UINT ||
		format == SK_PIXEL_FORMAT_RGB16_SINT || format == SK_PIXEL_FORMAT_RGB16_FLOAT) {
		/* RGB16 is 6 Bpp; treat as 8 for conservative alias sizing. */
		return 8u;
	}
	if (format == SK_PIXEL_FORMAT_RGB32_UINT || format == SK_PIXEL_FORMAT_RGB32_SINT || format == SK_PIXEL_FORMAT_RGB32_FLOAT) {
		return 12u;
	}
	if (format == SK_PIXEL_FORMAT_RGBA32_UINT || format == SK_PIXEL_FORMAT_RGBA32_SINT || format == SK_PIXEL_FORMAT_RGBA32_FLOAT) {
		return 16u;
	}
	/* Default / compressed / common 32-bit packings. */
	return 4u;
}

/** Resolve a dependency resource index through views to the parent texture. */
static u32 rg_resolve_dep_resource_index(const sk_render_graph_t* g, u32 resource_index) {
	const sk_rg_resource_node_t* res;
	if (resource_index == SK_RG_INVALID_INDEX) {
		return SK_RG_INVALID_INDEX;
	}
	res = rg_resource_at(g, resource_index);
	if (res == NULL) {
		return SK_RG_INVALID_INDEX;
	}
	if (res->kind == SK_RG_RESOURCE_VIEW && !rg_name_empty(res->u.view.texture_name)) {
		const sk_rg_resource_node_t* parent = rg_find_resource(g, res->u.view.texture_name);
		if (parent != NULL) {
			return parent->index;
		}
	}
	return resource_index;
}

static u64 rg_estimate_texture_bytes(const sk_render_graph_t* g, const sk_rg_resource_node_t* res) {
	const sk_rg_texture_desc_t* d;
	u32 w;
	u32 h;
	u32 depth;
	u32 layers;
	u32 samples;
	u32 mips;
	u32 bpp;
	u64 level_bytes;
	u64 total;
	u32 m;
	u32 lw;
	u32 lh;

	if (res == NULL || res->kind != SK_RG_RESOURCE_TEXTURE) {
		return 0ull;
	}
	d = &res->u.texture;
	w = d->extent.width;
	h = d->extent.height;
	if (w == 0u || h == 0u) {
		w = (u32)((f32)g->output_size.width * (d->scale_x > 0.0f ? d->scale_x : 1.0f));
		h = (u32)((f32)g->output_size.height * (d->scale_y > 0.0f ? d->scale_y : 1.0f));
	}
	if (w == 0u) {
		w = 1u;
	}
	if (h == 0u) {
		h = 1u;
	}
	depth = d->extent.depth != 0u ? d->extent.depth : 1u;
	layers = d->array_layers != 0u ? d->array_layers : 1u;
	samples = d->samples != 0u ? d->samples : 1u;
	mips = d->mip_levels != 0u ? d->mip_levels : 1u;
	bpp = rg_format_bytes_per_pixel(d->format);

	total = 0ull;
	lw = w;
	lh = h;
	for (m = 0u; m < mips; ++m) {
		level_bytes = (u64)lw * (u64)lh * (u64)depth * (u64)layers * (u64)samples * (u64)bpp;
		total += level_bytes;
		if (lw > 1u) {
			lw >>= 1u;
		}
		if (lh > 1u) {
			lh >>= 1u;
		}
	}
	return total;
}

static u64 rg_align_u64(u64 value, u64 alignment) {
	if (alignment == 0ull) {
		return value;
	}
	return (value + (alignment - 1ull)) / alignment * alignment;
}

static i32 rg_pass_writes_named_output(const sk_rg_pass_t* pass, const_chr_t output_name) {
	const sk_rg_dep_node_t* dep;
	if (rg_name_empty(output_name)) {
		return 0;
	}
	for (dep = pass->deps_head; dep != NULL; dep = dep->next) {
		if (!rg_name_eq(dep->resource_name, output_name)) {
			continue;
		}
		if (dep->access == SK_RG_ACCESS_WRITE || dep->access == SK_RG_ACCESS_READ_WRITE || dep->is_resolve) {
			return 1;
		}
	}
	return 0;
}

static i32 rg_pass_is_root(const sk_render_graph_t* g, const sk_rg_pass_t* pass) {
	if ((pass->flags & SK_RG_PASS_FLAG_SIDE_EFFECTS) != 0u) {
		return 1;
	}
	if (rg_pass_writes_named_output(pass, g->color_output)) {
		return 1;
	}
	if (rg_pass_writes_named_output(pass, g->depth_output)) {
		return 1;
	}
	return 0;
}

/**
 * Topological sort of all passes using declared edges.
 * Stage is the tie-break among zero-indegree candidates (lower stage first;
 * equal stage keeps lower declaration index).
 * On cycle returns SK_RG_ERR_CYCLE. Writes full order into compile.topo_order.
 */
static i32 rg_compile_topo_sort(sk_render_graph_t* g) {
	sk_rg_compile_state_t* c = &g->compile;
	const u32 n = g->memory.passes.live;
	u32 i;
	u32 sorted = 0u;

	if (n > c->pass_capacity) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_OUT_OF_SPACE);
		return SK_RG_ERR_OUT_OF_SPACE;
	}

	for (i = 0u; i < n; ++i) {
		c->indegree[i] = 0u;
		c->emitted[i] = 0u;
	}
	for (i = 0u; i < g->memory.edges.count; ++i) {
		const sk_rg_edge_t* e = (const sk_rg_edge_t*)rg_array_pool_at(&g->memory.edges, i);
		if (e == NULL || e->to >= n) {
			continue;
		}
		c->indegree[e->to] += 1u;
	}

	while (sorted < n) {
		u32 next = SK_RG_INVALID_INDEX;
		const sk_rg_pass_t* next_pass = NULL;

		for (i = 0u; i < n; ++i) {
			const sk_rg_pass_t* pass;
			if (c->emitted[i] != 0u || c->indegree[i] != 0u) {
				continue;
			}
			pass = rg_pass_at(g, i);
			if (pass == NULL) {
				continue;
			}
			if (next == SK_RG_INVALID_INDEX) {
				next = i;
				next_pass = pass;
				continue;
			}
			/* Lower stage wins; declaration index breaks remaining ties. */
			if (pass->stage < next_pass->stage || (pass->stage == next_pass->stage && i < next)) {
				next = i;
				next_pass = pass;
			}
		}

		if (next == SK_RG_INVALID_INDEX) {
			rg_memory_set_error(&g->memory, SK_RG_ERR_CYCLE);
			return SK_RG_ERR_CYCLE;
		}

		c->emitted[next] = 1u;
		c->topo_order[sorted] = next;
		sorted += 1u;

		for (i = 0u; i < g->memory.edges.count; ++i) {
			const sk_rg_edge_t* e = (const sk_rg_edge_t*)rg_array_pool_at(&g->memory.edges, i);
			if (e == NULL || e->from != next || e->to >= n) {
				continue;
			}
			if (c->indegree[e->to] > 0u) {
				c->indegree[e->to] -= 1u;
			}
		}
	}

	return SK_RG_OK;
}

/**
 * Mark passes needed for outputs / side effects and propagate producers via edges.
 * If no roots exist, keep every pass (culling is a no-op).
 */
static void rg_compile_cull(sk_render_graph_t* g) {
	sk_rg_compile_state_t* c = &g->compile;
	const u32 n = g->memory.passes.live;
	u32 i;
	i32 changed;
	i32 any_root = 0;

	for (i = 0u; i < n; ++i) {
		sk_rg_pass_t* pass = rg_pass_at(g, i);
		c->needed[i] = 0u;
		if (pass != NULL) {
			pass->flags &= ~(u32)SK_RG_PASS_FLAG_CULLED;
			if (rg_pass_is_root(g, pass)) {
				c->needed[i] = 1u;
				any_root = 1;
			}
		}
	}

	if (!any_root) {
		for (i = 0u; i < n; ++i) {
			c->needed[i] = 1u;
		}
		return;
	}

	/* Reverse reachability: if consumer needed, producer is needed. */
	do {
		changed = 0;
		for (i = 0u; i < g->memory.edges.count; ++i) {
			const sk_rg_edge_t* e = (const sk_rg_edge_t*)rg_array_pool_at(&g->memory.edges, i);
			if (e == NULL || e->from >= n || e->to >= n) {
				continue;
			}
			if (c->needed[e->to] != 0u && c->needed[e->from] == 0u) {
				c->needed[e->from] = 1u;
				changed = 1;
			}
		}
	} while (changed);

	for (i = 0u; i < n; ++i) {
		sk_rg_pass_t* pass = rg_pass_at(g, i);
		if (pass == NULL) {
			continue;
		}
		if (c->needed[i] == 0u) {
			pass->flags |= SK_RG_PASS_FLAG_CULLED;
		}
	}
}

static void rg_compile_build_order_lists(sk_render_graph_t* g, u32 topo_count) {
	sk_rg_compile_state_t* c = &g->compile;
	u32 i;

	c->order_count = 0u;
	c->culled_count = 0u;
	for (i = 0u; i < topo_count; ++i) {
		u32 pass_index = c->topo_order[i];
		if (c->needed[pass_index] != 0u) {
			c->order[c->order_count] = pass_index;
			c->order_count += 1u;
		} else {
			c->culled[c->culled_count] = pass_index;
			c->culled_count += 1u;
		}
	}
}

static void rg_compile_compute_lifetimes(sk_render_graph_t* g) {
	sk_rg_compile_state_t* c = &g->compile;
	const u32 res_n = g->memory.resources.live;
	u32 oi;
	u32 r;

	if (res_n > c->resource_capacity) {
		return;
	}
	for (r = 0u; r < res_n; ++r) {
		c->lifetimes[r].first_use = SK_RG_INVALID_INDEX;
		c->lifetimes[r].last_use = SK_RG_INVALID_INDEX;
		c->lifetimes[r].used = 0;
		c->lifetimes[r].first_use_is_write_only = 0;
		c->lifetimes[r].first_pass_writes = 0;
		c->lifetimes[r].first_pass_reads = 0;
	}

	for (oi = 0u; oi < c->order_count; ++oi) {
		const sk_rg_pass_t* pass = rg_pass_at(g, c->order[oi]);
		const sk_rg_dep_node_t* dep;
		if (pass == NULL) {
			continue;
		}
		for (dep = pass->deps_head; dep != NULL; dep = dep->next) {
			u32 ri = rg_resolve_dep_resource_index(g, dep->resource_index);
			sk_rg_lifetime_slot_t* life;
			i32 writes;
			i32 reads;
			if (ri == SK_RG_INVALID_INDEX || ri >= res_n) {
				continue;
			}
			life = &c->lifetimes[ri];
			writes = (dep->access == SK_RG_ACCESS_WRITE || dep->access == SK_RG_ACCESS_READ_WRITE || dep->is_resolve);
			reads = (dep->access == SK_RG_ACCESS_READ || dep->access == SK_RG_ACCESS_READ_WRITE);
			if (!life->used) {
				life->first_use = oi;
				life->last_use = oi;
				life->first_pass_writes = writes;
				life->first_pass_reads = reads;
				life->used = 1;
			} else {
				if (oi == life->first_use) {
					life->first_pass_writes = life->first_pass_writes || writes;
					life->first_pass_reads = life->first_pass_reads || reads;
				}
				if (oi > life->last_use) {
					life->last_use = oi;
				}
			}
		}
	}

	for (r = 0u; r < res_n; ++r) {
		sk_rg_lifetime_slot_t* life = &c->lifetimes[r];
		if (life->used) {
			life->first_use_is_write_only = life->first_pass_writes && !life->first_pass_reads;
		}
	}
}

/**
 * Place @p r into bucket @p h among already-placed lifetime-overlapping tenants.
 * First-fit gap search (offsets sorted via repeated min selection).
 */
static u64 rg_alias_find_offset(const sk_rg_compile_state_t* c, u32 h, const sk_rg_alias_slot_t* r, u32 self_idx) {
	u64 candidate = 0ull;
	const u32 n = c->alias_count;

	for (;;) {
		u64 next_start = 0xffffffffffffffffull;
		u64 next_end = 0ull;
		i32 found = 0;
		u32 b;

		candidate = rg_align_u64(candidate, r->alignment);

		for (b = 0u; b < n; ++b) {
			const sk_rg_alias_slot_t* t = &c->aliases[b];
			if (b == self_idx || t->bucket != h) {
				continue;
			}
			/* Lifetime overlap? */
			if (!(r->first_use <= t->last_use && t->first_use <= r->last_use)) {
				continue;
			}
			if (t->offset + t->size <= candidate) {
				continue;
			}
			if (t->offset < next_start) {
				next_start = t->offset;
				next_end = t->offset + t->size;
				found = 1;
			}
		}

		if (!found) {
			return candidate;
		}
		if (candidate + r->size <= next_start) {
			return candidate;
		}
		candidate = next_end;
	}
}

/**
 * First-fit alias packer matching main's ComputeRenderGraphAliasPlan:
 * size-desc order, non-overlapping lifetime tenants per bucket, memory type bits.
 */
static void rg_compile_pack_aliases(sk_render_graph_t* g) {
	sk_rg_compile_state_t* c = &g->compile;
	u32 n = c->alias_count;
	u32 i;
	u32 a;

	c->bucket_count = 0u;
	c->standalone_bytes = 0ull;
	c->aliased_bytes = 0ull;
	if (n == 0u) {
		return;
	}

	for (i = 0u; i < n; ++i) {
		c->alias_order[i] = i;
		c->standalone_bytes += c->aliases[i].size;
		c->aliases[i].bucket = SK_RG_INVALID_INDEX;
		c->aliases[i].offset = 0ull;
	}

	/* Insertion sort: larger size first, then earlier first_use, then index. */
	for (i = 1u; i < n; ++i) {
		u32 key = c->alias_order[i];
		const sk_rg_alias_slot_t* rk = &c->aliases[key];
		i32 j = (i32)i - 1;
		while (j >= 0) {
			const sk_rg_alias_slot_t* rj = &c->aliases[c->alias_order[(u32)j]];
			i32 less;
			if (rk->size != rj->size) {
				less = rk->size > rj->size;
			} else if (rk->first_use != rj->first_use) {
				less = rk->first_use < rj->first_use;
			} else if (rk->last_use != rj->last_use) {
				less = rk->last_use < rj->last_use;
			} else {
				less = key < c->alias_order[(u32)j];
			}
			if (!less) {
				break;
			}
			c->alias_order[(u32)j + 1u] = c->alias_order[(u32)j];
			--j;
		}
		c->alias_order[(u32)j + 1u] = key;
	}

	for (a = 0u; a < n; ++a) {
		u32 idx = c->alias_order[a];
		sk_rg_alias_slot_t* r = &c->aliases[idx];
		u32 chosen_heap = SK_RG_INVALID_INDEX;
		u64 chosen_offset = 0ull;
		u32 h;

		for (h = 0u; h < c->bucket_count; ++h) {
			if ((c->buckets[h].memory_type_bits & r->memory_type_bits) == 0u) {
				continue;
			}
			chosen_offset = rg_alias_find_offset(c, h, r, idx);
			chosen_heap = h;
			break; /* first-fit heap */
		}

		if (chosen_heap == SK_RG_INVALID_INDEX) {
			if (c->bucket_count >= c->resource_capacity) {
				chosen_heap = 0u;
				chosen_offset = 0ull;
			} else {
				chosen_heap = c->bucket_count;
				chosen_offset = 0ull;
				c->buckets[chosen_heap].size = 0ull;
				c->buckets[chosen_heap].alignment = r->alignment;
				c->buckets[chosen_heap].memory_type_bits = r->memory_type_bits;
				c->bucket_count += 1u;
			}
		}

		r->bucket = chosen_heap;
		r->offset = chosen_offset;
		if (chosen_offset + r->size > c->buckets[chosen_heap].size) {
			c->buckets[chosen_heap].size = chosen_offset + r->size;
		}
		if (r->alignment > c->buckets[chosen_heap].alignment) {
			c->buckets[chosen_heap].alignment = r->alignment;
		}
		c->buckets[chosen_heap].memory_type_bits &= r->memory_type_bits;
	}

	for (i = 0u; i < c->bucket_count; ++i) {
		c->aliased_bytes += c->buckets[i].size;
	}
}

static void rg_compile_select_alias_resources(sk_render_graph_t* g) {
	sk_rg_compile_state_t* c = &g->compile;
	const u32 res_n = g->memory.resources.live;
	u32 r;

	c->alias_count = 0u;
	for (r = 0u; r < res_n && r < c->resource_capacity; ++r) {
		sk_rg_resource_node_t* res = rg_resource_at(g, r);
		const sk_rg_lifetime_slot_t* life;
		sk_rg_alias_slot_t* slot;
		u64 bytes;

		if (res == NULL) {
			continue;
		}
		res->flags &= ~(u32)SK_RG_RESOURCE_FLAG_ALIASED;
		if (res->kind != SK_RG_RESOURCE_TEXTURE) {
			continue;
		}
		if (res->u.texture.ping_pong || res->u.texture.persistent) {
			continue;
		}
		if ((res->flags & (SK_RG_RESOURCE_FLAG_COLOR_OUTPUT | SK_RG_RESOURCE_FLAG_DEPTH_OUTPUT | SK_RG_RESOURCE_FLAG_IMPORTED)) != 0u) {
			continue;
		}
		if (!rg_name_empty(g->color_output) && rg_name_eq(res->name, g->color_output)) {
			continue;
		}
		if (!rg_name_empty(g->depth_output) && rg_name_eq(res->name, g->depth_output)) {
			continue;
		}

		life = &c->lifetimes[r];
		if (!life->used || !life->first_use_is_write_only) {
			continue;
		}

		bytes = rg_estimate_texture_bytes(g, res);
		if (bytes == 0ull) {
			continue;
		}

		slot = &c->aliases[c->alias_count];
		slot->resource_index = r;
		slot->first_use = life->first_use;
		slot->last_use = life->last_use;
		slot->size = bytes;
		slot->alignment = 256ull;
		slot->memory_type_bits = 0xffffffffu;
		slot->bucket = SK_RG_INVALID_INDEX;
		slot->offset = 0ull;
		c->alias_count += 1u;
		res->flags |= SK_RG_RESOURCE_FLAG_ALIASED;
	}

	rg_compile_pack_aliases(g);
}

static i32 render_graph_compile_impl(sk_render_graph_t* g) {
	sk_rg_compile_state_t* c;
	u32 n;
	i32 rc;
	u32 i;

	if (g == NULL) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	c = &g->compile;
	n = g->memory.passes.live;

	/* Reset previous compile results for this call. */
	c->order_count = 0u;
	c->culled_count = 0u;
	c->alias_count = 0u;
	c->bucket_count = 0u;
	c->standalone_bytes = 0ull;
	c->aliased_bytes = 0ull;
	c->compiled = 0;

	if (n > c->pass_capacity || g->memory.resources.live > c->resource_capacity) {
		rg_memory_set_error(&g->memory, SK_RG_ERR_OUT_OF_SPACE);
		return SK_RG_ERR_OUT_OF_SPACE;
	}

	/* Clear culled flags from any prior compile. */
	for (i = 0u; i < n; ++i) {
		sk_rg_pass_t* pass = rg_pass_at(g, i);
		if (pass != NULL) {
			pass->flags &= ~(u32)SK_RG_PASS_FLAG_CULLED;
		}
	}

	if (n == 0u) {
		/* Empty graph is a successful compile. */
		c->compiled = 1;
		g->topology_build_count += 1u;
		rg_memory_set_error(&g->memory, SK_RG_OK);
		return SK_RG_OK;
	}

	if (n == 1u) {
		c->topo_order[0] = 0u;
	} else {
		rc = rg_compile_topo_sort(g);
		if (rc != SK_RG_OK) {
			return rc;
		}
	}

	rg_compile_cull(g);
	rg_compile_build_order_lists(g, n);
	rg_compile_compute_lifetimes(g);
	rg_compile_select_alias_resources(g);

	c->compiled = 1;
	g->topology_build_count += 1u;
	rg_memory_set_error(&g->memory, SK_RG_OK);
	return SK_RG_OK;
}

static i32 render_graph_is_compiled_impl(const sk_render_graph_t* g) {
	return g != NULL && g->compile.compiled ? 1 : 0;
}

static u32 render_graph_get_compiled_pass_count_impl(const sk_render_graph_t* g) {
	if (g == NULL || !g->compile.compiled) {
		return 0u;
	}
	return g->compile.order_count;
}

static i32 render_graph_get_compiled_pass_order_impl(const sk_render_graph_t* g, u32 order_index, u32* out_pass_index) {
	if (g == NULL || out_pass_index == NULL || !g->compile.compiled) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	if (order_index >= g->compile.order_count) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	*out_pass_index = g->compile.order[order_index];
	return SK_RG_OK;
}

static u32 render_graph_get_culled_pass_count_impl(const sk_render_graph_t* g) {
	if (g == NULL || !g->compile.compiled) {
		return 0u;
	}
	return g->compile.culled_count;
}

static i32 render_graph_get_culled_pass_order_impl(const sk_render_graph_t* g, u32 culled_index, u32* out_pass_index) {
	if (g == NULL || out_pass_index == NULL || !g->compile.compiled) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	if (culled_index >= g->compile.culled_count) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	*out_pass_index = g->compile.culled[culled_index];
	return SK_RG_OK;
}

static i32 render_graph_get_resource_lifetime_impl(const sk_render_graph_t* g, u32 resource_index, sk_rg_lifetime_info_t* out) {
	const sk_rg_lifetime_slot_t* life;
	if (g == NULL || out == NULL || !g->compile.compiled) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	if (resource_index >= g->memory.resources.live) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	life = &g->compile.lifetimes[resource_index];
	memset(out, 0, sizeof(*out));
	out->resource_index = resource_index;
	out->used = life->used;
	out->first_use = life->used ? life->first_use : SK_RG_INVALID_USE;
	out->last_use = life->used ? life->last_use : SK_RG_INVALID_USE;
	out->first_use_is_write_only = life->first_use_is_write_only;
	return SK_RG_OK;
}

static u32 render_graph_get_alias_assignment_count_impl(const sk_render_graph_t* g) {
	if (g == NULL || !g->compile.compiled) {
		return 0u;
	}
	return g->compile.alias_count;
}

static i32 render_graph_get_alias_assignment_impl(const sk_render_graph_t* g, u32 index, sk_rg_alias_assignment_t* out) {
	const sk_rg_alias_slot_t* slot;
	const sk_rg_resource_node_t* res;
	if (g == NULL || out == NULL || !g->compile.compiled) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	if (index >= g->compile.alias_count) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	slot = &g->compile.aliases[index];
	res = rg_resource_at(g, slot->resource_index);
	memset(out, 0, sizeof(*out));
	out->resource_index = slot->resource_index;
	out->resource_name = res != NULL ? res->name : NULL;
	out->first_use = slot->first_use;
	out->last_use = slot->last_use;
	out->size = slot->size;
	out->alignment = slot->alignment;
	out->memory_type_bits = slot->memory_type_bits;
	out->bucket = slot->bucket;
	out->offset = slot->offset;
	return SK_RG_OK;
}

static u32 render_graph_get_alias_bucket_count_impl(const sk_render_graph_t* g) {
	if (g == NULL || !g->compile.compiled) {
		return 0u;
	}
	return g->compile.bucket_count;
}

static i32 render_graph_get_alias_bucket_info_impl(const sk_render_graph_t* g, u32 bucket_index, sk_rg_alias_bucket_info_t* out) {
	const sk_rg_alias_bucket_slot_t* b;
	if (g == NULL || out == NULL || !g->compile.compiled) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	if (bucket_index >= g->compile.bucket_count) {
		return SK_RG_ERR_INVALID_ARGUMENT;
	}
	b = &g->compile.buckets[bucket_index];
	out->index = bucket_index;
	out->size = b->size;
	out->alignment = b->alignment;
	out->memory_type_bits = b->memory_type_bits;
	return SK_RG_OK;
}

static u64 render_graph_get_alias_standalone_bytes_impl(const sk_render_graph_t* g) {
	if (g == NULL || !g->compile.compiled) {
		return 0ull;
	}
	return g->compile.standalone_bytes;
}

static u64 render_graph_get_alias_aliased_bytes_impl(const sk_render_graph_t* g) {
	if (g == NULL || !g->compile.compiled) {
		return 0ull;
	}
	return g->compile.aliased_bytes;
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
	render_graph_pass_set_side_effects_impl,
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
	render_graph_compile_impl,
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
	render_graph_is_compiled_impl,
	render_graph_get_compiled_pass_count_impl,
	render_graph_get_compiled_pass_order_impl,
	render_graph_get_culled_pass_count_impl,
	render_graph_get_culled_pass_order_impl,
	render_graph_get_resource_lifetime_impl,
	render_graph_get_alias_assignment_count_impl,
	render_graph_get_alias_assignment_impl,
	render_graph_get_alias_bucket_count_impl,
	render_graph_get_alias_bucket_info_impl,
	render_graph_get_alias_standalone_bytes_impl,
	render_graph_get_alias_aliased_bytes_impl,
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
	TEST_ASSERT_NOT_NULL(render_graph_api.pass_set_side_effects);
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
	TEST_ASSERT_NOT_NULL(render_graph_api.compile);
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
	TEST_ASSERT_NOT_NULL(render_graph_api.is_compiled);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_compiled_pass_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_compiled_pass_order);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_culled_pass_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_culled_pass_order);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_resource_lifetime);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_alias_assignment_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_alias_assignment);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_alias_bucket_count);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_alias_bucket_info);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_alias_standalone_bytes);
	TEST_ASSERT_NOT_NULL(render_graph_api.get_alias_aliased_bytes);
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

/* ------------------------------------------------------------------ */
/* Compile phase                                                       */
/* ------------------------------------------------------------------ */

SK_TEST(render_graph_compile_topo_stage_order) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* late;
	sk_rg_pass_t* early;
	sk_rg_pass_t* mid;
	u32 p0;
	u32 p1;
	u32 p2;
	sk_rg_pass_info_t pi;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "a", &tex);
	render_graph_api.create_texture(g, "b", &tex);
	render_graph_api.create_texture(g, "c", &tex);

	/* Declaration order intentionally not stage order. */
	late = render_graph_api.add_pass(g, "late", SK_RG_PASS_COMPUTE);
	early = render_graph_api.add_pass(g, "early", SK_RG_PASS_COMPUTE);
	mid = render_graph_api.add_pass(g, "mid", SK_RG_PASS_COMPUTE);
	TEST_ASSERT_NOT_NULL(late);
	TEST_ASSERT_NOT_NULL(early);
	TEST_ASSERT_NOT_NULL(mid);

	render_graph_api.pass_stage(late, 300);
	render_graph_api.pass_stage(early, 100);
	render_graph_api.pass_stage(mid, 200);
	render_graph_api.pass_write(late, "c");
	render_graph_api.pass_write(early, "a");
	render_graph_api.pass_write(mid, "b");
	/* Independent writes — stage tie-break alone decides order. */
	render_graph_api.set_color_output(g, "c");
	/* Keep all three: mid/early produce nothing consumed, so mark side effects
	 * only on early+mid would cull them. Color output is "c" written by late.
	 * Give early and mid side effects so they stay for the stage-order check. */
	render_graph_api.pass_set_side_effects(early, 1);
	render_graph_api.pass_set_side_effects(mid, 1);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_TRUE(render_graph_api.is_compiled(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 0u, &p0));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 1u, &p1));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 2u, &p2));

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, p0, &pi));
	TEST_ASSERT_EQUAL_STRING("early", pi.name);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, p1, &pi));
	TEST_ASSERT_EQUAL_STRING("mid", pi.name);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, p2, &pi));
	TEST_ASSERT_EQUAL_STRING("late", pi.name);
	TEST_ASSERT_TRUE(render_graph_api.topology_build_count(g) >= 1u);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_linear_chain_order) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;
	u32 p0;
	u32 p1;
	u32 p2;
	sk_rg_lifetime_info_t life;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "r0", &tex);
	render_graph_api.create_texture(g, "r1", &tex);
	render_graph_api.create_texture(g, "r2", &tex);

	a = render_graph_api.add_pass(g, "pass_a", SK_RG_PASS_COMPUTE);
	b = render_graph_api.add_pass(g, "pass_b", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "pass_c", SK_RG_PASS_COMPUTE);
	render_graph_api.pass_write(a, "r0");
	render_graph_api.pass_read(b, "r0");
	render_graph_api.pass_write(b, "r1");
	render_graph_api.pass_read(c, "r1");
	render_graph_api.pass_write(c, "r2");
	render_graph_api.set_color_output(g, "r2");

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(0u, render_graph_api.get_culled_pass_count(g));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 0u, &p0));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 1u, &p1));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 2u, &p2));
	TEST_ASSERT_EQUAL_UINT32(0u, p0);
	TEST_ASSERT_EQUAL_UINT32(1u, p1);
	TEST_ASSERT_EQUAL_UINT32(2u, p2);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_lifetime(g, 0u, &life));
	TEST_ASSERT_TRUE(life.used);
	TEST_ASSERT_EQUAL_UINT32(0u, life.first_use);
	TEST_ASSERT_EQUAL_UINT32(1u, life.last_use);
	TEST_ASSERT_TRUE(life.first_use_is_write_only);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_resource_lifetime(g, 2u, &life));
	TEST_ASSERT_EQUAL_UINT32(2u, life.first_use);
	TEST_ASSERT_EQUAL_UINT32(2u, life.last_use);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_cycle_is_defined_error) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "x", &tex);
	render_graph_api.create_texture(g, "y", &tex);
	a = render_graph_api.add_pass(g, "a", SK_RG_PASS_COMPUTE);
	b = render_graph_api.add_pass(g, "b", SK_RG_PASS_COMPUTE);
	render_graph_api.pass_write(a, "x");
	render_graph_api.pass_read(b, "x");
	render_graph_api.pass_write(b, "y");
	render_graph_api.pass_read(a, "y"); /* creates B→A edge → cycle with A→B */

	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_CYCLE, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_INT(SK_RG_ERR_CYCLE, render_graph_api.get_last_error(g));
	TEST_ASSERT_FALSE(render_graph_api.is_compiled(g));

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_culls_unused_pass) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* dead;
	sk_rg_pass_t* live;
	sk_rg_pass_info_t pi;
	u32 culled0;
	u32 order0;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "unused", &tex);
	render_graph_api.create_texture(g, "color", &tex);

	dead = render_graph_api.add_pass(g, "dead", SK_RG_PASS_COMPUTE);
	live = render_graph_api.add_pass(g, "live", SK_RG_PASS_GRAPHICS);
	TEST_ASSERT_NOT_NULL(dead);
	TEST_ASSERT_NOT_NULL(live);
	render_graph_api.pass_write(dead, "unused");
	render_graph_api.pass_write(live, "color");
	render_graph_api.set_color_output(g, "color");

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_culled_pass_count(g));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_compiled_pass_order(g, 0u, &order0));
	TEST_ASSERT_EQUAL_UINT32(1u, order0); /* live */
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_culled_pass_order(g, 0u, &culled0));
	TEST_ASSERT_EQUAL_UINT32(0u, culled0); /* dead */

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, 0u, &pi));
	TEST_ASSERT_TRUE((pi.flags & SK_RG_PASS_FLAG_CULLED) != 0u);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, 1u, &pi));
	TEST_ASSERT_TRUE((pi.flags & SK_RG_PASS_FLAG_CULLED) == 0u);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_side_effects_never_cull) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* side;
	sk_rg_pass_t* live;
	sk_rg_pass_info_t pi;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "scratch", &tex);
	render_graph_api.create_texture(g, "color", &tex);

	side = render_graph_api.add_pass(g, "side", SK_RG_PASS_TRANSFER);
	live = render_graph_api.add_pass(g, "live", SK_RG_PASS_GRAPHICS);
	render_graph_api.pass_write(side, "scratch");
	render_graph_api.pass_set_side_effects(side, 1);
	render_graph_api.pass_write(live, "color");
	render_graph_api.set_color_output(g, "color");

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_UINT32(2u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(0u, render_graph_api.get_culled_pass_count(g));
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_pass_info(g, 0u, &pi));
	TEST_ASSERT_TRUE((pi.flags & SK_RG_PASS_FLAG_SIDE_EFFECTS) != 0u);
	TEST_ASSERT_TRUE((pi.flags & SK_RG_PASS_FLAG_CULLED) == 0u);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_alias_disjoint_lifetimes) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* p0;
	sk_rg_pass_t* p1;
	sk_rg_pass_t* p2;
	/* Zero-init: filled in the loop below; cppcheck cannot prove all three names are found. */
	sk_rg_alias_assignment_t a0 = {0};
	sk_rg_alias_assignment_t a1 = {0};
	sk_rg_alias_assignment_t a2 = {0};
	u32 i;
	u32 found_a = 0u;
	u32 found_c = 0u;
	u64 off_a = 0ull;
	u64 off_c = 0ull;

	/* 64x64 RGBA8 → 16384 bytes each (extent in desc). */
	tex.extent.width = 64u;
	tex.extent.height = 64u;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.set_output_size(g, (sk_rg_extent_t){64u, 64u});
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "A", &tex);
	render_graph_api.create_texture(g, "B", &tex);
	render_graph_api.create_texture(g, "C", &tex);

	p0 = render_graph_api.add_pass(g, "produce", SK_RG_PASS_COMPUTE);
	p1 = render_graph_api.add_pass(g, "process", SK_RG_PASS_COMPUTE);
	p2 = render_graph_api.add_pass(g, "finalize", SK_RG_PASS_COMPUTE);
	render_graph_api.pass_write(p0, "A");
	render_graph_api.pass_read(p1, "A");
	render_graph_api.pass_write(p1, "B");
	render_graph_api.pass_read(p2, "B");
	render_graph_api.pass_write(p2, "C");
	/* No color output roots → keep all passes (no cull roots). */
	render_graph_api.pass_set_side_effects(p2, 1);

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_alias_assignment_count(g));
	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_alias_bucket_count(g));
	TEST_ASSERT_TRUE(render_graph_api.get_alias_aliased_bytes(g) < render_graph_api.get_alias_standalone_bytes(g));

	for (i = 0u; i < 3u; ++i) {
		sk_rg_alias_assignment_t as;
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_alias_assignment(g, i, &as));
		if (as.resource_name != NULL && strcmp(as.resource_name, "A") == 0) {
			found_a = 1u;
			off_a = as.offset;
			a0 = as;
		} else if (as.resource_name != NULL && strcmp(as.resource_name, "B") == 0) {
			a1 = as;
		} else if (as.resource_name != NULL && strcmp(as.resource_name, "C") == 0) {
			found_c = 1u;
			off_c = as.offset;
			a2 = as;
		}
	}
	TEST_ASSERT_TRUE(found_a);
	TEST_ASSERT_TRUE(found_c);
	/* A and C have disjoint lifetimes → same offset in one heap. */
	TEST_ASSERT_EQUAL_UINT32(a0.bucket, a2.bucket);
	TEST_ASSERT_EQUAL_UINT64(off_a, off_c);
	/* B overlaps A and C → different offset. */
	TEST_ASSERT_TRUE(a1.offset != off_a);
	(void)a0;
	(void)a1;
	(void)a2;

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_alias_excludes_outputs_and_persistent) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_texture_desc_t hist = rg_test_tex_desc();
	sk_rg_pass_t* p;
	u32 i;
	i32 saw_output = 0;
	i32 saw_hist = 0;
	i32 saw_transient = 0;

	hist.ping_pong = 1;
	tex.extent.width = 64u;
	tex.extent.height = 64u;
	hist.extent.width = 64u;
	hist.extent.height = 64u;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "History", &hist);
	render_graph_api.create_texture(g, "Transient", &tex);
	render_graph_api.create_texture(g, "Output", &tex);

	p = render_graph_api.add_pass(g, "acc", SK_RG_PASS_COMPUTE);
	render_graph_api.pass_read_write(p, "History");
	render_graph_api.pass_write(p, "Transient");
	render_graph_api.pass_write(p, "Output");
	render_graph_api.set_color_output(g, "Output");

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));

	for (i = 0u; i < render_graph_api.get_alias_assignment_count(g); ++i) {
		sk_rg_alias_assignment_t as;
		TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.get_alias_assignment(g, i, &as));
		if (as.resource_name != NULL && strcmp(as.resource_name, "Output") == 0) {
			saw_output = 1;
		}
		if (as.resource_name != NULL && strcmp(as.resource_name, "History") == 0) {
			saw_hist = 1;
		}
		if (as.resource_name != NULL && strcmp(as.resource_name, "Transient") == 0) {
			saw_transient = 1;
		}
	}
	TEST_ASSERT_FALSE(saw_output);
	TEST_ASSERT_FALSE(saw_hist);
	TEST_ASSERT_TRUE(saw_transient);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_zero_heap_allocs) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_memory_stats_t stats;
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;
	sk_rg_pass_t* dead;
	u32 heap_before;
	u32 heap_after_declare;
	u32 topo0;

	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.get_memory_stats(g, &stats);
	heap_before = stats.heap_alloc_count;

	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "r0", &tex);
	render_graph_api.create_texture(g, "r1", &tex);
	render_graph_api.create_texture(g, "r2", &tex);
	render_graph_api.create_texture(g, "dead_tex", &tex);

	a = render_graph_api.add_pass(g, "a", SK_RG_PASS_COMPUTE);
	b = render_graph_api.add_pass(g, "b", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "c", SK_RG_PASS_COMPUTE);
	dead = render_graph_api.add_pass(g, "dead", SK_RG_PASS_COMPUTE);
	render_graph_api.pass_write(a, "r0");
	render_graph_api.pass_read(b, "r0");
	render_graph_api.pass_write(b, "r1");
	render_graph_api.pass_read(c, "r1");
	render_graph_api.pass_write(c, "r2");
	render_graph_api.pass_write(dead, "dead_tex");
	render_graph_api.set_color_output(g, "r2");

	render_graph_api.get_memory_stats(g, &stats);
	heap_after_declare = stats.heap_alloc_count;
	TEST_ASSERT_EQUAL_UINT32(heap_before, heap_after_declare);

	topo0 = render_graph_api.topology_build_count(g);
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_UINT32(topo0 + 1u, render_graph_api.topology_build_count(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(1u, render_graph_api.get_culled_pass_count(g));

	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(heap_after_declare, stats.heap_alloc_count);

	/* Second compile same frame also heap-free. */
	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	render_graph_api.get_memory_stats(g, &stats);
	TEST_ASSERT_EQUAL_UINT32(heap_after_declare, stats.heap_alloc_count);

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

SK_TEST(render_graph_compile_keeps_producer_chain) {
	sk_render_graph_t* g = rg_test_create_graph();
	sk_rg_texture_desc_t tex = rg_test_tex_desc();
	sk_rg_pass_t* a;
	sk_rg_pass_t* b;
	sk_rg_pass_t* c;

	/* a→b→c with only c writing color output — a and b must be kept as producers. */
	TEST_ASSERT_NOT_NULL(g);
	render_graph_api.begin(g, NULL);
	render_graph_api.create_texture(g, "t0", &tex);
	render_graph_api.create_texture(g, "t1", &tex);
	render_graph_api.create_texture(g, "color", &tex);
	a = render_graph_api.add_pass(g, "a", SK_RG_PASS_COMPUTE);
	b = render_graph_api.add_pass(g, "b", SK_RG_PASS_COMPUTE);
	c = render_graph_api.add_pass(g, "c", SK_RG_PASS_GRAPHICS);
	render_graph_api.pass_write(a, "t0");
	render_graph_api.pass_read(b, "t0");
	render_graph_api.pass_write(b, "t1");
	render_graph_api.pass_read(c, "t1");
	render_graph_api.pass_write(c, "color");
	render_graph_api.set_color_output(g, "color");

	TEST_ASSERT_EQUAL_INT(SK_RG_OK, render_graph_api.compile(g));
	TEST_ASSERT_EQUAL_UINT32(3u, render_graph_api.get_compiled_pass_count(g));
	TEST_ASSERT_EQUAL_UINT32(0u, render_graph_api.get_culled_pass_count(g));

	render_graph_api.end(g);
	render_graph_api.destroy(g);
}

#endif /* SK_TESTS */
