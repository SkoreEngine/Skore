#pragma once

/**
 * @file entities.h
 * @brief ECS module: archetype + chunk storage, world/entity lifecycle,
 *        queries, systems, and deferred entity commands.
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
 */

#include "common.h"

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

/** Opaque system: a callback plus its read/write component sets. */
typedef struct sk_system_t sk_system_t;

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
} sk_entities_api_t;

#ifdef __cplusplus
}
#endif
