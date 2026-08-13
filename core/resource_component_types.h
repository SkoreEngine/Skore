#pragma once

/**
 * @file resource_component_types.h
 * @brief Built-in ECS component payload resource types (APX-300).
 *
 * Every built-in ECS component doubles as a repository payload type: the
 * component resource's registered repository type id IS the ECS component
 * type id (resource-to-ECS mapping contract §3), so an EntityResource
 * Components list entry resolves to its component identity without an extra
 * TypeID field.
 *
 * This module owns the repository half (payload type descriptors + type id
 * constants). The ECS half (component structs, on_load_asset loaders, ECS
 * registration) lives in plugins/entities/entities_builtins.{h,c} and shares
 * the constants below. The descriptors are deliberately free of any app-only
 * dependency (no filesystem / app API) so the sk-entities plugin — which
 * statically links sk-core only — can register them into a repository for
 * its own load tests without pulling in sk-app symbols.
 */

#include "common.h"
#include "repository.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Component type ids (also registered repository payload type ids)  */
/* ------------------------------------------------------------------ */

#define SK_TRANSFORM_COMPONENT_TYPE_ID_LO 0x5323e6a1e100f237ULL
#define SK_TRANSFORM_COMPONENT_TYPE_ID_HI 0xc189b0fbd9c3e645ULL
#define SK_TRANSFORM_COMPONENT_TYPE_ID SK_TYPE_ID("sk.transform_resource", SK_TRANSFORM_COMPONENT_TYPE_ID_LO, SK_TRANSFORM_COMPONENT_TYPE_ID_HI)

#define SK_CAMERA_COMPONENT_TYPE_ID_LO 0x781a3cec0388f676ULL
#define SK_CAMERA_COMPONENT_TYPE_ID_HI 0x61baf90b70507cd9ULL
#define SK_CAMERA_COMPONENT_TYPE_ID SK_TYPE_ID("sk.camera_resource", SK_CAMERA_COMPONENT_TYPE_ID_LO, SK_CAMERA_COMPONENT_TYPE_ID_HI)

#define SK_LIGHT_COMPONENT_TYPE_ID_LO 0x210e7394367d218aULL
#define SK_LIGHT_COMPONENT_TYPE_ID_HI 0xb684c7fc31903580ULL
#define SK_LIGHT_COMPONENT_TYPE_ID SK_TYPE_ID("sk.light_resource", SK_LIGHT_COMPONENT_TYPE_ID_LO, SK_LIGHT_COMPONENT_TYPE_ID_HI)

#define SK_MESH_RENDERER_COMPONENT_TYPE_ID_LO 0x74b137935b2283e4ULL
#define SK_MESH_RENDERER_COMPONENT_TYPE_ID_HI 0x5d263c30ca3ebe46ULL
#define SK_MESH_RENDERER_COMPONENT_TYPE_ID SK_TYPE_ID("sk.mesh_renderer_resource", SK_MESH_RENDERER_COMPONENT_TYPE_ID_LO, SK_MESH_RENDERER_COMPONENT_TYPE_ID_HI)

#define SK_STATIC_TAG_COMPONENT_TYPE_ID_LO 0xd68f78386e25a2a7ULL
#define SK_STATIC_TAG_COMPONENT_TYPE_ID_HI 0x3fa5bc6a9fe50ca6ULL
#define SK_STATIC_TAG_COMPONENT_TYPE_ID SK_TYPE_ID("sk.static_tag_resource", SK_STATIC_TAG_COMPONENT_TYPE_ID_LO, SK_STATIC_TAG_COMPONENT_TYPE_ID_HI)

/* ------------------------------------------------------------------ */
/*  Field indices                                                     */
/* ------------------------------------------------------------------ */

/* Field 0 is NOT a Name string: component resources are authored inside an
 * EntityResource's Components list, so the payload starts directly with the
 * authored component values (the mapping contract only keeps Name at index 0
 * for the entity_resource / scene_resource envelopes). */

enum sk_transform_resource_field_t {
	SK_TRANSFORM_FIELD_POSITION = 0,
	SK_TRANSFORM_FIELD_ROTATION = 1,
	SK_TRANSFORM_FIELD_SCALE = 2,
};

enum sk_camera_resource_field_t {
	SK_CAMERA_FIELD_PROJECTION = 0, /* sk_camera_projection_t (ENUM) */
	SK_CAMERA_FIELD_FOV_Y = 1,		/* vertical field of view, radians (FLOAT) */
	SK_CAMERA_FIELD_NEAR = 2,		/* near clip distance (FLOAT) */
	SK_CAMERA_FIELD_FAR = 3,		/* far clip distance (FLOAT) */
};

enum sk_light_resource_field_t {
	SK_LIGHT_FIELD_TYPE = 0,	  /* sk_light_type_t (ENUM) */
	SK_LIGHT_FIELD_COLOR = 1,	  /* linear-space RGBA (COLOR) */
	SK_LIGHT_FIELD_INTENSITY = 2, /* luminous intensity (FLOAT) */
	SK_LIGHT_FIELD_RANGE = 3,	  /* attenuation distance for point/spot (FLOAT) */
};

enum sk_mesh_renderer_resource_field_t {
	SK_MESH_RENDERER_FIELD_MESH = 0,	 /* REFERENCE -> sk.mesh_resource */
	SK_MESH_RENDERER_FIELD_MATERIAL = 1, /* REFERENCE -> sk.material_graph_resource */
};

/* StaticTagResource carries no authored fields: the ECS component is a
 * runtime-only marker, so its on_load_asset hook stays NULL (see
 * plugins/entities/entities_builtins.h). The repository type exists so the
 * component can still be listed in an EntityResource Components list and
 * round-trip through the package serializer. */

/**
 * Register every built-in component payload type (transform, camera, light,
 * mesh renderer, static tag) into @p repository. Called automatically by
 * sk_resource_asset_builtins_register_types for engine repositories; the
 * sk-entities plugin calls it directly in its own tests.
 * @return 0 on success, first register_type error otherwise.
 */
i32 sk_resource_component_types_register(sk_repository_t* repository);

/**
 * Resolve an EntityResource component sub-object to its component type id
 * (resource-to-ECS mapping contract §3): the component resource's registered
 * repository type id IS the ECS component type id, so no extra TypeID field
 * is stored on the component sub-object. Identity is recovered from the live
 * resource's registered type, so prototype-scoped lookups and loaded graphs
 * resolve the same way.
 *
 * @param repository    Repository owning @p component_rid (must not be NULL).
 * @param component_rid RID of a component sub-object from an EntityResource's
 *                      Components list.
 * @return The component's type id, or SK_TYPE_ID_ZERO when @p component_rid
 *         is not a live resource (or has no registered type).
 */
sk_type_id_t sk_resource_entity_component_type_id(const sk_repository_t* repository, sk_rid_t component_rid);

#ifdef __cplusplus
}
#endif
