#pragma once

/**
 * @file resource_jolt_component_types.h
 * @brief Physics component payload resource types (APX-306).
 *
 * Every physics ECS component doubles as a repository payload type: the
 * component resource's registered repository type id IS the ECS component
 * type id, so authored values serialize/deserialize round-trip through the
 * repository archives (resource_serialize.h) and appear in the repository
 * type listing like every other registered type.
 *
 * This module owns the repository half (payload type descriptors + type id
 * constants). The ECS half — POD component structs and ECS registration —
 * lives in plugins/jolt/jolt_components.{h,c} and shares the constants
 * below. The descriptors are deliberately free of any app-only dependency
 * (no filesystem / app API) and of any plugin header, so both the sk-jolt
 * plugin (statically linked sk-core only) and host tests can register them
 * into a repository without pulling in sk-app symbols.
 *
 * Value conventions mirror the vendored Jolt v5.6.0 defaults (see
 * plugins/jolt/jolt.h): motion types match JPH::EMotionType (Static=0,
 * Kinematic=1, Dynamic=2), object layers are 16-bit with NON_MOVING=0 /
 * MOVING=1, and the config defaults (friction 0.2, restitution 0.0,
 * damping 0.05 each, gravity factor 1.0, sleeping allowed) match Jolt's
 * BodyCreationSettings so un-authored configs map 1:1 onto body creation.
 */

#include "common.h"
#include "repository.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Component type ids (also registered repository payload type ids)  */
/* ------------------------------------------------------------------ */

#define SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_LO 0x85ea77931c85161fULL
#define SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_HI 0xaa96297198d1a8b0ULL
#define SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID SK_TYPE_ID("sk.rigid_body_config_resource", SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_LO, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID_HI)

#define SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_LO 0x6cfd0277f1d57ee9ULL
#define SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_HI 0x03c188e757c13e3aULL
#define SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID SK_TYPE_ID("sk.rigid_body_state_resource", SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_LO, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID_HI)

#define SK_BOX_COLLIDER_COMPONENT_TYPE_ID_LO 0x08d8cca1cf627bfcULL
#define SK_BOX_COLLIDER_COMPONENT_TYPE_ID_HI 0x943be79d6558989fULL
#define SK_BOX_COLLIDER_COMPONENT_TYPE_ID SK_TYPE_ID("sk.box_collider_resource", SK_BOX_COLLIDER_COMPONENT_TYPE_ID_LO, SK_BOX_COLLIDER_COMPONENT_TYPE_ID_HI)

#define SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_LO 0x866e8aa33d280086ULL
#define SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_HI 0x2c9cea7ba9600284ULL
#define SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID SK_TYPE_ID("sk.sphere_collider_resource", SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_LO, SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID_HI)

#define SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_LO 0x855d4a621b428ba4ULL
#define SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_HI 0xf668535766466110ULL
#define SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID SK_TYPE_ID("sk.capsule_collider_resource", SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_LO, SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID_HI)

/* ------------------------------------------------------------------ */
/*  Repository payload field indices (mirror the serialized fields)   */
/* ------------------------------------------------------------------ */

/* Field 0 is NOT a Name string: physics components are authored as plain
 * value payloads (the entity/scene resource envelopes own the name field). */

enum sk_rigid_body_config_field_t {
	SK_RIGID_BODY_CONFIG_FIELD_MOTION_TYPE = 0,		/* motion type (ENUM); see jolt.h sk_jolt_motion_type_t */
	SK_RIGID_BODY_CONFIG_FIELD_MASS = 1,			/* kg (FLOAT); 0 = infinite (static/kinematic) */
	SK_RIGID_BODY_CONFIG_FIELD_FRICTION = 2,		/* dimensionless (FLOAT) */
	SK_RIGID_BODY_CONFIG_FIELD_RESTITUTION = 3,		/* dimensionless (FLOAT) */
	SK_RIGID_BODY_CONFIG_FIELD_LINEAR_DAMPING = 4,	/* dv/dt = -c * v (FLOAT) */
	SK_RIGID_BODY_CONFIG_FIELD_ANGULAR_DAMPING = 5, /* dw/dt = -c * w (FLOAT) */
	SK_RIGID_BODY_CONFIG_FIELD_GRAVITY_FACTOR = 6,	/* gravity multiplier (FLOAT) */
	SK_RIGID_BODY_CONFIG_FIELD_OBJECT_LAYER = 7,	/* object layer (UINT); see jolt.h SK_JOLT_OBJECT_LAYER_* */
	SK_RIGID_BODY_CONFIG_FIELD_FLAGS = 8,			/* behavior flags (UINT); see jolt_components.h sk_rigid_body_flag_t */
};

enum sk_rigid_body_state_field_t {
	SK_RIGID_BODY_STATE_FIELD_LINEAR_VELOCITY = 0,	/* m/s (VEC3) */
	SK_RIGID_BODY_STATE_FIELD_ANGULAR_VELOCITY = 1, /* rad/s (VEC3) */
};

enum sk_box_collider_field_t {
	SK_BOX_COLLIDER_FIELD_HALF_EXTENT = 0, /* half size per local axis (VEC3) */
};

enum sk_sphere_collider_field_t {
	SK_SPHERE_COLLIDER_FIELD_RADIUS = 0, /* sphere radius (FLOAT) */
};

enum sk_capsule_collider_field_t {
	SK_CAPSULE_COLLIDER_FIELD_HALF_HEIGHT = 0, /* half height of the cylinder section (FLOAT) */
	SK_CAPSULE_COLLIDER_FIELD_RADIUS = 1,	   /* capsule radius (FLOAT) */
};

/**
 * Register every physics component payload type (rigid body config, rigid
 * body state, box / sphere / capsule collider) into @p repository via manual
 * field descriptors (reflection). The registered type ids are the same the
 * sk-jolt plugin registers the ECS components under.
 * @param repository Target repository (must not be NULL).
 * @return 0 on success, the first register_type error otherwise.
 */
i32 sk_jolt_component_types_register(sk_repository_t* repository);

#ifdef __cplusplus
}
#endif
