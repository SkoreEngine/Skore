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
 * (thirdparty/jolt) behind this boundary.
 *
 * Current stage (APX-320): the physics world lifecycle and the fixed-step
 * simulation loop are implemented — Jolt Factory/RegisterTypes setup, a
 * 10 MiB TempAllocator, a JobSystemThreadPool, the broad-phase layer
 * interface, the object-vs-broad-phase and object-layer pair filters, a
 * PhysicsSystem sized from sk_jolt_settings_t, and a fixed-timestep
 * accumulator driven by step() with configurable gravity / substeps /
 * timestep. init/shutdown are re-entrant and tear down in the exact reverse
 * construction order with no leaks; repeated engine start/stop cycles are
 * supported (verified by plugin tests). Rigid bodies are implemented:
 * body_create() builds a Jolt body from the box/sphere/capsule shape
 * descriptions with static/kinematic/dynamic motion types, body_destroy()
 * removes and destroys it, and the position / rotation / linear + angular
 * velocity accessors get and set the body state through POD structs
 * (sk_jolt_vec3_t / sk_jolt_quat_t). Every body handle is validated on access, so
 * using a handle after destroy (or after world shutdown) returns an error
 * instead of crashing. A CollisionListener is deliberately **not**
 * implemented (explicitly out of scope); character controllers remain
 * empty stubs (character_create returns NULL) until a later stage.
 *
 * # Body handles
 *
 * body_create() returns an opaque sk_jolt_body_t* that is valid until
 * body_destroy() (or world shutdown). All body accessors validate the
 * handle first: they return a non-zero error code when the world is not
 * initialized, the handle is NULL, or the handle has been destroyed, and
 * zero the output struct on failure. Handle records are retained until
 * world shutdown, so within a world a destroyed handle can never be aliased
 * by a later body and use-after-destroy is always detected — never a crash
 * and never silently reading another body. Handles become invalid when the
 * world is shut down: accessors return errors from then on, and a stale
 * handle from a previous world is rejected after the next init.
 *
 * Position/rotation are in world space and Jolt coordinates (Y-up, meters):
 * position is the body's shape origin (for these centered shapes this
 * coincides with the center of mass). Velocities are in m/s and rad/s
 * around the center of mass. Bodies are created at the origin with identity
 * rotation; hosts place them with body_set_position / body_set_rotation
 * before stepping. The vector/quaternion POD structs below have the exact
 * field layout of core/math3d.h sk_vec3_t / sk_quat_t so the two are
 * freely interchangeable at the C boundary.
 *
 * Value conventions mirror the vendored Jolt v5.6.0 defaults so the
 * integration maps 1:1:
 *   - motion types match JPH::EMotionType (Static=0, Kinematic=1, Dynamic=2),
 *   - object layers are 16-bit (JPH_OBJECT_LAYER_BITS == 16 default) with
 *     cObjectLayerInvalid == 0xFFFF,
 *   - broad-phase layers are 8-bit (JPH::BroadPhaseLayer::Type),
 *   - collision group ids / masks are 32-bit (JPH::CollisionGroup::GroupID).
 *
 * # Layer / collision-filter model
 *
 * Two object layers (non-moving static geometry, moving bodies) map 1:1 onto
 * two broad-phase layers, the classic Jolt HelloWorld setup; characters use
 * their own collision group so the pair filter can later separate dynamic
 * bodies from character probes without changing the layer set.
 *
 * # Thread model / lifecycle
 *
 * The whole table is main-thread only. step() accumulates host frame deltas
 * and drains them at the configured fixed rate, invoking the step callback
 * once per completed physics step — hosts drive it from the app main loop
 * with app_api->delta_time(ctx). init() may be called again after shutdown()
 * (or directly over a live world: it tears the old one down first); every
 * resource is owned by the module and released by shutdown(), so repeated
 * engine start/stop cycles do not leak.
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
/*  Transform POD structs                                              */
/* ------------------------------------------------------------------ */

/**
 * 3-component vector. Field layout is identical to core/math3d.h sk_vec3_t,
 * so the two types are interchangeable at the C boundary (hosts may pass a
 * sk_vec3_t* cast to sk_jolt_vec3_t*). Defined here instead of including
 * math3d.h so this header stays self-contained plain C for hosts and C++
 * translation units alike.
 * @field x X component (position: meters; velocity: m/s or rad/s).
 * @field y Y component.
 * @field z Z component.
 */
typedef struct sk_jolt_vec3_t {
	f32 x;
	f32 y;
	f32 z;
} sk_jolt_vec3_t;

/**
 * Unit quaternion (x, y, z imaginary, w scalar; identity = (0,0,0,1)).
 * Field layout is identical to core/math3d.h sk_quat_t (same interchange
 * note as sk_jolt_vec3_t).
 */
typedef struct sk_jolt_quat_t {
	f32 x;
	f32 y;
	f32 z;
	f32 w;
} sk_jolt_quat_t;

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

/**
 * Opaque rigid body handle returned by body_create(). Valid until
 * body_destroy() or world shutdown; every accessor validates it and returns
 * an error for a destroyed (or NULL) handle — see the module doc above.
 */
typedef struct sk_jolt_body_t sk_jolt_body_t;

/** Opaque character controller (Jolt CharacterVirtual behind the stub). */
typedef struct sk_jolt_character_t sk_jolt_character_t;

/* ------------------------------------------------------------------ */
/*  World settings                                                    */
/* ------------------------------------------------------------------ */

/**
 * Physics world settings passed to init. Fill via settings_defaults() and
 * tweak the fields you care about; init(NULL) uses the full default set.
 * Numeric caps (fixed_timestep, substeps, max_*) fall back to their defaults
 * when zero; gravity is honored as given (all-zero gravity is legal).
 */
typedef struct sk_jolt_settings_t {
	/** World gravity in m/s² per axis. Default {0, -9.81, 0}. */
	f32 gravity[3];
	/** Seconds per physics step (the fixed timestep). Default 1/60. Must be > 0. */
	f32 fixed_timestep;
	/** Collision/integration iterations per physics step. Default 1. Must be >= 1. */
	u32 substeps;
	/** Max simultaneous bodies (PhysicsSystem::Init inMaxBodies). Default 65536. */
	u32 max_bodies;
	/** Max broad-phase body pairs processed per step. Default 65536. */
	u32 max_body_pairs;
	/** Max simultaneous contact constraints per step. Default 10240. */
	u32 max_constraints;
} sk_jolt_settings_t;

/**
 * Step callback, invoked by step() once per completed fixed physics step.
 * Main-thread only; keep it short (Jolt has already finished the step).
 * @param user_data    Opaque pointer passed to step() (may be NULL).
 * @param physics_time Accumulated simulated time in seconds since init
 *                     (grows by fixed_timestep per step; f64 for long runs).
 * @param step_index   Zero-based count of completed steps since init.
 */
typedef void (*sk_jolt_step_callback_fn)(void_ptr_t user_data, f64 physics_time, u64 step_index);

/* ------------------------------------------------------------------ */
/*  Module API                                                        */
/* ------------------------------------------------------------------ */

/**
 * Global physics module API (one table per process after plugin load).
 * Registered under SK_JOLT_API_TYPE_ID; hosts resolve it via app_api->get_api.
 *
 * Lifecycle: init(settings) creates the Jolt world (Factory + registered
 * types, TempAllocator, JobSystemThreadPool, layer filters, PhysicsSystem).
 * Hosts then drive step(delta, callback, user_data) once per frame — the
 * plugin accumulates the frame delta and runs the physics world at the fixed
 * configured rate, calling the callback after each step. shutdown() tears the
 * world down in reverse construction order and releases every Jolt resource.
 * All entries are main-thread only; init/shutdown are re-entrant (init over a
 * live world replaces it; shutdown without init is a no-op).
 *
 * Rigid bodies are implemented (APX-320): body_create() / body_destroy()
 * and the position / rotation / velocity accessors (all handles are
 * validated, so use-after-destroy returns an error). Character controllers
 * are not implemented yet: character_create() reports failure (NULL).
 */
typedef struct sk_jolt_api_t {
	/**
	 * Initialize the physics module (Jolt factory/types, temp allocator, job
	 * system, broad-phase + object-layer filters, PhysicsSystem) with the
	 * given world settings. A previous world (if any) is shut down first, so
	 * repeated init/shutdown cycles are safe.
	 * @param settings World settings, or NULL for the defaults
	 *                 (see sk_jolt_settings_t / settings_defaults).
	 * @return 0 on success, non-zero on failure.
	 */
	i32 (*init)(const sk_jolt_settings_t* settings);

	/**
	 * Shut the physics module down: destroy the world (PhysicsSystem, filters,
	 * job system threads, temp allocator), unregister Jolt types and destroy
	 * the factory. Releases every Jolt allocation; safe to call without a
	 * matching init and safe to call repeatedly.
	 */
	void (*shutdown)(void); // NOLINT(modernize-redundant-void-arg) — C ABI: () would not be a prototype in C

	/**
	 * Fill @p out with the default world settings (gravity {0,-9.81,0},
	 * fixed_timestep 1/60, substeps 1, 65536 bodies / body pairs, 10240
	 * contact constraints). Callers tweak fields, then pass to init.
	 * @param out Destination struct; must not be NULL.
	 */
	void (*settings_defaults)(sk_jolt_settings_t* out);

	/**
	 * Set world gravity. Applies immediately to the live world; no-op when
	 * not initialized.
	 * @param x Gravity x component (m/s²).
	 * @param y Gravity y component (m/s²).
	 * @param z Gravity z component (m/s²).
	 */
	void (*set_gravity)(f32 x, f32 y, f32 z);

	/**
	 * Read world gravity. No-op (outputs zeroed) when not initialized.
	 * @param out_x Optional output for the x component (may be NULL).
	 * @param out_y Optional output for the y component (may be NULL).
	 * @param out_z Optional output for the z component (may be NULL).
	 */
	void (*get_gravity)(f32* out_x, f32* out_y, f32* out_z);

	/**
	 * Change the fixed timestep (seconds per physics step) of a live world.
	 * The step accumulator is drained so the new rate takes effect without a
	 * burst of catch-up steps.
	 * @param step Seconds per step; must be > 0.
	 * @return 0 on success, non-zero when not initialized or @p step <= 0.
	 */
	i32 (*set_fixed_timestep)(f32 step);

	/**
	 * Current fixed timestep in seconds.
	 * @return Seconds per step, or 0 when not initialized.
	 */
	f32 (*get_fixed_timestep)(void); // NOLINT(modernize-redundant-void-arg) — C ABI: () would not be a prototype in C

	/**
	 * Change the collision/integration substeps of a live world (the Jolt
	 * collision-steps parameter passed to PhysicsSystem::Update).
	 * @param count Substep count; must be >= 1.
	 * @return 0 on success, non-zero when not initialized or @p count == 0.
	 */
	i32 (*set_substeps)(u32 count);

	/**
	 * Current collision/integration substeps per physics step.
	 * @return Substep count, or 0 when not initialized.
	 */
	u32 (*get_substeps)(void); // NOLINT(modernize-redundant-void-arg) — C ABI: () would not be a prototype in C

	/**
	 * Advance the physics world by one host frame: add @p delta_time to the
	 * fixed-timestep accumulator and run PhysicsSystem::Update until it is
	 * drained, invoking @p callback after every completed step (once per
	 * physics step at the configured fixed rate). Huge frame deltas (e.g.
	 * after a debugger pause) are clamped to 0.25 s so a slow frame cannot
	 * trigger a "spiral of death" of catch-up steps. No-op when not
	 * initialized or @p delta_time <= 0. Main-thread only.
	 * @param delta_time Host frame delta in seconds (app delta_time).
	 * @param callback    Optional per-step callback (may be NULL).
	 * @param user_data   Opaque pointer forwarded to @p callback (may be NULL).
	 */
	void (*step)(f32 delta_time, sk_jolt_step_callback_fn callback, void_ptr_t user_data);

	/**
	 * Create a rigid body from a shape description and add it to the world
	 * (active, at the origin with identity rotation and zero velocity). The
	 * body uses the vendored Jolt BodyCreationSettings defaults for its
	 * material/motion properties (friction 0.2, restitution 0.0, linear +
	 * angular damping 0.05, gravity factor 1.0, sleeping allowed). Place it
	 * with body_set_position / body_set_rotation before stepping.
	 * @param shape        Shape description (box/sphere/capsule). Must not be NULL;
	 *                     a NULL shape or a bad shape value fails creation.
	 * @param motion_type  Motion type (see sk_jolt_motion_type_t); must be one of
	 *                     the three documented values.
	 * @param object_layer Object layer the body is placed on
	 *                     (SK_JOLT_OBJECT_LAYER_*; one of the two documented layers).
	 * @return New body handle (valid until body_destroy or shutdown), or NULL
	 *         when the world is not initialized or creation failed.
	 */
	sk_jolt_body_t* (*body_create)(const sk_jolt_shape_desc_t* shape, sk_jolt_motion_type_t motion_type, u32 object_layer);

	/**
	 * Destroy a body created with body_create: remove it from the world and
	 * release it. The handle becomes invalid — later accessor calls on it
	 * return an error instead of crashing. Destroying NULL or an already
	 * destroyed handle is a no-op.
	 * @param body Body handle (may be NULL).
	 */
	void (*body_destroy)(sk_jolt_body_t* body);

	/**
	 * Read the body's world-space position (meters, shape origin).
	 * @param body         Body handle; must be valid (created, not destroyed).
	 * @param out_position Output position; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p out_position is NULL (in that
	 *         case the output is zeroed when non-NULL).
	 */
	i32 (*body_get_position)(const sk_jolt_body_t* body, sk_jolt_vec3_t* out_position);

	/**
	 * Set the body's world-space position (meters). Wakes the body up.
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param position Position to set; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p position is NULL.
	 */
	i32 (*body_set_position)(sk_jolt_body_t* body, const sk_jolt_vec3_t* position);

	/**
	 * Read the body's world-space rotation (unit quaternion, (x, y, z, w)).
	 * @param body         Body handle; must be valid (created, not destroyed).
	 * @param out_rotation Output rotation; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p out_rotation is NULL (in that
	 *         case the output is zeroed when non-NULL).
	 */
	i32 (*body_get_rotation)(const sk_jolt_body_t* body, sk_jolt_quat_t* out_rotation);

	/**
	 * Set the body's world-space rotation (unit quaternion, (x, y, z, w)).
	 * Wakes the body up.
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param rotation Rotation to set; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p rotation is NULL.
	 */
	i32 (*body_set_rotation)(sk_jolt_body_t* body, const sk_jolt_quat_t* rotation);

	/**
	 * Read the body's linear velocity (m/s, center of mass). Static bodies
	 * always report zero.
	 * @param body         Body handle; must be valid (created, not destroyed).
	 * @param out_velocity Output linear velocity; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p out_velocity is NULL (in that
	 *         case the output is zeroed when non-NULL).
	 */
	i32 (*body_get_linear_velocity)(const sk_jolt_body_t* body, sk_jolt_vec3_t* out_velocity);

	/**
	 * Set the body's linear velocity (m/s). Wakes the body up; ignored for
	 * static bodies (they cannot move).
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param velocity Linear velocity to set; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p velocity is NULL.
	 */
	i32 (*body_set_linear_velocity)(sk_jolt_body_t* body, const sk_jolt_vec3_t* velocity);

	/**
	 * Read the body's angular velocity (rad/s around the center of mass).
	 * Static bodies always report zero.
	 * @param body         Body handle; must be valid (created, not destroyed).
	 * @param out_velocity Output angular velocity; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p out_velocity is NULL (in that
	 *         case the output is zeroed when non-NULL).
	 */
	i32 (*body_get_angular_velocity)(const sk_jolt_body_t* body, sk_jolt_vec3_t* out_velocity);

	/**
	 * Set the body's angular velocity (rad/s). Wakes the body up; ignored for
	 * static bodies (they cannot move).
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param velocity Angular velocity to set; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p velocity is NULL.
	 */
	i32 (*body_set_angular_velocity)(sk_jolt_body_t* body, const sk_jolt_vec3_t* velocity);

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
