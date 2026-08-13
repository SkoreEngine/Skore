/**
 * @file jolt.cpp
 * @brief Physics module implementation (C++ behind the C-only jolt.h surface).
 *
 * Implements the physics world lifecycle and fixed-step simulation on the
 * vendored Jolt (v5.6.0), structurally following SkoreEngine/Skore's
 * Runtime/Source/Skore/Scene/Physics.cpp:
 *
 *   init:     RegisterDefaultAllocator → new Factory → RegisterTypes →
 *             TempAllocatorImpl (10 MiB) → JobSystemThreadPool →
 *             BroadPhaseLayerInterface / ObjectVsBroadPhaseLayerFilter /
 *             ObjectLayerPairFilter → PhysicsSystem::Init (sized from
 *             sk_jolt_settings_t) → SetGravity.
 *   step:     fixed-timestep accumulator; each drained step runs
 *             PhysicsSystem::Update(dt, substeps, tempAllocator, jobSystem)
 *             and then invokes the host step callback once.
 *   shutdown: destroy the world (reverse construction order: PhysicsSystem →
 *             filters → job system threads → temp allocator), then
 *             UnregisterTypes → delete Factory. No leaks: every Jolt object
 *             is owned by the module and released here, so the engine can
 *             start/stop repeatedly with the plugin enabled.
 *
 * A CollisionListener is deliberately NOT implemented (explicitly out of
 * scope for APX-305). Rigid bodies are implemented (APX-320): body_create()
 * builds a Jolt body from the box/sphere/capsule shape descriptions with a
 * static/kinematic/dynamic motion type, body_destroy() removes and destroys
 * it, and the transform / velocity accessors round-trip position, rotation,
 * linear velocity and angular velocity through POD structs (sk_jolt_vec3_t /
 * sk_jolt_quat_t, layout-identical to the core math3d types). Every body
 * handle is validated against the live-handle registry, so use-after-destroy
 * (and use after world shutdown) returns an error code instead of crashing;
 * handle records are module-owned and released at shutdown. Character
 * controllers remain empty stubs until a later stage.
 *
 * Unit tests live in jolt_tests.c (C, like every other plugin's tests); this
 * TU only exposes SK_TESTS-only accessors for them.
 */

#include "jolt.h"

#include "app.h"
#include "common.h"
#include "logger.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <thread>
#include <unordered_set>
#include <vector>

/* Defined in jolt_components.c (C TU of this plugin): registers the physics
 * ECS components with the entities API resolved from the app registry.
 * Idempotent; no-op until the entities plugin has registered its table, so it
 * may be retried on later API calls (out-of-order plugin loads). */
extern "C" void sk_jolt_components_register_all(sk_app_context_t* context, const sk_app_api_t* app_api);

/* Host app registry cached at plugin load so in-plugin tests (and later the
 * integration) can resolve this plugin's own table. */
static sk_app_context_t* g_jolt_app_context = nullptr;
static const sk_app_api_t* g_jolt_app_api = nullptr;

namespace {

/* ---- world constants (mirror SkoreEngine/Skore + Jolt HelloWorld) ---- */

/** TempAllocator arena size: 10 MiB (same as the reference PhysicsScene). */
constexpr JPH::uint kTempAllocatorSize = 10u * 1024u * 1024u;

/** Non-moving geometry lands in broad-phase layer 0, moving bodies in 1. */
constexpr JPH::BroadPhaseLayer kBroadPhaseNonMoving(0);
constexpr JPH::BroadPhaseLayer kBroadPhaseMoving(1);

/** Defaults for sk_jolt_settings_t (documented in jolt.h). */
constexpr f32 kDefaultGravity[3] = {0.0f, -9.81f, 0.0f};
constexpr f32 kDefaultFixedTimestep = 1.0f / 60.0f;
constexpr u32 kDefaultSubsteps = 1u;
constexpr u32 kDefaultMaxBodies = 65536u;
constexpr u32 kDefaultMaxBodyPairs = 65536u;
constexpr u32 kDefaultMaxConstraints = 10240u;

/**
 * Upper bound on the host frame delta fed to the accumulator (seconds).
 * A single slow frame (debugger pause, swap stall) must not queue a runaway
 * number of catch-up physics steps — the classic "spiral of death".
 */
constexpr f32 kMaxFrameDelta = 0.25f;

/** Accumulator comparison slack; absorbs f32 drift so exact multiples of the
 * fixed timestep (e.g. 60 frames of 1/60 s) drain to exactly the right
 * number of steps instead of losing a fraction of a step per frame. */
constexpr f32 kAccumulatorEpsilon = 1.0e-4f;

/* ---- object-layer / broad-phase-layer collision model ---- */

/**
 * Layer collision masks: bit N = collides with object layer N. These ARE the
 * SK_JOLT_COLLISION_MASK_* constants from jolt.h (the documented filter
 * matrix); the object-layer pair filter below is driven straight from this
 * table, so the header's matrix and the running filter can never drift
 * apart. The matrix is symmetric, so every mask pair agrees in both
 * directions.
 */
constexpr u32 kLayerCollisionMasks[SK_JOLT_OBJECT_LAYER_COUNT] = {
	SK_JOLT_COLLISION_MASK_NON_MOVING,
	SK_JOLT_COLLISION_MASK_MOVING,
	SK_JOLT_COLLISION_MASK_SENSOR,
};

/**
 * Maps the object layers onto the two 8-bit broad-phase layers: static
 * geometry lives in its own layer (0); every moving object layer (MOVING
 * bodies and SENSOR ghosts) shares the moving layer (1), so moving-vs-static
 * pairs still get broad-phase candidates.
 */
class BroadPhaseLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface {
public:
	JPH::uint GetNumBroadPhaseLayers() const override {
		return SK_JOLT_BROAD_PHASE_LAYER_COUNT;
	}

	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
		switch (inLayer) {
		case SK_JOLT_OBJECT_LAYER_MOVING:
		case SK_JOLT_OBJECT_LAYER_SENSOR:
			return kBroadPhaseMoving;
		default: /* NON_MOVING (and unknown layers; body_create validates) */
			return kBroadPhaseNonMoving;
		}
	}
};

/**
 * Object layer vs broad-phase layer: mirrors the collision matrix so the
 * coarse broad-phase pass never admits a pair the object-layer pair filter
 * rejects. Concretely:
 *   - NON_MOVING (static geometry) only reaches the MOVING broad-phase
 *     layer (static-vs-static pairs never collide);
 *   - MOVING reaches both broad-phase layers (static + moving);
 *   - SENSOR reaches neither (ghost bodies pass through everything).
 */
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
	bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
		switch (inLayer1) {
		case SK_JOLT_OBJECT_LAYER_MOVING:
			return true;
		case SK_JOLT_OBJECT_LAYER_NON_MOVING:
			return inLayer2 == kBroadPhaseMoving;
		default: /* SENSOR (and unknown layers): ghost, no broad-phase pairs */
			return false;
		}
	}
};

/**
 * Object layer pair filter: a body on layer A collides with layer B iff
 * both collision masks agree — bit B set in mask A and bit A set in mask B.
 * Implemented straight from the SK_JOLT_COLLISION_MASK_* constants (see
 * kLayerCollisionMasks), so the matrix documented in jolt.h is the single
 * source of truth:
 *
 *                     NON_MOVING   MOVING   SENSOR
 *   NON_MOVING            no         yes      no
 *   MOVING                yes        yes      no
 *   SENSOR                no         no       no
 */
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
	bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
		if (inObject1 >= SK_JOLT_OBJECT_LAYER_COUNT || inObject2 >= SK_JOLT_OBJECT_LAYER_COUNT) {
			return false;
		}
		return (kLayerCollisionMasks[inObject1] & (1u << inObject2)) != 0u && (kLayerCollisionMasks[inObject2] & (1u << inObject1)) != 0u;
	}
};

/* ---- the live world ---- */

/**
 * Owns every per-world Jolt object. Member order is deliberate: the filters
 * are declared before the PhysicsSystem (which references them and must be
 * destroyed first), and the job system / temp allocator are declared first so
 * they outlive the system they feed. C++ destroys members in reverse
 * declaration order, so `delete world` tears down in exactly the right
 * sequence.
 */
struct JoltWorld {
	JPH::TempAllocatorImpl temp_allocator;
	JPH::JobSystemThreadPool job_system;
	BroadPhaseLayerInterfaceImpl broad_phase;
	ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase;
	ObjectLayerPairFilterImpl object_pair_filter;
	JPH::PhysicsSystem physics_system;

	/* Fixed-step driver state (main-thread only). */
	f32 fixed_timestep;
	u32 substeps;
	f32 accumulator;
	f64 physics_time;
	u64 step_index;

	explicit JoltWorld(const sk_jolt_settings_t& settings, int thread_count)
		: temp_allocator(kTempAllocatorSize), job_system(static_cast<JPH::uint>(JPH::cMaxPhysicsJobs), static_cast<JPH::uint>(JPH::cMaxPhysicsBarriers), thread_count),
		  fixed_timestep(settings.fixed_timestep), substeps(settings.substeps), accumulator(0.0f), physics_time(0.0), step_index(0u) {
		physics_system.Init(settings.max_bodies, 0u, /* inNumBodyMutexes: 0 = auto-detect */
							settings.max_body_pairs, settings.max_constraints, broad_phase, object_vs_broad_phase, object_pair_filter);
		physics_system.SetGravity(JPH::Vec3(settings.gravity[0], settings.gravity[1], settings.gravity[2]));
	}
};

/** The live world, or nullptr between init/shutdown cycles. */
JoltWorld* g_jolt_world = nullptr;

/** Plugin logger (created at init, destroyed at shutdown; NULL on failure). */
sk_logger_t* g_jolt_log = nullptr;

/* ---- rigid-body handle registry ---- */

/**
 * Opaque body handle record: the Jolt BodyID (index + sequence) that names
 * the body inside the physics system. The public sk_jolt_body_t* handed to
 * hosts IS the address of this record (the struct itself is never defined on
 * the C side).
 *
 * Validation model (see jolt.h "Body handles"): g_live_bodies holds the
 * public pointer VALUES of every live body of the current world. Accessors
 * validate by set membership — comparing pointer values, never dereferencing
 * the record — and short-circuit when the world is not initialized, so a
 * stale handle (destroyed, double-destroyed, or left over from a previous
 * world) is rejected without ever reading freed memory. Records themselves
 * are freed only at world shutdown, so within a world a destroyed handle can
 * never be aliased by a later body, and the whole registry is released by
 * shutdown() (the module does not leak across init/shutdown cycles).
 */
struct JoltBodyHandle {
	JPH::BodyID body_id = JPH::BodyID();
};

/** Handle records of the current world (live + destroyed); freed at shutdown. */
std::vector<JoltBodyHandle*> g_body_handles;

/** Public pointer values of the live bodies of the current world. */
std::unordered_set<sk_jolt_body_t*> g_live_bodies;

/**
 * Resolve a public handle to its record, or nullptr when the handle is NULL,
 * already destroyed, or the world is not initialized. The membership check
 * compares pointer values only and never dereferences a non-live handle, so
 * this is safe to call with any stale pointer.
 */
JoltBodyHandle* jolt_body_handle(sk_jolt_body_t* body) noexcept {
	if (g_jolt_world == nullptr || body == nullptr) {
		return nullptr;
	}
	if (g_live_bodies.find(body) == g_live_bodies.end()) {
		return nullptr;
	}
	return reinterpret_cast<JoltBodyHandle*>(body);
}

/** Release every body record of the current world (world must be gone). */
void jolt_body_registry_clear() noexcept {
	for (JoltBodyHandle* record : g_body_handles) {
		delete record;
	}
	g_body_handles.clear();
	g_live_bodies.clear();
}

/* ---- settings helpers ---- */

void jolt_settings_defaults(sk_jolt_settings_t* out) {
	out->gravity[0] = kDefaultGravity[0];
	out->gravity[1] = kDefaultGravity[1];
	out->gravity[2] = kDefaultGravity[2];
	out->fixed_timestep = kDefaultFixedTimestep;
	out->substeps = kDefaultSubsteps;
	out->max_bodies = kDefaultMaxBodies;
	out->max_body_pairs = kDefaultMaxBodyPairs;
	out->max_constraints = kDefaultMaxConstraints;
}

/**
 * Resolve the effective settings: start from the defaults, then honor the
 * caller's struct. Gravity is used as given (all-zero is legal); the numeric
 * caps fall back to defaults when the caller passed zero.
 */
void jolt_settings_resolve(const sk_jolt_settings_t* in, sk_jolt_settings_t* out) {
	jolt_settings_defaults(out);
	if (in == nullptr) {
		return;
	}
	if (in->fixed_timestep > 0.0f) {
		out->fixed_timestep = in->fixed_timestep;
	}
	if (in->substeps > 0u) {
		out->substeps = in->substeps;
	}
	if (in->max_bodies > 0u) {
		out->max_bodies = in->max_bodies;
	}
	if (in->max_body_pairs > 0u) {
		out->max_body_pairs = in->max_body_pairs;
	}
	if (in->max_constraints > 0u) {
		out->max_constraints = in->max_constraints;
	}
	out->gravity[0] = in->gravity[0];
	out->gravity[1] = in->gravity[1];
	out->gravity[2] = in->gravity[2];
}

/** Worker threads for the job system: all cores minus one (min 1). */
int jolt_thread_count() {
	unsigned int cores = std::thread::hardware_concurrency();
	return (cores > 1u) ? static_cast<int>(cores) - 1 : 1;
}

/* ---- module lifecycle ---- */

void jolt_shutdown_impl() noexcept {
	if (g_jolt_world != nullptr) {
		delete g_jolt_world; /* PhysicsSystem → filters → job threads → temp allocator */
		g_jolt_world = nullptr;
	}
	/* Body handles die with their world: the world is gone, so every handle
	 * record is released here. Stale handles are still rejected afterwards —
	 * accessors short-circuit on the null world before touching a record, and
	 * after a later init a pointer from a previous world is not in the new
	 * live-body set, so it fails validation without dereferencing anything. */
	jolt_body_registry_clear();
	if (JPH::Factory::sInstance != nullptr) {
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}
	if (g_jolt_log != nullptr) {
		sk_logger_api()->destroy_logger(g_jolt_log);
		g_jolt_log = nullptr;
	}
}

i32 jolt_init_impl(const sk_jolt_settings_t* settings) noexcept {
	try {
		/* Re-entrant: replace any live world (also cleans a half-built state). */
		jolt_shutdown_impl();

		/* Physics components register with the ECS once the entities plugin is
		 * loaded; retry here for out-of-order plugin load orders. */
		sk_jolt_components_register_all(g_jolt_app_context, g_jolt_app_api);

		sk_jolt_settings_t resolved;
		jolt_settings_resolve(settings, &resolved);

		g_jolt_log = sk_logger_api()->create_logger("jolt");

		/* Process-wide Jolt setup: allocator first (Jolt classes route new/
		 * delete through JPH::Allocate, which is null until registered). */
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		g_jolt_world = new JoltWorld(resolved, jolt_thread_count());

		if (g_jolt_log != nullptr) {
			sk_log_info(sk_logger_api(), g_jolt_log, "jolt world init: gravity=(%.2f, %.2f, %.2f) dt=%.6f substeps=%u bodies=%u pairs=%u constraints=%u",
						(double)resolved.gravity[0], (double)resolved.gravity[1], (double)resolved.gravity[2], (double)resolved.fixed_timestep, resolved.substeps,
						resolved.max_bodies, resolved.max_body_pairs, resolved.max_constraints);
		}
		return 0;
	} catch (...) {
		/* Allocation failure mid-init: leave the module fully shut down. */
		jolt_shutdown_impl();
		return -1;
	}
}

/* ---- runtime configuration ---- */

void jolt_set_gravity_impl(f32 x, f32 y, f32 z) noexcept {
	if (g_jolt_world != nullptr) {
		g_jolt_world->physics_system.SetGravity(JPH::Vec3(x, y, z));
	}
}

void jolt_get_gravity_impl(f32* out_x, f32* out_y, f32* out_z) noexcept {
	JPH::Vec3 g = (g_jolt_world != nullptr) ? g_jolt_world->physics_system.GetGravity() : JPH::Vec3(0.0f, 0.0f, 0.0f);
	if (out_x != nullptr) {
		*out_x = g.GetX();
	}
	if (out_y != nullptr) {
		*out_y = g.GetY();
	}
	if (out_z != nullptr) {
		*out_z = g.GetZ();
	}
}

i32 jolt_set_fixed_timestep_impl(f32 step) noexcept {
	if (g_jolt_world == nullptr || !(step > 0.0f)) {
		return -1;
	}
	g_jolt_world->fixed_timestep = step;
	/* Drain the accumulator so a rate change applies immediately instead of
	 * bursting through the leftover catch-up steps at the old rate. */
	g_jolt_world->accumulator = 0.0f;
	return 0;
}

f32 jolt_get_fixed_timestep_impl() noexcept {
	return (g_jolt_world != nullptr) ? g_jolt_world->fixed_timestep : 0.0f;
}

i32 jolt_set_substeps_impl(u32 count) noexcept {
	if (g_jolt_world == nullptr || count == 0u) {
		return -1;
	}
	g_jolt_world->substeps = count;
	return 0;
}

u32 jolt_get_substeps_impl() noexcept {
	return (g_jolt_world != nullptr) ? g_jolt_world->substeps : 0u;
}

/* ---- fixed-step simulation ---- */

void jolt_step_impl(f32 delta_time, sk_jolt_step_callback_fn callback, void_ptr_t user_data) noexcept {
	JoltWorld* world = g_jolt_world;
	if (world == nullptr || !(delta_time > 0.0f)) {
		return;
	}

	const f32 dt = (delta_time > kMaxFrameDelta) ? kMaxFrameDelta : delta_time;
	world->accumulator += dt;

	while (world->accumulator + kAccumulatorEpsilon >= world->fixed_timestep) {
		const JPH::EPhysicsUpdateError error = world->physics_system.Update(world->fixed_timestep, static_cast<int>(world->substeps), &world->temp_allocator, &world->job_system);
		if (error != JPH::EPhysicsUpdateError::None && g_jolt_log != nullptr) {
			sk_log_warn(sk_logger_api(), g_jolt_log, "physics update error 0x%x (increase max_body_pairs / max_constraints)", static_cast<unsigned>(error));
		}

		world->accumulator -= world->fixed_timestep;
		world->physics_time += static_cast<f64>(world->fixed_timestep);
		world->step_index += 1u;

		if (callback != nullptr) {
			callback(user_data, world->physics_time, world->step_index);
		}
	}
}

/* ---- rigid bodies ---- */

sk_jolt_body_t* jolt_body_create_impl(const sk_jolt_shape_desc_t* shape, sk_jolt_motion_type_t motion_type, u32 object_layer) noexcept {
	if (g_jolt_world == nullptr || shape == nullptr) {
		return nullptr;
	}
	if (motion_type != SK_JOLT_MOTION_TYPE_STATIC && motion_type != SK_JOLT_MOTION_TYPE_KINEMATIC && motion_type != SK_JOLT_MOTION_TYPE_DYNAMIC) {
		if (g_jolt_log != nullptr) {
			sk_log_warn(sk_logger_api(), g_jolt_log, "body create failed: invalid motion type %u", static_cast<unsigned>(motion_type));
		}
		return nullptr;
	}
	/* The object layer must be one of the documented layers (the pair and
	 * broad-phase filters index their mask tables by it). */
	if (object_layer >= SK_JOLT_OBJECT_LAYER_COUNT) {
		if (g_jolt_log != nullptr) {
			sk_log_warn(sk_logger_api(), g_jolt_log, "body create failed: invalid object layer %u", object_layer);
		}
		return nullptr;
	}

	/* Build the shape from the tagged description (kind selects the union
	 * member); a failed Create() (invalid dimensions) fails creation. */
	JPH::ShapeSettings::ShapeResult shape_result;
	switch (shape->kind) {
	case SK_JOLT_SHAPE_BOX: {
		const JPH::BoxShapeSettings settings(JPH::Vec3(shape->shape.box.half_extent[0], shape->shape.box.half_extent[1], shape->shape.box.half_extent[2]));
		shape_result = settings.Create();
		break;
	}
	case SK_JOLT_SHAPE_SPHERE: {
		const JPH::SphereShapeSettings settings(shape->shape.sphere.radius);
		shape_result = settings.Create();
		break;
	}
	case SK_JOLT_SHAPE_CAPSULE: {
		const JPH::CapsuleShapeSettings settings(shape->shape.capsule.half_height, shape->shape.capsule.radius);
		shape_result = settings.Create();
		break;
	}
	default:
		if (g_jolt_log != nullptr) {
			sk_log_warn(sk_logger_api(), g_jolt_log, "body create failed: unknown shape kind %d", static_cast<int>(shape->kind));
		}
		return nullptr;
	}
	if (!shape_result.IsValid()) {
		if (g_jolt_log != nullptr) {
			sk_log_warn(sk_logger_api(), g_jolt_log, "body create failed: shape error: %s", shape_result.GetError().c_str());
		}
		return nullptr;
	}

	/* Bodies start at the origin with identity rotation and zero velocity;
	 * hosts place them with the body_set_* accessors before stepping. The
	 * material/motion properties use the Jolt BodyCreationSettings defaults
	 * (friction 0.2, restitution 0.0, damping 0.05 each, gravity factor 1.0,
	 * sleeping allowed). */
	const JPH::BodyCreationSettings creation(shape_result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), static_cast<JPH::EMotionType>(motion_type),
											 static_cast<JPH::ObjectLayer>(object_layer));
	const JPH::BodyID id = g_jolt_world->physics_system.GetBodyInterface().CreateAndAddBody(creation, JPH::EActivation::Activate);
	if (id.IsInvalid()) {
		if (g_jolt_log != nullptr) {
			sk_log_warn(sk_logger_api(), g_jolt_log, "body create failed: no free body slots (max_bodies=%u)", g_jolt_world->physics_system.GetMaxBodies());
		}
		return nullptr;
	}

	auto* record = new JoltBodyHandle{id};
	g_body_handles.push_back(record);
	sk_jolt_body_t* handle = reinterpret_cast<sk_jolt_body_t*>(record);
	g_live_bodies.insert(handle);
	return handle;
}

void jolt_body_destroy_impl(sk_jolt_body_t* body) noexcept {
	/* NULL and already-destroyed handles are no-ops. After world shutdown the
	 * registry is gone, so nothing to do (the world already freed the body). */
	if (g_jolt_world == nullptr || body == nullptr || g_live_bodies.find(body) == g_live_bodies.end()) {
		return;
	}
	auto* record = reinterpret_cast<JoltBodyHandle*>(body);
	JPH::BodyInterface& body_interface = g_jolt_world->physics_system.GetBodyInterface();
	/* Jolt requires the body to leave the broad phase (and deactivate) before
	 * it can be destroyed: RemoveBody → DestroyBody, the canonical teardown. */
	body_interface.RemoveBody(record->body_id);
	body_interface.DestroyBody(record->body_id);
	g_live_bodies.erase(body);
}

/* Body accessors: every entry validates the handle first (returns an error
 * for NULL / destroyed handles and when the world is down) and zeroes its
 * output on failure. The Jolt BodyInterface calls used here are all safe for
 * static bodies (velocity getters report zero, velocity setters are ignored,
 * position/rotation setters move the body and update the broad phase). */

i32 jolt_body_get_position_impl(const sk_jolt_body_t* body, sk_jolt_vec3_t* out_position) noexcept {
	if (out_position != nullptr) {
		out_position->x = 0.0f;
		out_position->y = 0.0f;
		out_position->z = 0.0f;
	}
	JoltBodyHandle* record = jolt_body_handle(const_cast<sk_jolt_body_t*>(body));
	if (record == nullptr || out_position == nullptr) {
		return -1;
	}
	const JPH::RVec3 position = g_jolt_world->physics_system.GetBodyInterface().GetPosition(record->body_id);
	out_position->x = position.GetX();
	out_position->y = position.GetY();
	out_position->z = position.GetZ();
	return 0;
}

i32 jolt_body_set_position_impl(sk_jolt_body_t* body, const sk_jolt_vec3_t* position) noexcept {
	JoltBodyHandle* record = jolt_body_handle(body);
	if (record == nullptr || position == nullptr) {
		return -1;
	}
	g_jolt_world->physics_system.GetBodyInterface().SetPosition(record->body_id, JPH::RVec3(position->x, position->y, position->z), JPH::EActivation::Activate);
	return 0;
}

i32 jolt_body_get_rotation_impl(const sk_jolt_body_t* body, sk_jolt_quat_t* out_rotation) noexcept {
	if (out_rotation != nullptr) {
		out_rotation->x = 0.0f;
		out_rotation->y = 0.0f;
		out_rotation->z = 0.0f;
		out_rotation->w = 1.0f;
	}
	JoltBodyHandle* record = jolt_body_handle(const_cast<sk_jolt_body_t*>(body));
	if (record == nullptr || out_rotation == nullptr) {
		return -1;
	}
	const JPH::Quat rotation = g_jolt_world->physics_system.GetBodyInterface().GetRotation(record->body_id);
	out_rotation->x = rotation.GetX();
	out_rotation->y = rotation.GetY();
	out_rotation->z = rotation.GetZ();
	out_rotation->w = rotation.GetW();
	return 0;
}

i32 jolt_body_set_rotation_impl(sk_jolt_body_t* body, const sk_jolt_quat_t* rotation) noexcept {
	JoltBodyHandle* record = jolt_body_handle(body);
	if (record == nullptr || rotation == nullptr) {
		return -1;
	}
	g_jolt_world->physics_system.GetBodyInterface().SetRotation(record->body_id, JPH::Quat(rotation->x, rotation->y, rotation->z, rotation->w), JPH::EActivation::Activate);
	return 0;
}

i32 jolt_body_get_linear_velocity_impl(const sk_jolt_body_t* body, sk_jolt_vec3_t* out_velocity) noexcept {
	if (out_velocity != nullptr) {
		out_velocity->x = 0.0f;
		out_velocity->y = 0.0f;
		out_velocity->z = 0.0f;
	}
	JoltBodyHandle* record = jolt_body_handle(const_cast<sk_jolt_body_t*>(body));
	if (record == nullptr || out_velocity == nullptr) {
		return -1;
	}
	const JPH::Vec3 velocity = g_jolt_world->physics_system.GetBodyInterface().GetLinearVelocity(record->body_id);
	out_velocity->x = velocity.GetX();
	out_velocity->y = velocity.GetY();
	out_velocity->z = velocity.GetZ();
	return 0;
}

i32 jolt_body_set_linear_velocity_impl(sk_jolt_body_t* body, const sk_jolt_vec3_t* velocity) noexcept {
	JoltBodyHandle* record = jolt_body_handle(body);
	if (record == nullptr || velocity == nullptr) {
		return -1;
	}
	g_jolt_world->physics_system.GetBodyInterface().SetLinearVelocity(record->body_id, JPH::Vec3(velocity->x, velocity->y, velocity->z));
	return 0;
}

i32 jolt_body_get_angular_velocity_impl(const sk_jolt_body_t* body, sk_jolt_vec3_t* out_velocity) noexcept {
	if (out_velocity != nullptr) {
		out_velocity->x = 0.0f;
		out_velocity->y = 0.0f;
		out_velocity->z = 0.0f;
	}
	JoltBodyHandle* record = jolt_body_handle(const_cast<sk_jolt_body_t*>(body));
	if (record == nullptr || out_velocity == nullptr) {
		return -1;
	}
	const JPH::Vec3 velocity = g_jolt_world->physics_system.GetBodyInterface().GetAngularVelocity(record->body_id);
	out_velocity->x = velocity.GetX();
	out_velocity->y = velocity.GetY();
	out_velocity->z = velocity.GetZ();
	return 0;
}

i32 jolt_body_set_angular_velocity_impl(sk_jolt_body_t* body, const sk_jolt_vec3_t* velocity) noexcept {
	JoltBodyHandle* record = jolt_body_handle(body);
	if (record == nullptr || velocity == nullptr) {
		return -1;
	}
	g_jolt_world->physics_system.GetBodyInterface().SetAngularVelocity(record->body_id, JPH::Vec3(velocity->x, velocity->y, velocity->z));
	return 0;
}

/* ---- character stubs (not implemented at this stage) ---- */

sk_jolt_character_t* jolt_character_create_impl(const sk_jolt_shape_desc_t*, u32) noexcept {
	return nullptr;
}

void jolt_character_destroy_impl(sk_jolt_character_t*) noexcept {}

/* ---- API table (table-only; no public free-function mirrors) ---- */

const sk_jolt_api_t jolt_api = {
	jolt_init_impl,
	jolt_shutdown_impl,
	jolt_settings_defaults,
	jolt_set_gravity_impl,
	jolt_get_gravity_impl,
	jolt_set_fixed_timestep_impl,
	jolt_get_fixed_timestep_impl,
	jolt_set_substeps_impl,
	jolt_get_substeps_impl,
	jolt_step_impl,
	jolt_body_create_impl,
	jolt_body_destroy_impl,
	jolt_body_get_position_impl,
	jolt_body_set_position_impl,
	jolt_body_get_rotation_impl,
	jolt_body_set_rotation_impl,
	jolt_body_get_linear_velocity_impl,
	jolt_body_set_linear_velocity_impl,
	jolt_body_get_angular_velocity_impl,
	jolt_body_set_angular_velocity_impl,
	jolt_character_create_impl,
	jolt_character_destroy_impl,
};

} /* namespace */

/**
 * Register the physics API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_jolt_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_jolt_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	g_jolt_app_context = context;
	g_jolt_app_api = app_api;
	app_api->set_api(context, SK_JOLT_API_TYPE_ID, &jolt_api);
	sk_jolt_components_register_all(context, app_api);
}

/* ---- compile-time checks on the C boundary (all builds) ---- */

/* The public surface must stay C-compatible (POD, no C++ machinery): a C
 * translation unit includes jolt.h, so the structs must be trivial and
 * standard-layout for a C++ host to pass them to the C table, and the motion
 * type must stay a plain C enum. */
static_assert(std::is_trivial<sk_jolt_shape_desc_t>::value && std::is_standard_layout<sk_jolt_shape_desc_t>::value);
static_assert(std::is_trivial<sk_jolt_settings_t>::value && std::is_standard_layout<sk_jolt_settings_t>::value);
/* The transform accessors cross the C boundary through these POD structs, so
 * they must stay trivial/standard-layout for a C host. (sk_jolt_vec3_t /
 * sk_jolt_quat_t carry the exact field layout of core/math3d.h sk_vec3_t /
 * sk_quat_t by construction — same fields, same order, same types.) */
static_assert(std::is_trivial<sk_jolt_vec3_t>::value && std::is_standard_layout<sk_jolt_vec3_t>::value);
static_assert(std::is_trivial<sk_jolt_quat_t>::value && std::is_standard_layout<sk_jolt_quat_t>::value);
static_assert(std::is_enum<sk_jolt_motion_type_t>::value && std::is_trivial<sk_jolt_motion_type_t>::value);

/* Motion type values mirror JPH::EMotionType; layer/group constants mirror
 * the vendored Jolt defaults so later integration maps 1:1. */
static_assert((int)SK_JOLT_MOTION_TYPE_STATIC == 0 && (int)SK_JOLT_MOTION_TYPE_KINEMATIC == 1 && (int)SK_JOLT_MOTION_TYPE_DYNAMIC == 2);
static_assert(SK_JOLT_OBJECT_LAYER_NON_MOVING == 0u && SK_JOLT_OBJECT_LAYER_MOVING == 1u && SK_JOLT_OBJECT_LAYER_SENSOR == 2u);
static_assert(SK_JOLT_OBJECT_LAYER_COUNT == 3u);
static_assert(SK_JOLT_OBJECT_LAYER_INVALID == 0xFFFFu);
/* The per-layer collision masks must stay the bits of the layers they
 * collide with (the pair filter is driven from them; see the header
 * matrix). The matrix is symmetric, so each mask pair agrees both ways. */
static_assert(SK_JOLT_COLLISION_MASK_NON_MOVING == (1u << SK_JOLT_OBJECT_LAYER_MOVING));
static_assert(SK_JOLT_COLLISION_MASK_MOVING == ((1u << SK_JOLT_OBJECT_LAYER_NON_MOVING) | (1u << SK_JOLT_OBJECT_LAYER_MOVING)));
static_assert(SK_JOLT_COLLISION_MASK_SENSOR == 0u);
static_assert((SK_JOLT_COLLISION_MASK_NON_MOVING & (1u << SK_JOLT_OBJECT_LAYER_NON_MOVING)) == 0u);
static_assert((SK_JOLT_COLLISION_MASK_MOVING & (1u << SK_JOLT_OBJECT_LAYER_SENSOR)) == 0u);
static_assert((SK_JOLT_COLLISION_MASK_SENSOR & SK_JOLT_COLLISION_MASK_MOVING) == 0u);
static_assert(SK_JOLT_COLLISION_GROUP_INVALID == 0xFFFFFFFFu && SK_JOLT_COLLISION_MASK_ALL == 0xFFFFFFFFu);

#ifdef SK_TESTS
/* Test-only hook (SK_TESTS plugin builds only; never in a public header):
 * hands the plugin-local C test TU (jolt_tests.c) the app registry cached at
 * plugin load and this plugin's registered table, so the tests can verify the
 * registration landed and exercise the world lifecycle / fixed-step driver.
 * extern "C" keeps the names unmangled for the C caller within the same
 * shared library. */
extern "C" void sk_jolt_test_context(sk_app_context_t** out_context, const sk_app_api_t** out_app_api);
extern "C" const sk_jolt_api_t* sk_jolt_test_api(void);

extern "C" void sk_jolt_test_context(sk_app_context_t** out_context, const sk_app_api_t** out_app_api) {
	*out_context = g_jolt_app_context;
	*out_app_api = g_jolt_app_api;
}

extern "C" const sk_jolt_api_t* sk_jolt_test_api(void) {
	return &jolt_api;
}
#endif /* SK_TESTS */
