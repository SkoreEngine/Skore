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
 * storage through the SK_ECS_* macros. World / system / entitycommands
 * surfaces are declared in entities.h and built out by follow-up work on top
 * of this storage layer.
 */

#include "entities.h"

#include "allocator.h"
#include "app.h"
#include "array.h"

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
	u32 component_count; /* incl. the implicit entity column */
	u32 chunk_capacity;	 /* max rows per chunk */
	u32 chunk_row_size;	 /* sum of column strides (dense bytes per entity) */
	u32 chunk_data_size; /* bytes of the dense column region */
	u32 data_start;		 /* byte offset from chunk base to the first column */
	u32 chunk_align;	 /* allocation alignment for chunks of this archetype */
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
	for (u32 column = 0u; column < archetype->component_count; ++column) {
		if (SK_TYPE_ID_EQ(archetype->components[column].type_id, type_id)) {
			return (i32)column;
		}
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
};

/**
 * Register the ECS API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_entities_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_entities_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_ENTITIES_API_TYPE_ID, &entities_api);
}

#ifdef SK_TESTS
#include "test.h"

/*
 * Unit coverage for the ECS storage layer: type-id wiring, the entity handle,
 * the sk_type_id-keyed component registry, archetype signature normalization,
 * 16 KiB packing bounds / capacity calculation / column layout, and chunk slot
 * allocation / swap-remove / entity-to-location mapping. World / query /
 * system / entitycommands coverage lands with those surfaces.
 */

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
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.idem", 0x2222222222222222ULL, 0x3333333333333333ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "idem"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "idem-again"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, NULL));
}

SK_TEST(entities_register_component_conflict) {
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

/*
 * Exhausts the component registry (SK_ECS_MAX_COMPONENT_TYPES entries).
 * Keep this test last in the file: it fills the module registry for the
 * remainder of the plugin test run.
 */
SK_TEST(entities_register_component_capacity) {
	u32 accepted = 0u;
	i32 last = 0;
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_COMPONENT_TYPES + 4u; ++i) {
		sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.capacity", (u64)(i + 1000u), 0xAAAAAAAAAAAAAAAAULL);
		last = entities_api.register_component(id, 4u, 4u, "capacity");
		if (last == 0) {
			accepted += 1u;
		}
	}

	/* Earlier tests may have registered a few components; the registry caps
	 * out at SK_ECS_MAX_COMPONENT_TYPES total entries. */
	TEST_ASSERT_TRUE(accepted >= (u32)SK_ECS_MAX_COMPONENT_TYPES - 8u);
	TEST_ASSERT_EQUAL_INT32(-2, last);

	sk_type_id_t full_id = SK_TYPE_ID("sk.test.ecs.capacity.full", 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
	TEST_ASSERT_EQUAL_INT32(-2, entities_api.register_component(full_id, 4u, 4u, "full"));
}

#endif /* SK_TESTS */
