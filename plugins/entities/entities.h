#pragma once

/**
 * @file entities.h
 * @brief ECS module: archetype + chunk storage, world/entity lifecycle,
 *        queries, systems with a dependency-graph scheduler, and deferred
 *        entity commands.
 *
 * Implemented by the sk-entities plugin (SHARED, statically linked sk-core).
 * The plugin registers a static sk_entities_api_t on the app context; hosts
 * obtain it **only** via the app registry (no free-function mirrors):
 *
 *   const sk_entities_api_t* ecs =
 *       (const sk_entities_api_t*)app_api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
 *
 * Component types are identified by sk_type_id_t (see common.h): each
 * component's compile-time type id maps to its layout (size/align).
 *
 * # Storage model
 *
 * Every archetype is a signature: the sorted set of component type ids of the
 * entities it holds, plus the implicit entity component
 * (SK_ECS_ENTITY_COMPONENT_ID) which always lives in column 0. Entities are
 * stored densely in fixed 16 KiB chunks (SK_ECS_CHUNK_SIZE). Within a chunk
 * each component column is a contiguous, alignment-padded array of `capacity`
 * values (a column stride of align_up(size, align) bytes). The entity column
 * (column 0) maps each dense row to its stable sk_entity_t handle; an
 * entity's location is an sk_entity_location_t { chunk index, row }.
 *
 * Slots are allocated append-only (next free row) and freed by swap-remove
 * (the last row is moved into the freed row and reported so its location can
 * be updated), keeping every column dense.
 *
 * # World / entity lifecycle
 *
 * A sk_world_t owns an archetype table (archetypes are created lazily from
 * component signatures, retained even when empty) and a dense entity index:
 * each live entity has a slot whose index/generation form its stable
 * sk_entity_t handle and whose fields cache the entity's current
 * { archetype, chunk, row } location. Destroyed slots are recycled through a
 * free list with a bumped generation, so stale handles fail the generation
 * check. The immediate structural APIs (world_spawn / world_despawn /
 * world_add_component / world_remove_component) mutate storage right away and
 * are intentionally separate from the deferred sk_entitycommands_t surface.
 *
 * # Deferred entity commands
 *
 * A sk_entitycommands_t records structural changes (spawn / despawn /
 * add / remove component) without touching a world; the commands apply in
 * FIFO order onto a sk_world_t when the buffer is flushed with
 * commands_apply. Recording never mutates world storage, so commands can be
 * queued freely while a query is being iterated and applied cleanly
 * afterwards. commands_spawn returns a placeholder handle that resolves to
 * the real entity at apply time; other commands may reference that
 * placeholder (or any live real handle). Every apply/clear resets the buffer
 * for reuse.
 */

#include "common.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_entities_api_t in the app registry. */
#define SK_ENTITIES_API_TYPE_ID SK_TYPE_ID("sk.entities_api", 0x83bc3af6038a15d9ULL, 0x66d789c0523d7ddbULL)

/** Size of one ECS storage chunk in bytes. */
#define SK_ECS_CHUNK_SIZE 16384u

/** Byte alignment of a chunk's base block (cache-line aligned; a multiple of
 *  any power-of-two component alignment up to this value). */
#define SK_ECS_CHUNK_ALIGNMENT 64u

/** Maximum component columns in an archetype signature, including the implicit
 *  entity column (column 0). An archetype accepts at most this minus one user
 *  components. */
#define SK_ECS_MAX_ARCHETYPE_COLUMNS 64u

/** Type id of the implicit entity component (column 0 of every chunk). */
#define SK_ECS_ENTITY_COMPONENT_ID SK_TYPE_ID("sk.ecs.entity", 0x72469fa65f436eeaULL, 0x8ebdf6025b899b6bULL)

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */

/**
 * Stable entity handle: dense slot index + generation counter.
 * index == 0 is reserved and means "invalid"; destroyed slots are recycled
 * with a bumped generation so stale handles fail the generation check.
 */
typedef struct sk_entity_t {
	u32 index;
	u32 generation;
} sk_entity_t;

/** Invalid entity sentinel. */
#define SK_ENTITY_INVALID ((sk_entity_t){0u, 0u})

/** @return Non-zero if @p entity is a valid (non-zero index) handle. */
SK_FINLINE i32 sk_entity_is_valid(sk_entity_t entity) {
	return entity.index != 0u;
}

/** @return Non-zero if both handles reference the same slot + generation. */
SK_FINLINE i32 sk_entity_eq(sk_entity_t a, sk_entity_t b) {
	return (a.index == b.index) && (a.generation == b.generation);
}

/** Opaque world: owns archetypes, chunks, entity slots, and the component registry. */
typedef struct sk_world_t sk_world_t;

/** Opaque archetype: a fixed component layout; owns its chunks. */
typedef struct sk_archetype_t sk_archetype_t;

/** Opaque chunk: a fixed-size block of entities sharing one archetype. */
typedef struct sk_chunk_t sk_chunk_t;

/** Opaque query over component sets (matched against archetypes). */
typedef struct sk_query_t sk_query_t;

/** Opaque system: a callback plus its declared read/write component sets. */
typedef struct sk_system_t sk_system_t;

/** Opaque scheduler: systems + a deterministic dependency-graph run order. */
typedef struct sk_scheduler_t sk_scheduler_t;

/** Opaque deferred command buffer (structural changes applied on flush). */
typedef struct sk_entitycommands_t sk_entitycommands_t;

/**
 * Component layout registered under a sk_type_id_t.
 * @field type_id Compile-time component identity (see SK_ECS_ENTITY_COMPONENT_ID).
 * @field size    Byte size of one component value.
 * @field align   Byte alignment (must be > 0; power of two for real layouts).
 * @field name    Optional human-readable name (may be NULL).
 */
typedef struct sk_component_info_t {
	sk_type_id_t type_id;
	u32 size;
	u32 align;
	const_chr_t name;
} sk_component_info_t;

/**
 * Location of an entity inside an archetype's chunk storage.
 * @field chunk Index of the owning chunk within the archetype's chunk list.
 * @field row   Dense row within that chunk (0-based, less than the chunk's
 *              live count; the entity handle lives in column 0 at this row).
 */
typedef struct sk_entity_location_t {
	u32 chunk;
	u32 row;
} sk_entity_location_t;

/** Invalid / unmapped entity location sentinel. */
#define SK_ECS_LOCATION_NONE ((sk_entity_location_t){0xFFFFFFFFu, 0xFFFFFFFFu})

/* ------------------------------------------------------------------ */
/*  Systems and the dependency-graph scheduler                         */
/* ------------------------------------------------------------------ */

/** Maximum component ids one system may declare in its read set. */
#define SK_ECS_MAX_SYSTEM_COMPONENTS 32u

/** Maximum systems a scheduler may hold. */
#define SK_ECS_MAX_SYSTEMS 128u

/**
 * System descriptor: a callback plus the component sets it declares.
 *
 * The scheduler orders systems by these sets: a system that writes a
 * component must run before any system that also writes it (write-write) or
 * reads it (write-read); a system that only reads a component imposes no
 * order on other readers. Systems with disjoint sets stay unordered relative
 * to each other.
 *
 * @field callback   Per-frame entry point. Must not be NULL.
 * @field reads      Component ids the system reads (may be NULL with
 *                   read_count == 0).
 * @field read_count Number of read ids (<= SK_ECS_MAX_SYSTEM_COMPONENTS).
 * @field writes     Component ids the system writes (may be NULL with
 *                   write_count == 0).
 * @field write_count Number of write ids (<= SK_ECS_MAX_SYSTEM_COMPONENTS).
 * @field name       Optional human-readable name (may be NULL).
 * @field user_data  Opaque value passed back to @p callback (may be NULL).
 */
typedef struct sk_system_desc_t {
	void (*callback)(sk_world_t* world, f32 delta_time, void_ptr_t user_data);
	const sk_type_id_t* reads;
	u32 read_count;
	const sk_type_id_t* writes;
	u32 write_count;
	const_chr_t name;
	void_ptr_t user_data;
} sk_system_desc_t;

/* ------------------------------------------------------------------ */
/*  Query                                                             */
/* ------------------------------------------------------------------ */

/** Maximum query terms (including the implicit entity term 0) and the maximum
 *  number of excluded ids. */
#define SK_ECS_MAX_QUERY_TERMS 32u

/**
 * Query descriptor: match archetypes by required / optional / excluded
 * component sets identified with sk_type_id_t.
 *
 * @field required       Components a matched archetype must store (may be NULL
 *                       when required_count == 0).
 * @field required_count Number of required ids; 1 + required_count +
 *                       optional_count must not exceed SK_ECS_MAX_QUERY_TERMS.
 * @field optional       Components iterated only when the archetype stores
 *                       them (may be NULL when optional_count == 0).
 * @field optional_count Number of optional ids.
 * @field excluded       Components a matched archetype must not store (may be
 *                       NULL when excluded_count == 0).
 * @field excluded_count Number of excluded ids (<= SK_ECS_MAX_QUERY_TERMS).
 */
typedef struct sk_query_desc_t {
	const sk_type_id_t* required;
	u32 required_count;
	const sk_type_id_t* optional;
	u32 optional_count;
	const sk_type_id_t* excluded;
	u32 excluded_count;
} sk_query_desc_t;

/** Empty descriptor: matches every archetype. */
#define SK_QUERY_DESC_NONE ((sk_query_desc_t){NULL, 0u, NULL, 0u, NULL, 0u})

/**
 * Query iteration state (public layout so the SK_ECS_ITER_* macros can read
 * columns directly).
 *
 * Iteration walks each matched archetype's chunks in order and, within each
 * non-empty chunk, the live rows [0, count). query_iter_next precomputes a
 * per-term column base and stride for the current chunk:
 *
 *   - term 0 is always the entity column (column 0 of every chunk),
 *   - terms 1..required_count are the required ids in descriptor order,
 *   - the remaining terms are the optional ids in descriptor order.
 *
 * An optional term whose id is absent from the current archetype resolves to a
 * NULL field base with stride 0; the SK_ECS_ITER_* macros then yield NULL.
 *
 * @field query           Owning query (NULL for a zeroed / exhausted iterator).
 * @field archetype_index Current matched archetype.
 * @field chunk_index     Current chunk within that archetype.
 * @field row             Current row (0 <= row < count).
 * @field count           Live rows in the current chunk.
 * @field term_count      Term count (1 + required_count + optional_count).
 * @field fields          Per-term column base for the current chunk.
 * @field strides         Per-term dense stride (bytes) for the current chunk.
 */
typedef struct sk_query_iter_t {
	const sk_query_t* query;
	u32 archetype_index;
	u32 chunk_index;
	u32 row;
	u32 count;
	u32 term_count;
	void_ptr_t fields[SK_ECS_MAX_QUERY_TERMS];
	u32 strides[SK_ECS_MAX_QUERY_TERMS];
} sk_query_iter_t;

/** Zeroed iterator positioned before the first match. */
SK_FINLINE sk_query_iter_t sk_query_iter_make(const sk_query_t* query) {
	sk_query_iter_t it = {0};
	it.query = query;
	return it;
}

/* ------------------------------------------------------------------ */
/*  Query iteration macros (flecs-style)                              */
/* ------------------------------------------------------------------ */

/**
 * Iterate each matched chunk of @p query that has live rows. The body runs
 * once per chunk with @p var exposing count / fields / strides; iterate rows
 * with SK_ECS_ROW_FOREACH or access them directly with SK_ECS_ITER_COL.
 * @param ecs   sk_entities_api_t table (must not be NULL).
 * @param query Query to iterate (must not be NULL).
 * @param var   Iterator variable name.
 */
#define SK_ECS_QUERY_FOREACH(ecs, query, var) for (sk_query_iter_t var = sk_query_iter_make(query); (ecs)->query_iter_next(&(var));)

/**
 * Iterate each live row of the current chunk (used inside the
 * SK_ECS_QUERY_FOREACH body). Sets @p var.row to each live row in turn.
 */
#define SK_ECS_ROW_FOREACH(var) for ((var).row = 0u; (var).row < (var).count; ++(var).row)

/**
 * Iterate every entity matched by @p query. The body runs once per entity
 * with @p var.row set; read columns with SK_ECS_ITER_AT /
 * SK_ECS_ITER_ENTITY_ROW.
 */
#define SK_ECS_QUERY_EACH(ecs, query, var) \
	SK_ECS_QUERY_FOREACH(ecs, query, var)  \
	SK_ECS_ROW_FOREACH(var)

/** Live rows in the current chunk. */
#define SK_ECS_ITER_COUNT(var) ((var).count)

/** Entity column base (row 0) of the current chunk. */
#define SK_ECS_ITER_ENTITIES(var) ((sk_entity_t*)(var).fields[0])

/** Entity handle stored at @p row of the current chunk. */
#define SK_ECS_ITER_ENTITY(var, row) (*((const sk_entity_t*)((const u8*)(var).fields[0] + (size_t)(row) * (var).strides[0])))

/** Entity handle of the current row (inside SK_ECS_ROW_FOREACH). */
#define SK_ECS_ITER_ENTITY_ROW(var) (*((const sk_entity_t*)((const u8*)(var).fields[0] + (size_t)(var).row * (var).strides[0])))

/**
 * Typed pointer to @p term at the current row, or NULL when the term is an
 * optional component absent from the current archetype.
 * @param var  Iterator variable.
 * @param term Term index (0 = entity; < term_count).
 * @param Type Component C type (e.g. sk_pos_t).
 */
#define SK_ECS_ITER_AT(var, term, Type) ((Type*)(((var).fields[(term)] == NULL) ? NULL : ((u8*)(var).fields[(term)] + (size_t)(var).row * (var).strides[(term)])))

/**
 * Typed, stride-aware pointer to @p term at row @p row of the current chunk,
 * or NULL when the term is an optional component absent from the archetype.
 * @param var  Iterator variable.
 * @param term Term index (0 = entity; < term_count).
 * @param Type Component C type.
 * @param row  Row index (< SK_ECS_ITER_COUNT(var)).
 */
#define SK_ECS_ITER_COL(var, term, Type, row) ((Type*)(((var).fields[(term)] == NULL) ? NULL : ((u8*)(var).fields[(term)] + (size_t)(row) * (var).strides[(term)])))

/**
 * Global ECS module API (one table per process after plugin load).
 * Component identity is sk_type_id_t; registration maps a type id to its
 * layout. Archetypes are sorted component signatures owning 16 KiB chunks of
 * dense columns; chunk slots are allocated/freed by swap-remove.
 */
typedef struct sk_entities_api_t {
	/**
	 * Register a component type (idempotent when the layout matches).
	 * @param type_id Component identity (must not be SK_TYPE_ID_ZERO).
	 * @param size    Component byte size (must be > 0).
	 * @param align   Component byte alignment (must be > 0).
	 * @param name    Optional component name (may be NULL).
	 * @return 0 on success, -1 on conflicting re-registration, -2 when the
	 *         registry is full, -3 on invalid arguments.
	 */
	i32 (*register_component)(sk_type_id_t type_id, u32 size, u32 align, const_chr_t name);

	/**
	 * Look up a registered component's layout.
	 * @param type_id Component identity.
	 * @param out     Receives the component info on success (may be NULL).
	 * @return 0 on success, non-zero if @p type_id is not registered.
	 */
	i32 (*component_info)(sk_type_id_t type_id, sk_component_info_t* out);

	/**
	 * Create an archetype from a component signature.
	 *
	 * The signature is the set of user component ids the archetype stores.
	 * It is normalized to a sorted set (ascending by sk_type_id_t) and the
	 * implicit entity component is inserted at column 0. The 16 KiB chunk
	 * layout (column offsets, strides, capacity) is computed up front.
	 *
	 * @param components      User components of the signature (may be NULL
	 *                        with component_count == 0 for an entity-only
	 *                        archetype). Each size/align must be > 0, align
	 *                        must be a power of two, and the entity component
	 *                        id must not appear.
	 * @param component_count Number of user components (< SK_ECS_MAX_ARCHETYPE_COLUMNS).
	 * @return New archetype (owns zero chunks), or NULL on invalid arguments,
	 *         duplicate ids, a layout that cannot fit in 16 KiB, or OOM.
	 */
	sk_archetype_t* (*archetype_create)(const sk_component_info_t* components, u32 component_count);

	/**
	 * Destroy an archetype and every chunk it owns.
	 * @param archetype Archetype to destroy (must not be NULL).
	 */
	void (*archetype_destroy)(sk_archetype_t* archetype);

	/**
	 * Number of columns in the archetype signature, including the entity column.
	 * @param archetype Archetype (must not be NULL).
	 * @return Column count (>= 1).
	 */
	u32 (*archetype_component_count)(const sk_archetype_t* archetype);

	/**
	 * Component type id stored at @p column.
	 * @param archetype Archetype (must not be NULL).
	 * @param column    Column index (< archetype_component_count).
	 * @return Type id of that column (column 0 is SK_ECS_ENTITY_COMPONENT_ID).
	 */
	sk_type_id_t (*archetype_component_id)(const sk_archetype_t* archetype, u32 column);

	/**
	 * Registered byte size of the component at @p column.
	 * @param archetype Archetype (must not be NULL).
	 * @param column    Column index (< archetype_component_count).
	 * @return Component size in bytes.
	 */
	u32 (*archetype_component_size)(const sk_archetype_t* archetype, u32 column);

	/**
	 * Registered byte alignment of the component at @p column.
	 * @param archetype Archetype (must not be NULL).
	 * @param column    Column index (< archetype_component_count).
	 * @return Component alignment in bytes.
	 */
	u32 (*archetype_component_align)(const sk_archetype_t* archetype, u32 column);

	/**
	 * Map a component type id to its column index.
	 * @param archetype Archetype (must not be NULL).
	 * @param type_id   Component identity.
	 * @return Column index, or -1 if the type id is not in the signature.
	 */
	i32 (*archetype_column)(const sk_archetype_t* archetype, sk_type_id_t type_id);

	/**
	 * Whether @p type_id is part of the archetype signature.
	 * @param archetype Archetype (must not be NULL).
	 * @param type_id   Component identity.
	 * @return Non-zero if present.
	 */
	i32 (*archetype_has)(const sk_archetype_t* archetype, sk_type_id_t type_id);

	/**
	 * Byte offset of a column base from the start of any chunk of this archetype.
	 * @param archetype Archetype (must not be NULL).
	 * @param column    Column index (< archetype_component_count).
	 * @return Offset in bytes; the column base is always aligned to the
	 *         component's alignment.
	 */
	u32 (*archetype_column_offset)(const sk_archetype_t* archetype, u32 column);

	/**
	 * Dense stride (padded element size) of a column.
	 * @param archetype Archetype (must not be NULL).
	 * @param column    Column index (< archetype_component_count).
	 * @return align_up(size, align) bytes between consecutive rows.
	 */
	u32 (*archetype_column_stride)(const sk_archetype_t* archetype, u32 column);

	/**
	 * Maximum number of entities one chunk of this archetype can hold.
	 * @param archetype Archetype (must not be NULL).
	 * @return Row capacity (>= 1).
	 */
	u32 (*archetype_chunk_capacity)(const sk_archetype_t* archetype);

	/**
	 * Dense bytes per entity row (sum of column strides).
	 * @param archetype Archetype (must not be NULL).
	 * @return Bytes of packed data for one entity.
	 */
	u32 (*archetype_chunk_row_size)(const sk_archetype_t* archetype);

	/**
	 * Bytes of the dense column region inside a chunk.
	 * @param archetype Archetype (must not be NULL).
	 * @return chunk_capacity * row_size plus inter-column alignment padding.
	 */
	u32 (*archetype_chunk_data_size)(const sk_archetype_t* archetype);

	/**
	 * Create a new empty chunk for @p archetype and append it to the
	 * archetype's chunk list.
	 * @param archetype Archetype (must not be NULL).
	 * @return 0 on success, non-zero on allocation failure.
	 */
	i32 (*archetype_add_chunk)(sk_archetype_t* archetype);

	/**
	 * Number of chunks currently owned by @p archetype.
	 * @param archetype Archetype (must not be NULL).
	 * @return Chunk count.
	 */
	u32 (*archetype_chunk_count)(const sk_archetype_t* archetype);

	/**
	 * Chunk at @p index in the archetype's chunk list.
	 * @param archetype Archetype (must not be NULL).
	 * @param index     Chunk index.
	 * @return The chunk, or NULL when @p index is out of range.
	 */
	sk_chunk_t* (*archetype_chunk)(const sk_archetype_t* archetype, u32 index);

	/**
	 * Locate an entity inside this archetype's chunks.
	 * @param archetype Archetype (must not be NULL).
	 * @param entity    Entity handle to find.
	 * @param out       Receives the entity location (may be NULL).
	 * @return 0 when found, -1 when the entity is not in any of this
	 *         archetype's chunks.
	 */
	i32 (*archetype_location)(sk_archetype_t* archetype, sk_entity_t entity, sk_entity_location_t* out);

	/**
	 * Create a standalone empty chunk for @p archetype (not attached to any
	 * chunk list; free with chunk_destroy).
	 * @param archetype Archetype (must not be NULL).
	 * @return New chunk, or NULL on allocation failure.
	 */
	sk_chunk_t* (*chunk_create)(const sk_archetype_t* archetype);

	/**
	 * Destroy a chunk created with chunk_create.
	 * @param chunk Chunk to destroy (must not be NULL).
	 */
	void (*chunk_destroy)(sk_chunk_t* chunk);

	/**
	 * Archetype a chunk stores data for.
	 * @param chunk Chunk (must not be NULL).
	 * @return The archetype.
	 */
	const sk_archetype_t* (*chunk_archetype)(const sk_chunk_t* chunk);

	/**
	 * Live (occupied) row count of a chunk.
	 * @param chunk Chunk (must not be NULL).
	 * @return Number of allocated slots.
	 */
	u32 (*chunk_count)(const sk_chunk_t* chunk);

	/**
	 * Row capacity of a chunk (equals the archetype's chunk capacity).
	 * @param chunk Chunk (must not be NULL).
	 * @return Maximum rows before the chunk is full.
	 */
	u32 (*chunk_capacity)(const sk_chunk_t* chunk);

	/**
	 * Whether the chunk has no free slots left.
	 * @param chunk Chunk (must not be NULL).
	 * @return Non-zero if count == capacity.
	 */
	i32 (*chunk_full)(const sk_chunk_t* chunk);

	/**
	 * Allocate the next free slot and store @p entity in column 0.
	 * @param chunk   Chunk (must not be NULL).
	 * @param entity  Entity handle stored at the new row.
	 * @param out_row Receives the allocated row (may be NULL).
	 * @return 0 on success, -1 when the chunk is full.
	 */
	i32 (*chunk_allocate)(sk_chunk_t* chunk, sk_entity_t entity, u32* out_row);

	/**
	 * Free the slot at @p row by swap-removing it (keeps columns dense).
	 * @param chunk     Chunk (must not be NULL).
	 * @param row       Row to free (must be < chunk_count).
	 * @param out_moved When the freed row is not the last row, the entity that
	 *                  was moved into @p row is reported here so its location
	 *                  can be updated; otherwise SK_ENTITY_INVALID (may be NULL).
	 */
	void (*chunk_remove)(sk_chunk_t* chunk, u32 row, sk_entity_t* out_moved);

	/**
	 * Base pointer of a component column.
	 * @param chunk  Chunk (must not be NULL).
	 * @param column Column index (< archetype_component_count).
	 * @return Column base, or NULL when @p column is out of range.
	 */
	const_ptr_t (*chunk_column)(const sk_chunk_t* chunk, u32 column);

	/** Mutable variant of chunk_column. */
	void_ptr_t (*chunk_column_mut)(sk_chunk_t* chunk, u32 column);

	/**
	 * Pointer to the component value of a live row.
	 * @param chunk  Chunk (must not be NULL).
	 * @param column Column index (< archetype_component_count).
	 * @param row    Live row (< chunk_count).
	 * @return Pointer to the value, or NULL when the row or column is out of
	 *         range.
	 */
	const_ptr_t (*chunk_get)(const sk_chunk_t* chunk, u32 column, u32 row);

	/** Mutable variant of chunk_get. */
	void_ptr_t (*chunk_get_mut)(sk_chunk_t* chunk, u32 column, u32 row);

	/**
	 * Entity handle stored at @p row (column 0).
	 * @param chunk Chunk (must not be NULL).
	 * @param row   Live row (< chunk_count).
	 * @return The entity handle.
	 */
	sk_entity_t (*chunk_entity)(const sk_chunk_t* chunk, u32 row);

	/**
	 * Find the row of @p entity within this chunk.
	 * @param chunk   Chunk (must not be NULL).
	 * @param entity  Entity handle to search for.
	 * @param out_row Receives the row (may be NULL).
	 * @return 0 when found, -1 when not present.
	 */
	i32 (*chunk_find)(const sk_chunk_t* chunk, sk_entity_t entity, u32* out_row);

	/* ---- query ---- */

	/**
	 * Create a query matching archetypes by required / optional / excluded
	 * component sets.
	 *
	 * A query matches an archetype when its signature contains every required
	 * id and no excluded id; optional ids are consulted only when iterating
	 * (an absent optional term iterates as a NULL field). Terms are ordered:
	 * term 0 is the implicit entity component, terms 1..required_count are the
	 * required ids in descriptor order, and the remaining terms are the
	 * optional ids in descriptor order.
	 *
	 * @param desc Descriptor (must not be NULL). Ids must be non-zero;
	 *             1 + required_count + optional_count must not exceed
	 *             SK_ECS_MAX_QUERY_TERMS; excluded_count must not exceed
	 *             SK_ECS_MAX_QUERY_TERMS; an id array must be non-NULL whenever
	 *             its count is non-zero.
	 * @return New query (with no matched archetypes yet), or NULL on invalid
	 *         arguments or OOM.
	 */
	sk_query_t* (*query_create)(const sk_query_desc_t* desc);

	/**
	 * Destroy a query and its matched archetype set.
	 * @param query Query (must not be NULL).
	 */
	void (*query_destroy)(sk_query_t* query);

	/**
	 * Number of terms in the query, including the implicit entity term 0.
	 * @param query Query (must not be NULL).
	 * @return 1 + required_count + optional_count.
	 */
	u32 (*query_term_count)(const sk_query_t* query);

	/**
	 * Type id of term @p term.
	 * @param query Query (must not be NULL).
	 * @param term  Term index (< query_term_count).
	 * @return Component identity of that term (term 0 is
	 *         SK_ECS_ENTITY_COMPONENT_ID).
	 */
	sk_type_id_t (*query_term_id)(const sk_query_t* query, u32 term);

	/**
	 * Whether @p term is always present (1) or optional (0).
	 * @param query Query (must not be NULL).
	 * @param term  Term index (< query_term_count).
	 * @return Non-zero for the entity term and every required term.
	 */
	i32 (*query_term_required)(const sk_query_t* query, u32 term);

	/**
	 * Whether @p archetype matches the query: it stores every required id and
	 * no excluded id. Optional ids are not consulted.
	 * @param query     Query (must not be NULL).
	 * @param archetype Archetype (must not be NULL).
	 * @return Non-zero when the archetype matches.
	 */
	i32 (*query_matches)(const sk_query_t* query, const sk_archetype_t* archetype);

	/**
	 * Observe @p archetype: append it to the query's matched set when it
	 * matches (idempotent; an already-observed archetype is not duplicated).
	 * @param query     Query (must not be NULL).
	 * @param archetype Archetype (must not be NULL).
	 * @return 0 when matched (added or already known), 1 when it does not
	 *         match, -1 on allocation failure.
	 */
	i32 (*query_observe)(sk_query_t* query, sk_archetype_t* archetype);

	/**
	 * Number of archetypes currently matched by the query.
	 * @param query Query (must not be NULL).
	 * @return Matched archetype count.
	 */
	u32 (*query_archetype_count)(const sk_query_t* query);

	/**
	 * Matched archetype at @p index.
	 * @param query Query (must not be NULL).
	 * @param index Archetype index.
	 * @return The archetype, or NULL when @p index is out of range.
	 */
	const sk_archetype_t* (*query_archetype)(const sk_query_t* query, u32 index);

	/**
	 * Advance a query iterator to the next matched chunk with live rows.
	 *
	 * A zeroed iterator (sk_query_iter_make) starts before the first match;
	 * each non-zero return exposes a chunk whose live rows are [0, count) and
	 * whose term fields / strides are precomputed for that chunk. Unmatched
	 * archetypes and empty chunks are skipped. Returns 0 once every matched
	 * archetype is exhausted (later calls keep returning 0).
	 *
	 * @param it Iterator (must not be NULL; must belong to a live query).
	 * @return Non-zero while a chunk is available, 0 at the end.
	 */
	i32 (*query_iter_next)(sk_query_iter_t* it);

	/**
	 * Entity handle stored at @p row of the iterator's current chunk.
	 * @param it  Iterator (must not be NULL; must expose a loaded chunk).
	 * @param row Live row (< SK_ECS_ITER_COUNT(it)).
	 * @return The entity handle.
	 */
	sk_entity_t (*query_iter_entity)(const sk_query_iter_t* it, u32 row);

	/**
	 * Pointer to @p term's value at @p row of the iterator's current chunk.
	 * @param it   Iterator (must not be NULL; must expose a loaded chunk).
	 * @param term Term index (< term_count).
	 * @param row  Live row (< count).
	 * @return Stride-aware value pointer, or NULL when an optional term is
	 *         absent from the current archetype.
	 */
	void_ptr_t (*query_iter_field)(const sk_query_iter_t* it, u32 term, u32 row);

	/* ---- world (immediate structural ops) ---- */

	/**
	 * Create a world: an archetype table (created lazily from component
	 * signatures), a dense entity index with generation-tagged handles, and
	 * the set of world-managed queries (which observe every archetype the
	 * world creates).
	 * @return New world, or NULL on allocation failure.
	 */
	sk_world_t* (*world_create)(void);

	/**
	 * Destroy a world and everything it owns: its archetypes/chunks, its
	 * world-managed queries, and its entity index. Query objects created via
	 * world_query_create are owned by the world and must not be freed
	 * separately.
	 * @param world World to destroy (must not be NULL).
	 */
	void (*world_destroy)(sk_world_t* world);

	/**
	 * Spawn an entity holding the given component signature.
	 *
	 * The signature is the set of user component ids the new entity stores.
	 * Ids must be registered (see register_component); unregistered ids,
	 * SK_TYPE_ID_ZERO, the implicit entity component id, and signatures over
	 * SK_ECS_MAX_ARCHETYPE_COLUMNS make the spawn fail (SK_ENTITY_INVALID).
	 * The signature is normalized (sorted + deduplicated) and the owning
	 * archetype is created on demand. All components are zero-initialized.
	 *
	 * @param world           World (must not be NULL).
	 * @param component_ids   Component ids of the signature (may be NULL with
	 *                        component_count == 0 for an entity-only spawn).
	 * @param component_count Number of ids (< SK_ECS_MAX_ARCHETYPE_COLUMNS).
	 * @return Stable entity handle, or SK_ENTITY_INVALID on failure.
	 */
	sk_entity_t (*world_spawn)(sk_world_t* world, const sk_type_id_t* component_ids, u32 component_count);

	/**
	 * Destroy an entity immediately: its slot is recycled with a bumped
	 * generation (stale handles fail world_alive) and its row is
	 * swap-removed from its chunk.
	 * @param world  World (must not be NULL).
	 * @param entity Entity handle (must be alive).
	 * @return 0 on success, -1 when @p entity is invalid or dead.
	 */
	i32 (*world_despawn)(sk_world_t* world, sk_entity_t entity);

	/**
	 * Whether @p entity is live in @p world (valid index, matching
	 * generation, not despawned).
	 * @return Non-zero when alive.
	 */
	i32 (*world_alive)(sk_world_t* world, sk_entity_t entity);

	/**
	 * Number of live entities in @p world.
	 * @param world World (must not be NULL).
	 * @return Live entity count.
	 */
	u32 (*world_count)(const sk_world_t* world);

	/**
	 * Whether a live entity stores a component of @p type_id.
	 * @param world   World (must not be NULL).
	 * @param entity  Entity handle (must be alive).
	 * @param type_id Component identity.
	 * @return Non-zero when the component is present.
	 */
	i32 (*world_has_component)(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id);

	/**
	 * Add a component to an entity, moving it to the archetype for the new
	 * signature. Shared component values are preserved; the new component is
	 * zero-initialized. Adding a component the entity already has is an
	 * idempotent success.
	 * @param world   World (must not be NULL).
	 * @param entity  Entity handle (must be alive).
	 * @param type_id Component identity (must be registered).
	 * @return 0 on success, -1 when @p entity is dead or the signature cannot
	 *         be created (unregistered id, invalid id, OOM).
	 */
	i32 (*world_add_component)(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id);

	/**
	 * Remove a component from an entity, moving it to the archetype for the
	 * remaining signature. Removing a component the entity does not have is
	 * an idempotent success.
	 * @param world   World (must not be NULL).
	 * @param entity  Entity handle (must be alive).
	 * @param type_id Component identity.
	 * @return 0 on success, -1 when @p entity is dead or on OOM.
	 */
	i32 (*world_remove_component)(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id);

	/**
	 * Mutable pointer to a live entity's component value.
	 * @param world   World (must not be NULL).
	 * @param entity  Entity handle (must be alive).
	 * @param type_id Component identity.
	 * @return Pointer to the value, or NULL when @p entity is dead or lacks
	 *         the component.
	 */
	void_ptr_t (*world_component)(sk_world_t* world, sk_entity_t entity, sk_type_id_t type_id);

	/**
	 * Create a world-managed query. The query observes every archetype the
	 * world currently owns and every archetype the world creates later.
	 * The returned query is owned by the world and destroyed with it.
	 * @param world World (must not be NULL).
	 * @param desc  Query descriptor (see query_create; must not be NULL).
	 * @return New query, or NULL on invalid descriptor or OOM.
	 */
	sk_query_t* (*world_query_create)(sk_world_t* world, const sk_query_desc_t* desc);

	/* ---- systems ---- */

	/**
	 * Create a system from a descriptor. The read/write component sets are
	 * copied into the system; a type id may appear at most once per set (an id
	 * in both the read and write set of the same system is allowed and means
	 * the system reads what it modifies in place).
	 * @param desc Descriptor (must not be NULL; callback must not be NULL).
	 *             Ids must be non-zero; read_count / write_count must not
	 *             exceed SK_ECS_MAX_SYSTEM_COMPONENTS; an id array must be
	 *             non-NULL whenever its count is non-zero.
	 * @return New system, or NULL on invalid arguments or OOM.
	 */
	sk_system_t* (*system_create)(const sk_system_desc_t* desc);

	/**
	 * Destroy a system. A destroyed system must have been removed from (or
	 * never added to) every scheduler.
	 * @param system System to destroy (must not be NULL).
	 */
	void (*system_destroy)(sk_system_t* system);

	/**
	 * Number of read ids declared by a system.
	 * @param system System (must not be NULL).
	 * @return Read count.
	 */
	u32 (*system_read_count)(const sk_system_t* system);

	/**
	 * Number of write ids declared by a system.
	 * @param system System (must not be NULL).
	 * @return Write count.
	 */
	u32 (*system_write_count)(const sk_system_t* system);

	/**
	 * Read id declared by a system.
	 * @param system System (must not be NULL).
	 * @param index  Read index (< system_read_count).
	 * @return The component id.
	 */
	sk_type_id_t (*system_read_id)(const sk_system_t* system, u32 index);

	/**
	 * Write id declared by a system.
	 * @param system System (must not be NULL).
	 * @param index  Write index (< system_write_count).
	 * @return The component id.
	 */
	sk_type_id_t (*system_write_id)(const sk_system_t* system, u32 index);

	/**
	 * Name of a system.
	 * @param system System (must not be NULL).
	 * @return The descriptor name (may be NULL).
	 */
	const_chr_t (*system_name)(const sk_system_t* system);

	/**
	 * Invoke a system's callback once.
	 * @param system     System (must not be NULL).
	 * @param world      World handed to the callback (must not be NULL).
	 * @param delta_time Per-frame delta handed to the callback.
	 */
	void (*system_run)(sk_system_t* system, sk_world_t* world, f32 delta_time);

	/* ---- scheduler ---- */

	/**
	 * Create an empty scheduler.
	 * @return New scheduler, or NULL on allocation failure.
	 */
	sk_scheduler_t* (*scheduler_create)(void);

	/**
	 * Destroy a scheduler and its built order. Does not destroy the systems
	 * added to it (they stay owned by the caller).
	 * @param scheduler Scheduler to destroy (must not be NULL).
	 */
	void (*scheduler_destroy)(sk_scheduler_t* scheduler);

	/**
	 * Append a system to a scheduler. Adding a system invalidates any
	 * previously built run order; the next scheduler_build / scheduler_run
	 * recomputes it.
	 * @param scheduler Scheduler (must not be NULL).
	 * @param system    System to add (must not be NULL; must outlive the
	 *                  scheduler).
	 * @return The system's index (0-based registration order) on success,
	 *         -1 on NULL arguments, -2 when the scheduler is full
	 *         (SK_ECS_MAX_SYSTEMS), -3 on allocation failure.
	 */
	i32 (*scheduler_add)(sk_scheduler_t* scheduler, sk_system_t* system);

	/**
	 * Number of systems in a scheduler.
	 * @param scheduler Scheduler (must not be NULL).
	 * @return System count.
	 */
	u32 (*scheduler_system_count)(const sk_scheduler_t* scheduler);

	/**
	 * System at @p index of a scheduler (registration order).
	 * @param scheduler Scheduler (must not be NULL).
	 * @param index     System index.
	 * @return The system, or NULL when @p index is out of range.
	 */
	sk_system_t* (*scheduler_system)(const sk_scheduler_t* scheduler, u32 index);

	/**
	 * Build (or rebuild) the dependency graph and compute a deterministic
	 * topological run order.
	 *
	 * Edges are added only for real conflicts: for each component shared by
	 * two systems, a write-write or write-read overlap orders the earlier
	 * (lower index) writer before the later one / before the reader. Systems
	 * whose sets are disjoint share no edge. The order is deterministic:
	 * among systems whose dependencies are already satisfied, the lowest
	 * system index (registration order) runs first.
	 *
	 * On a cycle the graph cannot be fully ordered: this returns -1, sets
	 * has_cycle, and keeps the prefix ordered before the cycle (order_count
	 * is smaller than system_count). scheduler_run refuses to execute while a
	 * cycle is present.
	 * @param scheduler Scheduler (must not be NULL).
	 * @return 0 on success, -1 when the dependency graph contains a cycle,
	 *         -2 on allocation failure.
	 */
	i32 (*scheduler_build)(sk_scheduler_t* scheduler);

	/**
	 * Whether the last scheduler_build found a cycle.
	 * @param scheduler Scheduler (must not be NULL).
	 * @return Non-zero after a build that hit a cycle, 0 otherwise.
	 */
	i32 (*scheduler_has_cycle)(const sk_scheduler_t* scheduler);

	/**
	 * Number of systems in the built run order.
	 * @param scheduler Scheduler (must not be NULL).
	 * @return Systems ordered by the last build (system_count when it
	 *         succeeded, less when a cycle truncated the order, 0 before
	 *         any build).
	 */
	u32 (*scheduler_order_count)(const sk_scheduler_t* scheduler);

	/**
	 * System at run position @p position of the built order.
	 * @param scheduler Scheduler (must not be NULL).
	 * @param position  Run position (< scheduler_order_count).
	 * @return The system, or NULL when @p position is out of range.
	 */
	sk_system_t* (*scheduler_order_at)(const sk_scheduler_t* scheduler, u32 position);

	/**
	 * Run every system in the built deterministic order, once each. Rebuilds
	 * the order when systems were added since the last build.
	 * @param scheduler  Scheduler (must not be NULL).
	 * @param world      World handed to every system callback (must not be
	 *                   NULL).
	 * @param delta_time Delta handed to every system callback.
	 * @return 0 when every system ran, -1 when the dependency graph has a
	 *         cycle (no callback is invoked), -2 on allocation failure during
	 *         an implicit rebuild.
	 */
	i32 (*scheduler_run)(sk_scheduler_t* scheduler, sk_world_t* world, f32 delta_time);

	/* ---- deferred entity commands ---- */

	/**
	 * Create a deferred command buffer.
	 * @return New buffer (empty), or NULL on allocation failure.
	 */
	sk_entitycommands_t* (*commands_create)(void);

	/**
	 * Destroy a command buffer and every recorded command (copied values
	 * included). Does not touch any world.
	 * @param commands Buffer (must not be NULL).
	 */
	void (*commands_destroy)(sk_entitycommands_t* commands);

	/**
	 * Number of commands currently recorded in @p commands.
	 * @param commands Buffer (must not be NULL).
	 * @return Recorded command count (0 after apply/clear).
	 */
	u32 (*commands_count)(const sk_entitycommands_t* commands);

	/**
	 * Record a deferred spawn. No entity is created until the buffer is
	 * applied. Returns a placeholder handle that only this buffer
	 * understands: it may be passed to commands_despawn /
	 * commands_add_component / commands_remove_component and resolves to the
	 * real entity at apply time. Placeholders are invalid after apply/clear.
	 * @param commands        Buffer (must not be NULL).
	 * @param component_ids   Spawn signature (may be NULL with count 0).
	 * @param component_count Number of signature ids.
	 * @return Placeholder handle (valid), or SK_ENTITY_INVALID on invalid
	 *         arguments or OOM.
	 */
	sk_entity_t (*commands_spawn)(sk_entitycommands_t* commands, const sk_type_id_t* component_ids, u32 component_count);

	/**
	 * Record a deferred despawn of @p entity (a live handle or a placeholder
	 * from commands_spawn). Applied in command order at apply time.
	 * @return 0 on success, -1 on an invalid handle or OOM.
	 */
	i32 (*commands_despawn)(sk_entitycommands_t* commands, sk_entity_t entity);

	/**
	 * Record a deferred component add. When @p value is non-NULL a copy is
	 * taken immediately (the component must be registered so its size is
	 * known); when NULL the component is zero-initialized at apply time.
	 * @param commands Buffer (must not be NULL).
	 * @param entity   Entity handle or placeholder.
	 * @param type_id  Component identity.
	 * @param value    Initial component value to copy, or NULL for zero-init.
	 * @return 0 on success, -1 on invalid arguments or OOM.
	 */
	i32 (*commands_add_component)(sk_entitycommands_t* commands, sk_entity_t entity, sk_type_id_t type_id, const_ptr_t value);

	/**
	 * Record a deferred component removal.
	 * @param commands Buffer (must not be NULL).
	 * @param entity   Entity handle or placeholder.
	 * @param type_id  Component identity.
	 * @return 0 on success, -1 on invalid arguments or OOM.
	 */
	i32 (*commands_remove_component)(sk_entitycommands_t* commands, sk_entity_t entity, sk_type_id_t type_id);

	/**
	 * Apply every recorded command onto @p world in FIFO order, then reset
	 * the buffer (reusable immediately). Deferred spawns create real
	 * entities; commands referencing a placeholder resolve through the
	 * spawn order. Commands whose target entity is no longer alive (e.g.
	 * despawned earlier in the same buffer) are skipped. Recording never
	 * mutates world storage, so commands may be queued during query
	 * iteration and applied cleanly afterwards.
	 * @param commands Buffer (must not be NULL).
	 * @param world    Target world (must not be NULL).
	 * @return 0 when every command applied, non-zero if any command failed
	 *         (spawn on unregistered components, OOM, etc.).
	 */
	i32 (*commands_apply)(sk_entitycommands_t* commands, sk_world_t* world);

	/**
	 * Drop every recorded command (and copied values) without applying.
	 * Keeps the buffer's capacity so it can be reused.
	 * @param commands Buffer (must not be NULL).
	 */
	void (*commands_clear)(sk_entitycommands_t* commands);
} sk_entities_api_t;

#ifdef __cplusplus
}
#endif
