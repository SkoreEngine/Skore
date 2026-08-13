#pragma once

/**
 * @file jolt.h
 * @brief Physics module (Jolt integration) public surface — C headers only.
 *
 * Implemented by the sk-jolt plugin (SHARED, statically linked sk-core).
 * The plugin registers a static sk_jolt_api_t on the app context from
 * sk_plugin_entry_point; hosts obtain it **only** via the app registry:
 *
 *   const sk_jolt_api_t* jolt =
 *       (const sk_jolt_api_t*)app_api->get_api(ctx, SK_JOLT_API_TYPE_ID);
 *
 * This header is the *entire* public surface and is plain C: opaque handles,
 * POD structs, and enums/constants. No C++ types, templates, or name mangling
 * appear here — a C compiler must be able to consume it. The implementation
 * lives in .cpp translation units that link the vendored Jolt library
 * (thirdparty/jolt) behind this boundary; at this stage every table entry is
 * an empty stub and no Jolt API is invoked yet.
 *
 * Value conventions mirror the vendored Jolt v5.6.0 defaults so later
 * integration maps 1:1:
 *   - motion types match JPH::EMotionType (Static=0, Kinematic=1, Dynamic=2),
 *   - object layers are 16-bit (JPH_OBJECT_LAYER_BITS == 16 default) with
 *     cObjectLayerInvalid == 0xFFFF,
 *   - broad-phase layers are 8-bit (JPH::BroadPhaseLayer::Type),
 *   - collision group ids / masks are 32-bit (JPH::CollisionGroup::GroupID).
 *
 * # Layer / collision-filter model (scaffold)
 *
 * Two object layers (non-moving static geometry, moving bodies) map 1:1 onto
 * two broad-phase layers, the classic Jolt HelloWorld setup; characters use
 * their own collision group so the pair filter can later separate dynamic
 * bodies from character probes without changing the layer set.
 */

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type id for sk_jolt_api_t in the app registry.
 * C consumers get the usual SK_TYPE_ID macro; C++ cannot use the C99 compound
 * literal that macro expands to, so it receives an equivalent const value.
 * Both forms are the same 128-bit id and are passed by value. */
#ifdef __cplusplus
static const sk_type_id_t SK_JOLT_API_TYPE_ID = {0xbc3401b6de3b56a8ULL, 0x3eee2db1d0c19f18ULL};
#else
#define SK_JOLT_API_TYPE_ID SK_TYPE_ID("sk.jolt_api", 0xbc3401b6de3b56a8ULL, 0x3eee2db1d0c19f18ULL)
#endif

/* ------------------------------------------------------------------ */
/*  Motion type                                                       */
/* ------------------------------------------------------------------ */

/**
 * Body motion type, mirroring JPH::EMotionType.
 * @field SK_JOLT_MOTION_TYPE_STATIC     Non-movable body (0).
 * @field SK_JOLT_MOTION_TYPE_KINEMATIC  Movable by velocities only; ignores forces (1).
 * @field SK_JOLT_MOTION_TYPE_DYNAMIC    Responds to forces like a normal rigid body (2).
 */
typedef enum sk_jolt_motion_type_t {
	SK_JOLT_MOTION_TYPE_STATIC = 0,
	SK_JOLT_MOTION_TYPE_KINEMATIC = 1,
	SK_JOLT_MOTION_TYPE_DYNAMIC = 2,
} sk_jolt_motion_type_t;

/* ------------------------------------------------------------------ */
/*  Shape descriptions (POD)                                          */
/* ------------------------------------------------------------------ */

/** Shape kind tag; selects the active member of sk_jolt_shape_desc_t. */
typedef enum sk_jolt_shape_kind_t {
	SK_JOLT_SHAPE_BOX = 0,
	SK_JOLT_SHAPE_SPHERE = 1,
	SK_JOLT_SHAPE_CAPSULE = 2,
} sk_jolt_shape_kind_t;

/**
 * Box shape: half extents (JPH::BoxShapeSettings mHalfExtent).
 * @field half_extent Half size of the box in each local axis (x, y, z), >= 0.
 */
typedef struct sk_jolt_box_shape_desc_t {
	f32 half_extent[3];
} sk_jolt_box_shape_desc_t;

/**
 * Sphere shape: radius (JPH::SphereShapeSettings mRadius).
 * @field radius Sphere radius, > 0.
 */
typedef struct sk_jolt_sphere_shape_desc_t {
	f32 radius;
} sk_jolt_sphere_shape_desc_t;

/**
 * Capsule shape: radius + half height of the cylindrical section
 * (JPH::CapsuleShapeSettings mRadius / mHalfHeightOfCylinder; the spherical
 * caps are excluded from the half height).
 * @field half_height Half height of the cylinder part, >= 0 (0 forms a sphere).
 * @field radius      Capsule radius, > 0.
 */
typedef struct sk_jolt_capsule_shape_desc_t {
	f32 half_height;
	f32 radius;
} sk_jolt_capsule_shape_desc_t;

/**
 * Tagged shape description: one POD struct covering the three scaffold
 * primitives. @p kind selects the active member of the @p shape union.
 * Zero the whole struct (or only touch the selected member) before use.
 */
typedef struct sk_jolt_shape_desc_t {
	sk_jolt_shape_kind_t kind;
	union {
		sk_jolt_box_shape_desc_t box;
		sk_jolt_sphere_shape_desc_t sphere;
		sk_jolt_capsule_shape_desc_t capsule;
	} shape;
} sk_jolt_shape_desc_t;

/* ------------------------------------------------------------------ */
/*  Layers and collision-filter constants                             */
/* ------------------------------------------------------------------ */

/** Object layer (Jolt ObjectLayer, 16-bit) of static, non-moving geometry. */
#define SK_JOLT_OBJECT_LAYER_NON_MOVING 0u

/** Object layer (Jolt ObjectLayer, 16-bit) of dynamic/kinematic bodies. */
#define SK_JOLT_OBJECT_LAYER_MOVING 1u

/** Invalid object layer (Jolt cObjectLayerInvalid with 16-bit layers). */
#define SK_JOLT_OBJECT_LAYER_INVALID 0xFFFFu

/** Broad-phase layer (Jolt BroadPhaseLayer::Type, 8-bit) for static geometry. */
#define SK_JOLT_BROAD_PHASE_LAYER_NON_MOVING 0u

/** Broad-phase layer (Jolt BroadPhaseLayer::Type, 8-bit) for moving bodies. */
#define SK_JOLT_BROAD_PHASE_LAYER_MOVING 1u

/** Number of broad-phase layers (Jolt BroadPhaseLayerInterface size). */
#define SK_JOLT_BROAD_PHASE_LAYER_COUNT 2u

/** Collision group (Jolt CollisionGroup::GroupID, 32-bit) for regular bodies. */
#define SK_JOLT_COLLISION_GROUP_DEFAULT 1u

/** Collision group (Jolt CollisionGroup::GroupID, 32-bit) for characters. */
#define SK_JOLT_COLLISION_GROUP_CHARACTER 2u

/** Invalid collision group (Jolt CollisionGroup::cInvalidGroup). */
#define SK_JOLT_COLLISION_GROUP_INVALID 0xFFFFFFFFu

/** Full collision mask: collides with every group (Jolt GroupFilterTable default). */
#define SK_JOLT_COLLISION_MASK_ALL 0xFFFFFFFFu

/* ------------------------------------------------------------------ */
/*  Opaque handles                                                    */
/* ------------------------------------------------------------------ */

/** Opaque rigid body (Jolt Body / BodyID behind the stub). */
typedef struct sk_jolt_body_t sk_jolt_body_t;

/** Opaque character controller (Jolt CharacterVirtual behind the stub). */
typedef struct sk_jolt_character_t sk_jolt_character_t;

/* ------------------------------------------------------------------ */
/*  Module API                                                        */
/* ------------------------------------------------------------------ */

/**
 * Global physics module API (one table per process after plugin load).
 * Registered under SK_JOLT_API_TYPE_ID; hosts resolve it via app_api->get_api.
 * Every entry is an empty stub at this stage: no Jolt API is called, create
 * entry points report failure (NULL) until the integration lands.
 */
typedef struct sk_jolt_api_t {
	/**
	 * Initialize the physics module (creates the Jolt world, broadphase, and
	 * collision filters). Stub: no-op reporting success.
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*init)(void); // NOLINT(modernize-redundant-void-arg) — C ABI: () would not be a prototype in C

	/**
	 * Shut the physics module down and release the world created by init.
	 * Stub: no-op. Safe to call without a matching init.
	 */
	void (*shutdown)(void); // NOLINT(modernize-redundant-void-arg) — C ABI: () would not be a prototype in C

	/**
	 * Create a rigid body from a shape description.
	 * @param shape        Shape description (box/sphere/capsule). Must not be NULL.
	 * @param motion_type  Motion type (see sk_jolt_motion_type_t).
	 * @param object_layer Object layer the body is placed on (SK_JOLT_OBJECT_LAYER_*).
	 * @return New body handle, or NULL when creation failed. Stub: always NULL.
	 */
	sk_jolt_body_t* (*body_create)(const sk_jolt_shape_desc_t* shape, sk_jolt_motion_type_t motion_type, u32 object_layer);

	/**
	 * Destroy a body created with body_create.
	 * @param body Body handle (may be NULL; NULL is a no-op). Stub: no-op.
	 */
	void (*body_destroy)(sk_jolt_body_t* body);

	/**
	 * Create a character controller (capsule-shaped virtual character).
	 * @param shape        Shape description for the character capsule.
	 * @param object_layer Object layer the character is placed on (SK_JOLT_OBJECT_LAYER_*).
	 * @return New character handle, or NULL when creation failed. Stub: always NULL.
	 */
	sk_jolt_character_t* (*character_create)(const sk_jolt_shape_desc_t* shape, u32 object_layer);

	/**
	 * Destroy a character created with character_create.
	 * @param character Character handle (may be NULL; NULL is a no-op). Stub: no-op.
	 */
	void (*character_destroy)(sk_jolt_character_t* character);
} sk_jolt_api_t;

#ifdef __cplusplus
}
#endif
