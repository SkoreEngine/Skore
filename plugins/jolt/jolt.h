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
 * Current stage (APX-305): the physics world lifecycle and the fixed-step
 * simulation loop are implemented — Jolt Factory/RegisterTypes setup, a
 * 10 MiB TempAllocator, a JobSystemThreadPool, the broad-phase layer
 * interface, the object-vs-broad-phase and object-layer pair filters, a
 * PhysicsSystem sized from sk_jolt_settings_t, and a fixed-timestep
 * accumulator driven by step() with configurable gravity / substeps /
 * timestep. init/shutdown are re-entrant and tear down in the exact reverse
 * construction order with no leaks; repeated engine start/stop cycles are
 * supported (verified by plugin tests). A CollisionListener is deliberately
 * **not** implemented (explicitly out of scope); rigid bodies and character
 * controllers remain empty stubs (body_create / character_create return
 * NULL) until a later stage.
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
 * Rigid bodies and character controllers are not implemented yet: the
 * create entry points report failure (NULL) until the integration lands.
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
