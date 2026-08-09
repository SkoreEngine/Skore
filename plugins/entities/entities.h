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
 * component's compile-time type id maps to its layout (size/align). Every
 * chunk stores the implicit entity component (SK_ECS_ENTITY_COMPONENT_ID) in
 * column 0; user components are registered under their own type ids.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_entities_api_t in the app registry. */
#define SK_ENTITIES_API_TYPE_ID SK_TYPE_ID("sk.entities_api", 0x83bc3af6038a15d9ULL, 0x66d789c0523d7ddbULL)

/** Size of one ECS storage chunk in bytes. */
#define SK_ECS_CHUNK_SIZE 16384u

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
 * Global ECS module API (one table per process after plugin load).
 * Component identity is sk_type_id_t; registration maps a type id to its
 * layout. Later phases add world / archetype / chunk / query / system /
 * entitycommands entry points here.
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
} sk_entities_api_t;

#ifdef __cplusplus
}
#endif
