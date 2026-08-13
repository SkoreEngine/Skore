#pragma once

/**
 * @file jolt_components.h
 * @brief Physics ECS components (rigid body + colliders) for the sk-jolt plugin.
 *
 * The physics integration composes an entity from small, focused components
 * instead of one fat component:
 *
 *   - sk_rigid_body_config_t  (cold / authored) — motion type, mass,
 *     friction, restitution, damping, gravity factor, object layer, flags,
 *     plus the opaque Jolt body handle that the integration fills in.
 *   - sk_rigid_body_state_t   (hot / per-frame) — linear + angular velocity.
 *     Kept in its own component so simulation systems can iterate and write
 *     it independently of the config (no false sharing with cold data).
 *   - sk_box_collider_t / sk_sphere_collider_t / sk_capsule_collider_t —
 *     shape data in separate components so a body is composed from whatever
 *     colliders it needs.
 *
 * Each component doubles as a repository payload type (reflection via manual
 * field descriptors, see repository.h): the registered repository type id IS
 * the ECS component type id (constants in core/resource_jolt_component_types.h),
 * so authored values serialize/deserialize through the repository and its
 * JSON/binary archives (resource_serialize.h). The runtime-only Jolt body
 * handle is deliberately NOT a repository field — pointers cannot round-trip —
 * so it lives only on the ECS struct.
 *
 * This is the C half of the plugin (POD structs + registration only). It
 * performs no Jolt interaction: the only Jolt type referenced is the opaque
 * body handle. Body/shape creation from these components is a later stage.
 */

#include "common.h"
#include "entities.h" /* sk_entities_api_t */
#include "jolt.h"	  /* sk_jolt_body_t, sk_jolt_motion_type_t, layers */
#include "repository.h"
#include "resource_jolt_component_types.h" /* component type ids + field indices */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Rigid body flags                                                  */
/* ------------------------------------------------------------------ */

/**
 * Rigid body behavior flags (bitmask in sk_rigid_body_config_t.flags; also
 * the serialized Flags payload field).
 * @field SK_RIGID_BODY_FLAG_NONE                No special behavior (0).
 * @field SK_RIGID_BODY_FLAG_SENSOR              Trigger body: reports contacts, never responds (Jolt Body::SetIsSensor).
 * @field SK_RIGID_BODY_FLAG_ALLOW_SLEEPING      Body may fall asleep when idle (Jolt default behavior).
 * @field SK_RIGID_BODY_FLAG_CONTINUOUS_COLLISION Swept CCD: prevents tunneling at high speed (Jolt MotionQuality::LinearCast).
 */
typedef enum sk_rigid_body_flag_t {
	SK_RIGID_BODY_FLAG_NONE = 0,
	SK_RIGID_BODY_FLAG_SENSOR = (1u << 0),
	SK_RIGID_BODY_FLAG_ALLOW_SLEEPING = (1u << 1),
	SK_RIGID_BODY_FLAG_CONTINUOUS_COLLISION = (1u << 2),
} sk_rigid_body_flag_t;

/* ------------------------------------------------------------------ */
/*  Component structs (POD; layout is part of the public API)         */
/* ------------------------------------------------------------------ */

/**
 * Rigid body configuration (cold data — authored once, read by the physics
 * integration). Sits in its own component so per-frame simulation never
 * touches it; the hot per-frame velocities live in sk_rigid_body_state_t.
 * Defaults mirror the vendored Jolt BodyCreationSettings values (plus the
 * authored mass and the moving object layer, see
 * core/resource_jolt_component_types.c).
 *
 * @field motion_type   SK_JOLT_MOTION_TYPE_STATIC / KINEMATIC / DYNAMIC.
 * @field mass          Mass in kg; 0 = infinite mass (static / kinematic).
 * @field friction      Jolt mFriction (usually 0..1).
 * @field restitution   Jolt mRestitution (usually 0..1).
 * @field linear_damping  Jolt mLinearDamping: dv/dt = -c * v.
 * @field angular_damping Jolt mAngularDamping: dw/dt = -c * w.
 * @field gravity_factor  Jolt mGravityFactor: multiplier on world gravity.
 * @field object_layer  Jolt object layer (SK_JOLT_OBJECT_LAYER_*).
 * @field flags         sk_rigid_body_flag_t bitmask.
 * @field body          Opaque Jolt body handle, filled in by the integration
 *                      when the body is created; NULL until then. Not
 *                      serialized (runtime-only binding, see file comment).
 */
typedef struct sk_rigid_body_config_t {
	sk_jolt_motion_type_t motion_type;
	f32 mass;
	f32 friction;
	f32 restitution;
	f32 linear_damping;
	f32 angular_damping;
	f32 gravity_factor;
	u32 object_layer;
	u32 flags;
	u8 _pad0[4]; /* align body on LP64/LLP64; runtime-only, not serialized */
	sk_jolt_body_t* body;
} sk_rigid_body_config_t;

/**
 * Rigid body per-frame state (hot data — iterated and written by simulation
 * systems every tick). Deliberately a separate component from the config so
 * the velocities stay in one compact cache line and hot loops only touch the
 * fields they write.
 * @field linear_velocity  Velocity in m/s.
 * @field angular_velocity Angular velocity in rad/s.
 */
typedef struct sk_rigid_body_state_t {
	sk_vec3_t linear_velocity;
	sk_vec3_t angular_velocity;
} sk_rigid_body_state_t;

/**
 * Box collider shape data (sk.box_collider_resource).
 * @field half_extent Half size of the box in each local axis (Jolt
 *                    BoxShapeSettings mHalfExtent); each component >= 0.
 */
typedef struct sk_box_collider_t {
	sk_vec3_t half_extent;
} sk_box_collider_t;

/**
 * Sphere collider shape data (sk.sphere_collider_resource).
 * @field radius Sphere radius (Jolt SphereShapeSettings mRadius), > 0.
 */
typedef struct sk_sphere_collider_t {
	f32 radius;
} sk_sphere_collider_t;

/**
 * Capsule collider shape data (sk.capsule_collider_resource).
 * @field half_height Half height of the cylinder section (Jolt
 *                    CapsuleShapeSettings mHalfHeightOfCylinder), >= 0.
 * @field radius      Capsule radius (Jolt CapsuleShapeSettings mRadius), > 0.
 */
typedef struct sk_capsule_collider_t {
	f32 half_height;
	f32 radius;
} sk_capsule_collider_t;

/* ------------------------------------------------------------------ */
/*  Registration                                                      */
/* ------------------------------------------------------------------ */

/**
 * Register every physics component (rigid body config, rigid body state, box
 * / sphere / capsule collider) with @p ecs, using the same type ids the
 * repository payload types register under. Idempotent: re-registration with a
 * matching layout is a no-op, so plugins/hosts/tests may call it repeatedly.
 * @param ecs ECS API table (must not be NULL).
 * @return 0 on success, the first register_component error otherwise.
 */
i32 sk_jolt_components_register(const sk_entities_api_t* ecs);

#ifdef __cplusplus
}
#endif
