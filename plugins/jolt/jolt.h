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
 * Current stage (APX-321): the physics world lifecycle and the fixed-step
 * simulation loop are implemented — Jolt Factory/RegisterTypes setup, a
 * 10 MiB TempAllocator, a JobSystemThreadPool, the broad-phase layer
 * interface, the object-vs-broad-phase and object-layer pair filters, a
 * PhysicsSystem sized from sk_jolt_settings_t, and a fixed-timestep
 * accumulator driven by step() with configurable gravity / substeps /
 * timestep. init/shutdown are re-entrant and tear down in the exact reverse
 * construction order with no leaks; repeated engine start/stop cycles are
 * supported (verified by plugin tests). Rigid bodies are implemented:
 * body_create() builds a Jolt body from the box/sphere/capsule shape
 * descriptions with static/kinematic/dynamic motion types and an explicit
 * object layer, body_destroy() removes and destroys it, and the position /
 * rotation / linear + angular velocity accessors get and set the body state
 * through POD structs (sk_jolt_vec3_t / sk_jolt_quat_t). Every body handle is
 * validated on access, so using a handle after destroy (or after world
 * shutdown) returns an error instead of crashing.
 *
 * Scene queries are implemented (APX-322): ray_cast() and sphere_cast() run
 * Jolt narrow-phase queries against the live world and report the closest hit
 * in a POD sk_jolt_query_hit_t (hit body handle, world-space hit point, unit
 * surface normal, and parametric fraction along the query direction). Both
 * queries are cast "from" an object layer and honor the same layer / collision
 * matrix as the contact filters: a query from layer L only hits bodies on
 * layers that collide with L, so a ray from the ghost SENSOR layer hits
 * nothing, a ray from NON_MOVING does not hit static geometry, and a query
 * never hits a body on a layer it does not collide with. Queries are
 * main-thread only, safe to run between steps (they take Jolt body locks),
 * and fail with an error code instead of crashing without a live world.
 *
 * The layer and collision-filter constants below are wired to the filters:
 * the object-layer pair filter is driven straight from the per-layer
 * SK_JOLT_COLLISION_MASK_* masks (the documented matrix is the single
 * source of truth), the object-vs-broad-phase filter mirrors the same
 * matrix, and the layer set includes the ghost SK_JOLT_OBJECT_LAYER_SENSOR
 * (collides with nothing — passes through floors and bodies). A
 * CollisionListener is deliberately **not** implemented (explicitly out of
 * scope); character controllers remain empty stubs (character_create
 * returns NULL) until a later stage.
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
 * Three object layers implement the documented collision matrix (see the
 * constants below): NON_MOVING static geometry never collides with other
 * static geometry; MOVING bodies collide with static geometry and with each
 * other; SENSOR ghost bodies collide with nothing (they fall through the
 * floor and pass through other bodies — triggers/probes). The object-layer
 * pair filter is driven straight from the per-layer SK_JOLT_COLLISION_MASK_*
 * constants and the object-vs-broad-phase filter mirrors the same matrix, so
 * the header is the single source of truth for the filter behavior.
 * NON_MOVING maps to broad-phase layer 0; MOVING and SENSOR (both can move)
 * share broad-phase layer 1. Collision groups are a separate mechanism:
 * characters use their own collision group so the group filter can later
 * separate dynamic bodies from character probes without changing the layer
 * set.
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

/*
 * Object-layer collision matrix — the documented filter contract. A pair of
 * bodies collides iff the matrix entry for their two object layers is
 * "yes" (the matrix is symmetric). The same contract is encoded in the
 * SK_JOLT_COLLISION_MASK_* per-layer masks below and implemented by the
 * object-layer pair filter in jolt.cpp; the object-vs-broad-phase filter
 * mirrors it so the coarse broad-phase pass never admits a pair the pair
 * filter would reject.
 *
 *                     NON_MOVING   MOVING   SENSOR
 *   NON_MOVING (static)    no        yes       no
 *   MOVING (dynamic)       yes       yes       no
 *   SENSOR (ghost)         no        no        no
 *
 *   NON_MOVING  Static world geometry (floors, walls). Never moves and
 *               never collides with other static geometry.
 *   MOVING      Dynamic/kinematic bodies. Collide with static geometry and
 *               with each other.
 *   SENSOR      Ghost bodies. Collide with nothing: they pass through the
 *               static floor and through every other body (triggers,
 *               probes, or bodies that must never block).
 */

/** Object layer (Jolt ObjectLayer, 16-bit) of static, non-moving geometry. */
#define SK_JOLT_OBJECT_LAYER_NON_MOVING 0u

/** Object layer (Jolt ObjectLayer, 16-bit) of dynamic/kinematic bodies. */
#define SK_JOLT_OBJECT_LAYER_MOVING 1u

/** Object layer (Jolt ObjectLayer, 16-bit) of ghost bodies: collide with nothing. */
#define SK_JOLT_OBJECT_LAYER_SENSOR 2u

/** Number of object layers (size of the collision-mask / broad-phase tables). */
#define SK_JOLT_OBJECT_LAYER_COUNT 3u

/** Invalid object layer (Jolt cObjectLayerInvalid with 16-bit layers). */
#define SK_JOLT_OBJECT_LAYER_INVALID 0xFFFFu

/** Broad-phase layer (Jolt BroadPhaseLayer::Type, 8-bit) for static geometry. */
#define SK_JOLT_BROAD_PHASE_LAYER_NON_MOVING 0u

/** Broad-phase layer (Jolt BroadPhaseLayer::Type, 8-bit) for moving bodies. */
#define SK_JOLT_BROAD_PHASE_LAYER_MOVING 1u

/** Number of broad-phase layers (Jolt BroadPhaseLayerInterface size). */
#define SK_JOLT_BROAD_PHASE_LAYER_COUNT 2u

/*
 * Collision-filter masks: one bit per object layer (bit N = object layer N,
 * i.e. 1u << SK_JOLT_OBJECT_LAYER_<N>). A body on layer L collides with
 * layer M iff bit M is set in SK_JOLT_COLLISION_MASK_<L>; the matrix is
 * symmetric, so both masks must agree (the pair filter requires both
 * directions). These masks ARE the matrix above — the object-layer pair
 * filter is driven straight from them, so the header stays the single
 * source of truth.
 */

/** Collision-filter mask of NON_MOVING: collides with MOVING bodies only. */
#define SK_JOLT_COLLISION_MASK_NON_MOVING (1u << SK_JOLT_OBJECT_LAYER_MOVING)

/** Collision-filter mask of MOVING: collides with static geometry and MOVING bodies. */
#define SK_JOLT_COLLISION_MASK_MOVING ((1u << SK_JOLT_OBJECT_LAYER_NON_MOVING) | (1u << SK_JOLT_OBJECT_LAYER_MOVING))

/** Collision-filter mask of SENSOR: collides with nothing. */
#define SK_JOLT_COLLISION_MASK_SENSOR 0u

/*
 * Collision groups (Jolt CollisionGroup::GroupID, 32-bit) are a separate
 * filter mechanism from the object layers above: layers are resolved by the
 * broad-phase / object-layer pair filters, groups by the per-body
 * GroupFilter during pair processing. Bodies currently carry the Jolt
 * default (no group filter, cInvalidGroup), which collides with everything;
 * the character stage will give character probes their own group so the
 * group filter can separate them from dynamic bodies without changing the
 * layer set — SK_JOLT_COLLISION_GROUP_DEFAULT / _CHARACTER are that
 * contract.
 */

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

/** Forward declaration of the ECS world (owned by the entities plugin). */
typedef struct sk_world_t sk_world_t;

/**
 * Forward declaration of the ECS entity handle (owned by the entities plugin;
 * POD {u32 index; u32 generation;}, passed by value — see entities.h). The
 * complete definition is not required here: jolt.h only declares the function
 * pointer below that takes it by value, and callers already include entities.h.
 */
typedef struct sk_entity_t sk_entity_t;

/* ------------------------------------------------------------------ */
/*  Scene queries (ray / shape casts)                                 */
/* ------------------------------------------------------------------ */

/**
 * Closest-hit result of a scene query (ray_cast / sphere_cast): the body
 * that was hit, the world-space hit point, the world-space unit surface
 * normal at that point, and the parametric fraction along the query
 * direction. Zeroed when the query found no hit.
 *
 * @field body     Body handle of the hit body (valid until body_destroy /
 *                 world shutdown; NULL when no hit occurred).
 * @field position World-space hit point in meters (on the hit body's
 *                 surface).
 * @field normal   World-space unit surface normal of the hit body at the
 *                 hit point (points out of the body, toward the query).
 * @field fraction Parametric distance along the query direction:
 *                 position = query origin + query direction * fraction.
 *                 With a unit-length direction this equals the distance in
 *                 meters; hits beyond max_distance are never reported, so
 *                 with direction length == max_distance it is in [0, 1].
 */
typedef struct sk_jolt_query_hit_t {
	sk_jolt_body_t* body;
	sk_jolt_vec3_t position;
	sk_jolt_vec3_t normal;
	f32 fraction;
} sk_jolt_query_hit_t;

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
 * validated, so use-after-destroy returns an error). Scene queries are
 * implemented (APX-322): ray_cast() / sphere_cast() report the closest hit
 * as a POD sk_jolt_query_hit_t, honoring the layer collision matrix
 * (queries are cast from an object layer and only hit colliding layers).
 * Character controllers are not implemented yet: character_create() reports
 * failure (NULL).
 *
 * ECS sync (APX-307): sync_world() / write_back() / step_world() reconcile
 * rigid-body entities with Jolt. The entity↔BodyID map is plugin-private.
 *
 * Component changes vs. direct body operations (APX-308): ECS components are
 * plain data and nothing inside them can notify the sync when gameplay
 * mutates a field, so cold/authored component mutations (sk_rigid_body_config_t
 * fields — motion type, mass, friction, restitution, damping, gravity factor,
 * object layer, flags — and the collider shape fields) reach the Jolt body
 * ONLY after entity_require_update() marks the entity dirty; the next
 * sync_world() / step_world() consumes that dirty set and rebuilds or
 * reconfigures the body from the new component values. Without the call the
 * mutation is silently ignored. Component add/remove and entity spawn/despawn
 * are detected automatically (no require-update), and the hot per-frame
 * channels — the transform pose and the rigid-body-state velocities — are
 * applied every step. The direct body_* runtime operations (velocity get/set,
 * forces / impulses / torques, teleport, activate / deactivate, motion type)
 * act on the live Jolt body immediately: they never read or write ECS
 * components, so they never need entity_require_update(). See
 * entity_require_update() and the body_* entries below for the exact split.
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
	 *                     (SK_JOLT_OBJECT_LAYER_*; one of the documented layers —
	 *                     see the filter matrix above).
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
	 * Read the body's motion type (see sk_jolt_motion_type_t). Direct runtime
	 * query — never needs entity_require_update() (the ECS sync only reapplies
	 * the config's motion type after entity_require_update(), so a body whose
	 * motion type was changed here keeps it until then).
	 * @param body              Body handle; must be valid (created, not destroyed).
	 * @param out_motion_type   Output motion type; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p out_motion_type is NULL (in that
	 *         case the output is zeroed when non-NULL).
	 */
	i32 (*body_get_motion_type)(const sk_jolt_body_t* body, sk_jolt_motion_type_t* out_motion_type);

	/**
	 * Change the body's motion type at runtime (static ↔ kinematic ↔ dynamic).
	 * This is the direct runtime alternative to mutating the rigid-body config
	 * component's motion_type field + entity_require_update(): it acts on the
	 * live body immediately and is not gated by the dirty set. Switching to
	 * STATIC zeroes velocity and cancels forces; switching to DYNAMIC /
	 * KINEMATIC wakes the body up. Bodies are created with
	 * mAllowDynamicOrKinematic so every transition is supported (the ECS sync
	 * path already does the same), and switching to STATIC stops and freezes
	 * the body (velocity zeroed, forces cancelled).
	 * @param body        Body handle; must be valid (created, not destroyed).
	 * @param motion_type One of the three documented sk_jolt_motion_type_t values.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p motion_type is not one of the
	 *         three documented values.
	 */
	i32 (*body_set_motion_type)(sk_jolt_body_t* body, sk_jolt_motion_type_t motion_type);

	/**
	 * Apply a force to the body at its center of mass (world units: newtons).
	 * Forces accumulate until the next physics step, then integrate into the
	 * velocity; they are consumed each step. Wakes the body up; ignored for
	 * static bodies. Direct runtime operation — never needs
	 * entity_require_update().
	 * @param body  Body handle; must be valid (created, not destroyed).
	 * @param force Force vector in newtons; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p force is NULL.
	 */
	i32 (*body_add_force)(sk_jolt_body_t* body, const sk_jolt_vec3_t* force);

	/**
	 * Apply a force at a world-space point (producing force + torque). Same
	 * contract as body_add_force.
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param force    Force vector in newtons; must not be NULL.
	 * @param position World-space point where the force is applied (meters);
	 *                 must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or any argument is NULL.
	 */
	i32 (*body_add_force_at_position)(sk_jolt_body_t* body, const sk_jolt_vec3_t* force, const sk_jolt_vec3_t* position);

	/**
	 * Apply an impulse to the body at its center of mass (world units: N·s).
	 * An impulse is an instantaneous velocity change — unlike a force it is
	 * fully applied at the moment of the call (Δv = impulse / mass) instead of
	 * accumulating over the next step. Wakes the body up; ignored for static
	 * bodies. Direct runtime operation — never needs entity_require_update().
	 * @param body    Body handle; must be valid (created, not destroyed).
	 * @param impulse Impulse vector in N·s; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p impulse is NULL.
	 */
	i32 (*body_add_impulse)(sk_jolt_body_t* body, const sk_jolt_vec3_t* impulse);

	/**
	 * Apply an impulse at a world-space point (producing impulse + angular
	 * impulse). Same contract as body_add_impulse.
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param impulse  Impulse vector in N·s; must not be NULL.
	 * @param position World-space point where the impulse is applied (meters);
	 *                 must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or any argument is NULL.
	 */
	i32 (*body_add_impulse_at_position)(sk_jolt_body_t* body, const sk_jolt_vec3_t* impulse, const sk_jolt_vec3_t* position);

	/**
	 * Apply a torque to the body (world units: N·m). Like forces, torques
	 * accumulate until the next physics step, then integrate into the angular
	 * velocity. Wakes the body up; ignored for static bodies. Direct runtime
	 * operation — never needs entity_require_update().
	 * @param body   Body handle; must be valid (created, not destroyed).
	 * @param torque Torque vector in N·m; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p torque is NULL.
	 */
	i32 (*body_add_torque)(sk_jolt_body_t* body, const sk_jolt_vec3_t* torque);

	/**
	 * Apply an instantaneous angular impulse to the body (world units: N·m·s).
	 * Fully applied at the moment of the call (Δω = impulse / inertia). Wakes
	 * the body up; ignored for static bodies. Direct runtime operation — never
	 * needs entity_require_update().
	 * @param body             Body handle; must be valid (created, not destroyed).
	 * @param angular_impulse  Angular impulse vector in N·m·s; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p angular_impulse is NULL.
	 */
	i32 (*body_add_angular_impulse)(sk_jolt_body_t* body, const sk_jolt_vec3_t* angular_impulse);

	/**
	 * Teleport the body to a new world-space position / rotation in one atomic
	 * operation (Jolt BodyInterface::SetPositionAndRotation). This is the
	 * direct runtime replacement for mutating a dynamic body's transform
	 * component: the placement is immediate and wakes the body up, so it keeps
	 * simulating from the new pose on the next step. Static / kinematic bodies
	 * are re-placed the same way. Direct runtime operation — never needs
	 * entity_require_update().
	 * @param body     Body handle; must be valid (created, not destroyed).
	 * @param position New world-space position (meters); must not be NULL.
	 * @param rotation New world-space rotation (unit quaternion); must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or any argument is NULL.
	 */
	i32 (*body_teleport)(sk_jolt_body_t* body, const sk_jolt_vec3_t* position, const sk_jolt_quat_t* rotation);

	/**
	 * Wake a body up so it resumes simulation on the next physics step (Jolt
	 * BodyInterface::ActivateBody). Sleeping bodies (see
	 * body_deactivate / SK_RIGID_BODY_FLAG_ALLOW_SLEEPING) are not integrated
	 * until activated. Activating an already-active body is a no-op; ignored
	 * for static bodies (they never simulate). Direct runtime operation — never
	 * needs entity_require_update().
	 * @param body Body handle; must be valid (created, not destroyed).
	 * @return 0 on success; non-zero when the world is not initialized or the
	 *         handle is NULL / destroyed.
	 */
	i32 (*body_activate)(sk_jolt_body_t* body);

	/**
	 * Put a body to sleep so it stops simulating (Jolt
	 * BodyInterface::DeactivateBody): its velocity is zeroed and it is removed
	 * from the active set until body_activate() (or a wake-inducing call like
	 * a velocity setter, force, impulse, or teleport). Useful to freeze a body
	 * at a fixed pose. Direct runtime operation — never needs
	 * entity_require_update().
	 * @param body Body handle; must be valid (created, not destroyed).
	 * @return 0 on success; non-zero when the world is not initialized or the
	 *         handle is NULL / destroyed.
	 */
	i32 (*body_deactivate)(sk_jolt_body_t* body);

	/**
	 * Query whether a body is currently actively simulating (Jolt
	 * BodyInterface::IsActive): 1 while awake, 0 while asleep (deactivated or
	 * resting). Static bodies always report 0. Direct runtime query — never
	 * needs entity_require_update().
	 * @param body       Body handle; must be valid (created, not destroyed).
	 * @param out_active Output: 1 = active, 0 = inactive; must not be NULL.
	 * @return 0 on success; non-zero when the world is not initialized, the
	 *         handle is NULL / destroyed, or @p out_active is NULL (in that
	 *         case the output is zeroed when non-NULL).
	 */
	i32 (*body_is_active)(const sk_jolt_body_t* body, i32* out_active);

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

	/**
	 * Cast a ray against the world and return the closest hit. The ray is
	 * cast "from" @p object_layer: it only hits bodies on layers that
	 * collide with that layer per the filter matrix (the
	 * SK_JOLT_COLLISION_MASK_* constants — the same contract the contact
	 * filters implement), so a ray from the ghost SENSOR layer hits nothing,
	 * a ray from NON_MOVING does not hit static geometry, and a ray never
	 * hits a body on a layer it does not collide with. Convex bodies are
	 * treated as solid (a ray starting inside one reports a hit at fraction
	 * 0). Main-thread only; safe to call between steps.
	 * @param origin       Ray origin in world space (meters). Must not be NULL.
	 * @param direction    Ray direction; need not be unit length. The hit
	 *                     fraction is measured along this vector
	 *                     (hit position = origin + direction * fraction), so
	 *                     with a unit direction the fraction equals the
	 *                     distance in meters. Must be non-zero.
	 * @param max_distance Maximum ray length in meters; hits beyond it are
	 *                     ignored. Must be > 0.
	 * @param object_layer Object layer the ray is cast from (one of the
	 *                     SK_JOLT_OBJECT_LAYER_* values).
	 * @param out_hit      Output hit info; zeroed when there is no hit. Must
	 *                     not be NULL.
	 * @return 0 when a hit was found (out_hit filled), 1 when the query ran
	 *         but found nothing (out_hit zeroed), negative when the query
	 *         was invalid: world not initialized, NULL origin/direction/
	 *         out_hit, max_distance <= 0, zero-length direction, or unknown
	 *         object layer (out_hit zeroed).
	 */
	i32 (*ray_cast)(const sk_jolt_vec3_t* origin, const sk_jolt_vec3_t* direction, f32 max_distance, u32 object_layer, sk_jolt_query_hit_t* out_hit);

	/**
	 * Cast a sphere along a ray against the world and return the closest
	 * hit. Same layer-filter contract as ray_cast: the sphere is cast "from"
	 * @p object_layer and only hits bodies on layers that collide with it.
	 * The reported hit point is the contact point on the hit body's surface
	 * and the normal is the hit body's surface normal there; the fraction is
	 * measured along @p direction from the sphere's start center (the
	 * sphere's surface first touches the body at that point — with a unit
	 * direction the fraction equals the distance in meters to first
	 * contact). Main-thread only; safe to call between steps.
	 * @param radius       Sphere radius in meters. Must be > 0.
	 * @param origin       Center of the sphere at the start of the cast
	 *                     (world space, meters). Must not be NULL.
	 * @param direction    Cast direction; need not be unit length (same
	 *                     fraction convention as ray_cast). Must be non-zero.
	 * @param max_distance Maximum cast length in meters. Must be > 0.
	 * @param object_layer Object layer the cast is made from (one of the
	 *                     SK_JOLT_OBJECT_LAYER_* values).
	 * @param out_hit      Output hit info; zeroed when there is no hit. Must
	 *                     not be NULL.
	 * @return 0 hit / 1 no hit / negative invalid input (same contract and
	 *         failure modes as ray_cast).
	 */
	i32 (*sphere_cast)(f32 radius, const sk_jolt_vec3_t* origin, const sk_jolt_vec3_t* direction, f32 max_distance, u32 object_layer, sk_jolt_query_hit_t* out_hit);

	/**
	 * Reconcile @p world with the live Jolt scene: create a body when an
	 * entity has sk_rigid_body_config_t plus at least one collider, destroy
	 * the body when the entity dies or those components are removed, push
	 * transform / config changes into Jolt, and rebuild the shape when
	 * collider data changes. Static and kinematic bodies follow the entity
	 * transform; dynamic teleports are pushed only when the transform
	 * differs from the last write-back. Sleeping / activation follow the
	 * config flags. The entity↔BodyID map is kept in plugin C++ state.
	 * No-op when the physics world is not initialized or @p world is NULL.
	 * Main-thread only.
	 * @param world ECS world to reconcile (must not be NULL for work to run).
	 */
	void (*sync_world)(sk_world_t* world);

	/**
	 * Write simulated pose and velocities back onto ECS entities that have
	 * a live Jolt body: position / rotation onto sk_transform_t (when
	 * present) and linear / angular velocity onto sk_rigid_body_state_t
	 * (when present). Call after step() — or use step_world(), which does
	 * this after every completed fixed step. No-op without a live world or
	 * when @p world is NULL. Main-thread only.
	 * @param world ECS world to write (must be the world last synced, or
	 *              the same world passed to step_world).
	 */
	void (*write_back)(sk_world_t* world);

	/**
	 * One host-frame physics tick against an ECS world: sync_world(@p world),
	 * then step(@p delta_time) with write_back after every completed fixed
	 * step. Main-thread only.
	 * @param world      ECS world (may be NULL: then only step() runs).
	 * @param delta_time Host frame delta in seconds.
	 */
	void (*step_world)(sk_world_t* world, f32 delta_time);

	/**
	 * Mark an entity's physics state dirty after mutating its cold / authored
	 * component data, and have the sync consume that dirty set on the next
	 * step.
	 *
	 * ECS components are plain data (POD structs) with no setter hooks, so
	 * nothing inside them can trigger a physics update automatically. Gameplay
	 * code that mutates the rigid-body config component (motion type, mass,
	 * friction, restitution, linear/angular damping, gravity factor, object
	 * layer, flags) or a collider shape component (box half extent, sphere
	 * radius, capsule half height / radius) of an entity that already has a
	 * live Jolt body MUST call this afterwards: the next sync_world() /
	 * step_world() then rebuilds or reconfigures the body from the new values.
	 * Without the call those mutations are silently ignored — the body keeps
	 * its previous configuration (verified: a mutated field without
	 * entity_require_update leaves the body unchanged).
	 *
	 * NOT required (the sync handles these automatically):
	 *   - spawning / despawning the entity or adding / removing components
	 *     (structural changes are detected by the sync itself),
	 *   - mutating the transform component of a static / kinematic body, which
	 *     is followed every step (the hot per-frame pose channel),
	 *   - mutating the rigid-body state component (linear / angular velocity),
	 *     the hot per-frame velocity channel,
	 *   - any body_* direct runtime operation (velocity get/set, forces /
	 *     impulses / torques, body_teleport, body_activate / body_deactivate,
	 *     body_set_motion_type) — these act on the live Jolt body immediately
	 *     and never go through component mutation.
	 *
	 * Marking the same entity repeatedly before a sync is harmless (dirty is a
	 * set); the set is consumed and cleared by the next sync. The entity must
	 * be alive and carry a live Jolt body; otherwise the call is a no-op.
	 * Main-thread only.
	 * @param world  ECS world the entity belongs to (must not be NULL for work
	 *               to run; the world last synced).
	 * @param entity Entity whose rigid-body / collider components changed.
	 */
	void (*entity_require_update)(sk_world_t* world, sk_entity_t entity);
} sk_jolt_api_t;

#ifdef __cplusplus
}
#endif
