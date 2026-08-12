/**
 * @file entities.c
 * @brief ECS module implementation + in-source unit tests.
 *
 * Archetype signatures (sorted sk_type_id_t component sets; column 0 is the
 * implicit entity component) with fixed 16 KiB chunk storage. Each chunk is
 * one cache-line-aligned SK_ECS_CHUNK_SIZE block holding dense, aligned
 * component columns; slots are allocated append-only and freed by swap-remove
 * so every column stays dense. The entity handle lives in column 0 (giving the
 * row -> entity mapping) and the archetype chunk list + chunk_find give the
 * entity -> { chunk, row } location.
 *
 * Component identity is wired through sk_type_id_t: a module-level registry
 * maps each component type id to its layout (size/align/name). Queries match
 * archetypes by required/optional/excluded component sets and iterate their
 * storage through the SK_ECS_* macros.
 *
 * On top of the storage layer sits a sk_world_t (an archetype table created
 * lazily from spawn signatures, a dense generation-tagged entity index, and
 * the world-managed query set that observes every new archetype) with
 * immediate structural ops (world_spawn / world_despawn /
 * world_add_component / world_remove_component). The deferred
 * sk_entitycommands_t surface records the same structural changes without
 * touching the world and applies them in FIFO order with commands_apply, so
 * commands can be queued during query iteration and flushed afterwards.
 */

#include "entities.h"

#include "allocator.h"
#include "app.h"
#include "array.h"
#include "hashmap.h"
#include "profiler.h"

#include <stddef.h>
#include <string.h>

enum { SK_ECS_MAX_COMPONENT_TYPES = 256u };

/* Alignment of sk_entity_t derived from a standard-layout probe struct. */
typedef struct sk_ecs_align_probe_t {
	char probe;
	sk_entity_t member;
} sk_ecs_align_probe_t;

#define SK_ECS_ENTITY_ALIGN ((u32)offsetof(sk_ecs_align_probe_t, member))

/* Implicit entity component: column 0 of every chunk.
 * Brace-initialized (SK_TYPE_ID expands to a compound literal, which is not a
 * static-initializer constant) with the halves of SK_ECS_ENTITY_COMPONENT_ID. */
static const sk_component_info_t entity_component = {
	{0x72469fa65f436eeaULL, 0x8ebdf6025b899b6bULL},
	(u32)sizeof(sk_entity_t),
	SK_ECS_ENTITY_ALIGN,
	"entity",
};

/* ---- 16 KiB chunk storage ---- */

/*
 * A chunk is one fixed-size block: a small header followed by the dense
 * component columns. Column bases are placed at archetype-computed byte
 * offsets (archetype->column_offsets) relative to the chunk base, so the
 * whole block is allocated up front and columns are aligned within it.
 */
struct sk_chunk_t {
	const sk_archetype_t* archetype;
	u32 count;
};

/*
 * Archetype: a sorted component signature plus its precomputed 16 KiB chunk
 * layout. Owns a growable list of chunks (chunk_index in sk_entity_location_t
 * indexes this list). Fixed-capacity arrays (no heap for the hot layout).
 */
struct sk_archetype_t {
	u32 component_count;  /* incl. the implicit entity column */
	u32 chunk_capacity;	  /* max rows per chunk */
	u32 chunk_row_size;	  /* sum of column strides (dense bytes per entity) */
	u32 chunk_data_size;  /* bytes of the dense column region */
	u32 data_start;		  /* byte offset from chunk base to the first column */
	u32 chunk_align;	  /* allocation alignment for chunks of this archetype */
	u32 first_free_chunk; /* index of the first chunk with a free row (hint) */
	sk_component_info_t components[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	u32 column_offsets[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	u32 column_stride[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	SK_ARRAY(sk_chunk_t*) chunks;
};

/* ---- layout math ---- */

static u32 ecs_align_up(u32 value, u32 alignment) {
	const u32 rem = value % alignment;
	return (rem == 0u) ? value : (value + (alignment - rem));
}

static i32 ecs_type_id_lt(sk_type_id_t a, sk_type_id_t b) {
	return (a.lo < b.lo) || ((a.lo == b.lo) && (a.hi < b.hi));
}

/* Non-zero when the columns of @p comps fit in a chunk at @p capacity rows. */
static i32 ecs_layout_fits(const sk_component_info_t* comps, u32 count, u32 data_start, u32 capacity) {
	u32 offset = data_start;
	for (u32 i = 0u; i < count; ++i) {
		const u32 aligned = ecs_align_up(offset, comps[i].align);
		if (aligned > (u32)SK_ECS_CHUNK_SIZE) {
			return 0;
		}
		const u64 bytes = (u64)ecs_align_up(comps[i].size, comps[i].align) * (u64)capacity;
		if (bytes > (u64)((u32)SK_ECS_CHUNK_SIZE - aligned)) {
			return 0;
		}
		offset = aligned + (u32)bytes;
	}
	return 1;
}

/* Fill column bases (relative to the chunk base) and strides for @p capacity.
 * @p capacity must already be validated by ecs_layout_fits (no overflow). */
static u32 ecs_compute_layout(const sk_component_info_t* comps, u32 count, u32 data_start, u32 capacity, u32* out_offsets, u32* out_strides) {
	u32 offset = data_start;
	for (u32 i = 0u; i < count; ++i) {
		offset = ecs_align_up(offset, comps[i].align);
		const u32 stride = ecs_align_up(comps[i].size, comps[i].align);
		if (out_offsets != NULL) {
			out_offsets[i] = offset;
		}
		if (out_strides != NULL) {
			out_strides[i] = stride;
		}
		offset += stride * capacity;
	}
	return offset;
}

/* Largest row count that fits the given columns inside SK_ECS_CHUNK_SIZE. */
static u32 ecs_find_capacity(const sk_component_info_t* comps, u32 count, u32 data_start) {
	if (data_start >= (u32)SK_ECS_CHUNK_SIZE) {
		return 0u;
	}
	u64 total_stride = 0ull;
	for (u32 i = 0u; i < count; ++i) {
		total_stride += (u64)ecs_align_up(comps[i].size, comps[i].align);
	}
	/* Empty archetype / zero-size columns: no dense rows fit a stride of zero. */
	if (total_stride == 0ull) {
		return 0u;
	}
	/* Upper bound ignoring inter-column alignment padding. */
	u32 ub = (u32)(((u64)SK_ECS_CHUNK_SIZE - (u64)data_start) / total_stride);
	u32 lo = 0u;
	while (lo < ub) {
		const u32 mid = lo + (ub - lo + 1u) / 2u;
		if (ecs_layout_fits(comps, count, data_start, mid)) {
			lo = mid;
		} else {
			ub = mid - 1u;
		}
	}
	return lo;
}

/* ---- chunk allocation (single aligned 16 KiB block) ---- */

/* Allocate a chunk aligned to @p align (power of two). The original allocator
 * pointer is stored just below the aligned block so a plain free round-trips. */
static sk_chunk_t* ecs_chunk_alloc(u32 align) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const uintptr_t align_v = (uintptr_t)align;
	void_ptr_t raw = alloc->alloc(alloc->instance, (size_t)SK_ECS_CHUNK_SIZE + (size_t)align + sizeof(void_ptr_t));
	if (raw == NULL) {
		return NULL;
	}
	uintptr_t base = (uintptr_t)raw;
	uintptr_t aligned = (base + sizeof(void_ptr_t) + (align_v - 1u)) & ~(align_v - 1u);
	memcpy((void_ptr_t*)(aligned - sizeof(void_ptr_t)), &raw, sizeof(void_ptr_t));
	return (sk_chunk_t*)aligned;
}

static void ecs_chunk_free(sk_chunk_t* chunk) {
	const sk_allocator_t* alloc = sk_allocator_default();
	void_ptr_t raw;
	memcpy(&raw, (void_ptr_t*)((uintptr_t)chunk - sizeof(void_ptr_t)), sizeof(void_ptr_t));
	alloc->free(alloc->instance, raw);
}

/* ---- component registry (sk_type_id_t keyed; main-thread ownership) ---- */

static sk_component_info_t ecs_component_registry[SK_ECS_MAX_COMPONENT_TYPES];
static u32 ecs_component_count = 0u;

/* Instrumentation: the sk-profiler table is resolved at plugin entry and
 * re-checked at world_create (the host loads plugins in sorted filename order,
 * so sk-entities registers before sk-profiler; worlds are only created by
 * hosts after bootstrap, by which point the profiler is registered). The hot
 * getter is a plain cached load so uninstrumented builds (and hosts without
 * sk-profiler, e.g. the ECS benchmark) pay nothing per call; all zone macros
 * are NULL-safe no-ops when the profiler is absent. */
static sk_app_context_t* g_ecs_app_context = NULL;
static const sk_app_api_t* g_ecs_app_api = NULL;
static const sk_profiler_api_t* g_ecs_profiler_api = NULL;

static inline const sk_profiler_api_t* ecs_profiler_api(void) {
	return g_ecs_profiler_api;
}

static void ecs_resolve_profiler(void) {
	if (g_ecs_profiler_api == NULL && g_ecs_app_api != NULL) {
		g_ecs_profiler_api = (const sk_profiler_api_t*)g_ecs_app_api->get_api(g_ecs_app_context, SK_PROFILER_API_TYPE_ID);
	}
}

static i32 register_component_impl(sk_type_id_t type_id, u32 size, u32 align, const_chr_t name) {
	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO) || size == 0u || align == 0u) {
		return -3;
	}

	for (u32 i = 0u; i < ecs_component_count; ++i) {
		if (SK_TYPE_ID_EQ(ecs_component_registry[i].type_id, type_id)) {
			if (ecs_component_registry[i].size != size || ecs_component_registry[i].align != align) {
				return -1;
			}
			return 0;
		}
	}

	if (ecs_component_count >= (u32)SK_ECS_MAX_COMPONENT_TYPES) {
		return -2;
	}

	ecs_component_registry[ecs_component_count].type_id = type_id;
	ecs_component_registry[ecs_component_count].size = size;
	ecs_component_registry[ecs_component_count].align = align;
	ecs_component_registry[ecs_component_count].name = name;
	ecs_component_count += 1u;
	return 0;
}

static i32 component_info_impl(sk_type_id_t type_id, sk_component_info_t* out) {
	for (u32 i = 0u; i < ecs_component_count; ++i) {
		if (SK_TYPE_ID_EQ(ecs_component_registry[i].type_id, type_id)) {
			if (out != NULL) {
				*out = ecs_component_registry[i];
			}
			return 0;
		}
	}
	return -1;
}

/* ---- archetype API ---- */

/* Chunk API used by the archetype chunk list / location lookups (defined below). */
static sk_chunk_t* chunk_create_impl(const sk_archetype_t* archetype);
static void chunk_destroy_impl(sk_chunk_t* chunk);
static i32 chunk_find_impl(const sk_chunk_t* chunk, sk_entity_t entity, u32* out_row);

static sk_archetype_t* archetype_create_impl(const sk_component_info_t* components, u32 component_count) {
	if (component_count >= (u32)SK_ECS_MAX_ARCHETYPE_COLUMNS) {
		return NULL;
	}
	if (component_count > 0u && components == NULL) {
		return NULL;
	}

	sk_component_info_t user[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	for (u32 i = 0u; i < component_count; ++i) {
		const sk_component_info_t* comp = &components[i];
		if (comp->size == 0u || comp->align == 0u || (comp->align & (comp->align - 1u)) != 0u) {
			return NULL;
		}
		if (SK_TYPE_ID_EQ(comp->type_id, SK_TYPE_ID_ZERO) || SK_TYPE_ID_EQ(comp->type_id, SK_ECS_ENTITY_COMPONENT_ID)) {
			return NULL;
		}
		user[i] = *comp;
	}

	/* Normalize the signature: sort ascending by type id, then reject dupes. */
	for (u32 i = 1u; i < component_count; ++i) {
		const sk_component_info_t key = user[i];
		u32 j = i;
		while (j > 0u && ecs_type_id_lt(key.type_id, user[j - 1u].type_id)) {
			user[j] = user[j - 1u];
			j -= 1u;
		}
		user[j] = key;
	}
	for (u32 i = 1u; i < component_count; ++i) {
		if (SK_TYPE_ID_EQ(user[i - 1u].type_id, user[i].type_id)) {
			return NULL;
		}
	}

	/* Column 0 is the implicit entity component; user columns follow sorted. */
	sk_component_info_t all[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	const u32 all_count = component_count + 1u;
	all[0] = entity_component;
	for (u32 i = 0u; i < component_count; ++i) {
		all[i + 1u] = user[i];
	}

	u32 max_align = 1u;
	for (u32 i = 0u; i < all_count; ++i) {
		if (all[i].align > max_align) {
			max_align = all[i].align;
		}
	}

	const u32 data_start = ecs_align_up((u32)sizeof(sk_chunk_t), max_align);
	const u32 capacity = ecs_find_capacity(all, all_count, data_start);
	if (capacity == 0u) {
		return NULL;
	}

	u32 offsets[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	u32 strides[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	const u32 data_end = ecs_compute_layout(all, all_count, data_start, capacity, offsets, strides);

	const sk_allocator_t* alloc = sk_allocator_default();
	sk_archetype_t* a = (sk_archetype_t*)alloc->alloc(alloc->instance, sizeof(sk_archetype_t));
	if (a == NULL) {
		return NULL;
	}

	u64 row_size = 0ull;
	for (u32 i = 0u; i < all_count; ++i) {
		a->components[i] = all[i];
		a->column_offsets[i] = offsets[i];
		a->column_stride[i] = strides[i];
		row_size += (u64)strides[i];
	}

	a->component_count = all_count;
	a->chunk_capacity = capacity;
	a->chunk_row_size = (u32)row_size;
	a->chunk_data_size = data_end - data_start;
	a->data_start = data_start;
	a->chunk_align = (max_align > (u32)SK_ECS_CHUNK_ALIGNMENT) ? max_align : (u32)SK_ECS_CHUNK_ALIGNMENT;
	a->first_free_chunk = 0u;
	sk_array_init(&a->chunks, alloc);
	return a;
}

static void archetype_destroy_impl(sk_archetype_t* archetype) {
	const sk_allocator_t* alloc = sk_allocator_default();
	for (u32 i = 0u; i < archetype->chunks.count; ++i) {
		ecs_chunk_free(archetype->chunks.items[i]);
	}
	sk_array_free(&archetype->chunks);
	alloc->free(alloc->instance, archetype);
}

static u32 archetype_component_count_impl(const sk_archetype_t* archetype) {
	return archetype->component_count;
}

static sk_type_id_t archetype_component_id_impl(const sk_archetype_t* archetype, u32 column) {
	return archetype->components[column].type_id;
}

static u32 archetype_component_size_impl(const sk_archetype_t* archetype, u32 column) {
	return archetype->components[column].size;
}

static u32 archetype_component_align_impl(const sk_archetype_t* archetype, u32 column) {
	return archetype->components[column].align;
}

static i32 archetype_column_impl(const sk_archetype_t* archetype, sk_type_id_t type_id) {
	/* Column 0 is the implicit entity component; user columns (1..count-1)
	 * are sorted ascending by type id, so an exact lookup is a binary search
	 * instead of a linear scan. */
	if (SK_TYPE_ID_EQ(type_id, archetype->components[0u].type_id)) {
		return 0;
	}
	u32 lo = 1u;
	u32 hi = archetype->component_count;
	while (lo < hi) {
		const u32 mid = lo + (hi - lo) / 2u;
		if (ecs_type_id_lt(archetype->components[mid].type_id, type_id)) {
			lo = mid + 1u;
		} else {
			hi = mid;
		}
	}
	if (lo < archetype->component_count && SK_TYPE_ID_EQ(archetype->components[lo].type_id, type_id)) {
		return (i32)lo;
	}
	return -1;
}

static i32 archetype_has_impl(const sk_archetype_t* archetype, sk_type_id_t type_id) {
	return archetype_column_impl(archetype, type_id) >= 0;
}

static u32 archetype_column_offset_impl(const sk_archetype_t* archetype, u32 column) {
	return archetype->column_offsets[column];
}

static u32 archetype_column_stride_impl(const sk_archetype_t* archetype, u32 column) {
	return archetype->column_stride[column];
}

static u32 archetype_chunk_capacity_impl(const sk_archetype_t* archetype) {
	return archetype->chunk_capacity;
}

static u32 archetype_chunk_row_size_impl(const sk_archetype_t* archetype) {
	return archetype->chunk_row_size;
}

static u32 archetype_chunk_data_size_impl(const sk_archetype_t* archetype) {
	return archetype->chunk_data_size;
}

static i32 archetype_add_chunk_impl(sk_archetype_t* archetype) {
	sk_chunk_t* chunk = chunk_create_impl(archetype);
	if (chunk == NULL) {
		return -1;
	}
	if (sk_array_push(&archetype->chunks, chunk) != 0) {
		ecs_chunk_free(chunk);
		return -1;
	}
	return 0;
}

static u32 archetype_chunk_count_impl(const sk_archetype_t* archetype) {
	return archetype->chunks.count;
}

static sk_chunk_t* archetype_chunk_impl(const sk_archetype_t* archetype, u32 index) {
	if (index >= archetype->chunks.count) {
		return NULL;
	}
	return archetype->chunks.items[index];
}

static i32 archetype_location_impl(sk_archetype_t* archetype, sk_entity_t entity, sk_entity_location_t* out) {
	for (u32 chunk_index = 0u; chunk_index < archetype->chunks.count; ++chunk_index) {
		u32 row = 0u;
		if (chunk_find_impl(archetype->chunks.items[chunk_index], entity, &row) == 0) {
			if (out != NULL) {
				out->chunk = chunk_index;
				out->row = row;
			}
			return 0;
		}
	}
	return -1;
}

/* ---- chunk API ---- */

static sk_chunk_t* chunk_create_impl(const sk_archetype_t* archetype) {
	sk_chunk_t* chunk = ecs_chunk_alloc(archetype->chunk_align);
	if (chunk == NULL) {
		return NULL;
	}
	chunk->archetype = archetype;
	chunk->count = 0u;
	return chunk;
}

static void chunk_destroy_impl(sk_chunk_t* chunk) {
	ecs_chunk_free(chunk);
}

static const sk_archetype_t* chunk_archetype_impl(const sk_chunk_t* chunk) {
	return chunk->archetype;
}

static u32 chunk_count_impl(const sk_chunk_t* chunk) {
	return chunk->count;
}

static u32 chunk_capacity_impl(const sk_chunk_t* chunk) {
	return chunk->archetype->chunk_capacity;
}

static i32 chunk_full_impl(const sk_chunk_t* chunk) {
	return chunk->count >= chunk->archetype->chunk_capacity;
}

static const_ptr_t chunk_row_ptr_const(const sk_chunk_t* chunk, u32 column, u32 row) {
	const sk_archetype_t* archetype = chunk->archetype;
	const size_t offset = (size_t)archetype->column_offsets[column] + (size_t)row * archetype->column_stride[column];
	return (const_ptr_t)((const u8*)chunk + offset);
}

static void_ptr_t chunk_row_ptr_mut(sk_chunk_t* chunk, u32 column, u32 row) {
	const sk_archetype_t* archetype = chunk->archetype;
	const size_t offset = (size_t)archetype->column_offsets[column] + (size_t)row * archetype->column_stride[column];
	return (void_ptr_t)((u8*)chunk + offset);
}

static const_ptr_t chunk_column_impl(const sk_chunk_t* chunk, u32 column) {
	if (column >= chunk->archetype->component_count) {
		return NULL;
	}
	return chunk_row_ptr_const(chunk, column, 0u);
}

static void_ptr_t chunk_column_mut_impl(sk_chunk_t* chunk, u32 column) {
	if (column >= chunk->archetype->component_count) {
		return NULL;
	}
	return chunk_row_ptr_mut(chunk, column, 0u);
}

static const_ptr_t chunk_get_impl(const sk_chunk_t* chunk, u32 column, u32 row) {
	if (column >= chunk->archetype->component_count || row >= chunk->count) {
		return NULL;
	}
	return chunk_row_ptr_const(chunk, column, row);
}

static void_ptr_t chunk_get_mut_impl(sk_chunk_t* chunk, u32 column, u32 row) {
	if (column >= chunk->archetype->component_count || row >= chunk->count) {
		return NULL;
	}
	return chunk_row_ptr_mut(chunk, column, row);
}

static i32 chunk_allocate_impl(sk_chunk_t* chunk, sk_entity_t entity, u32* out_row) {
	const sk_archetype_t* archetype = chunk->archetype;
	if (chunk->count >= archetype->chunk_capacity) {
		return -1;
	}
	const u32 row = chunk->count;
	if (out_row != NULL) {
		*out_row = row;
	}
	memcpy(chunk_row_ptr_mut(chunk, 0u, row), &entity, sizeof(sk_entity_t));
	chunk->count += 1u;
	return 0;
}

static void chunk_remove_impl(sk_chunk_t* chunk, u32 row, sk_entity_t* out_moved) {
	const sk_archetype_t* archetype = chunk->archetype;
	sk_entity_t moved = SK_ENTITY_INVALID;
	const u32 last = chunk->count - 1u;
	if (row != last) {
		memcpy(&moved, chunk_row_ptr_const(chunk, 0u, last), sizeof(sk_entity_t));
		for (u32 column = 0u; column < archetype->component_count; ++column) {
			memcpy(chunk_row_ptr_mut(chunk, column, row), chunk_row_ptr_const(chunk, column, last), (size_t)archetype->components[column].size);
		}
	}
	if (out_moved != NULL) {
		*out_moved = moved;
	}
	chunk->count = last;
}

static sk_entity_t chunk_entity_impl(const sk_chunk_t* chunk, u32 row) {
	sk_entity_t entity;
	memcpy(&entity, chunk_row_ptr_const(chunk, 0u, row), sizeof(sk_entity_t));
	return entity;
}

static i32 chunk_find_impl(const sk_chunk_t* chunk, sk_entity_t entity, u32* out_row) {
	for (u32 row = 0u; row < chunk->count; ++row) {
		sk_entity_t at;
		memcpy(&at, chunk_row_ptr_const(chunk, 0u, row), sizeof(sk_entity_t));
		if (sk_entity_eq(at, entity)) {
			if (out_row != NULL) {
				*out_row = row;
			}
			return 0;
		}
	}
	return -1;
}

/* ---- query API ---- */

/*
 * Query: required / optional / excluded component sets plus the matched
 * archetype list. Terms are laid out as term 0 = implicit entity component,
 * then the required ids in descriptor order, then the optional ids in
 * descriptor order; required[] records which terms are mandatory (term 0 is
 * always required). Matched archetypes are appended by query_observe (driven
 * by the world when an archetype is created); iteration walks their chunks
 * through query_iter_next, skipping empty chunks.
 */
struct sk_query_t {
	u32 term_count;
	sk_type_id_t terms[SK_ECS_MAX_QUERY_TERMS];
	u32 required[SK_ECS_MAX_QUERY_TERMS];
	u32 excluded_count;
	sk_type_id_t excluded[SK_ECS_MAX_QUERY_TERMS];
	SK_ARRAY(sk_archetype_t*) archetypes;
};

static sk_query_t* query_create_impl(const sk_query_desc_t* desc) {
	if (desc == NULL) {
		return NULL;
	}
	const u32 term_count = 1u + desc->required_count + desc->optional_count;
	if (term_count > (u32)SK_ECS_MAX_QUERY_TERMS || desc->excluded_count > (u32)SK_ECS_MAX_QUERY_TERMS) {
		return NULL;
	}
	if ((desc->required_count > 0u && desc->required == NULL) || (desc->optional_count > 0u && desc->optional == NULL) || (desc->excluded_count > 0u && desc->excluded == NULL)) {
		return NULL;
	}
	for (u32 i = 0u; i < desc->required_count; ++i) {
		if (SK_TYPE_ID_EQ(desc->required[i], SK_TYPE_ID_ZERO)) {
			return NULL;
		}
	}
	for (u32 i = 0u; i < desc->optional_count; ++i) {
		if (SK_TYPE_ID_EQ(desc->optional[i], SK_TYPE_ID_ZERO)) {
			return NULL;
		}
	}
	for (u32 i = 0u; i < desc->excluded_count; ++i) {
		if (SK_TYPE_ID_EQ(desc->excluded[i], SK_TYPE_ID_ZERO)) {
			return NULL;
		}
	}

	/* A type id may appear at most once among the query terms: each term maps
	 * 1:1 to a component column, so a duplicate would be ambiguous. */
	for (u32 i = 0u; i < desc->required_count; ++i) {
		for (u32 j = i + 1u; j < desc->required_count; ++j) {
			if (SK_TYPE_ID_EQ(desc->required[i], desc->required[j])) {
				return NULL;
			}
		}
	}
	for (u32 i = 0u; i < desc->optional_count; ++i) {
		for (u32 j = i + 1u; j < desc->optional_count; ++j) {
			if (SK_TYPE_ID_EQ(desc->optional[i], desc->optional[j])) {
				return NULL;
			}
		}
		for (u32 j = 0u; j < desc->required_count; ++j) {
			if (SK_TYPE_ID_EQ(desc->optional[i], desc->required[j])) {
				return NULL;
			}
		}
	}

	const sk_allocator_t* alloc = sk_allocator_default();
	sk_query_t* q = (sk_query_t*)alloc->alloc(alloc->instance, sizeof(sk_query_t));
	if (q == NULL) {
		return NULL;
	}
	sk_array_init(&q->archetypes, alloc);

	q->term_count = term_count;
	q->excluded_count = desc->excluded_count;
	q->terms[0] = SK_ECS_ENTITY_COMPONENT_ID;
	q->required[0] = 1u;
	for (u32 i = 0u; i < desc->required_count; ++i) {
		q->terms[1u + i] = desc->required[i];
		q->required[1u + i] = 1u;
	}
	for (u32 i = 0u; i < desc->optional_count; ++i) {
		q->terms[1u + desc->required_count + i] = desc->optional[i];
		q->required[1u + desc->required_count + i] = 0u;
	}
	for (u32 i = 0u; i < desc->excluded_count; ++i) {
		q->excluded[i] = desc->excluded[i];
	}
	return q;
}

static void query_destroy_impl(sk_query_t* query) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_array_free(&query->archetypes);
	alloc->free(alloc->instance, query);
}

static u32 query_term_count_impl(const sk_query_t* query) {
	return query->term_count;
}

static sk_type_id_t query_term_id_impl(const sk_query_t* query, u32 term) {
	return query->terms[term];
}

static i32 query_term_required_impl(const sk_query_t* query, u32 term) {
	return (i32)query->required[term];
}

static i32 query_matches_impl(const sk_query_t* query, const sk_archetype_t* archetype) {
	for (u32 i = 0u; i < query->term_count; ++i) {
		if (query->required[i] != 0u && !archetype_has_impl(archetype, query->terms[i])) {
			return 0;
		}
	}
	for (u32 i = 0u; i < query->excluded_count; ++i) {
		if (archetype_has_impl(archetype, query->excluded[i])) {
			return 0;
		}
	}
	return 1;
}

static i32 query_observe_impl(sk_query_t* query, sk_archetype_t* archetype) {
	if (!query_matches_impl(query, archetype)) {
		return 1;
	}
	for (u32 i = 0u; i < query->archetypes.count; ++i) {
		if (query->archetypes.items[i] == archetype) {
			return 0;
		}
	}
	if (sk_array_push(&query->archetypes, archetype) != 0) {
		return -1;
	}
	return 0;
}

static u32 query_archetype_count_impl(const sk_query_t* query) {
	return query->archetypes.count;
}

static const sk_archetype_t* query_archetype_impl(const sk_query_t* query, u32 index) {
	if (index >= query->archetypes.count) {
		return NULL;
	}
	return query->archetypes.items[index];
}

static i32 query_iter_next_impl(sk_query_iter_t* it) {
	const sk_query_t* query = it->query;
	if (query == NULL) {
		return 0;
	}
	it->term_count = query->term_count;
	while (it->archetype_index < query->archetypes.count) {
		sk_archetype_t* archetype = query->archetypes.items[it->archetype_index];
		while (it->chunk_index < archetype->chunks.count) {
			sk_chunk_t* chunk = archetype->chunks.items[it->chunk_index];
			it->chunk_index += 1u;
			if (chunk->count == 0u) {
				continue;
			}
			it->count = chunk->count;
			for (u32 term = 0u; term < query->term_count; ++term) {
				const i32 column = archetype_column_impl(archetype, query->terms[term]);
				if (column < 0) {
					it->fields[term] = NULL;
					it->strides[term] = 0u;
				} else {
					it->fields[term] = chunk_row_ptr_mut(chunk, (u32)column, 0u);
					it->strides[term] = archetype->column_stride[(u32)column];
				}
			}
			return 1;
		}
		it->archetype_index += 1u;
		it->chunk_index = 0u;
	}
	return 0;
}

static sk_entity_t query_iter_entity_impl(const sk_query_iter_t* it, u32 row) {
	sk_entity_t entity;
	memcpy(&entity, (const u8*)it->fields[0] + (size_t)row * it->strides[0], sizeof(sk_entity_t));
	return entity;
}

static void_ptr_t query_iter_field_impl(const sk_query_iter_t* it, u32 term, u32 row) {
	if (it->fields[term] == NULL) {
		return NULL;
	}
	return (void_ptr_t)((u8*)it->fields[term] + (size_t)row * it->strides[term]);
}

/* ---- world ---- */

/*
 * A world owns an archetype table (created lazily from spawn/add/remove
 * signatures, retained even when empty), a dense entity index whose slots
 * carry a generation counter (slot index + 1 == entity.index) and cache the
 * entity's { archetype, chunk, row } location, a free list of recycled slot
 * indices, and the world-managed query set that observes every archetype the
 * world creates. Slots are append-only; despawned slots are recycled with a
 * bumped generation so stale handles fail the generation check.
 */
typedef struct sk_entity_slot_t {
	u32 generation;
	u32 archetype_index; /* UINT32_MAX when the slot is dead */
	u32 chunk;
	u32 row;
} sk_entity_slot_t;

struct sk_world_t {
	SK_ARRAY(sk_archetype_t*) archetypes;
	SK_ARRAY(sk_entity_slot_t) slots;
	SK_ARRAY(u32) free_slots;
	SK_ARRAY(sk_query_t*) queries;
	SK_HASH_MAP(u64, u32) archetype_cache; /* signature hash -> archetypes index */
	u32 alive_count;
};

/* Live slot for @p entity, or NULL when dead / invalid / stale generation. */
static sk_entity_slot_t* world_slot_at(sk_world_t* world, sk_entity_t entity) {
	if (entity.index == 0u) {
		return NULL;
	}
	const u32 slot_index = entity.index - 1u;
	if (slot_index >= world->slots.count) {
		return NULL;
	}
	sk_entity_slot_t* slot = &world->slots.items[slot_index];
	if (slot->generation != entity.generation || slot->archetype_index == 0xFFFFFFFFu) {
		return NULL;
	}
	return slot;
}

/* Record that @p chunk_index now has a free row (a slot was removed there),
 * so later allocations prefer reusing it over appending a new chunk. */
static void archetype_note_free_chunk(sk_archetype_t* archetype, u32 chunk_index) {
	if (chunk_index < archetype->first_free_chunk) {
		archetype->first_free_chunk = chunk_index;
	}
}

/* First chunk with a free row in @p archetype, appending one when needed.
 * first_free_chunk stays at the first non-full chunk: it is advanced to the
 * chunk an allocation lands in and pulled back by archetype_note_free_chunk
 * whenever a row is freed earlier, so the scan only ever walks full chunks. */
static sk_chunk_t* archetype_free_chunk(sk_archetype_t* archetype, u32* out_chunk_index) {
	for (u32 i = archetype->first_free_chunk; i < archetype->chunks.count; ++i) {
		sk_chunk_t* chunk = archetype->chunks.items[i];
		if (chunk->count < archetype->chunk_capacity) {
			archetype->first_free_chunk = i;
			*out_chunk_index = i;
			return chunk;
		}
	}
	if (archetype_add_chunk_impl(archetype) != 0) {
		return NULL;
	}
	archetype->first_free_chunk = archetype->chunks.count - 1u;
	*out_chunk_index = archetype->chunks.count - 1u;
	return archetype->chunks.items[*out_chunk_index];
}

/*
 * Sort @p ids ascending by type id and drop duplicates into @p out (which
 * must hold SK_ECS_MAX_ARCHETYPE_COLUMNS entries). Returns the normalized
 * count, or UINT32_MAX when @p count is too large or any id is
 * SK_TYPE_ID_ZERO / the implicit entity component.
 */
static u32 ecs_normalize_ids(const sk_type_id_t* ids, u32 count, sk_type_id_t* out) {
	if (count >= (u32)SK_ECS_MAX_ARCHETYPE_COLUMNS) {
		return 0xFFFFFFFFu;
	}
	sk_type_id_t tmp[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	u32 n = 0u;
	for (u32 i = 0u; i < count; ++i) {
		if (SK_TYPE_ID_EQ(ids[i], SK_TYPE_ID_ZERO) || SK_TYPE_ID_EQ(ids[i], SK_ECS_ENTITY_COMPONENT_ID)) {
			return 0xFFFFFFFFu;
		}
		tmp[n++] = ids[i];
	}
	for (u32 i = 1u; i < n; ++i) {
		const sk_type_id_t key = tmp[i];
		u32 j = i;
		while (j > 0u && ecs_type_id_lt(key, tmp[j - 1u])) {
			tmp[j] = tmp[j - 1u];
			j -= 1u;
		}
		tmp[j] = key;
	}
	u32 out_count = 0u;
	for (u32 i = 0u; i < n; ++i) {
		if (out_count > 0u && SK_TYPE_ID_EQ(out[out_count - 1u], tmp[i])) {
			continue;
		}
		out[out_count++] = tmp[i];
	}
	return out_count;
}

/* Hash a u64 archetype-signature key (hashmap hash_fn wrapper). */
static u64 ecs_hash_u64_key(const void* key) {
	return sk_hash_u64(*(const u64*)key);
}

/* 64-bit hash of a normalized (sorted, deduplicated) component signature.
 * Equal signatures always hash alike; collisions are possible in theory and
 * resolved by re-verifying the signature after the cache lookup. Cheap
 * xor/fold mix: one multiply + two xors per id (the map re-hashes the key
 * with sk_hash_u64 for table placement). */
static u64 ecs_signature_hash(const sk_type_id_t* ids, u32 count) {
	u64 h = 14695981039346656037ull;
	for (u32 i = 0u; i < count; ++i) {
		h ^= ids[i].lo;
		h *= 1099511628211ull;
		h ^= ids[i].hi;
		h *= 1099511628211ull;
	}
	h ^= (u64)count * 0x9e3779b97f4a7c15ull;
	return h;
}

/*
 * Find the archetype for the normalized signature @p ids (or create it from
 * the component registry on demand). Newly created archetypes are observed by
 * every world-managed query. Returns the archetype's index in
 * world->archetypes, or UINT32_MAX on invalid ids / unregistered components /
 * OOM.
 */
static u32 world_archetype_for(sk_world_t* world, const sk_type_id_t* ids, u32 count) {
	sk_type_id_t norm[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	const u32 ncount = ecs_normalize_ids(ids, count, norm);
	if (ncount == 0xFFFFFFFFu) {
		return 0xFFFFFFFFu;
	}
	const u64 sig_hash = ecs_signature_hash(norm, ncount);

	/* Fast path: the signature hash was cached for an existing archetype.
	 * The signature is re-verified in case two distinct signatures ever
	 * collide; a mismatch falls through to the linear scan. */
	u32 cached = 0u;
	if (sk_hash_map_get(&world->archetype_cache, sig_hash, &cached) == 0 && cached < world->archetypes.count) {
		const sk_archetype_t* arch = world->archetypes.items[cached];
		if (arch->component_count == ncount + 1u) {
			i32 match = 1;
			for (u32 c = 0u; c < ncount; ++c) {
				if (!SK_TYPE_ID_EQ(arch->components[c + 1u].type_id, norm[c])) {
					match = 0;
					break;
				}
			}
			if (match != 0) {
				return cached;
			}
		}
	}

	for (u32 i = 0u; i < world->archetypes.count; ++i) {
		const sk_archetype_t* arch = world->archetypes.items[i];
		if (arch->component_count != ncount + 1u) {
			continue;
		}
		i32 match = 1;
		for (u32 c = 0u; c < ncount; ++c) {
			if (!SK_TYPE_ID_EQ(arch->components[c + 1u].type_id, norm[c])) {
				match = 0;
				break;
			}
		}
		if (match != 0) {
			return i;
		}
	}
	sk_component_info_t infos[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	for (u32 i = 0u; i < ncount; ++i) {
		if (component_info_impl(norm[i], &infos[i]) != 0) {
			return 0xFFFFFFFFu;
		}
	}
	sk_archetype_t* arch = archetype_create_impl(infos, ncount);
	if (arch == NULL) {
		return 0xFFFFFFFFu;
	}
	if (sk_array_push(&world->archetypes, arch) != 0) {
		archetype_destroy_impl(arch);
		return 0xFFFFFFFFu;
	}
	/* Cache is a performance optimization: an insert failure (OOM) must not
	 * fail the spawn; the linear scan above remains a correct fallback. */
	(void)sk_hash_map_put(&world->archetype_cache, sig_hash, world->archetypes.count - 1u);
	for (u32 i = 0u; i < world->queries.count; ++i) {
		query_observe_impl(world->queries.items[i], arch);
	}
	return world->archetypes.count - 1u;
}

/*
 * Move @p entity from its current archetype to @p new_arch_index: copy every
 * shared component into a freshly allocated target row, zero-fill new
 * components, then swap-remove the old row (updating the location of the
 * entity that fills the gap). The slot is updated to the new location.
 */
static i32 world_move_entity(sk_world_t* world, sk_entity_t entity, sk_entity_slot_t* slot, u32 new_arch_index) {
	sk_archetype_t* old_arch = world->archetypes.items[slot->archetype_index];
	const u32 old_chunk_index = slot->chunk;
	sk_chunk_t* old_chunk = old_arch->chunks.items[old_chunk_index];
	const u32 old_row = slot->row;

	sk_archetype_t* new_arch = world->archetypes.items[new_arch_index];
	u32 new_chunk_index = 0u;
	sk_chunk_t* new_chunk = archetype_free_chunk(new_arch, &new_chunk_index);
	if (new_chunk == NULL) {
		return -1;
	}
	const u32 new_row = new_chunk->count;

	memcpy(chunk_row_ptr_mut(new_chunk, 0u, new_row), &entity, sizeof(sk_entity_t));
	for (u32 column = 1u; column < new_arch->component_count; ++column) {
		const i32 old_column = archetype_column_impl(old_arch, new_arch->components[column].type_id);
		void_ptr_t dst = chunk_row_ptr_mut(new_chunk, column, new_row);
		if (old_column >= 0) {
			memcpy(dst, chunk_row_ptr_const(old_chunk, (u32)old_column, old_row), new_arch->components[column].size);
		} else {
			memset(dst, 0, new_arch->components[column].size);
		}
	}
	new_chunk->count += 1u;

	sk_entity_t moved = SK_ENTITY_INVALID;
	chunk_remove_impl(old_chunk, old_row, &moved);
	archetype_note_free_chunk(old_arch, old_chunk_index);
	if (sk_entity_is_valid(moved)) {
		sk_entity_slot_t* moved_slot = world_slot_at(world, moved);
		if (moved_slot != NULL) {
			moved_slot->chunk = old_chunk_index;
			moved_slot->row = old_row;
		}
	}
	slot->archetype_index = new_arch_index;
	slot->chunk = new_chunk_index;
	slot->row = new_row;
	return 0;
}

static sk_world_t* world_create_impl(void) {
	ecs_resolve_profiler();
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_world_t* world = (sk_world_t*)alloc->alloc(alloc->instance, sizeof(sk_world_t));
	if (world == NULL) {
		return NULL;
	}
	sk_array_init(&world->archetypes, alloc);
	sk_array_init(&world->slots, alloc);
	sk_array_init(&world->free_slots, alloc);
	sk_array_init(&world->queries, alloc);
	if (sk_hash_map_init(&world->archetype_cache, alloc, ecs_hash_u64_key, NULL) != 0) {
		alloc->free(alloc->instance, world);
		return NULL;
	}
	world->alive_count = 0u;
	return world;
}

static void world_destroy_impl(sk_world_t* world) {
	const sk_allocator_t* alloc = sk_allocator_default();
	for (u32 i = 0u; i < world->queries.count; ++i) {
		query_destroy_impl(world->queries.items[i]);
	}
	for (u32 i = 0u; i < world->archetypes.count; ++i) {
		archetype_destroy_impl(world->archetypes.items[i]);
	}
	sk_array_free(&world->queries);
	sk_array_free(&world->archetypes);
	sk_array_free(&world->slots);
	sk_array_free(&world->free_slots);
	sk_hash_map_free(&world->archetype_cache);
	alloc->free(alloc->instance, world);
}

static sk_entity_t world_spawn_impl(sk_world_t* world, const sk_type_id_t* component_ids, u32 component_count) {
	SK_PROFILE_CPU_ZONE(ecs_profiler_api(), "ecs spawn");
	if (component_count > 0u && component_ids == NULL) {
		return SK_ENTITY_INVALID;
	}
	const u32 archetype_index = world_archetype_for(world, component_ids, component_count);
	if (archetype_index == 0xFFFFFFFFu) {
		return SK_ENTITY_INVALID;
	}
	sk_archetype_t* arch = world->archetypes.items[archetype_index];
	u32 chunk_index = 0u;
	sk_chunk_t* chunk = archetype_free_chunk(arch, &chunk_index);
	if (chunk == NULL) {
		return SK_ENTITY_INVALID;
	}

	u32 slot_index = 0u;
	if (world->free_slots.count > 0u) {
		slot_index = sk_array_pop(&world->free_slots);
	} else {
		const sk_entity_slot_t fresh = {0u, 0xFFFFFFFFu, 0u, 0u};
		if (sk_array_push(&world->slots, fresh) != 0) {
			return SK_ENTITY_INVALID;
		}
		slot_index = world->slots.count - 1u;
	}
	sk_entity_slot_t* slot = &world->slots.items[slot_index];
	if (slot->generation == 0u) {
		slot->generation = 1u;
	}
	const sk_entity_t entity = {slot_index + 1u, slot->generation};

	u32 row = 0u;
	chunk_allocate_impl(chunk, entity, &row);
	for (u32 column = 1u; column < arch->component_count; ++column) {
		memset(chunk_row_ptr_mut(chunk, column, row), 0, arch->components[column].size);
	}
	slot->archetype_index = archetype_index;
	slot->chunk = chunk_index;
	slot->row = row;
	world->alive_count += 1u;
	return entity;
}

static i32 world_despawn_impl(sk_world_t* world, sk_entity_t entity) {
	SK_PROFILE_CPU_ZONE(ecs_profiler_api(), "ecs despawn");
	sk_entity_slot_t* slot = world_slot_at(world, entity);
	if (slot == NULL) {
		return -1;
	}
	sk_archetype_t* arch = world->archetypes.items[slot->archetype_index];
	sk_chunk_t* chunk = arch->chunks.items[slot->chunk];
	const u32 row = slot->row;
	const u32 slot_index = entity.index - 1u;

	sk_entity_t moved = SK_ENTITY_INVALID;
	chunk_remove_impl(chunk, row, &moved);
	archetype_note_free_chunk(arch, slot->chunk);
	if (sk_entity_is_valid(moved)) {
		sk_entity_slot_t* moved_slot = world_slot_at(world, moved);
		if (moved_slot != NULL) {
			moved_slot->chunk = slot->chunk;
			moved_slot->row = row;
		}
	}
	slot->generation += 1u;
	if (slot->generation == 0u) {
		slot->generation = 1u;
	}
	slot->archetype_index = 0xFFFFFFFFu;
	slot->chunk = 0u;
	slot->row = 0u;
	if (sk_array_push(&world->free_slots, slot_index) != 0) {
		/* The slot is lost (already dead); still report a successful despawn. */
	}
	world->alive_count -= 1u;
	return 0;
}

static i32 world_alive_impl(sk_world_t* world, sk_entity_t entity) {
	return world_slot_at(world, entity) != NULL;
}

static u32 world_count_impl(const sk_world_t* world) {
	return world->alive_count;
}

static void_ptr_t world_component_impl(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id) {
	sk_entity_slot_t* slot = world_slot_at(world, entity);
	if (slot == NULL) {
		return NULL;
	}
	const sk_archetype_t* arch = world->archetypes.items[slot->archetype_index];
	const i32 column = archetype_column_impl(arch, type_id);
	if (column < 0) {
		return NULL;
	}
	sk_chunk_t* chunk = arch->chunks.items[slot->chunk];
	return chunk_row_ptr_mut(chunk, (u32)column, slot->row);
}

static i32 world_has_component_impl(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id) {
	return world_component_impl(world, entity, type_id) != NULL;
}

static i32 world_add_component_impl(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id) {
	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO) || SK_TYPE_ID_EQ(type_id, SK_ECS_ENTITY_COMPONENT_ID)) {
		return -1;
	}
	sk_entity_slot_t* slot = world_slot_at(world, entity);
	if (slot == NULL) {
		return -1;
	}
	const sk_archetype_t* arch = world->archetypes.items[slot->archetype_index];
	if (archetype_has_impl(arch, type_id)) {
		return 0;
	}
	sk_type_id_t ids[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	u32 count = arch->component_count - 1u;
	for (u32 i = 0u; i < count; ++i) {
		ids[i] = arch->components[i + 1u].type_id;
	}
	ids[count++] = type_id;
	const u32 target = world_archetype_for(world, ids, count);
	if (target == 0xFFFFFFFFu) {
		return -1;
	}
	return world_move_entity(world, entity, slot, target);
}

static i32 world_remove_component_impl(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id) {
	sk_entity_slot_t* slot = world_slot_at(world, entity);
	if (slot == NULL) {
		return -1;
	}
	const sk_archetype_t* arch = world->archetypes.items[slot->archetype_index];
	if (!archetype_has_impl(arch, type_id)) {
		return 0;
	}
	sk_type_id_t ids[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	u32 count = 0u;
	for (u32 i = 1u; i < arch->component_count; ++i) {
		if (!SK_TYPE_ID_EQ(arch->components[i].type_id, type_id)) {
			ids[count++] = arch->components[i].type_id;
		}
	}
	const u32 target = world_archetype_for(world, ids, count);
	if (target == 0xFFFFFFFFu) {
		return -1;
	}
	return world_move_entity(world, entity, slot, target);
}

static sk_query_t* world_query_create_impl(sk_world_t* world, const sk_query_desc_t* desc) {
	sk_query_t* query = query_create_impl(desc);
	if (query == NULL) {
		return NULL;
	}
	for (u32 i = 0u; i < world->archetypes.count; ++i) {
		query_observe_impl(query, world->archetypes.items[i]);
	}
	if (sk_array_push(&world->queries, query) != 0) {
		query_destroy_impl(query);
		return NULL;
	}
	return query;
}

/* ---- systems ---- */

/*
 * A system is a callback plus its declared read/write component sets (copied
 * at creation). The sets drive the scheduler's dependency graph: a shared
 * component that one side writes orders that side first; read-read overlaps
 * impose no order.
 */
struct sk_system_t {
	void (*callback)(sk_world_t* world, f32 delta_time, void_ptr_t user_data);
	sk_type_id_t reads[SK_ECS_MAX_SYSTEM_COMPONENTS];
	u32 read_count;
	sk_type_id_t writes[SK_ECS_MAX_SYSTEM_COMPONENTS];
	u32 write_count;
	const_chr_t name;
	void_ptr_t user_data;
};

/* Non-zero when @p ids is a valid component set: count in range, non-NULL
 * array when non-empty, no zero id, no duplicates within the set. */
static i32 ecs_system_ids_valid(const sk_type_id_t* ids, u32 count) {
	if (count > (u32)SK_ECS_MAX_SYSTEM_COMPONENTS) {
		return 0;
	}
	if (count > 0u && ids == NULL) {
		return 0;
	}
	for (u32 i = 0u; i < count; ++i) {
		if (SK_TYPE_ID_EQ(ids[i], SK_TYPE_ID_ZERO)) {
			return 0;
		}
		for (u32 j = i + 1u; j < count; ++j) {
			if (SK_TYPE_ID_EQ(ids[i], ids[j])) {
				return 0;
			}
		}
	}
	return 1;
}

static sk_system_t* system_create_impl(const sk_system_desc_t* desc) {
	if (desc == NULL || desc->callback == NULL) {
		return NULL;
	}
	if (!ecs_system_ids_valid(desc->reads, desc->read_count) || !ecs_system_ids_valid(desc->writes, desc->write_count)) {
		return NULL;
	}
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_system_t* system = (sk_system_t*)alloc->alloc(alloc->instance, sizeof(sk_system_t));
	if (system == NULL) {
		return NULL;
	}
	system->callback = desc->callback;
	system->read_count = desc->read_count;
	system->write_count = desc->write_count;
	system->name = desc->name;
	system->user_data = desc->user_data;
	if (desc->read_count > 0u) {
		memcpy(system->reads, desc->reads, (size_t)desc->read_count * sizeof(sk_type_id_t));
	}
	if (desc->write_count > 0u) {
		memcpy(system->writes, desc->writes, (size_t)desc->write_count * sizeof(sk_type_id_t));
	}
	return system;
}

static void system_destroy_impl(sk_system_t* system) {
	const sk_allocator_t* alloc = sk_allocator_default();
	alloc->free(alloc->instance, system);
}

static u32 system_read_count_impl(const sk_system_t* system) {
	return system->read_count;
}

static u32 system_write_count_impl(const sk_system_t* system) {
	return system->write_count;
}

static sk_type_id_t system_read_id_impl(const sk_system_t* system, u32 index) {
	return system->reads[index];
}

static sk_type_id_t system_write_id_impl(const sk_system_t* system, u32 index) {
	return system->writes[index];
}

static const_chr_t system_name_impl(const sk_system_t* system) {
	return system->name;
}

static void system_run_impl(sk_system_t* system, sk_world_t* world, f32 delta_time) {
	system->callback(world, delta_time, system->user_data);
}

/* ---- scheduler (dependency-graph topo sort) ---- */

/*
 * A scheduler owns its systems and a flat N*N adjacency matrix (adj[i*N+j]
 * == 1 means system i must run before system j). The matrix, indegree array,
 * and the built order are recomputed by scheduler_build whenever systems are
 * added. The order is deterministic: Kahn's algorithm always picks the
 * lowest-index zero-indegree system, so registration order breaks ties among
 * independent systems.
 */
struct sk_scheduler_t {
	SK_ARRAY(sk_system_t*) systems;
	SK_ARRAY(u8) adj;		/* N*N flat matrix (N = systems.count) */
	SK_ARRAY(u32) indegree; /* per-system remaining predecessor count */
	SK_ARRAY(u32) order;	/* built run order (system indices) */
	u32 matrix_n;			/* systems.count at the last build */
	i32 built;				/* order is fresh w.r.t. systems */
	i32 has_cycle;
};

/*
 * Whether system @p i must run before system @p j.
 *
 * Only real conflicts produce an edge:
 *  - write-write: both write the same id; both directions are in conflict, so
 *    the earlier-registered (lower index) system runs first.
 *  - write-read: @p i writes an id @p j reads.
 * The read-write mirror is covered when the pair is tested the other way
 * around (a reader never gets an edge onto a writer). Read-read overlaps and
 * disjoint sets add no edge.
 */
static i32 ecs_systems_conflict(const sk_scheduler_t* scheduler, u32 i, u32 j) {
	const sk_system_t* a = scheduler->systems.items[i];
	const sk_system_t* b = scheduler->systems.items[j];
	for (u32 wi = 0u; wi < a->write_count; ++wi) {
		for (u32 wj = 0u; wj < b->write_count; ++wj) {
			if (SK_TYPE_ID_EQ(a->writes[wi], b->writes[wj])) {
				return (i < j) ? 1 : 0;
			}
		}
	}
	for (u32 wi = 0u; wi < a->write_count; ++wi) {
		for (u32 rj = 0u; rj < b->read_count; ++rj) {
			if (SK_TYPE_ID_EQ(a->writes[wi], b->reads[rj])) {
				return 1;
			}
		}
	}
	return 0;
}

static i32 scheduler_rebuild(sk_scheduler_t* scheduler) {
	const u32 n = scheduler->systems.count;

	scheduler->built = 0;
	scheduler->has_cycle = 0;
	scheduler->matrix_n = n;
	sk_array_clear(&scheduler->order);

	if (n == 0u) {
		scheduler->built = 1;
		return 0;
	}
	if (sk_array_resize(&scheduler->adj, n * n) != 0 || sk_array_resize(&scheduler->indegree, n) != 0) {
		return -2;
	}
	if (sk_array_reserve(&scheduler->order, n) != 0) {
		return -2;
	}
	memset(scheduler->adj.items, 0, (size_t)n * n);
	memset(scheduler->indegree.items, 0, (size_t)n * sizeof(u32));

	for (u32 i = 0u; i < n; ++i) {
		for (u32 j = 0u; j < n; ++j) {
			if (i == j || ecs_systems_conflict(scheduler, i, j) == 0) {
				continue;
			}
			scheduler->adj.items[i * n + j] = 1u;
			scheduler->indegree.items[j] += 1u;
		}
	}

	u8 processed[SK_ECS_MAX_SYSTEMS];
	memset(processed, 0, sizeof(processed));

	for (u32 emitted = 0u; emitted < n; ++emitted) {
		u32 pick = n;
		for (u32 i = 0u; i < n; ++i) {
			if (processed[i] == 0u && scheduler->indegree.items[i] == 0u) {
				pick = i;
				break;
			}
		}
		if (pick == n) {
			scheduler->has_cycle = 1;
			scheduler->built = 1;
			return -1;
		}
		processed[pick] = 1u;
		if (sk_array_push(&scheduler->order, pick) != 0) {
			return -2;
		}
		for (u32 j = 0u; j < n; ++j) {
			if (scheduler->adj.items[pick * n + j] != 0u) {
				scheduler->indegree.items[j] -= 1u;
			}
		}
	}
	scheduler->built = 1;
	return 0;
}

static sk_scheduler_t* scheduler_create_impl(void) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_scheduler_t* scheduler = (sk_scheduler_t*)alloc->alloc(alloc->instance, sizeof(sk_scheduler_t));
	if (scheduler == NULL) {
		return NULL;
	}
	sk_array_init(&scheduler->systems, alloc);
	sk_array_init(&scheduler->adj, alloc);
	sk_array_init(&scheduler->indegree, alloc);
	sk_array_init(&scheduler->order, alloc);
	scheduler->matrix_n = 0u;
	scheduler->built = 0;
	scheduler->has_cycle = 0;
	return scheduler;
}

static void scheduler_destroy_impl(sk_scheduler_t* scheduler) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_array_free(&scheduler->systems);
	sk_array_free(&scheduler->adj);
	sk_array_free(&scheduler->indegree);
	sk_array_free(&scheduler->order);
	alloc->free(alloc->instance, scheduler);
}

static i32 scheduler_add_impl(sk_scheduler_t* scheduler, sk_system_t* system) {
	if (scheduler == NULL || system == NULL) {
		return -1;
	}
	if (scheduler->systems.count >= (u32)SK_ECS_MAX_SYSTEMS) {
		return -2;
	}
	if (sk_array_push(&scheduler->systems, system) != 0) {
		return -3;
	}
	scheduler->built = 0;
	return (i32)(scheduler->systems.count - 1u);
}

static u32 scheduler_system_count_impl(const sk_scheduler_t* scheduler) {
	return scheduler->systems.count;
}

static sk_system_t* scheduler_system_impl(const sk_scheduler_t* scheduler, u32 index) {
	if (index >= scheduler->systems.count) {
		return NULL;
	}
	return scheduler->systems.items[index];
}

static i32 scheduler_build_impl(sk_scheduler_t* scheduler) {
	return scheduler_rebuild(scheduler);
}

static i32 scheduler_has_cycle_impl(const sk_scheduler_t* scheduler) {
	return scheduler->has_cycle;
}

static u32 scheduler_order_count_impl(const sk_scheduler_t* scheduler) {
	return scheduler->order.count;
}

static sk_system_t* scheduler_order_at_impl(const sk_scheduler_t* scheduler, u32 position) {
	if (position >= scheduler->order.count) {
		return NULL;
	}
	return scheduler->systems.items[scheduler->order.items[position]];
}

static i32 scheduler_run_impl(sk_scheduler_t* scheduler, sk_world_t* world, f32 delta_time) {
	SK_PROFILE_CPU_ZONE(ecs_profiler_api(), "ecs scheduler run");
	if (!scheduler->built) {
		const i32 rc = scheduler_rebuild(scheduler);
		if (rc != 0) {
			return rc;
		}
	}
	if (scheduler->has_cycle) {
		return -1;
	}
	for (u32 p = 0u; p < scheduler->order.count; ++p) {
		sk_system_t* system = scheduler->systems.items[scheduler->order.items[p]];
		system->callback(world, delta_time, system->user_data);
	}
	return 0;
}

/* ---- deferred entity commands ---- */

/*
 * Deferred command buffer. Recording appends to a FIFO command list without
 * touching any world; commands_apply walks the list in order, resolving
 * placeholder handles (spawned earlier in the same buffer) to their real
 * entity and applying each op, then resets the buffer for reuse.
 *
 * A placeholder handle is an sk_entity_t whose index has the high bit set
 * (SK_ECS_DEFERRED_BASE) and whose low bits index cmds->pending; real world
 * handles never carry the high bit (slot indices are far below 2^31).
 */
#define SK_ECS_DEFERRED_BASE 0x80000000u

typedef enum sk_ecs_command_kind_t {
	SK_ECS_CMD_SPAWN,
	SK_ECS_CMD_DESPAWN,
	SK_ECS_CMD_ADD_COMPONENT,
	SK_ECS_CMD_REMOVE_COMPONENT,
} sk_ecs_command_kind_t;

typedef struct sk_ecs_command_t {
	sk_ecs_command_kind_t kind;
	union {
		struct {
			u32 pending_index;
			u32 component_count;
			u32 ids_offset; /* into cmds->scratch: sk_type_id_t[component_count] */
		} spawn;
		struct {
			sk_entity_t entity;
		} despawn;
		struct {
			sk_entity_t entity;
			sk_type_id_t type_id;
			u32 value_offset; /* into cmds->scratch */
			u32 value_size;
		} add;
		struct {
			sk_entity_t entity;
			sk_type_id_t type_id;
		} remove;
	} u;
} sk_ecs_command_t;

struct sk_entitycommands_t {
	SK_ARRAY(sk_ecs_command_t) commands;
	SK_ARRAY(u8) scratch;		   /* copied component id arrays + values */
	SK_ARRAY(sk_entity_t) pending; /* deferred index -> real entity (apply time) */
};

/* Reserve @p size aligned bytes in the buffer's scratch arena. */
static i32 commands_scratch_reserve(sk_entitycommands_t* commands, u32 size, u32* out_offset) {
	const u32 aligned = ecs_align_up(commands->scratch.count, 8u);
	if (sk_array_reserve(&commands->scratch, aligned + size) != 0) {
		return -1;
	}
	commands->scratch.count = aligned + size;
	*out_offset = aligned;
	return 0;
}

static sk_entity_t commands_resolve(const sk_entitycommands_t* commands, sk_entity_t entity) {
	if ((entity.index & SK_ECS_DEFERRED_BASE) != 0u) {
		const u32 pending_index = entity.index & ~SK_ECS_DEFERRED_BASE;
		if (pending_index < commands->pending.count) {
			return commands->pending.items[pending_index];
		}
		return SK_ENTITY_INVALID;
	}
	return entity;
}

static sk_entitycommands_t* commands_create_impl(void) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_entitycommands_t* commands = (sk_entitycommands_t*)alloc->alloc(alloc->instance, sizeof(sk_entitycommands_t));
	if (commands == NULL) {
		return NULL;
	}
	sk_array_init(&commands->commands, alloc);
	sk_array_init(&commands->scratch, alloc);
	sk_array_init(&commands->pending, alloc);
	return commands;
}

static void commands_destroy_impl(sk_entitycommands_t* commands) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_array_free(&commands->commands);
	sk_array_free(&commands->scratch);
	sk_array_free(&commands->pending);
	alloc->free(alloc->instance, commands);
}

static u32 commands_count_impl(const sk_entitycommands_t* commands) {
	return commands->commands.count;
}

static sk_entity_t commands_spawn_impl(sk_entitycommands_t* commands, const sk_type_id_t* component_ids, u32 component_count) {
	if (component_count > 0u && component_ids == NULL) {
		return SK_ENTITY_INVALID;
	}
	for (u32 i = 0u; i < component_count; ++i) {
		if (SK_TYPE_ID_EQ(component_ids[i], SK_TYPE_ID_ZERO)) {
			return SK_ENTITY_INVALID;
		}
	}
	const u32 pending_index = commands->pending.count;
	if (sk_array_push(&commands->pending, SK_ENTITY_INVALID) != 0) {
		return SK_ENTITY_INVALID;
	}
	u32 ids_offset = 0u;
	if (component_count > 0u) {
		const u32 bytes = component_count * sizeof(sk_type_id_t);
		if (commands_scratch_reserve(commands, bytes, &ids_offset) != 0) {
			const sk_entity_t dropped = sk_array_pop(&commands->pending);
			(void)dropped;
			return SK_ENTITY_INVALID;
		}
		memcpy(commands->scratch.items + ids_offset, component_ids, bytes);
	}
	sk_ecs_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_ECS_CMD_SPAWN;
	cmd.u.spawn.pending_index = pending_index;
	cmd.u.spawn.component_count = component_count;
	cmd.u.spawn.ids_offset = ids_offset;
	if (sk_array_push(&commands->commands, cmd) != 0) {
		const sk_entity_t dropped = sk_array_pop(&commands->pending);
		(void)dropped;
		return SK_ENTITY_INVALID;
	}
	return (sk_entity_t){SK_ECS_DEFERRED_BASE | pending_index, 0u};
}

static i32 commands_despawn_impl(sk_entitycommands_t* commands, sk_entity_t entity) {
	if (!sk_entity_is_valid(entity)) {
		return -1;
	}
	sk_ecs_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_ECS_CMD_DESPAWN;
	cmd.u.despawn.entity = entity;
	return sk_array_push(&commands->commands, cmd);
}

static i32 commands_add_component_impl(sk_entitycommands_t* commands, sk_entity_t entity, sk_type_id_t type_id, const_ptr_t value) {
	if (!sk_entity_is_valid(entity) || SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO) || SK_TYPE_ID_EQ(type_id, SK_ECS_ENTITY_COMPONENT_ID)) {
		return -1;
	}
	u32 value_offset = 0u;
	u32 value_size = 0u;
	if (value != NULL) {
		sk_component_info_t info;
		if (component_info_impl(type_id, &info) != 0 || commands_scratch_reserve(commands, info.size, &value_offset) != 0) {
			return -1;
		}
		memcpy(commands->scratch.items + value_offset, value, info.size);
		value_size = info.size;
	}
	sk_ecs_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_ECS_CMD_ADD_COMPONENT;
	cmd.u.add.entity = entity;
	cmd.u.add.type_id = type_id;
	cmd.u.add.value_offset = value_offset;
	cmd.u.add.value_size = value_size;
	return sk_array_push(&commands->commands, cmd);
}

static i32 commands_remove_component_impl(sk_entitycommands_t* commands, sk_entity_t entity, sk_type_id_t type_id) {
	if (!sk_entity_is_valid(entity) || SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO) || SK_TYPE_ID_EQ(type_id, SK_ECS_ENTITY_COMPONENT_ID)) {
		return -1;
	}
	sk_ecs_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.kind = SK_ECS_CMD_REMOVE_COMPONENT;
	cmd.u.remove.entity = entity;
	cmd.u.remove.type_id = type_id;
	return sk_array_push(&commands->commands, cmd);
}

static void commands_clear_impl(sk_entitycommands_t* commands) {
	sk_array_clear(&commands->commands);
	sk_array_clear(&commands->scratch);
	sk_array_clear(&commands->pending);
}

static i32 commands_apply_impl(sk_entitycommands_t* commands, sk_world_t* world) {
	i32 result = 0;
	for (u32 i = 0u; i < commands->commands.count; ++i) {
		const sk_ecs_command_t* cmd = &commands->commands.items[i];
		switch (cmd->kind) {
		case SK_ECS_CMD_SPAWN: {
			/* Scratch offsets are 8-aligned (commands_scratch_reserve); the
			 * id array is written/read through this cast. */
			const sk_type_id_t* ids = (cmd->u.spawn.component_count > 0u) ? (const sk_type_id_t*)(const_ptr_t)(commands->scratch.items + cmd->u.spawn.ids_offset) : NULL;
			const sk_entity_t actual = world_spawn_impl(world, ids, cmd->u.spawn.component_count);
			commands->pending.items[cmd->u.spawn.pending_index] = actual;
			if (!sk_entity_is_valid(actual)) {
				result = -1;
			}
			break;
		}
		case SK_ECS_CMD_DESPAWN: {
			const sk_entity_t target = commands_resolve(commands, cmd->u.despawn.entity);
			if (sk_entity_is_valid(target) && world_alive_impl(world, target)) {
				if (world_despawn_impl(world, target) != 0) {
					result = -1;
				}
			}
			break;
		}
		case SK_ECS_CMD_ADD_COMPONENT: {
			const sk_entity_t target = commands_resolve(commands, cmd->u.add.entity);
			if (sk_entity_is_valid(target) && world_alive_impl(world, target)) {
				if (world_add_component_impl(world, target, cmd->u.add.type_id) != 0) {
					result = -1;
				} else if (cmd->u.add.value_size > 0u) {
					void_ptr_t slot = world_component_impl(world, target, cmd->u.add.type_id);
					if (slot != NULL) {
						memcpy(slot, commands->scratch.items + cmd->u.add.value_offset, cmd->u.add.value_size);
					}
				}
			}
			break;
		}
		case SK_ECS_CMD_REMOVE_COMPONENT: {
			const sk_entity_t target = commands_resolve(commands, cmd->u.remove.entity);
			if (sk_entity_is_valid(target) && world_alive_impl(world, target)) {
				if (world_remove_component_impl(world, target, cmd->u.remove.type_id) != 0) {
					result = -1;
				}
			}
			break;
		}
		}
	}
	commands_clear_impl(commands);
	return result;
}

/* ---- API table (table-only; no public free-function mirrors) ---- */

static const sk_entities_api_t entities_api = {
	register_component_impl,
	component_info_impl,

	archetype_create_impl,
	archetype_destroy_impl,
	archetype_component_count_impl,
	archetype_component_id_impl,
	archetype_component_size_impl,
	archetype_component_align_impl,
	archetype_column_impl,
	archetype_has_impl,
	archetype_column_offset_impl,
	archetype_column_stride_impl,
	archetype_chunk_capacity_impl,
	archetype_chunk_row_size_impl,
	archetype_chunk_data_size_impl,
	archetype_add_chunk_impl,
	archetype_chunk_count_impl,
	archetype_chunk_impl,
	archetype_location_impl,

	chunk_create_impl,
	chunk_destroy_impl,
	chunk_archetype_impl,
	chunk_count_impl,
	chunk_capacity_impl,
	chunk_full_impl,
	chunk_allocate_impl,
	chunk_remove_impl,
	chunk_column_impl,
	chunk_column_mut_impl,
	chunk_get_impl,
	chunk_get_mut_impl,
	chunk_entity_impl,
	chunk_find_impl,

	query_create_impl,
	query_destroy_impl,
	query_term_count_impl,
	query_term_id_impl,
	query_term_required_impl,
	query_matches_impl,
	query_observe_impl,
	query_archetype_count_impl,
	query_archetype_impl,
	query_iter_next_impl,
	query_iter_entity_impl,
	query_iter_field_impl,

	world_create_impl,
	world_destroy_impl,
	world_spawn_impl,
	world_despawn_impl,
	world_alive_impl,
	world_count_impl,
	world_has_component_impl,
	world_add_component_impl,
	world_remove_component_impl,
	world_component_impl,
	world_query_create_impl,

	system_create_impl,
	system_destroy_impl,
	system_read_count_impl,
	system_write_count_impl,
	system_read_id_impl,
	system_write_id_impl,
	system_name_impl,
	system_run_impl,

	scheduler_create_impl,
	scheduler_destroy_impl,
	scheduler_add_impl,
	scheduler_system_count_impl,
	scheduler_system_impl,
	scheduler_build_impl,
	scheduler_has_cycle_impl,
	scheduler_order_count_impl,
	scheduler_order_at_impl,
	scheduler_run_impl,

	commands_create_impl,
	commands_destroy_impl,
	commands_count_impl,
	commands_spawn_impl,
	commands_despawn_impl,
	commands_add_component_impl,
	commands_remove_component_impl,
	commands_apply_impl,
	commands_clear_impl,
};

/**
 * Register the ECS API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_entities_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_entities_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	g_ecs_app_context = context;
	g_ecs_app_api = app_api;
	/* Best-effort: the profiler may not be registered yet (sk-entities sorts
	 * before sk-profiler); ecs_profiler_api() re-checks at world_create. */
	g_ecs_profiler_api = (const sk_profiler_api_t*)app_api->get_api(context, SK_PROFILER_API_TYPE_ID);
	app_api->set_api(context, SK_ENTITIES_API_TYPE_ID, &entities_api);
}

#ifdef SK_TESTS
#include "test.h"

/*
 * Unit coverage for the ECS storage layer: type-id wiring (equal-lo/high-half
 * ordering), the entity handle, the sk_type_id-keyed component registry,
 * archetype signature normalization (sort, dedup, duplicate / zero / entity-id
 * rejection, 16 KiB packing bounds down to capacity 0), chunk slot allocation
 * / swap-remove / entity-to-location mapping, the world surface (spawn /
 * despawn / add / remove with generation handles, signature normalization,
 * unregistered and unfittable-layout failures, out-of-range handles, and the
 * swap-remove slot rewrite during archetype moves), the deferred
 * entitycommands buffer (FIFO apply, placeholders, record-time validation,
 * removal commands, unknown-placeholder skipping, apply-time failure
 * reporting, deferred-until-apply semantics, reuse/clear), and the systems /
 * dependency-graph scheduler (registration validation incl. set-size caps,
 * empty builds, add validation / capacity / out-of-range accessors, built-order
 * accessors, write-write and write-read chains, diamond joins, no edges on
 * disjoint or read-read sets, deterministic tie-break, implicit rebuild, and
 *  cycle detection on build and run).
 */

/* Clear the module component registry. Tests that register components start
 * from a clean registry so they pass regardless of the toolchain's
 * constructor registration order (e.g. the capacity test may run before the
 * roundtrip/idempotent/conflict tests on MSVC, which would otherwise leave the
 * registry full and make later register_component calls return -2). */
static void ecs_component_registry_reset(void) {
	ecs_component_count = 0u;
}

SK_TEST(entities_api_table_is_complete) {
	TEST_ASSERT_NOT_NULL(entities_api.register_component);
	TEST_ASSERT_NOT_NULL(entities_api.component_info);
	TEST_ASSERT_NOT_NULL(entities_api.archetype_create);
	TEST_ASSERT_NOT_NULL(entities_api.archetype_destroy);
	TEST_ASSERT_NOT_NULL(entities_api.archetype_column);
	TEST_ASSERT_NOT_NULL(entities_api.archetype_add_chunk);
	TEST_ASSERT_NOT_NULL(entities_api.archetype_location);
	TEST_ASSERT_NOT_NULL(entities_api.chunk_create);
	TEST_ASSERT_NOT_NULL(entities_api.chunk_allocate);
	TEST_ASSERT_NOT_NULL(entities_api.chunk_remove);
	TEST_ASSERT_NOT_NULL(entities_api.chunk_get);
	TEST_ASSERT_NOT_NULL(entities_api.chunk_get_mut);
	TEST_ASSERT_NOT_NULL(entities_api.chunk_find);
	TEST_ASSERT_NOT_NULL(entities_api.system_create);
	TEST_ASSERT_NOT_NULL(entities_api.system_destroy);
	TEST_ASSERT_NOT_NULL(entities_api.system_run);
	TEST_ASSERT_NOT_NULL(entities_api.scheduler_create);
	TEST_ASSERT_NOT_NULL(entities_api.scheduler_destroy);
	TEST_ASSERT_NOT_NULL(entities_api.scheduler_add);
	TEST_ASSERT_NOT_NULL(entities_api.scheduler_build);
	TEST_ASSERT_NOT_NULL(entities_api.scheduler_run);
	TEST_ASSERT_NOT_NULL(entities_api.scheduler_order_count);
}

SK_TEST(entities_api_type_id_nonzero) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_ENTITIES_API_TYPE_ID, SK_TYPE_ID_ZERO));
}

SK_TEST(entities_entity_component_id_distinct) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, SK_TYPE_ID_ZERO));
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, SK_ENTITIES_API_TYPE_ID));
}

SK_TEST(entities_chunk_size_is_16kib) {
	TEST_ASSERT_EQUAL_UINT32(16384u, SK_ECS_CHUNK_SIZE);
}

SK_TEST(entities_entity_handle) {
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(sk_entity_t));
	TEST_ASSERT_FALSE(sk_entity_is_valid(SK_ENTITY_INVALID));

	sk_entity_t e = {1u, 0u};
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));
	TEST_ASSERT_TRUE(sk_entity_eq(e, (sk_entity_t){1u, 0u}));
	TEST_ASSERT_FALSE(sk_entity_eq(e, (sk_entity_t){1u, 1u}));
	TEST_ASSERT_FALSE(sk_entity_eq(e, (sk_entity_t){2u, 0u}));
	TEST_ASSERT_FALSE(sk_entity_eq(e, SK_ENTITY_INVALID));
}

SK_TEST(entities_register_component_roundtrip) {
	ecs_component_registry_reset();
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.roundtrip", 0x0102030405060708ULL, 0x1112131415161718ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 12u, 4u, "roundtrip"));

	sk_component_info_t info;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.component_info(id, &info));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, info.type_id));
	TEST_ASSERT_EQUAL_UINT32(12u, info.size);
	TEST_ASSERT_EQUAL_UINT32(4u, info.align);
	TEST_ASSERT_EQUAL_STRING("roundtrip", info.name);
}

SK_TEST(entities_register_component_idempotent) {
	ecs_component_registry_reset();
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.idem", 0x2222222222222222ULL, 0x3333333333333333ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "idem"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "idem-again"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, NULL));
}

SK_TEST(entities_register_component_conflict) {
	ecs_component_registry_reset();
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.conflict", 0x4444444444444444ULL, 0x5555555555555555ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "conflict"));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.register_component(id, 16u, 8u, "conflict-other"));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.register_component(id, 8u, 16u, "conflict-align"));

	sk_component_info_t info;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.component_info(id, &info));
	TEST_ASSERT_EQUAL_UINT32(8u, info.size);
	TEST_ASSERT_EQUAL_UINT32(8u, info.align);
}

SK_TEST(entities_register_component_invalid) {
	TEST_ASSERT_EQUAL_INT32(-3, entities_api.register_component(SK_TYPE_ID_ZERO, 8u, 8u, "bad-id"));

	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.invalid", 0x6666666666666666ULL, 0x7777777777777777ULL);
	TEST_ASSERT_EQUAL_INT32(-3, entities_api.register_component(id, 0u, 8u, "bad-size"));
	TEST_ASSERT_EQUAL_INT32(-3, entities_api.register_component(id, 8u, 0u, "bad-align"));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.component_info(id, NULL));
}

SK_TEST(entities_component_info_missing) {
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.missing", 0x8888888888888888ULL, 0x9999999999999999ULL);
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.component_info(id, NULL));

	sk_component_info_t info = {0};
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.component_info(id, &info));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID_ZERO, info.type_id));
}

SK_TEST(entities_archetype_rejects_invalid) {
	TEST_ASSERT_NULL(entities_api.archetype_create(NULL, 1u));

	const sk_component_info_t zero_size = {SK_TYPE_ID("sk.test.ecs.bad.a", 0x01ULL, 0x01ULL), 0u, 4u, "zero-size"};
	TEST_ASSERT_NULL(entities_api.archetype_create(&zero_size, 1u));

	const sk_component_info_t zero_align = {SK_TYPE_ID("sk.test.ecs.bad.b", 0x02ULL, 0x01ULL), 4u, 0u, "zero-align"};
	TEST_ASSERT_NULL(entities_api.archetype_create(&zero_align, 1u));

	const sk_component_info_t non_pow2 = {SK_TYPE_ID("sk.test.ecs.bad.c", 0x03ULL, 0x01ULL), 4u, 3u, "align-3"};
	TEST_ASSERT_NULL(entities_api.archetype_create(&non_pow2, 1u));

	const sk_component_info_t entity_dup = {SK_ECS_ENTITY_COMPONENT_ID, 8u, 4u, "entity-dup"};
	TEST_ASSERT_NULL(entities_api.archetype_create(&entity_dup, 1u));

	const sk_component_info_t dup[2] = {
		{SK_TYPE_ID("sk.test.ecs.dup", 0x04ULL, 0x01ULL), 4u, 4u, "dup-1"},
		{SK_TYPE_ID("sk.test.ecs.dup", 0x04ULL, 0x01ULL), 4u, 4u, "dup-2"},
	};
	TEST_ASSERT_NULL(entities_api.archetype_create(dup, 2u));

	/* component_count == SK_ECS_MAX_ARCHETYPE_COLUMNS exceeds the column limit. */
	sk_component_info_t too_many[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_ARCHETYPE_COLUMNS; ++i) {
		too_many[i] = (sk_component_info_t){SK_TYPE_ID("sk.test.ecs.many", (u64)i + 0x100ULL, 0x01ULL), 4u, 4u, "many"};
	}
	TEST_ASSERT_NULL(entities_api.archetype_create(too_many, (u32)SK_ECS_MAX_ARCHETYPE_COLUMNS));
}

SK_TEST(entities_archetype_entity_only_capacity) {
	sk_archetype_t* a = entities_api.archetype_create(NULL, 0u);
	TEST_ASSERT_NOT_NULL(a);

	/* Entity-only archetype: column 0 only; 2046 rows of 8 bytes fit 16 KiB. */
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.archetype_component_count(a));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, entities_api.archetype_component_id(a, 0u)));
	TEST_ASSERT_EQUAL_UINT32(2046u, entities_api.archetype_chunk_capacity(a));
	TEST_ASSERT_EQUAL_UINT32(8u, entities_api.archetype_chunk_row_size(a));
	TEST_ASSERT_EQUAL_UINT32(16u, entities_api.archetype_column_offset(a, 0u));
	TEST_ASSERT_EQUAL_UINT32(8u, entities_api.archetype_column_stride(a, 0u));
	TEST_ASSERT_EQUAL_UINT32(16368u, entities_api.archetype_chunk_data_size(a));

	entities_api.archetype_destroy(a);
}

SK_TEST(entities_archetype_signature_sorted) {
	const sk_component_info_t unsorted[3] = {
		{SK_TYPE_ID("sk.test.ecs.zeta", 0x1000000000000030ULL, 0x01ULL), 4u, 4u, "zeta"},
		{SK_TYPE_ID("sk.test.ecs.alpha", 0x1000000000000010ULL, 0x01ULL), 8u, 8u, "alpha"},
		{SK_TYPE_ID("sk.test.ecs.mid", 0x1000000000000020ULL, 0x01ULL), 2u, 2u, "mid"},
	};
	sk_archetype_t* a = entities_api.archetype_create(unsorted, 3u);
	TEST_ASSERT_NOT_NULL(a);

	/* Column 0 is the entity component; user columns are stored sorted. */
	TEST_ASSERT_EQUAL_UINT32(4u, entities_api.archetype_component_count(a));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, entities_api.archetype_component_id(a, 0u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID("sk.test.ecs.alpha", 0x1000000000000010ULL, 0x01ULL), entities_api.archetype_component_id(a, 1u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID("sk.test.ecs.mid", 0x1000000000000020ULL, 0x01ULL), entities_api.archetype_component_id(a, 2u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID("sk.test.ecs.zeta", 0x1000000000000030ULL, 0x01ULL), entities_api.archetype_component_id(a, 3u)));

	TEST_ASSERT_EQUAL_INT32(1, entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.alpha", 0x1000000000000010ULL, 0x01ULL)));
	TEST_ASSERT_EQUAL_INT32(3, entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.zeta", 0x1000000000000030ULL, 0x01ULL)));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.missing", 0x10000000000000FFULL, 0x01ULL)));
	TEST_ASSERT_TRUE(entities_api.archetype_has(a, SK_TYPE_ID("sk.test.ecs.mid", 0x1000000000000020ULL, 0x01ULL)));
	TEST_ASSERT_FALSE(entities_api.archetype_has(a, SK_TYPE_ID("sk.test.ecs.missing", 0x10000000000000FFULL, 0x01ULL)));

	entities_api.archetype_destroy(a);
}

SK_TEST(entities_archetype_capacity_and_layout) {
	const sk_component_info_t comps[2] = {
		{SK_TYPE_ID("sk.test.ecs.cap.a", 0x1000000000000040ULL, 0x01ULL), 4u, 4u, "a"},
		{SK_TYPE_ID("sk.test.ecs.cap.b", 0x1000000000000050ULL, 0x01ULL), 8u, 8u, "b"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 2u);
	TEST_ASSERT_NOT_NULL(a);

	/* entity(8,4) + a(4,4) + b(8,8): data_start 16, strides 8/4/8, capacity 818 */
	TEST_ASSERT_EQUAL_UINT32(818u, entities_api.archetype_chunk_capacity(a));
	TEST_ASSERT_EQUAL_UINT32(20u, entities_api.archetype_chunk_row_size(a));
	TEST_ASSERT_EQUAL_UINT32(16u, entities_api.archetype_column_offset(a, 0u));
	TEST_ASSERT_EQUAL_UINT32(8u, entities_api.archetype_column_stride(a, 0u));
	TEST_ASSERT_EQUAL_UINT32(6560u, entities_api.archetype_column_offset(a, 1u));
	TEST_ASSERT_EQUAL_UINT32(4u, entities_api.archetype_column_stride(a, 1u));
	TEST_ASSERT_EQUAL_UINT32(9832u, entities_api.archetype_column_offset(a, 2u));
	TEST_ASSERT_EQUAL_UINT32(8u, entities_api.archetype_column_stride(a, 2u));
	TEST_ASSERT_EQUAL_UINT32(16360u, entities_api.archetype_chunk_data_size(a));

	/* Packing bound: the last column's end stays inside the 16 KiB chunk. */
	const u32 end = entities_api.archetype_column_offset(a, 2u) + 818u * 8u;
	TEST_ASSERT_TRUE(end <= (u32)SK_ECS_CHUNK_SIZE);

	entities_api.archetype_destroy(a);
}

SK_TEST(entities_archetype_align16_layout) {
	const sk_component_info_t comps[2] = {
		{SK_TYPE_ID("sk.test.ecs.a16.pad", 0x1000000000000060ULL, 0x01ULL), 8u, 4u, "pad"},
		{SK_TYPE_ID("sk.test.ecs.a16.big", 0x1000000000000070ULL, 0x01ULL), 16u, 16u, "big"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 2u);
	TEST_ASSERT_NOT_NULL(a);

	const i32 col_big = entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.a16.big", 0x1000000000000070ULL, 0x01ULL));
	TEST_ASSERT_TRUE(col_big >= 0);
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.archetype_column_offset(a, (u32)col_big) % 16u);
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.archetype_column_offset(a, 0u) % 4u);
	TEST_ASSERT_EQUAL_UINT32(16u, entities_api.archetype_column_stride(a, (u32)col_big));

	sk_chunk_t* chunk = entities_api.chunk_create(a);
	TEST_ASSERT_NOT_NULL(chunk);
	TEST_ASSERT_EQUAL_UINT32(0u, (u32)((uintptr_t)chunk % (uintptr_t)SK_ECS_CHUNK_ALIGNMENT));
	const_ptr_t col = entities_api.chunk_column(chunk, (u32)col_big);
	TEST_ASSERT_NOT_NULL(col);
	TEST_ASSERT_EQUAL_UINT32(0u, (u32)((uintptr_t)col % (uintptr_t)16u));

	entities_api.chunk_destroy(chunk);
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_type_id_equal_lo_sort) {
	/* Two ids sharing lo but differing hi: the signature sort must compare the
	 * high half so the ascending order is by hi. */
	const sk_component_info_t comps[2] = {
		{SK_TYPE_ID("sk.test.ecs.sorthi.b", 0x1000000000000F00ULL, 0x0000000000000002ULL), 4u, 4u, "hi2"},
		{SK_TYPE_ID("sk.test.ecs.sorthi.a", 0x1000000000000F00ULL, 0x0000000000000001ULL), 4u, 4u, "hi1"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 2u);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID("sk.test.ecs.sorthi.a", 0x1000000000000F00ULL, 0x0000000000000001ULL), entities_api.archetype_component_id(a, 1u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID("sk.test.ecs.sorthi.b", 0x1000000000000F00ULL, 0x0000000000000002ULL), entities_api.archetype_component_id(a, 2u)));
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_archetype_packing_bound_and_capacity_zero) {
	/* Inter-column alignment padding can push the greedy upper bound past the
	 * 16 KiB block, so the binary search must descend to a smaller capacity. */
	const sk_component_info_t tight[3] = {
		{SK_TYPE_ID("sk.test.ecs.fit.pad", 0x1000000000000E01ULL, 0x01ULL), 1u, 1u, "pad"},
		{SK_TYPE_ID("sk.test.ecs.fit.big", 0x1000000000000E02ULL, 0x01ULL), 4096u, 4096u, "big"},
		{SK_TYPE_ID("sk.test.ecs.fit.tail", 0x1000000000000E03ULL, 0x01ULL), 4u, 4u, "tail"},
	};
	sk_archetype_t* a = entities_api.archetype_create(tight, 3u);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.archetype_chunk_capacity(a));
	entities_api.archetype_destroy(a);

	/* An alignment larger than the 16 KiB chunk cannot host a single row. */
	const sk_component_info_t huge_align = {SK_TYPE_ID("sk.test.ecs.fit.hugealign", 0x1000000000000E04ULL, 0x01ULL), 8u, 32768u, "huge-align"};
	TEST_ASSERT_NULL(entities_api.archetype_create(&huge_align, 1u));

	/* A column wider than the 16 KiB chunk cannot host a single row. */
	const sk_component_info_t huge_size = {SK_TYPE_ID("sk.test.ecs.fit.hugesize", 0x1000000000000E05ULL, 0x01ULL), 16384u, 1u, "huge-size"};
	TEST_ASSERT_NULL(entities_api.archetype_create(&huge_size, 1u));
}

SK_TEST(entities_archetype_component_align_roundtrip) {
	const sk_component_info_t comps[1] = {
		{SK_TYPE_ID("sk.test.ecs.calign.c", 0x1000000000000D01ULL, 0x01ULL), 8u, 16u, "c"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 1u);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_EQUAL_UINT32(16u, entities_api.archetype_component_align(a, 1u));
	TEST_ASSERT_EQUAL_UINT32(SK_ECS_ENTITY_ALIGN, entities_api.archetype_component_align(a, 0u));
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_chunk_allocate_and_packing_bounds) {
	const sk_component_info_t comps[2] = {
		{SK_TYPE_ID("sk.test.ecs.pack.a", 0x1000000000000080ULL, 0x01ULL), 4u, 4u, "a"},
		{SK_TYPE_ID("sk.test.ecs.pack.b", 0x1000000000000090ULL, 0x01ULL), 8u, 8u, "b"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 2u);
	TEST_ASSERT_NOT_NULL(a);
	sk_chunk_t* chunk = entities_api.chunk_create(a);
	TEST_ASSERT_NOT_NULL(chunk);

	const u32 capacity = entities_api.archetype_chunk_capacity(a);
	TEST_ASSERT_EQUAL_UINT32(capacity, entities_api.chunk_capacity(chunk));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.chunk_count(chunk));
	TEST_ASSERT_FALSE(entities_api.chunk_full(chunk));

	for (u32 i = 0u; i < capacity; ++i) {
		u32 row = 0u;
		TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, (sk_entity_t){i + 1u, 0u}, &row));
		TEST_ASSERT_EQUAL_UINT32(i, row);
	}
	TEST_ASSERT_TRUE(entities_api.chunk_full(chunk));
	TEST_ASSERT_EQUAL_UINT32(capacity, entities_api.chunk_count(chunk));
	{
		u32 row = 0u;
		TEST_ASSERT_EQUAL_INT32(-1, entities_api.chunk_allocate(chunk, (sk_entity_t){0xFFFFu, 0u}, &row));
	}

	/* Every live column row lies within the 16 KiB chunk block. */
	const u32 column_count = entities_api.archetype_component_count(a);
	const uintptr_t base = (uintptr_t)chunk;
	for (u32 column = 0u; column < column_count; ++column) {
		const u32 size = entities_api.archetype_component_size(a, column);
		for (u32 r = 0u; r < capacity; ++r) {
			const_ptr_t p = entities_api.chunk_get(chunk, column, r);
			TEST_ASSERT_NOT_NULL(p);
			const uintptr_t addr = (uintptr_t)p;
			TEST_ASSERT_TRUE(addr >= base);
			TEST_ASSERT_TRUE(addr + (uintptr_t)size <= base + (uintptr_t)SK_ECS_CHUNK_SIZE);
		}
	}

	/* Safe accessors reject out-of-range columns and rows. */
	TEST_ASSERT_NULL(entities_api.chunk_get(chunk, column_count, 0u));
	TEST_ASSERT_NULL(entities_api.chunk_get_mut(chunk, 0u, capacity));
	TEST_ASSERT_NULL(entities_api.chunk_get(chunk, 0u, capacity + 1u));
	TEST_ASSERT_NULL(entities_api.chunk_column(chunk, column_count));
	TEST_ASSERT_NULL(entities_api.chunk_column_mut(chunk, column_count + 3u));

	entities_api.chunk_destroy(chunk);
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_chunk_columns_and_access) {
	typedef struct ecs_test_comp_a_t {
		u32 value;
	} ecs_test_comp_a_t;
	typedef struct ecs_test_comp_b_t {
		f32 x;
		f32 y;
	} ecs_test_comp_b_t;

	const sk_component_info_t comps[2] = {
		{SK_TYPE_ID("sk.test.ecs.col.a", 0x10000000000000A0ULL, 0x01ULL), (u32)sizeof(ecs_test_comp_a_t), 4u, "a"},
		{SK_TYPE_ID("sk.test.ecs.col.b", 0x10000000000000B0ULL, 0x01ULL), (u32)sizeof(ecs_test_comp_b_t), 4u, "b"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 2u);
	TEST_ASSERT_NOT_NULL(a);
	sk_chunk_t* chunk = entities_api.chunk_create(a);
	TEST_ASSERT_NOT_NULL(chunk);

	const u32 col_a = (u32)entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.col.a", 0x10000000000000A0ULL, 0x01ULL));
	const u32 col_b = (u32)entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.col.b", 0x10000000000000B0ULL, 0x01ULL));
	TEST_ASSERT_EQUAL_UINT32(1u, col_a);
	TEST_ASSERT_EQUAL_UINT32(2u, col_b);

	const sk_entity_t e0 = {10u, 0u};
	u32 row = 0u;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, e0, &row));
	TEST_ASSERT_TRUE(sk_entity_eq(e0, entities_api.chunk_entity(chunk, row)));

	ecs_test_comp_a_t* pa = (ecs_test_comp_a_t*)entities_api.chunk_get_mut(chunk, col_a, row);
	ecs_test_comp_b_t* pb = (ecs_test_comp_b_t*)entities_api.chunk_get_mut(chunk, col_b, row);
	TEST_ASSERT_NOT_NULL(pa);
	TEST_ASSERT_NOT_NULL(pb);
	pa->value = 7u;
	pb->x = 1.5f;
	pb->y = -2.5f;

	const sk_entity_t e1 = {11u, 0u};
	u32 row1 = 0u;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, e1, &row1));
	TEST_ASSERT_EQUAL_UINT32(1u, row1);
	((ecs_test_comp_a_t*)entities_api.chunk_get_mut(chunk, col_a, row1))->value = 9u;

	/* Read back row 0 through the const accessors. */
	const ecs_test_comp_a_t* ra = (const ecs_test_comp_a_t*)entities_api.chunk_get(chunk, col_a, 0u);
	const ecs_test_comp_b_t* rb = (const ecs_test_comp_b_t*)entities_api.chunk_get(chunk, col_b, 0u);
	TEST_ASSERT_NOT_NULL(ra);
	TEST_ASSERT_NOT_NULL(rb);
	TEST_ASSERT_EQUAL_UINT32(7u, ra->value);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.5f, rb->x);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, -2.5f, rb->y);

	/* Dense columns: column base == row 0, rows are stride-consecutive. */
	const_ptr_t colb = entities_api.chunk_column(chunk, col_b);
	TEST_ASSERT_EQUAL_PTR(colb, entities_api.chunk_get(chunk, col_b, 0u));
	TEST_ASSERT_EQUAL_PTR((const_ptr_t)((const u8*)colb + 8u), entities_api.chunk_get(chunk, col_b, 1u));

	/* row -> entity and entity -> row within the chunk. */
	TEST_ASSERT_TRUE(sk_entity_eq(e1, entities_api.chunk_entity(chunk, 1u)));
	u32 found = 0u;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_find(chunk, e1, &found));
	TEST_ASSERT_EQUAL_UINT32(1u, found);
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.chunk_find(chunk, (sk_entity_t){99u, 0u}, NULL));

	entities_api.chunk_destroy(chunk);
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_chunk_swap_remove) {
	sk_archetype_t* a = entities_api.archetype_create(NULL, 0u);
	TEST_ASSERT_NOT_NULL(a);
	sk_chunk_t* chunk = entities_api.chunk_create(a);
	TEST_ASSERT_NOT_NULL(chunk);

	const sk_entity_t e0 = {1u, 0u};
	const sk_entity_t e1 = {2u, 0u};
	const sk_entity_t e2 = {3u, 0u};
	u32 r0 = 0u, r1 = 0u, r2 = 0u;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, e0, &r0));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, e1, &r1));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, e2, &r2));
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.chunk_count(chunk));

	/* Freeing row 0 swap-removes the last entity (e2) into row 0. */
	sk_entity_t moved = SK_ENTITY_INVALID;
	entities_api.chunk_remove(chunk, 0u, &moved);
	TEST_ASSERT_TRUE(sk_entity_eq(e2, moved));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.chunk_count(chunk));
	TEST_ASSERT_TRUE(sk_entity_eq(e2, entities_api.chunk_entity(chunk, 0u)));
	TEST_ASSERT_TRUE(sk_entity_eq(e1, entities_api.chunk_entity(chunk, 1u)));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.chunk_find(chunk, e0, NULL));

	/* Freeing the last row reports no moved entity. */
	moved = (sk_entity_t){77u, 0u};
	entities_api.chunk_remove(chunk, 1u, &moved);
	TEST_ASSERT_FALSE(sk_entity_is_valid(moved));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.chunk_count(chunk));
	TEST_ASSERT_TRUE(sk_entity_eq(e2, entities_api.chunk_entity(chunk, 0u)));

	entities_api.chunk_destroy(chunk);
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_archetype_chunks_and_location) {
	sk_archetype_t* a = entities_api.archetype_create(NULL, 0u);
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.archetype_add_chunk(a));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.archetype_add_chunk(a));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.archetype_chunk_count(a));

	sk_chunk_t* c0 = entities_api.archetype_chunk(a, 0u);
	sk_chunk_t* c1 = entities_api.archetype_chunk(a, 1u);
	TEST_ASSERT_NOT_NULL(c0);
	TEST_ASSERT_NOT_NULL(c1);
	TEST_ASSERT_NULL(entities_api.archetype_chunk(a, 2u));

	const sk_entity_t e = {7u, 0u};
	u32 row = 0u;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(c1, e, &row));

	sk_entity_location_t loc = SK_ECS_LOCATION_NONE;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.archetype_location(a, e, &loc));
	TEST_ASSERT_EQUAL_UINT32(1u, loc.chunk);
	TEST_ASSERT_EQUAL_UINT32(0u, loc.row);

	TEST_ASSERT_EQUAL_INT32(-1, entities_api.archetype_location(a, (sk_entity_t){99u, 0u}, &loc));

	/* archetype_destroy frees the chunks it owns. */
	entities_api.archetype_destroy(a);
}

SK_TEST(entities_chunk_archetype_and_column_mut) {
	const sk_component_info_t comps[1] = {
		{SK_TYPE_ID("sk.test.ecs.cmut.c", 0x1000000000000C01ULL, 0x01ULL), 4u, 4u, "c"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 1u);
	TEST_ASSERT_NOT_NULL(a);
	sk_chunk_t* chunk = entities_api.chunk_create(a);
	TEST_ASSERT_NOT_NULL(chunk);

	/* A chunk reports its owning archetype. */
	TEST_ASSERT_EQUAL_PTR((const_ptr_t)a, entities_api.chunk_archetype(chunk));

	/* The mutable column base aliases row 0 of that column. */
	const u32 col_c = (u32)entities_api.archetype_column(a, SK_TYPE_ID("sk.test.ecs.cmut.c", 0x1000000000000C01ULL, 0x01ULL));
	u32 row = 0u;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, (sk_entity_t){5u, 0u}, &row));
	void_ptr_t col = entities_api.chunk_column_mut(chunk, col_c);
	TEST_ASSERT_NOT_NULL(col);
	TEST_ASSERT_EQUAL_PTR(entities_api.chunk_get_mut(chunk, col_c, 0u), col);
	((u32*)col)[0] = 123u;
	TEST_ASSERT_EQUAL_UINT32(123u, *(const u32*)entities_api.chunk_get(chunk, col_c, 0u));

	entities_api.chunk_destroy(chunk);
	entities_api.archetype_destroy(a);
}

/* ---- query tests ---- */

typedef struct ecs_query_pos_t {
	f32 x;
	f32 y;
} ecs_query_pos_t;

typedef struct ecs_query_vel_t {
	f32 vx;
	f32 vy;
} ecs_query_vel_t;

typedef struct ecs_query_tag_t {
	u32 flags;
} ecs_query_tag_t;

#define TEST_QUERY_POS_ID SK_TYPE_ID("sk.test.ecs.query.pos", 0x2A00000000000001ULL, 0x0100000000000001ULL)
#define TEST_QUERY_VEL_ID SK_TYPE_ID("sk.test.ecs.query.vel", 0x2A00000000000002ULL, 0x0100000000000001ULL)
#define TEST_QUERY_TAG_ID SK_TYPE_ID("sk.test.ecs.query.tag", 0x2A00000000000003ULL, 0x0100000000000001ULL)

static sk_archetype_t* query_test_archetype(const sk_component_info_t* comps, u32 count) {
	sk_archetype_t* a = entities_api.archetype_create(comps, count);
	TEST_ASSERT_NOT_NULL(a);
	return a;
}

static sk_chunk_t* query_test_seed_chunk(sk_archetype_t* a, u32 entity_start, u32 n) {
	TEST_ASSERT_EQUAL_INT32(0, entities_api.archetype_add_chunk(a));
	sk_chunk_t* chunk = entities_api.archetype_chunk(a, entities_api.archetype_chunk_count(a) - 1u);
	TEST_ASSERT_NOT_NULL(chunk);
	for (u32 i = 0u; i < n; ++i) {
		u32 row = 0u;
		TEST_ASSERT_EQUAL_INT32(0, entities_api.chunk_allocate(chunk, (sk_entity_t){entity_start + i, 0u}, &row));
	}
	return chunk;
}

SK_TEST(entities_query_create_validation) {
	TEST_ASSERT_NULL(entities_api.query_create(NULL));

	const sk_type_id_t one[1] = {TEST_QUERY_POS_ID};
	const sk_query_desc_t null_arr = {NULL, 1u, NULL, 0u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&null_arr));

	const sk_type_id_t zero[1] = {SK_TYPE_ID_ZERO};
	const sk_query_desc_t zero_req = {zero, 1u, NULL, 0u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&zero_req));
	const sk_query_desc_t zero_opt = {NULL, 0u, zero, 1u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&zero_opt));
	const sk_query_desc_t zero_exc = {NULL, 0u, NULL, 0u, zero, 1u};
	TEST_ASSERT_NULL(entities_api.query_create(&zero_exc));

	const sk_type_id_t dup[2] = {TEST_QUERY_POS_ID, TEST_QUERY_POS_ID};
	const sk_query_desc_t dup_req = {dup, 2u, NULL, 0u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&dup_req));
	const sk_query_desc_t dup_opt = {NULL, 0u, dup, 2u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&dup_opt));
	const sk_query_desc_t dup_cross = {one, 1u, one, 1u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&dup_cross));

	/* 1 + SK_ECS_MAX_QUERY_TERMS required terms exceeds the term cap. */
	sk_type_id_t too_many[SK_ECS_MAX_QUERY_TERMS];
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_QUERY_TERMS; ++i) {
		too_many[i] = SK_TYPE_ID("sk.test.ecs.query.many", (u64)i + 0x1000ULL, 0x01ULL);
	}
	const sk_query_desc_t overflow = {too_many, (u32)SK_ECS_MAX_QUERY_TERMS, NULL, 0u, NULL, 0u};
	TEST_ASSERT_NULL(entities_api.query_create(&overflow));

	/* Excluded set over the cap. */
	sk_type_id_t excl_many[(u32)SK_ECS_MAX_QUERY_TERMS + 1u];
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_QUERY_TERMS + 1u; ++i) {
		excl_many[i] = SK_TYPE_ID("sk.test.ecs.query.excl", (u64)i + 0x2000ULL, 0x01ULL);
	}
	const sk_query_desc_t excl_overflow = {NULL, 0u, NULL, 0u, excl_many, (u32)SK_ECS_MAX_QUERY_TERMS + 1u};
	TEST_ASSERT_NULL(entities_api.query_create(&excl_overflow));
}

SK_TEST(entities_query_term_metadata) {
	const sk_type_id_t required[2] = {TEST_QUERY_POS_ID, TEST_QUERY_VEL_ID};
	const sk_type_id_t optional[1] = {TEST_QUERY_TAG_ID};
	const sk_query_desc_t desc = {required, 2u, optional, 1u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&desc);
	TEST_ASSERT_NOT_NULL(q);

	/* Term 0 is the entity component, then required ids, then optional ids. */
	TEST_ASSERT_EQUAL_UINT32(4u, entities_api.query_term_count(q));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, entities_api.query_term_id(q, 0u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_QUERY_POS_ID, entities_api.query_term_id(q, 1u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_QUERY_VEL_ID, entities_api.query_term_id(q, 2u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_QUERY_TAG_ID, entities_api.query_term_id(q, 3u)));
	TEST_ASSERT_TRUE(entities_api.query_term_required(q, 0u));
	TEST_ASSERT_TRUE(entities_api.query_term_required(q, 1u));
	TEST_ASSERT_TRUE(entities_api.query_term_required(q, 2u));
	TEST_ASSERT_FALSE(entities_api.query_term_required(q, 3u));

	entities_api.query_destroy(q);

	/* Empty descriptor: entity term only. */
	const sk_query_desc_t none = SK_QUERY_DESC_NONE;
	sk_query_t* qnone = entities_api.query_create(&none);
	TEST_ASSERT_NOT_NULL(qnone);
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.query_term_count(qnone));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, entities_api.query_term_id(qnone, 0u)));
	TEST_ASSERT_TRUE(entities_api.query_term_required(qnone, 0u));
	entities_api.query_destroy(qnone);
}

SK_TEST(entities_query_matches_sets) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t vel = {TEST_QUERY_VEL_ID, (u32)sizeof(ecs_query_vel_t), 4u, "vel"};
	const sk_component_info_t tag = {TEST_QUERY_TAG_ID, (u32)sizeof(ecs_query_tag_t), 4u, "tag"};

	const sk_component_info_t comps_pv[2] = {pos, vel};
	const sk_component_info_t comps_p[1] = {pos};
	const sk_component_info_t comps_pt[2] = {pos, tag};
	const sk_component_info_t comps_t[1] = {tag};

	sk_archetype_t* pv = query_test_archetype(comps_pv, 2u);
	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	sk_archetype_t* pt = query_test_archetype(comps_pt, 2u);
	sk_archetype_t* t = query_test_archetype(comps_t, 1u);

	const sk_type_id_t req_pos[1] = {TEST_QUERY_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* qpos = entities_api.query_create(&d_pos);
	TEST_ASSERT_NOT_NULL(qpos);
	TEST_ASSERT_TRUE(entities_api.query_matches(qpos, pv));
	TEST_ASSERT_TRUE(entities_api.query_matches(qpos, p));
	TEST_ASSERT_TRUE(entities_api.query_matches(qpos, pt));
	TEST_ASSERT_FALSE(entities_api.query_matches(qpos, t));
	entities_api.query_destroy(qpos);

	const sk_type_id_t req_pv[2] = {TEST_QUERY_POS_ID, TEST_QUERY_VEL_ID};
	const sk_query_desc_t d_pv = {req_pv, 2u, NULL, 0u, NULL, 0u};
	sk_query_t* qpv = entities_api.query_create(&d_pv);
	TEST_ASSERT_NOT_NULL(qpv);
	TEST_ASSERT_TRUE(entities_api.query_matches(qpv, pv));
	TEST_ASSERT_FALSE(entities_api.query_matches(qpv, p));
	TEST_ASSERT_FALSE(entities_api.query_matches(qpv, pt));
	TEST_ASSERT_FALSE(entities_api.query_matches(qpv, t));
	entities_api.query_destroy(qpv);

	const sk_type_id_t excl_vel[1] = {TEST_QUERY_VEL_ID};
	const sk_query_desc_t d_excl = {req_pos, 1u, NULL, 0u, excl_vel, 1u};
	sk_query_t* qex = entities_api.query_create(&d_excl);
	TEST_ASSERT_NOT_NULL(qex);
	TEST_ASSERT_FALSE(entities_api.query_matches(qex, pv));
	TEST_ASSERT_TRUE(entities_api.query_matches(qex, p));
	TEST_ASSERT_TRUE(entities_api.query_matches(qex, pt));
	TEST_ASSERT_FALSE(entities_api.query_matches(qex, t));
	entities_api.query_destroy(qex);

	const sk_type_id_t req_tag[1] = {TEST_QUERY_TAG_ID};
	const sk_query_desc_t d_tag = {req_tag, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* qtag = entities_api.query_create(&d_tag);
	TEST_ASSERT_NOT_NULL(qtag);
	TEST_ASSERT_FALSE(entities_api.query_matches(qtag, pv));
	TEST_ASSERT_FALSE(entities_api.query_matches(qtag, p));
	TEST_ASSERT_TRUE(entities_api.query_matches(qtag, pt));
	TEST_ASSERT_TRUE(entities_api.query_matches(qtag, t));
	entities_api.query_destroy(qtag);

	const sk_query_desc_t d_none = SK_QUERY_DESC_NONE;
	sk_query_t* qnone = entities_api.query_create(&d_none);
	TEST_ASSERT_NOT_NULL(qnone);
	TEST_ASSERT_TRUE(entities_api.query_matches(qnone, pv));
	TEST_ASSERT_TRUE(entities_api.query_matches(qnone, p));
	TEST_ASSERT_TRUE(entities_api.query_matches(qnone, pt));
	TEST_ASSERT_TRUE(entities_api.query_matches(qnone, t));
	entities_api.query_destroy(qnone);

	entities_api.archetype_destroy(pv);
	entities_api.archetype_destroy(p);
	entities_api.archetype_destroy(pt);
	entities_api.archetype_destroy(t);
}

SK_TEST(entities_query_observe_and_archetype_list) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t tag = {TEST_QUERY_TAG_ID, (u32)sizeof(ecs_query_tag_t), 4u, "tag"};
	const sk_component_info_t comps_p[1] = {pos};
	const sk_component_info_t comps_t[1] = {tag};

	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	sk_archetype_t* t = query_test_archetype(comps_t, 1u);

	const sk_type_id_t req_pos[1] = {TEST_QUERY_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&d_pos);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.query_archetype_count(q));

	/* Matching archetypes are appended once; non-matching are rejected. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, p));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, p));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.query_observe(q, t));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.query_archetype_count(q));
	TEST_ASSERT_EQUAL_PTR(p, entities_api.query_archetype(q, 0u));
	TEST_ASSERT_NULL(entities_api.query_archetype(q, 1u));

	entities_api.query_destroy(q);
	entities_api.archetype_destroy(p);
	entities_api.archetype_destroy(t);
}

SK_TEST(entities_query_iteration_multi_archetype) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t vel = {TEST_QUERY_VEL_ID, (u32)sizeof(ecs_query_vel_t), 4u, "vel"};
	const sk_component_info_t comps_pv[2] = {pos, vel};
	const sk_component_info_t comps_p[1] = {pos};

	/* Archetype {pos, vel}: entities 1..3. */
	sk_archetype_t* pv = query_test_archetype(comps_pv, 2u);
	sk_chunk_t* chunk_pv = query_test_seed_chunk(pv, 1u, 3u);
	const u32 col_pos_pv = (u32)entities_api.archetype_column(pv, TEST_QUERY_POS_ID);
	const u32 col_vel_pv = (u32)entities_api.archetype_column(pv, TEST_QUERY_VEL_ID);
	for (u32 row = 0u; row < 3u; ++row) {
		ecs_query_pos_t* pp = (ecs_query_pos_t*)entities_api.chunk_get_mut(chunk_pv, col_pos_pv, row);
		ecs_query_vel_t* vp = (ecs_query_vel_t*)entities_api.chunk_get_mut(chunk_pv, col_vel_pv, row);
		TEST_ASSERT_NOT_NULL(pp);
		TEST_ASSERT_NOT_NULL(vp);
		pp->x = (f32)row;
		pp->y = (f32)row + 1.0f;
		vp->vx = 1.0f;
		vp->vy = 2.0f;
	}

	/* Archetype {pos}: entities 10..11. */
	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	sk_chunk_t* chunk_p = query_test_seed_chunk(p, 10u, 2u);
	const u32 col_pos_p = (u32)entities_api.archetype_column(p, TEST_QUERY_POS_ID);
	for (u32 row = 0u; row < 2u; ++row) {
		ecs_query_pos_t* pp = (ecs_query_pos_t*)entities_api.chunk_get_mut(chunk_p, col_pos_p, row);
		TEST_ASSERT_NOT_NULL(pp);
		pp->x = (f32)row + 100.0f;
		pp->y = 0.0f;
	}

	const sk_type_id_t req_pos[1] = {TEST_QUERY_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&d_pos);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, pv));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, p));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.query_archetype_count(q));

	/* SK_ECS_QUERY_EACH walks every matched archetype's live rows. */
	u32 entities_seen = 0u;
	f32 sum_x = 0.0f;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		const ecs_query_pos_t* pp = SK_ECS_ITER_AT(it, 1, ecs_query_pos_t);
		TEST_ASSERT_NOT_NULL(pp);
		TEST_ASSERT_TRUE(sk_entity_is_valid(SK_ECS_ITER_ENTITY_ROW(it)));
		entities_seen += 1u;
		sum_x += pp->x;
	}
	TEST_ASSERT_EQUAL_UINT32(5u, entities_seen);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 204.0f, sum_x);

	entities_api.query_destroy(q);
	entities_api.archetype_destroy(pv);
	entities_api.archetype_destroy(p);
}

SK_TEST(entities_query_iteration_optional_terms) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t vel = {TEST_QUERY_VEL_ID, (u32)sizeof(ecs_query_vel_t), 4u, "vel"};
	const sk_component_info_t comps_pv[2] = {pos, vel};
	const sk_component_info_t comps_p[1] = {pos};

	/* Archetype {pos, vel} stores the optional term. */
	sk_archetype_t* pv = query_test_archetype(comps_pv, 2u);
	sk_chunk_t* chunk_pv = query_test_seed_chunk(pv, 1u, 1u);
	const u32 col_vel = (u32)entities_api.archetype_column(pv, TEST_QUERY_VEL_ID);
	((ecs_query_vel_t*)entities_api.chunk_get_mut(chunk_pv, col_vel, 0u))->vx = 9.0f;

	/* Archetype {pos} does not store the optional term. */
	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	query_test_seed_chunk(p, 10u, 1u);

	const sk_type_id_t req_pos[1] = {TEST_QUERY_POS_ID};
	const sk_type_id_t opt_vel[1] = {TEST_QUERY_VEL_ID};
	const sk_query_desc_t d_opt = {req_pos, 1u, opt_vel, 1u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&d_opt);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, pv));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, p));

	/* The optional term yields a typed pointer or NULL per archetype. */
	u32 with_vel = 0u;
	u32 without_vel = 0u;
	f32 vel_sum = 0.0f;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		const ecs_query_vel_t* vp = SK_ECS_ITER_AT(it, 2, ecs_query_vel_t);
		if (vp != NULL) {
			with_vel += 1u;
			vel_sum += vp->vx;
		} else {
			without_vel += 1u;
		}
	}
	TEST_ASSERT_EQUAL_UINT32(1u, with_vel);
	TEST_ASSERT_EQUAL_UINT32(1u, without_vel);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.0f, vel_sum);

	/* Direct API mirrors the macro path for an absent optional term. */
	u32 seen_opt = 0u;
	u32 seen_null = 0u;
	sk_query_iter_t it = sk_query_iter_make(q);
	while (entities_api.query_iter_next(&it) != 0) {
		for (u32 row = 0u; row < it.count; ++row) {
			seen_opt += 1u;
			if (entities_api.query_iter_field(&it, 2u, row) == NULL) {
				seen_null += 1u;
			}
		}
	}
	TEST_ASSERT_EQUAL_UINT32(2u, seen_opt);
	TEST_ASSERT_EQUAL_UINT32(1u, seen_null);

	entities_api.query_destroy(q);
	entities_api.archetype_destroy(pv);
	entities_api.archetype_destroy(p);
}

SK_TEST(entities_query_iter_skips_empty_chunks) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t comps_p[1] = {pos};

	/* Chunk 0 stays empty; chunk 1 holds one entity. */
	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.archetype_add_chunk(p));
	sk_chunk_t* chunk_p = query_test_seed_chunk(p, 7u, 1u);
	const u32 col_pos = (u32)entities_api.archetype_column(p, TEST_QUERY_POS_ID);
	((ecs_query_pos_t*)entities_api.chunk_get_mut(chunk_p, col_pos, 0u))->x = 42.0f;
	((ecs_query_pos_t*)entities_api.chunk_get_mut(chunk_p, col_pos, 0u))->y = -1.0f;

	const sk_type_id_t req_pos[1] = {TEST_QUERY_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&d_pos);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, p));

	/* SK_ECS_QUERY_FOREACH yields one chunk despite the empty one. */
	u32 chunks = 0u;
	u32 rows = 0u;
	SK_ECS_QUERY_FOREACH(&entities_api, q, it) {
		chunks += 1u;
		rows += SK_ECS_ITER_COUNT(it);
		TEST_ASSERT_EQUAL_UINT32(2u, it.term_count);
		const sk_entity_t e0 = SK_ECS_ITER_ENTITY(it, 0u);
		TEST_ASSERT_TRUE(sk_entity_eq((sk_entity_t){7u, 0u}, e0));
		TEST_ASSERT_EQUAL_PTR(SK_ECS_ITER_ENTITIES(it), SK_ECS_ITER_COL(it, 0, sk_entity_t, 0));
	}
	TEST_ASSERT_EQUAL_UINT32(1u, chunks);
	TEST_ASSERT_EQUAL_UINT32(1u, rows);

	entities_api.query_destroy(q);
	entities_api.archetype_destroy(p);
}

SK_TEST(entities_query_iter_accessors) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t comps_p[1] = {pos};

	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	sk_chunk_t* chunk_p = query_test_seed_chunk(p, 7u, 1u);
	const u32 col_pos = (u32)entities_api.archetype_column(p, TEST_QUERY_POS_ID);
	((ecs_query_pos_t*)entities_api.chunk_get_mut(chunk_p, col_pos, 0u))->x = 42.0f;
	((ecs_query_pos_t*)entities_api.chunk_get_mut(chunk_p, col_pos, 0u))->y = -1.0f;

	const sk_type_id_t req_pos[1] = {TEST_QUERY_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&d_pos);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, p));

	sk_query_iter_t it = sk_query_iter_make(q);
	TEST_ASSERT_TRUE(entities_api.query_iter_next(&it));
	TEST_ASSERT_EQUAL_UINT32(1u, SK_ECS_ITER_COUNT(it));

	const sk_entity_t e = entities_api.query_iter_entity(&it, 0u);
	TEST_ASSERT_TRUE(sk_entity_eq((sk_entity_t){7u, 0u}, e));

	const ecs_query_pos_t* pp = (const ecs_query_pos_t*)entities_api.query_iter_field(&it, 1u, 0u);
	TEST_ASSERT_NOT_NULL(pp);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 42.0f, pp->x);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, pp->y);

	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_iter_next(&it));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_iter_next(&it));

	entities_api.query_destroy(q);
	entities_api.archetype_destroy(p);
}

SK_TEST(entities_query_iteration_empty_matches) {
	const sk_component_info_t pos = {TEST_QUERY_POS_ID, (u32)sizeof(ecs_query_pos_t), 4u, "pos"};
	const sk_component_info_t tag = {TEST_QUERY_TAG_ID, (u32)sizeof(ecs_query_tag_t), 4u, "tag"};
	const sk_component_info_t comps_p[1] = {pos};
	const sk_component_info_t comps_t[1] = {tag};

	sk_archetype_t* p = query_test_archetype(comps_p, 1u);
	query_test_seed_chunk(p, 1u, 1u);
	sk_archetype_t* t = query_test_archetype(comps_t, 1u);

	const sk_type_id_t req_tag[1] = {TEST_QUERY_TAG_ID};
	const sk_query_desc_t d_tag = {req_tag, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.query_create(&d_tag);
	TEST_ASSERT_NOT_NULL(q);
	TEST_ASSERT_FALSE(entities_api.query_matches(q, p));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.query_observe(q, p));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_observe(q, t));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.query_archetype_count(q));

	/* t matches but owns no chunks -> iteration yields nothing. */
	sk_query_iter_t it = sk_query_iter_make(q);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_iter_next(&it));

	entities_api.query_destroy(q);
	entities_api.archetype_destroy(p);
	entities_api.archetype_destroy(t);
}

SK_TEST(entities_query_iter_null_query) {
	/* A zeroed iterator with no query is immediately exhausted. */
	sk_query_iter_t it = sk_query_iter_make(NULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_iter_next(&it));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.query_iter_next(&it));
}

/* ---- world + deferred entitycommands tests ---- */

typedef struct ecs_world_pos_t {
	f32 x;
	f32 y;
} ecs_world_pos_t;

typedef struct ecs_world_vel_t {
	f32 vx;
	f32 vy;
} ecs_world_vel_t;

typedef struct ecs_world_tag_t {
	u32 flags;
} ecs_world_tag_t;

#define TEST_WORLD_POS_ID SK_TYPE_ID("sk.test.ecs.world.pos", 0x3B00000000000001ULL, 0x0200000000000001ULL)
#define TEST_WORLD_VEL_ID SK_TYPE_ID("sk.test.ecs.world.vel", 0x3B00000000000002ULL, 0x0200000000000001ULL)
#define TEST_WORLD_TAG_ID SK_TYPE_ID("sk.test.ecs.world.tag", 0x3B00000000000003ULL, 0x0200000000000001ULL)

/* Registered on demand by the spawn-validation test: a layout that cannot fit
 * inside a 16 KiB chunk. */
#define TEST_ECS_HUGE_ID SK_TYPE_ID("sk.test.ecs.world.huge", 0x3B000000000000EEULL, 0x0200000000000001ULL)
/* Deliberately never registered: drives the unregistered-component failure paths. */
#define TEST_ECS_UNREG_ID SK_TYPE_ID("sk.test.ecs.world.unreg", 0x3B000000000000DDULL, 0x0200000000000001ULL)

static void ecs_world_register_components(void) {
	ecs_component_registry_reset();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(TEST_WORLD_POS_ID, (u32)sizeof(ecs_world_pos_t), 4u, "world-pos"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(TEST_WORLD_VEL_ID, (u32)sizeof(ecs_world_vel_t), 4u, "world-vel"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(TEST_WORLD_TAG_ID, (u32)sizeof(ecs_world_tag_t), 4u, "world-tag"));
}

SK_TEST(entities_world_spawn_despawn_generation) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.world_count(world));

	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	const sk_type_id_t pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};

	sk_entity_t e0 = entities_api.world_spawn(world, NULL, 0u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e0));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e0));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.world_count(world));

	sk_entity_t e1 = entities_api.world_spawn(world, pos_id, 1u);
	sk_entity_t e2 = entities_api.world_spawn(world, pv, 2u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e1));
	TEST_ASSERT_TRUE(sk_entity_is_valid(e2));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e1));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e2));
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.world_count(world));

	/* Despawn frees the slot; stale handles fail the generation check. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_despawn(world, e0));
	TEST_ASSERT_FALSE(entities_api.world_alive(world, e0));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.world_despawn(world, e0));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.world_count(world));

	/* Slot recycling: a new spawn reuses slot 0 with a bumped generation. */
	sk_entity_t e3 = entities_api.world_spawn(world, NULL, 0u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e3));
	TEST_ASSERT_EQUAL_UINT32(e0.index, e3.index);
	TEST_ASSERT_TRUE(e3.generation > e0.generation);
	TEST_ASSERT_FALSE(sk_entity_eq(e0, e3));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e3));
	TEST_ASSERT_FALSE(entities_api.world_alive(world, (sk_entity_t){e3.index, e0.generation}));

	/* Spawn with an unregistered component fails cleanly. */
	const sk_type_id_t unregistered = SK_TYPE_ID("sk.test.ecs.world.unregistered", 0x3B000000000000FFULL, 0x0200000000000001ULL);
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.world_spawn(world, &unregistered, 1u)));

	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.world_count(world));
	entities_api.world_destroy(world);
}

SK_TEST(entities_world_add_remove_component) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	sk_entity_t e = entities_api.world_spawn(world, pos_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));

	ecs_world_pos_t* p = (ecs_world_pos_t*)entities_api.world_component(world, e, TEST_WORLD_POS_ID);
	TEST_ASSERT_NOT_NULL(p);
	p->x = 10.0f;
	p->y = 20.0f;

	/* Add a component: the move between archetypes preserves existing data. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_add_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));
	ecs_world_vel_t* v = (ecs_world_vel_t*)entities_api.world_component(world, e, TEST_WORLD_VEL_ID);
	TEST_ASSERT_NOT_NULL(v);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, v->vx); /* zero-initialized */
	p = (ecs_world_pos_t*)entities_api.world_component(world, e, TEST_WORLD_POS_ID);
	TEST_ASSERT_NOT_NULL(p);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 10.0f, p->x); /* survived the move */
	v->vx = 3.0f;
	v->vy = 4.0f;

	/* Adding an already-present component is idempotent. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_add_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));
	v = (ecs_world_vel_t*)entities_api.world_component(world, e, TEST_WORLD_VEL_ID);
	TEST_ASSERT_NOT_NULL(v);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, v->vx);

	/* Remove restores the {pos} archetype. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_remove_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_NULL(entities_api.world_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_remove_component(world, e, TEST_WORLD_VEL_ID));

	/* Ops on dead / invalid entities fail cleanly. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_despawn(world, e));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.world_add_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.world_remove_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_NULL(entities_api.world_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_FALSE(entities_api.world_alive(world, SK_ENTITY_INVALID));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_spawn_validation) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	/* A NULL id array with a positive count. */
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.world_spawn(world, NULL, 1u)));

	/* Zero / entity component ids are rejected by signature normalization. */
	const sk_type_id_t zero[1] = {SK_TYPE_ID_ZERO};
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.world_spawn(world, zero, 1u)));
	const sk_type_id_t entity_comp[1] = {SK_ECS_ENTITY_COMPONENT_ID};
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.world_spawn(world, entity_comp, 1u)));

	/* Signatures at/over the column limit fail. */
	sk_type_id_t too_many[SK_ECS_MAX_ARCHETYPE_COLUMNS];
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_ARCHETYPE_COLUMNS; ++i) {
		too_many[i] = SK_TYPE_ID("sk.test.ecs.wspawn.many", (u64)i + 0x100ULL, 0x0200000000000002ULL);
	}
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.world_spawn(world, too_many, (u32)SK_ECS_MAX_ARCHETYPE_COLUMNS)));

	/* Unsorted + duplicate ids normalize (sort + dedup) to one archetype. */
	const sk_type_id_t messy[4] = {TEST_WORLD_TAG_ID, TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID, TEST_WORLD_POS_ID};
	sk_entity_t e = entities_api.world_spawn(world, messy, 4u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_TAG_ID));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.world_count(world));

	/* A registered component whose layout cannot fit 16 KiB fails to spawn. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(TEST_ECS_HUGE_ID, 16384u, 1u, "huge"));
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.world_spawn(world, &TEST_ECS_HUGE_ID, 1u)));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_alive_out_of_range) {
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	/* Handles beyond the slot table and stale slot indices are dead. */
	TEST_ASSERT_FALSE(entities_api.world_alive(world, (sk_entity_t){0x40000000u, 0u}));
	TEST_ASSERT_FALSE(entities_api.world_alive(world, (sk_entity_t){1u, 0u}));
	TEST_ASSERT_FALSE(entities_api.world_alive(world, SK_ENTITY_INVALID));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_add_component_validation) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	sk_entity_t e = entities_api.world_spawn(world, pos_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));

	/* Zero / entity component ids are rejected before any archetype lookup. */
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.world_add_component(world, e, SK_TYPE_ID_ZERO));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.world_add_component(world, e, SK_ECS_ENTITY_COMPONENT_ID));

	/* An unregistered id fails the target-archetype lookup. */
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.world_add_component(world, e, TEST_ECS_UNREG_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_ECS_UNREG_ID));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_POS_ID));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_query_create_rejects_invalid_desc) {
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_NULL(entities_api.world_query_create(world, NULL));
	entities_api.world_destroy(world);
}

SK_TEST(entities_world_move_updates_swapped_slot) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	sk_entity_t a = entities_api.world_spawn(world, pos_id, 1u);
	sk_entity_t b = entities_api.world_spawn(world, pos_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(a));
	TEST_ASSERT_TRUE(sk_entity_is_valid(b));
	((ecs_world_pos_t*)entities_api.world_component(world, a, TEST_WORLD_POS_ID))->x = 1.0f;
	((ecs_world_pos_t*)entities_api.world_component(world, b, TEST_WORLD_POS_ID))->x = 2.0f;

	/* Moving `a` (row 0) out swaps the last entity `b` (row 1) into row 0;
	 * b's cached location must be rewritten or its component reads break. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_add_component(world, a, TEST_WORLD_VEL_ID));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, b));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, b, TEST_WORLD_POS_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, b, TEST_WORLD_VEL_ID));
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, ((const ecs_world_pos_t*)entities_api.world_component(world, b, TEST_WORLD_POS_ID))->x);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, ((const ecs_world_pos_t*)entities_api.world_component(world, a, TEST_WORLD_POS_ID))->x);

	/* Both a ({pos, vel}) and b ({pos}) match the pos query. */
	const sk_type_id_t req_pos[1] = {TEST_WORLD_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.world_query_create(world, &d_pos);
	TEST_ASSERT_NOT_NULL(q);
	u32 seen = 0u;
	f32 sum = 0.0f;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		const ecs_world_pos_t* pp = SK_ECS_ITER_AT(it, 1, ecs_world_pos_t);
		TEST_ASSERT_NOT_NULL(pp);
		seen += 1u;
		sum += pp->x;
	}
	TEST_ASSERT_EQUAL_UINT32(2u, seen);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, sum);

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_archetype_cache) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_hash_map_count(&world->archetype_cache));

	/* Repeated spawns of one signature share a single cached archetype. */
	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	for (u32 i = 0u; i < 10u; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, pos_id, 1u)));
	}
	TEST_ASSERT_EQUAL_UINT32(1u, world->archetypes.count);
	TEST_ASSERT_EQUAL_UINT32(1u, sk_hash_map_count(&world->archetype_cache));

	/* Unsorted and sorted spellings of the same set normalize to one entry. */
	const sk_type_id_t pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	const sk_type_id_t vp[2] = {TEST_WORLD_VEL_ID, TEST_WORLD_POS_ID};
	for (u32 i = 0u; i < 3u; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, pv, 2u)));
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, vp, 2u)));
	}
	TEST_ASSERT_EQUAL_UINT32(2u, world->archetypes.count);
	TEST_ASSERT_EQUAL_UINT32(2u, sk_hash_map_count(&world->archetype_cache));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_archetype_cache_many_distinct) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	/* All 2^3 component subsets create distinct archetypes, each cached once. */
	for (u32 mask = 0u; mask < 8u; ++mask) {
		sk_type_id_t ids[3];
		u32 n = 0u;
		if ((mask & 1u) != 0u) {
			ids[n++] = TEST_WORLD_POS_ID;
		}
		if ((mask & 2u) != 0u) {
			ids[n++] = TEST_WORLD_VEL_ID;
		}
		if ((mask & 4u) != 0u) {
			ids[n++] = TEST_WORLD_TAG_ID;
		}
		for (u32 rep = 0u; rep < 3u; ++rep) {
			TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, ids, n)));
		}
	}
	TEST_ASSERT_EQUAL_UINT32(8u, world->archetypes.count);
	TEST_ASSERT_EQUAL_UINT32(8u, sk_hash_map_count(&world->archetype_cache));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_add_remove_reuses_archetypes) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	sk_entity_t e = entities_api.world_spawn(world, pos_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));

	/* add/remove round-trips must hit the cached target archetypes, not
	 * accumulate new ones. */
	for (u32 i = 0u; i < 8u; ++i) {
		TEST_ASSERT_EQUAL_INT32(0, entities_api.world_add_component(world, e, TEST_WORLD_VEL_ID));
		TEST_ASSERT_EQUAL_INT32(0, entities_api.world_remove_component(world, e, TEST_WORLD_VEL_ID));
	}
	TEST_ASSERT_EQUAL_UINT32(2u, world->archetypes.count); /* {pos}, {pos, vel} */
	TEST_ASSERT_EQUAL_UINT32(2u, sk_hash_map_count(&world->archetype_cache));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));

	entities_api.world_destroy(world);
}

SK_TEST(entities_world_chunk_hint_reuses_freed_rows) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	sk_entity_t first = entities_api.world_spawn(world, pv, 2u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(first));
	sk_archetype_t* arch = world->archetypes.items[0u];
	const u32 capacity = arch->chunk_capacity;

	/* Fill chunk 0, then overflow into chunk 1. */
	for (u32 i = 1u; i < capacity; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, pv, 2u)));
	}
	TEST_ASSERT_EQUAL_UINT32(1u, arch->chunks.count);
	TEST_ASSERT_EQUAL_UINT32(capacity, arch->chunks.items[0u]->count);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, pv, 2u)));
	TEST_ASSERT_EQUAL_UINT32(2u, arch->chunks.count);
	TEST_ASSERT_EQUAL_UINT32(1u, arch->chunks.items[1u]->count);

	/* Despawning the earliest entity frees a row in chunk 0; the next spawn
	 * must reuse that row instead of appending a third chunk. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.world_despawn(world, first));
	TEST_ASSERT_EQUAL_UINT32(capacity - 1u, arch->chunks.items[0u]->count);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.world_spawn(world, pv, 2u)));
	TEST_ASSERT_EQUAL_UINT32(2u, arch->chunks.count);
	TEST_ASSERT_EQUAL_UINT32(capacity, arch->chunks.items[0u]->count);
	TEST_ASSERT_EQUAL_UINT32(1u, arch->chunks.items[1u]->count);

	entities_api.world_destroy(world);
}

SK_TEST(entities_archetype_column_binary_search_before_entity_id) {
	/* User components whose type ids sort on either side of the implicit
	 * entity component id must still map to their exact (non-zero) column. */
	const sk_type_id_t low_id = SK_TYPE_ID("sk.test.ecs.col.low", 0x0000000000000001ULL, 0x0000000000000001ULL);
	const sk_type_id_t high_id = SK_TYPE_ID("sk.test.ecs.col.high", 0xFFFF000000000000ULL, 0x0000000000000001ULL);
	const sk_component_info_t comps[2] = {
		{low_id, 4u, 4u, "low"},
		{high_id, 4u, 4u, "high"},
	};
	sk_archetype_t* a = entities_api.archetype_create(comps, 2u);
	TEST_ASSERT_NOT_NULL(a);

	TEST_ASSERT_EQUAL_INT32(0, entities_api.archetype_column(a, SK_ECS_ENTITY_COMPONENT_ID));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.archetype_column(a, low_id));
	TEST_ASSERT_EQUAL_INT32(2, entities_api.archetype_column(a, high_id));
	TEST_ASSERT_TRUE(entities_api.archetype_has(a, low_id));
	TEST_ASSERT_FALSE(entities_api.archetype_has(a, SK_TYPE_ID("sk.test.ecs.col.missing", 0x0000000000000002ULL, 0x0000000000000001ULL)));

	entities_api.archetype_destroy(a);
}

SK_TEST(entities_world_query_observes_new_archetypes) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t req_pos[1] = {TEST_WORLD_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.world_query_create(world, &d_pos);
	TEST_ASSERT_NOT_NULL(q);

	/* Spawns that create a new {pos} archetype after the query must be seen. */
	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	sk_entity_t a = entities_api.world_spawn(world, pos_id, 1u);
	sk_entity_t b = entities_api.world_spawn(world, pos_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(a));
	TEST_ASSERT_TRUE(sk_entity_is_valid(b));
	entities_api.world_spawn(world, NULL, 0u); /* no pos: not matched */

	((ecs_world_pos_t*)entities_api.world_component(world, a, TEST_WORLD_POS_ID))->x = 1.0f;
	((ecs_world_pos_t*)entities_api.world_component(world, b, TEST_WORLD_POS_ID))->x = 2.0f;

	u32 seen = 0u;
	f32 sum = 0.0f;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		const ecs_world_pos_t* pp = SK_ECS_ITER_AT(it, 1, ecs_world_pos_t);
		TEST_ASSERT_NOT_NULL(pp);
		TEST_ASSERT_TRUE(sk_entity_is_valid(SK_ECS_ITER_ENTITY_ROW(it)));
		seen += 1u;
		sum += pp->x;
	}
	TEST_ASSERT_EQUAL_UINT32(2u, seen);
	TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, sum);

	/* The world-managed query is destroyed with the world. */
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_apply_order) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	/* Record a spawn, then component adds referencing the deferred handle,
	 * then a second spawn that is immediately despawned. */
	sk_entity_t ph = entities_api.commands_spawn(cmds, NULL, 0u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(ph));
	const ecs_world_vel_t vel = {5.0f, 6.0f};
	const ecs_world_tag_t tag = {7u};
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, ph, TEST_WORLD_VEL_ID, &vel));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, ph, TEST_WORLD_TAG_ID, &tag));
	sk_entity_t ph2 = entities_api.commands_spawn(cmds, NULL, 0u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(ph2));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_despawn(cmds, ph2));

	/* Nothing applied while recording: the world is untouched. */
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.world_count(world));
	TEST_ASSERT_EQUAL_UINT32(5u, entities_api.commands_count(cmds));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));

	/* ph2 was spawned then despawned in FIFO order: only ph survives. */
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.world_count(world));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));

	/* Locate the survivor and verify the ordered component values. */
	const sk_query_desc_t d_none = SK_QUERY_DESC_NONE;
	sk_query_t* q = entities_api.world_query_create(world, &d_none);
	TEST_ASSERT_NOT_NULL(q);
	sk_entity_t found = SK_ENTITY_INVALID;
	u32 seen = 0u;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		found = SK_ECS_ITER_ENTITY_ROW(it);
		seen += 1u;
	}
	TEST_ASSERT_EQUAL_UINT32(1u, seen);
	TEST_ASSERT_TRUE(entities_api.world_alive(world, found));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, found, TEST_WORLD_VEL_ID));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, found, TEST_WORLD_TAG_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, found, TEST_WORLD_POS_ID));
	const ecs_world_vel_t* v = (const ecs_world_vel_t*)entities_api.world_component(world, found, TEST_WORLD_VEL_ID);
	TEST_ASSERT_NOT_NULL(v);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 5.0f, v->vx);
	TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.0f, v->vy);
	const ecs_world_tag_t* tg = (const ecs_world_tag_t*)entities_api.world_component(world, found, TEST_WORLD_TAG_ID);
	TEST_ASSERT_NOT_NULL(tg);
	TEST_ASSERT_EQUAL_UINT32(7u, tg->flags);

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_batch_create_destroy) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	const sk_type_id_t pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	const u32 total = 100u;
	sk_entity_t placeholders[100];
	for (u32 i = 0u; i < total; ++i) {
		sk_entity_t ph = entities_api.commands_spawn(cmds, pv, 2u);
		TEST_ASSERT_TRUE(sk_entity_is_valid(ph));
		placeholders[i] = ph;
		const ecs_world_pos_t pos = {(f32)i, (f32)i + 1.0f};
		TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, ph, TEST_WORLD_POS_ID, &pos));
	}
	/* Despawn every even placeholder: 50 survivors (odd i). */
	for (u32 i = 0u; i < total; i += 2u) {
		TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_despawn(cmds, placeholders[i]));
	}
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.world_count(world));
	TEST_ASSERT_EQUAL_UINT32(250u, entities_api.commands_count(cmds)); /* 100 spawn + 100 add + 50 despawn */

	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(50u, entities_api.world_count(world));

	/* Survivors hold {pos, vel} with the recorded values: sum of odd i. */
	const sk_type_id_t req_pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	const sk_query_desc_t d_pv = {req_pv, 2u, NULL, 0u, NULL, 0u};
	sk_query_t* q = entities_api.world_query_create(world, &d_pv);
	TEST_ASSERT_NOT_NULL(q);
	u32 seen = 0u;
	f32 sum_x = 0.0f;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		const ecs_world_pos_t* pp = SK_ECS_ITER_AT(it, 1, ecs_world_pos_t);
		const ecs_world_vel_t* vp = SK_ECS_ITER_AT(it, 2, ecs_world_vel_t);
		TEST_ASSERT_NOT_NULL(pp);
		TEST_ASSERT_NOT_NULL(vp);
		TEST_ASSERT_TRUE(sk_entity_is_valid(SK_ECS_ITER_ENTITY_ROW(it)));
		seen += 1u;
		sum_x += pp->x;
	}
	TEST_ASSERT_EQUAL_UINT32(50u, seen);
	TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2500.0f, sum_x); /* 1 + 3 + ... + 99 */

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_deferred_during_query_iteration) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);

	const sk_type_id_t pos_id[1] = {TEST_WORLD_POS_ID};
	for (u32 i = 0u; i < 3u; ++i) {
		sk_entity_t e = entities_api.world_spawn(world, pos_id, 1u);
		TEST_ASSERT_TRUE(sk_entity_is_valid(e));
		((ecs_world_pos_t*)entities_api.world_component(world, e, TEST_WORLD_POS_ID))->x = (f32)(i + 1);
	}
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.world_count(world));

	const sk_type_id_t req_pos[1] = {TEST_WORLD_POS_ID};
	const sk_query_desc_t d_pos = {req_pos, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* qpos = entities_api.world_query_create(world, &d_pos);
	TEST_ASSERT_NOT_NULL(qpos);

	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	/* Record structural changes while a query is live; the world must not
	 * change until apply. */
	sk_entity_t first = SK_ENTITY_INVALID;
	u32 rows_seen = 0u;
	SK_ECS_QUERY_EACH(&entities_api, qpos, it) {
		const sk_entity_t current = SK_ECS_ITER_ENTITY_ROW(it);
		if (rows_seen == 0u) {
			first = current;
			TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_despawn(cmds, current));
		}
		const ecs_world_vel_t vel = {1.0f, 2.0f};
		TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, current, TEST_WORLD_VEL_ID, &vel));
		rows_seen += 1u;
	}
	TEST_ASSERT_EQUAL_UINT32(3u, rows_seen);

	/* One more deferred spawn with {pos, vel}. */
	const sk_type_id_t pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.commands_spawn(cmds, pv, 2u)));

	/* Pending commands never touch the world. */
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.world_count(world));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, first));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));

	/* 3 spawned - 1 despawned + 1 spawned = 3 live; the despawned one is gone. */
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.world_count(world));
	TEST_ASSERT_FALSE(entities_api.world_alive(world, first));

	/* All survivors now hold {pos, vel}. */
	const sk_type_id_t req_pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	const sk_query_desc_t d_pv = {req_pv, 2u, NULL, 0u, NULL, 0u};
	sk_query_t* qpv = entities_api.world_query_create(world, &d_pv);
	TEST_ASSERT_NOT_NULL(qpv);
	u32 seen_pos = 0u;
	SK_ECS_QUERY_EACH(&entities_api, qpos, it) {
		seen_pos += 1u;
	}
	u32 seen_pv = 0u;
	SK_ECS_QUERY_EACH(&entities_api, qpv, it) {
		seen_pv += 1u;
	}
	TEST_ASSERT_EQUAL_UINT32(3u, seen_pos);
	TEST_ASSERT_EQUAL_UINT32(3u, seen_pv);

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_buffer_reuse_and_clear) {
	ecs_world_register_components();

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	/* Apply resets the buffer; it can be re-recorded immediately. */
	for (u32 i = 0u; i < 5u; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.commands_spawn(cmds, NULL, 0u)));
	}
	TEST_ASSERT_EQUAL_UINT32(5u, entities_api.commands_count(cmds));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(5u, entities_api.world_count(world));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));

	/* Clear drops pending commands without touching the world. */
	for (u32 i = 0u; i < 3u; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.commands_spawn(cmds, NULL, 0u)));
	}
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.commands_count(cmds));
	entities_api.commands_clear(cmds);
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));
	TEST_ASSERT_EQUAL_UINT32(5u, entities_api.world_count(world));

	/* Reuse after clear and after apply. */
	for (u32 i = 0u; i < 2u; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.commands_spawn(cmds, NULL, 0u)));
	}
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(7u, entities_api.world_count(world));
	for (u32 i = 0u; i < 4u; ++i) {
		TEST_ASSERT_TRUE(sk_entity_is_valid(entities_api.commands_spawn(cmds, NULL, 0u)));
	}
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(11u, entities_api.world_count(world));

	/* Value copies are taken at record time: mutating the caller's memory
	 * afterwards must not affect the applied value. */
	ecs_world_vel_t vel = {9.0f, 9.0f};
	sk_entity_t ph = entities_api.commands_spawn(cmds, NULL, 0u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(ph));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, ph, TEST_WORLD_VEL_ID, &vel));
	vel.vx = 999.0f;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(12u, entities_api.world_count(world));

	const sk_query_desc_t d_none = SK_QUERY_DESC_NONE;
	sk_query_t* q = entities_api.world_query_create(world, &d_none);
	TEST_ASSERT_NOT_NULL(q);
	u32 vel_entities = 0u;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		const sk_entity_t e = SK_ECS_ITER_ENTITY_ROW(it);
		if (entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID)) {
			const ecs_world_vel_t* v = (const ecs_world_vel_t*)entities_api.world_component(world, e, TEST_WORLD_VEL_ID);
			TEST_ASSERT_NOT_NULL(v);
			TEST_ASSERT_FLOAT_WITHIN(1e-5f, 9.0f, v->vx);
			TEST_ASSERT_FLOAT_WITHIN(1e-5f, 9.0f, v->vy);
			vel_entities += 1u;
		}
	}
	TEST_ASSERT_EQUAL_UINT32(1u, vel_entities);

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_spawn_validation) {
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.commands_spawn(cmds, NULL, 1u)));
	const sk_type_id_t zero[1] = {SK_TYPE_ID_ZERO};
	TEST_ASSERT_FALSE(sk_entity_is_valid(entities_api.commands_spawn(cmds, zero, 1u)));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));

	entities_api.commands_destroy(cmds);
}

SK_TEST(entities_commands_target_validation) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	const ecs_world_vel_t vel = {1.0f, 2.0f};
	const sk_entity_t handle = {1u, 0u};

	/* Invalid handles are rejected at record time. */
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_despawn(cmds, SK_ENTITY_INVALID));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_add_component(cmds, SK_ENTITY_INVALID, TEST_WORLD_VEL_ID, &vel));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_remove_component(cmds, SK_ENTITY_INVALID, TEST_WORLD_VEL_ID));

	/* Zero / entity component ids are rejected for add/remove. */
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_add_component(cmds, handle, SK_TYPE_ID_ZERO, &vel));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_add_component(cmds, handle, SK_ECS_ENTITY_COMPONENT_ID, &vel));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_remove_component(cmds, handle, SK_TYPE_ID_ZERO));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_remove_component(cmds, handle, SK_ECS_ENTITY_COMPONENT_ID));

	/* Copying a value for an unregistered type fails at record time. */
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_add_component(cmds, handle, TEST_ECS_UNREG_ID, &vel));

	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));
	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_remove_component_roundtrip) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	/* Deferred spawn {pos, vel}, then remove vel before apply. */
	const sk_type_id_t pv[2] = {TEST_WORLD_POS_ID, TEST_WORLD_VEL_ID};
	sk_entity_t ph = entities_api.commands_spawn(cmds, pv, 2u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(ph));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_remove_component(cmds, ph, TEST_WORLD_VEL_ID));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));

	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.world_count(world));
	const sk_query_desc_t d_none = SK_QUERY_DESC_NONE;
	sk_query_t* q = entities_api.world_query_create(world, &d_none);
	TEST_ASSERT_NOT_NULL(q);
	sk_entity_t e = SK_ENTITY_INVALID;
	SK_ECS_QUERY_EACH(&entities_api, q, it) {
		e = SK_ECS_ITER_ENTITY_ROW(it);
	}
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));
	TEST_ASSERT_TRUE(entities_api.world_has_component(world, e, TEST_WORLD_POS_ID));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));

	/* Removing a component the entity lacks is an idempotent success. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_remove_component(cmds, e, TEST_WORLD_VEL_ID));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_WORLD_VEL_ID));

	/* Removing from an entity despawned earlier in the same buffer is skipped. */
	sk_entity_t ph2 = entities_api.commands_spawn(cmds, pv, 2u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(ph2));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_despawn(cmds, ph2));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_remove_component(cmds, ph2, TEST_WORLD_POS_ID));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.world_count(world));

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_unknown_placeholder_skipped) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	/* A placeholder whose pending index never existed resolves to INVALID and
	 * its ops are skipped at apply. */
	const sk_entity_t bogus = {SK_ECS_DEFERRED_BASE | 0x1000u, 0u};
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_despawn(cmds, bogus));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, bogus, TEST_WORLD_VEL_ID, NULL));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_remove_component(cmds, bogus, TEST_WORLD_VEL_ID));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.world_count(world));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

SK_TEST(entities_commands_apply_reports_failures) {
	ecs_world_register_components();
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	sk_entitycommands_t* cmds = entities_api.commands_create();
	TEST_ASSERT_NOT_NULL(cmds);

	/* A deferred spawn carrying an unregistered component fails at apply. */
	sk_entity_t ph = entities_api.commands_spawn(cmds, &TEST_ECS_UNREG_ID, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(ph));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.world_count(world));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.commands_count(cmds));

	/* Adding an unregistered component (recorded with a NULL value, which is
	 * not looked up until apply) fails at apply. */
	sk_entity_t e = entities_api.world_spawn(world, NULL, 0u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.commands_add_component(cmds, e, TEST_ECS_UNREG_ID, NULL));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.commands_apply(cmds, world));
	TEST_ASSERT_TRUE(entities_api.world_alive(world, e));
	TEST_ASSERT_FALSE(entities_api.world_has_component(world, e, TEST_ECS_UNREG_ID));

	entities_api.commands_destroy(cmds);
	entities_api.world_destroy(world);
}

/* ---- systems / scheduler test harness ---- */

/* A recorded run: each system appends its (unique) user_data id when run. */
typedef struct ecs_test_run_t {
	u32 ids[SK_ECS_MAX_SYSTEMS];
	u32 count;
} ecs_test_run_t;

static void ecs_test_run_callback(sk_world_t* world, f32 delta_time, void_ptr_t user_data) {
	(void)world;
	(void)delta_time;
	ecs_test_run_t* run = (ecs_test_run_t*)user_data;
	TEST_ASSERT_TRUE(run->count < (u32)SK_ECS_MAX_SYSTEMS);
	run->ids[run->count] = run->count + 1u;
	run->count += 1u;
}

static ecs_test_run_t ecs_test_run_make(void) {
	ecs_test_run_t run = {0};
	return run;
}

static void ecs_test_assert_run(const ecs_test_run_t* run, const u32* expected, u32 expected_count) {
	TEST_ASSERT_EQUAL_UINT32(expected_count, run->count);
	for (u32 i = 0u; i < expected_count; ++i) {
		TEST_ASSERT_EQUAL_UINT32(expected[i], run->ids[i]);
	}
}

/* Component ids used by the system / scheduler tests (never registered; the
 * scheduler only compares them, it does not require component registration). */
#define TEST_SYS_A_ID SK_TYPE_ID("sk.test.ecs.sys.a", 0x3D00000000000001ULL, 0x0700000000000001ULL)
#define TEST_SYS_B_ID SK_TYPE_ID("sk.test.ecs.sys.b", 0x3D00000000000002ULL, 0x0700000000000001ULL)
#define TEST_SYS_C_ID SK_TYPE_ID("sk.test.ecs.sys.c", 0x3D00000000000003ULL, 0x0700000000000001ULL)
#define TEST_SYS_D_ID SK_TYPE_ID("sk.test.ecs.sys.d", 0x3D00000000000004ULL, 0x0700000000000001ULL)

/* Build a system descriptor for the shared run-record callback. */
static sk_system_desc_t ecs_test_sys_desc(ecs_test_run_t* run, const sk_type_id_t* reads, u32 read_count, const sk_type_id_t* writes, u32 write_count) {
	sk_system_desc_t desc = {0};
	desc.callback = ecs_test_run_callback;
	desc.reads = reads;
	desc.read_count = read_count;
	desc.writes = writes;
	desc.write_count = write_count;
	desc.user_data = run;
	return desc;
}

SK_TEST(entities_system_create_validation) {
	TEST_ASSERT_NULL(entities_api.system_create(NULL));

	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t desc = ecs_test_sys_desc(&run, NULL, 0u, NULL, 0u);
	desc.callback = NULL;
	TEST_ASSERT_NULL(entities_api.system_create(&desc));

	/* A count with a NULL id array is invalid. */
	desc = ecs_test_sys_desc(&run, NULL, 1u, NULL, 0u);
	TEST_ASSERT_NULL(entities_api.system_create(&desc));

	/* A zero id in either set is invalid. */
	const sk_type_id_t zero_ids[1] = {SK_TYPE_ID_ZERO};
	desc = ecs_test_sys_desc(&run, zero_ids, 1u, NULL, 0u);
	TEST_ASSERT_NULL(entities_api.system_create(&desc));

	/* Duplicate ids within one set are invalid. */
	const sk_type_id_t dup_ids[2] = {TEST_SYS_A_ID, TEST_SYS_A_ID};
	desc = ecs_test_sys_desc(&run, NULL, 0u, dup_ids, 2u);
	TEST_ASSERT_NULL(entities_api.system_create(&desc));

	/* The same id in both the read and write set of one system is allowed
	 * (the system modifies a component in place). */
	const sk_type_id_t rw_ids[1] = {TEST_SYS_A_ID};
	desc = ecs_test_sys_desc(&run, rw_ids, 1u, rw_ids, 1u);
	sk_system_t* system = entities_api.system_create(&desc);
	TEST_ASSERT_NOT_NULL(system);
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.system_read_count(system));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.system_write_count(system));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_SYS_A_ID, entities_api.system_read_id(system, 0u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_SYS_A_ID, entities_api.system_write_id(system, 0u)));
	entities_api.system_destroy(system);
}

SK_TEST(entities_system_reads_writes_roundtrip) {
	sk_type_id_t reads[2] = {TEST_SYS_A_ID, TEST_SYS_B_ID};
	const sk_type_id_t writes[1] = {TEST_SYS_C_ID};
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t desc = ecs_test_sys_desc(&run, reads, 2u, writes, 1u);
	desc.name = "roundtrip";
	sk_system_t* system = entities_api.system_create(&desc);
	TEST_ASSERT_NOT_NULL(system);

	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.system_read_count(system));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.system_write_count(system));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_SYS_A_ID, entities_api.system_read_id(system, 0u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_SYS_B_ID, entities_api.system_read_id(system, 1u)));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_SYS_C_ID, entities_api.system_write_id(system, 0u)));
	TEST_ASSERT_EQUAL_STRING("roundtrip", entities_api.system_name(system));

	/* The declared sets are copied: the caller's arrays may go stale. */
	reads[0] = SK_TYPE_ID_ZERO;
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(TEST_SYS_A_ID, entities_api.system_read_id(system, 0u)));

	/* Invoking the callback runs exactly once with the stored user_data. */
	TEST_ASSERT_EQUAL_UINT32(0u, run.count);
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	entities_api.system_run(system, world, 1.0f);
	TEST_ASSERT_EQUAL_UINT32(1u, run.count);
	TEST_ASSERT_EQUAL_UINT32(1u, run.ids[0]);
	entities_api.world_destroy(world);

	entities_api.system_destroy(system);
}

SK_TEST(entities_scheduler_simple_chain_write_read) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};

	/* writer(a) -> reader(a)+writer(b) -> reader(b). */
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t w_a = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_desc_t rw_b = ecs_test_sys_desc(&run, a, 1u, b, 1u);
	sk_system_desc_t r_c = ecs_test_sys_desc(&run, b, 1u, NULL, 0u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_NOT_NULL(scheduler);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_a)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&rw_b)));
	TEST_ASSERT_EQUAL_INT32(2, entities_api.scheduler_add(scheduler, entities_api.system_create(&r_c)));
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.scheduler_system_count(scheduler));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_FALSE(entities_api.scheduler_has_cycle(scheduler));
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.scheduler_order_count(scheduler));

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	const u32 expected[3] = {1u, 2u, 3u};
	ecs_test_assert_run(&run, expected, 3u);
	entities_api.world_destroy(world);

	for (u32 i = 0u; i < 3u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_simple_chain_write_write) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};

	/* Two writers of the same component: the earlier-registered runs first
	 * (write-write conflict). reader(c) after both stays last. */
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t w_a0 = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_desc_t w_a1 = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_desc_t r_c = ecs_test_sys_desc(&run, b, 1u, NULL, 0u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_a0)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_a1)));
	TEST_ASSERT_EQUAL_INT32(2, entities_api.scheduler_add(scheduler, entities_api.system_create(&r_c)));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_EQUAL_UINT32(3u, entities_api.scheduler_order_count(scheduler));

	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	const u32 expected[3] = {1u, 2u, 3u};
	ecs_test_assert_run(&run, expected, 3u);
	entities_api.world_destroy(world);

	for (u32 i = 0u; i < 3u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_diamond_graph) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};
	const sk_type_id_t c[1] = {TEST_SYS_C_ID};

	/* writer(a) -> { reader(a)+writer(b), reader(a)+writer(c) } -> reader(b,c).
	 * The two middle systems are independent (only read a), so the order
	 * among them is deterministic by registration index (tie-break). */
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t w_a = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_desc_t rw_b = ecs_test_sys_desc(&run, a, 1u, b, 1u);
	sk_system_desc_t rw_c = ecs_test_sys_desc(&run, a, 1u, c, 1u);
	sk_system_desc_t r_bc = ecs_test_sys_desc(&run, b, 2u, NULL, 0u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_a)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&rw_b)));
	TEST_ASSERT_EQUAL_INT32(2, entities_api.scheduler_add(scheduler, entities_api.system_create(&rw_c)));
	TEST_ASSERT_EQUAL_INT32(3, entities_api.scheduler_add(scheduler, entities_api.system_create(&r_bc)));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_FALSE(entities_api.scheduler_has_cycle(scheduler));
	TEST_ASSERT_EQUAL_UINT32(4u, entities_api.scheduler_order_count(scheduler));

	/* Deterministic: registration-order tie-break among the independent
	 * middle systems gives 0,1,2,3. */
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	const u32 expected[4] = {1u, 2u, 3u, 4u};
	ecs_test_assert_run(&run, expected, 4u);

	/* Rebuild is deterministic: another build yields the same order. */
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	run = ecs_test_run_make();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	ecs_test_assert_run(&run, expected, 4u);
	entities_api.world_destroy(world);

	for (u32 i = 0u; i < 4u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_no_false_edges_disjoint_sets) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};

	/* Pure readers of the same component share no edge (read-read). */
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t r_a0 = ecs_test_sys_desc(&run, a, 1u, NULL, 0u);
	sk_system_desc_t r_a1 = ecs_test_sys_desc(&run, a, 1u, NULL, 0u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&r_a0)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&r_a1)));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_FALSE(entities_api.scheduler_has_cycle(scheduler));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.scheduler_order_count(scheduler));
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	const u32 expected_rr[2] = {1u, 2u};
	ecs_test_assert_run(&run, expected_rr, 2u);
	for (u32 i = 0u; i < 2u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);

	/* Fully disjoint systems (different writes) share no edge. */
	run = ecs_test_run_make();
	sk_system_desc_t w_a = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_desc_t w_b = ecs_test_sys_desc(&run, NULL, 0u, b, 1u);

	scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_a)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_b)));

	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_FALSE(entities_api.scheduler_has_cycle(scheduler));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.scheduler_order_count(scheduler));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	const u32 expected_disjoint[2] = {1u, 2u};
	ecs_test_assert_run(&run, expected_disjoint, 2u);
	entities_api.world_destroy(world);
	for (u32 i = 0u; i < 2u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_cycle_detected) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};

	/* a -> b -> a: writer(a)+reader(b), then writer(b)+reader(a). */
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t s0 = ecs_test_sys_desc(&run, b, 1u, a, 1u);
	sk_system_desc_t s1 = ecs_test_sys_desc(&run, a, 1u, b, 1u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&s0)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&s1)));

	/* Build detects the cycle and returns an error. */
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_TRUE(entities_api.scheduler_has_cycle(scheduler));
	TEST_ASSERT_TRUE(entities_api.scheduler_order_count(scheduler) < entities_api.scheduler_system_count(scheduler));

	/* Running while a cycle is present refuses to execute any callback. */
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.scheduler_run(scheduler, world, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(0u, run.count);
	entities_api.world_destroy(world);

	for (u32 i = 0u; i < 2u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_rebuilds_on_add) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};

	/* scheduler_run implicitly rebuilds when systems were added after the
	 * last build, so the new system joins the run order. */
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t w_a = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_desc_t r_b = ecs_test_sys_desc(&run, b, 1u, NULL, 0u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&w_a)));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.scheduler_order_count(scheduler));

	/* Adding a second system invalidates the order; run rebuilds it. */
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&r_b)));
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_run(scheduler, world, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_api.scheduler_order_count(scheduler));
	const u32 expected[2] = {1u, 2u};
	ecs_test_assert_run(&run, expected, 2u);
	entities_api.world_destroy(world);

	for (u32 i = 0u; i < 2u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_system_read_count_overflow) {
	ecs_test_run_t run = ecs_test_run_make();
	sk_type_id_t reads[SK_ECS_MAX_SYSTEM_COMPONENTS + 1u];
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_SYSTEM_COMPONENTS + 1u; ++i) {
		reads[i] = SK_TYPE_ID("sk.test.ecs.sys.over", (u64)i + 0x10ULL, 0x01ULL);
	}
	sk_system_desc_t desc = ecs_test_sys_desc(&run, reads, (u32)SK_ECS_MAX_SYSTEM_COMPONENTS + 1u, NULL, 0u);
	TEST_ASSERT_NULL(entities_api.system_create(&desc));
}

SK_TEST(entities_scheduler_empty_build) {
	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_NOT_NULL(scheduler);
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.scheduler_system_count(scheduler));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_FALSE(entities_api.scheduler_has_cycle(scheduler));
	TEST_ASSERT_EQUAL_UINT32(0u, entities_api.scheduler_order_count(scheduler));
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_add_validation_and_capacity) {
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t desc = ecs_test_sys_desc(&run, NULL, 0u, NULL, 0u);
	sk_system_t* system = entities_api.system_create(&desc);
	TEST_ASSERT_NOT_NULL(system);

	TEST_ASSERT_EQUAL_INT32(-1, entities_api.scheduler_add(NULL, system));
	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_NOT_NULL(scheduler);
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.scheduler_add(scheduler, NULL));

	/* Filling the scheduler returns -2 once the cap is reached. */
	i32 last = 0;
	for (u32 i = 0u; i <= (u32)SK_ECS_MAX_SYSTEMS; ++i) {
		last = entities_api.scheduler_add(scheduler, system);
	}
	TEST_ASSERT_EQUAL_INT32(-2, last);
	TEST_ASSERT_EQUAL_UINT32((u32)SK_ECS_MAX_SYSTEMS, entities_api.scheduler_system_count(scheduler));

	/* Out-of-range system access returns NULL. */
	TEST_ASSERT_NULL(entities_api.scheduler_system(scheduler, (u32)SK_ECS_MAX_SYSTEMS));
	TEST_ASSERT_NULL(entities_api.scheduler_system(scheduler, 0x40000000u));

	entities_api.system_destroy(system);
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_order_accessors) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t w_a = ecs_test_sys_desc(&run, NULL, 0u, a, 1u);
	sk_system_t* s0 = entities_api.system_create(&w_a);
	TEST_ASSERT_NOT_NULL(s0);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, s0));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_build(scheduler));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_api.scheduler_order_count(scheduler));
	TEST_ASSERT_EQUAL_PTR((const_ptr_t)s0, (const_ptr_t)entities_api.scheduler_order_at(scheduler, 0u));
	TEST_ASSERT_NULL(entities_api.scheduler_order_at(scheduler, 1u));
	TEST_ASSERT_NULL(entities_api.scheduler_order_at(scheduler, 0x40000000u));

	entities_api.system_destroy(s0);
	entities_api.scheduler_destroy(scheduler);
}

SK_TEST(entities_scheduler_run_implicit_build_rejects_cycle) {
	const sk_type_id_t a[1] = {TEST_SYS_A_ID};
	const sk_type_id_t b[1] = {TEST_SYS_B_ID};
	ecs_test_run_t run = ecs_test_run_make();
	sk_system_desc_t s0 = ecs_test_sys_desc(&run, b, 1u, a, 1u);
	sk_system_desc_t s1 = ecs_test_sys_desc(&run, a, 1u, b, 1u);

	sk_scheduler_t* scheduler = entities_api.scheduler_create();
	TEST_ASSERT_EQUAL_INT32(0, entities_api.scheduler_add(scheduler, entities_api.system_create(&s0)));
	TEST_ASSERT_EQUAL_INT32(1, entities_api.scheduler_add(scheduler, entities_api.system_create(&s1)));

	/* Never build: scheduler_run's implicit rebuild detects the cycle. */
	sk_world_t* world = entities_api.world_create();
	TEST_ASSERT_NOT_NULL(world);
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.scheduler_run(scheduler, world, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(0u, run.count);
	entities_api.world_destroy(world);

	for (u32 i = 0u; i < 2u; ++i) {
		entities_api.system_destroy(entities_api.scheduler_system(scheduler, i));
	}
	entities_api.scheduler_destroy(scheduler);
}

/*
 * Exhausts the component registry (SK_ECS_MAX_COMPONENT_TYPES entries).
 * Starts from a clean registry and fills it to capacity regardless of when
 * this test runs (constructor registration order is toolchain-dependent).
 */
SK_TEST(entities_register_component_capacity) {
	ecs_component_registry_reset();
	u32 accepted = 0u;
	i32 last = 0;
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_COMPONENT_TYPES + 4u; ++i) {
		sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.capacity", (u64)(i + 1000u), 0xAAAAAAAAAAAAAAAAULL);
		last = entities_api.register_component(id, 4u, 4u, "capacity");
		if (last == 0) {
			accepted += 1u;
		}
	}

	TEST_ASSERT_EQUAL_UINT32((u32)SK_ECS_MAX_COMPONENT_TYPES, accepted);
	TEST_ASSERT_EQUAL_INT32(-2, last);

	sk_type_id_t full_id = SK_TYPE_ID("sk.test.ecs.capacity.full", 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
	TEST_ASSERT_EQUAL_INT32(-2, entities_api.register_component(full_id, 4u, 4u, "full"));
}

#endif /* SK_TESTS */
