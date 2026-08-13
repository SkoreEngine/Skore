/**
 * @file physics_scene.c
 * @brief Runnable end-to-end physics validation scene (sk-physics-scene, APX-310).
 *
 * Builds a small physics scene and simulates it through the sk-jolt plugin
 * using exactly the engine's own integration path: sk_app_init auto-loads the
 * sk-entities + sk-jolt plugins, app_physics_startup creates the app-owned ECS
 * scene world and initializes the jolt world with the default settings, and the
 * scene drives it with jolt->step_world(world, 1/60) per frame — the same call
 * sk_app_tick's engine-systems hook makes (app_tick_engine_systems).
 *
 * The scene exercises every rigid-body path of the integration at once:
 *
 *   - a static ground plane (NON_MOVING box, top surface at y = 0),
 *   - a stack of dynamic boxes and spheres that fall under gravity and settle
 *     (a 3-box tower plus three spheres resting on the flat floor, all
 *     spawned ~3 m up so the fall is visible),
 *   - a kinematic platform (MOVING box) that moves vertically while carrying a
 *     dynamic rider box on top,
 *   - a character controller (CharacterVirtual capsule) that walks +X over a
 *     low terrain step and up a three-step stair of the configured step
 *     height, then keeps walking on the flat ground.
 *
 * After the run the tool prints the observed behavior and exits non-zero when
 * an expectation fails, so the target doubles as a CTest smoke test:
 *
 *   - stable resting: every stack body stops moving (asleep) and holds its
 *     pose — residual drift over the last seconds below 1 mm, residual
 *     velocity ≈ 0,
 *   - no jitter: settled poses do not oscillate (drift check above),
 *   - no tunneling at the default 1/60 timestep: no stack body ever drops
 *     below the floor top, and the rider never penetrates the platform,
 *   - kinematic platform: the rider follows the platform up and down and is
 *     still resting on it at the end,
 *   - character: grounded while walking, climbs the stairs (height gain above
 *     the stair tops), keeps walking past them, stays grounded,
 *   - deterministic-enough repeat runs: the identical scene simulated twice in
 *     one process ends with final poses within a small epsilon. Each run
 *     starts from a fresh physics world (the jolt world is shut down and
 *     re-initialized between runs — reusing the live world leaves broadphase
 *     tree state from the first run behind and would perturb the result);
 *     with that, repeat runs are observed bit-exact (delta 0.0).
 *
 * Run:  ./build/bin/sk-physics-scene   (also registered with CTest)
 * The executable resolves plugins from {exe dir}/plugins, so run it from the
 * build output directory (ctest does this automatically).
 */

#include "app.h"
#include "common.h"
#include "entities.h"
#include "jolt.h"
#include "jolt_components.h"
#include "math3d.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- scene layout (meters, Jolt Y-up; the ground top surface is at y = 0) ---- */

#define SCENE_DT (1.0f / 60.0f) /* default fixed timestep */
#define SCENE_FRAMES 900u		/* 15.0 s of simulated time */
#define SCENE_BOXES 3u
#define SCENE_SPHERES 3u
#define SCENE_STAIRS 3u
#define SCENE_ENTITY_CAP 16u

#define SCENE_GROUND_HALF 50.0f
#define SCENE_GROUND_Y (-0.5f) /* box half (50, 0.5, 50) -> top at y = 0 */

#define SCENE_BOX_HALF 0.5f /* 1 m cube */
#define SCENE_SPHERE_R 0.5f
#define SCENE_TOWER_X (-8.0f)
#define SCENE_TOWER_Z (-4.0f)
#define SCENE_SPHERE_Z 4.0f

#define SCENE_PLATFORM_X 4.0f
#define SCENE_PLATFORM_Z (-6.0f)
#define SCENE_PLATFORM_HALF 1.5f
#define SCENE_PLATFORM_HALF_Y 0.25f
#define SCENE_PLATFORM_Y0 1.0f
#define SCENE_PLATFORM_AMP 0.6f
#define SCENE_PLATFORM_PERIOD 6.0f /* y(t) = Y0 + AMP * sin(2*pi*t/PERIOD) */
#define SCENE_RIDER_HALF 0.4f

#define SCENE_TERRAIN_X 6.5f
#define SCENE_TERRAIN_TOP 0.25f
#define SCENE_STAIR_X0 10.0f /* stairs walk +X: centers X0 + i*2, tops 0.35/0.70/1.05 */
#define SCENE_STAIR_PITCH 2.0f
#define SCENE_STAIR_HALF 1.0f
#define SCENE_STAIR_HALF_Z 2.0f
#define SCENE_STAIR_RISE 0.35f /* <= character step_height 0.4 */

#define SCENE_WALK_SPEED 2.5f
#define SCENE_CHARACTER_X0 0.5f

#define SCENE_MOVE_EPS 0.05f /* m/s: "not moving" threshold for settle tracking */
#define SCENE_MOVE_EPS_SQ (SCENE_MOVE_EPS * SCENE_MOVE_EPS)
#define SCENE_TUNNEL_MARGIN \
	0.05f								/* floor penetration tolerance (m): covers the transient contact
										  * overlap when a body lands from 3 m at ~7.7 m/s; tunneling (a body
										  * ending below the floor) stays far outside this */
#define SCENE_RIDER_TUNNEL_MARGIN 0.06f /* rider-vs-platform penetration tolerance (m) */
#define SCENE_DETERMINISM_EPS 1.0e-3f	/* repeat-run pose delta (m) */
#define SCENE_DRIFT_EPS 1.0e-3f			/* settled-pose drift over the last 5 s (m) */

/* ---- scene bookkeeping ---- */

typedef struct physics_scene_t {
	u32 count;
	sk_entity_t entities[SCENE_ENTITY_CAP];
	sk_entity_t ground;
	sk_entity_t terrain;
	sk_entity_t stairs[SCENE_STAIRS];
	sk_entity_t boxes[SCENE_BOXES];
	sk_entity_t spheres[SCENE_SPHERES];
	sk_entity_t platform;
	sk_entity_t rider;
	sk_entity_t character;
} physics_scene_t;

/* One final pose for the determinism comparison (9 entities: 3 boxes, 3
 * spheres, platform, rider, character). */
typedef struct scene_pose_t {
	f32 x, y, z;
	f32 rx, ry, rz, rw;
	f32 vx, vy, vz;
} scene_pose_t;

enum { SCENE_POSE_COUNT = (SCENE_BOXES + SCENE_SPHERES) + 3u }; /* stack + platform + rider + character */

typedef struct scene_observations_t {
	/* stack: settle tracking + tunneling + drift */
	u32 last_moving[SCENE_BOXES + SCENE_SPHERES]; /* last frame each stack body had |v| >= 0.05 m/s */
	u32 settle_frame;							  /* max of last_moving (all stack bodies asleep by then) */
	f32 top_y_300;								  /* top tower box y at t = 5 s */
	f32 top_y_600;								  /* top tower box y at t = 10 s */
	f32 top_y_final;							  /* top tower box y at t = 15 s */
	sk_vec3_t stack_pos_600[SCENE_BOXES + SCENE_SPHERES];
	sk_vec3_t stack_pos_final[SCENE_BOXES + SCENE_SPHERES];
	f32 max_stack_drift;	/* max |pos(t=10 s) - pos(t=15 s)| over stack bodies (m) */
	f32 max_stack_velocity; /* max |v| over stack bodies at the final frame (m/s) */
	f32 min_stack_bottom;	/* min body-bottom y over all stack bodies, all frames (m; tunneling) */
	/* platform + rider */
	f32 min_rider_clearance; /* min(platform_top - rider_bottom) over all frames (m; negative = penetration) */
	f32 platform_y_final;
	f32 rider_y_final;
	/* character */
	i32 char_grounded_90;
	i32 char_grounded_final;
	f32 char_max_y;
	f32 char_min_y;
	f32 char_final_x;
	f32 char_final_y;
	/* determinism (filled after run B) */
	f32 max_run_delta;
} scene_observations_t;

/* ---- component fill helpers (cold / authored data) ---- */

static void scene_fill_static_cfg(sk_rigid_body_config_t* cfg) {
	cfg->motion_type = SK_JOLT_MOTION_TYPE_STATIC;
	cfg->mass = 0.0f;
	cfg->friction = 0.5f;
	cfg->restitution = 0.0f;
	cfg->linear_damping = 0.0f;
	cfg->angular_damping = 0.0f;
	cfg->gravity_factor = 0.0f;
	cfg->object_layer = SK_JOLT_OBJECT_LAYER_NON_MOVING;
	cfg->flags = SK_RIGID_BODY_FLAG_NONE;
	cfg->body = NULL;
}

static void scene_fill_dynamic_cfg(sk_rigid_body_config_t* cfg) {
	cfg->motion_type = SK_JOLT_MOTION_TYPE_DYNAMIC;
	cfg->mass = 1.0f;
	cfg->friction = 0.5f;
	cfg->restitution = 0.0f;
	cfg->linear_damping = 0.05f;
	cfg->angular_damping = 0.05f;
	cfg->gravity_factor = 1.0f;
	cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
	cfg->flags = SK_RIGID_BODY_FLAG_ALLOW_SLEEPING;
	cfg->body = NULL;
}

static void scene_fill_kinematic_cfg(sk_rigid_body_config_t* cfg) {
	cfg->motion_type = SK_JOLT_MOTION_TYPE_KINEMATIC;
	cfg->mass = 0.0f;
	cfg->friction = 0.5f;
	cfg->restitution = 0.0f;
	cfg->linear_damping = 0.0f;
	cfg->angular_damping = 0.0f;
	cfg->gravity_factor = 0.0f;
	cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
	cfg->flags = SK_RIGID_BODY_FLAG_NONE;
	cfg->body = NULL;
}

/* ---- spawn helpers ---- */

static void scene_init(physics_scene_t* scene) {
	memset(scene, 0, sizeof(*scene));
}

static void scene_record(physics_scene_t* scene, sk_entity_t entity) {
	if (scene->count < SCENE_ENTITY_CAP) {
		scene->entities[scene->count] = entity;
		scene->count += 1u;
	}
}

static void scene_set_pose(const sk_entities_api_t* ecs, sk_world_t* world, sk_entity_t entity, f32 x, f32 y, f32 z) {
	sk_transform_t* xf = (sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID);
	if (xf != NULL) {
		xf->position = sk_vec3(x, y, z);
		xf->rotation = sk_quat(0.0f, 0.0f, 0.0f, 1.0f);
	}
}

static sk_entity_t scene_spawn_box(const sk_entities_api_t* ecs, sk_world_t* world, physics_scene_t* scene, sk_jolt_motion_type_t motion, f32 x, f32 y, f32 z, f32 half_x,
								   f32 half_y, f32 half_z) {
	const sk_type_id_t ids[] = {SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, SK_TRANSFORM_COMPONENT_TYPE_ID, SK_BOX_COLLIDER_COMPONENT_TYPE_ID};
	sk_entity_t entity = ecs->world_spawn(world, ids, 4u);
	if (!sk_entity_is_valid(entity)) {
		return entity;
	}
	sk_rigid_body_config_t* cfg = (sk_rigid_body_config_t*)ecs->world_component(world, entity, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	sk_box_collider_t* collider = (sk_box_collider_t*)ecs->world_component(world, entity, SK_BOX_COLLIDER_COMPONENT_TYPE_ID);
	if (cfg != NULL) {
		if (motion == SK_JOLT_MOTION_TYPE_STATIC) {
			scene_fill_static_cfg(cfg);
		} else if (motion == SK_JOLT_MOTION_TYPE_KINEMATIC) {
			scene_fill_kinematic_cfg(cfg);
		} else {
			scene_fill_dynamic_cfg(cfg);
			cfg->motion_type = motion;
		}
	}
	if (collider != NULL) {
		collider->half_extent = sk_vec3(half_x, half_y, half_z);
	}
	scene_set_pose(ecs, world, entity, x, y, z);
	scene_record(scene, entity);
	return entity;
}

static sk_entity_t scene_spawn_sphere(const sk_entities_api_t* ecs, sk_world_t* world, physics_scene_t* scene, f32 x, f32 y, f32 z, f32 radius) {
	const sk_type_id_t ids[] = {SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, SK_TRANSFORM_COMPONENT_TYPE_ID,
								SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID};
	sk_entity_t entity = ecs->world_spawn(world, ids, 4u);
	if (!sk_entity_is_valid(entity)) {
		return entity;
	}
	sk_rigid_body_config_t* cfg = (sk_rigid_body_config_t*)ecs->world_component(world, entity, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	sk_sphere_collider_t* collider = (sk_sphere_collider_t*)ecs->world_component(world, entity, SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID);
	if (cfg != NULL) {
		scene_fill_dynamic_cfg(cfg);
	}
	if (collider != NULL) {
		collider->radius = radius;
	}
	scene_set_pose(ecs, world, entity, x, y, z);
	scene_record(scene, entity);
	return entity;
}

static sk_entity_t scene_spawn_character(const sk_entities_api_t* ecs, sk_world_t* world, physics_scene_t* scene, f32 x, f32 y, f32 z) {
	const sk_type_id_t ids[] = {SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID, SK_CHARACTER_STATE_COMPONENT_TYPE_ID, SK_TRANSFORM_COMPONENT_TYPE_ID};
	sk_entity_t entity = ecs->world_spawn(world, ids, 3u);
	if (!sk_entity_is_valid(entity)) {
		return entity;
	}
	sk_character_config_t* cfg = (sk_character_config_t*)ecs->world_component(world, entity, SK_CHARACTER_CONFIG_COMPONENT_TYPE_ID);
	if (cfg != NULL) {
		cfg->radius = 0.3f;
		cfg->height = 1.8f;
		cfg->max_slope_angle = sk_radians(50.0f);
		cfg->step_height = 0.4f;
		cfg->mass = 70.0f;
		cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
		cfg->character = NULL;
	}
	scene_set_pose(ecs, world, entity, x, y, z);
	scene_record(scene, entity);
	return entity;
}

static void scene_build(const sk_entities_api_t* ecs, sk_world_t* world, physics_scene_t* scene) {
	u32 i;
	scene->ground = scene_spawn_box(ecs, world, scene, SK_JOLT_MOTION_TYPE_STATIC, 0.0f, SCENE_GROUND_Y, 0.0f, SCENE_GROUND_HALF, 0.5f, SCENE_GROUND_HALF);
	scene->terrain = scene_spawn_box(ecs, world, scene, SK_JOLT_MOTION_TYPE_STATIC, SCENE_TERRAIN_X, 0.5f * SCENE_TERRAIN_TOP, 0.0f, 1.0f, 0.5f * SCENE_TERRAIN_TOP, 2.0f);
	for (i = 0u; i < SCENE_STAIRS; ++i) {
		const f32 top = SCENE_STAIR_RISE * ((f32)i + 1.0f);
		scene->stairs[i] = scene_spawn_box(ecs, world, scene, SK_JOLT_MOTION_TYPE_STATIC, SCENE_STAIR_X0 + SCENE_STAIR_PITCH * (f32)i, 0.5f * top, 0.0f, SCENE_STAIR_HALF,
										   0.5f * top, SCENE_STAIR_HALF_Z);
	}
	/* Box tower (spawned ~3 m above the floor so the stack visibly falls and
	 * settles; tiny 1 cm gaps keep the bodies slightly separated at spawn). */
	for (i = 0u; i < SCENE_BOXES; ++i) {
		scene->boxes[i] = scene_spawn_box(ecs, world, scene, SK_JOLT_MOTION_TYPE_DYNAMIC, SCENE_TOWER_X, 3.51f + 1.01f * (f32)i, SCENE_TOWER_Z, SCENE_BOX_HALF, SCENE_BOX_HALF,
										  SCENE_BOX_HALF);
	}
	/* Spheres: three resting on the flat floor — all spawned ~3 m up so they
	 * fall and settle. (Spheres only rest on flat supports: a sphere on a box
	 * top or another sphere is a point contact that rolls off during the
	 * landing jostle, which would mask the stability observation.) */
	scene->spheres[0] = scene_spawn_sphere(ecs, world, scene, SCENE_TOWER_X, 3.51f, 2.0f, SCENE_SPHERE_R);
	scene->spheres[1] = scene_spawn_sphere(ecs, world, scene, SCENE_TOWER_X, 3.51f, SCENE_SPHERE_Z, SCENE_SPHERE_R);
	scene->spheres[2] = scene_spawn_sphere(ecs, world, scene, SCENE_TOWER_X - 2.0f, 3.51f, SCENE_SPHERE_Z, SCENE_SPHERE_R);
	/* Kinematic platform (moves vertically during the run) + dynamic rider. */
	scene->platform = scene_spawn_box(ecs, world, scene, SK_JOLT_MOTION_TYPE_KINEMATIC, SCENE_PLATFORM_X, SCENE_PLATFORM_Y0, SCENE_PLATFORM_Z, SCENE_PLATFORM_HALF,
									  SCENE_PLATFORM_HALF_Y, SCENE_PLATFORM_HALF);
	scene->rider = scene_spawn_box(ecs, world, scene, SK_JOLT_MOTION_TYPE_DYNAMIC, SCENE_PLATFORM_X, SCENE_PLATFORM_Y0 + SCENE_PLATFORM_HALF_Y + SCENE_RIDER_HALF + 0.05f,
								   SCENE_PLATFORM_Z, SCENE_RIDER_HALF, SCENE_RIDER_HALF, SCENE_RIDER_HALF);
	/* Character controller walking +X. */
	scene->character = scene_spawn_character(ecs, world, scene, SCENE_CHARACTER_X0, 0.05f, 0.0f);
}

static void scene_teardown(const sk_entities_api_t* ecs, sk_world_t* world, physics_scene_t* scene) {
	u32 i;
	for (i = 0u; i < scene->count; ++i) {
		(void)ecs->world_despawn(world, scene->entities[i]);
	}
	scene->count = 0u;
}

/* Every scene entity must have spawned (the jolt plugin registered the
 * physics components on app init; if that failed, spawns come back invalid). */
static i32 scene_validate(const physics_scene_t* scene) {
	u32 i;
	if (scene->count != (5u + SCENE_STAIRS + SCENE_BOXES + SCENE_SPHERES)) {
		return 0;
	}
	for (i = 0u; i < scene->count; ++i) {
		if (!sk_entity_is_valid(scene->entities[i])) {
			return 0;
		}
	}
	return 1;
}

/* ---- per-frame observation ---- */

static f32 scene_vel_norm_sq(const sk_vec3_t* v) {
	return v->x * v->x + v->y * v->y + v->z * v->z;
}

static void scene_observe_stack_body(const sk_entities_api_t* ecs, sk_world_t* world, sk_entity_t entity, u32 index, f32 bottom_offset, u32 frame, scene_observations_t* obs) {
	const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, entity, SK_TRANSFORM_COMPONENT_TYPE_ID);
	const sk_rigid_body_state_t* st = (const sk_rigid_body_state_t*)ecs->world_component(world, entity, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
	if (xf == NULL) {
		return;
	}
	{
		const f32 bottom = xf->position.y - bottom_offset;
		if (bottom < obs->min_stack_bottom) {
			obs->min_stack_bottom = bottom;
		}
	}
	if (st != NULL) {
		const f32 v2 = scene_vel_norm_sq(&st->linear_velocity);
		if (v2 >= SCENE_MOVE_EPS_SQ) {
			obs->last_moving[index] = frame;
		}
		if (frame == SCENE_FRAMES - 1u) {
			const f32 v = sqrtf(v2);
			if (v > obs->max_stack_velocity) {
				obs->max_stack_velocity = v;
			}
		}
	}
	if (frame == 600u) {
		obs->stack_pos_600[index] = xf->position;
	}
	if (frame == SCENE_FRAMES - 1u) {
		obs->stack_pos_final[index] = xf->position;
	}
}

static void scene_observe_frame(const sk_entities_api_t* ecs, sk_world_t* world, const physics_scene_t* scene, u32 frame, scene_observations_t* obs) {
	u32 i;
	const sk_transform_t* platform_xf = (const sk_transform_t*)ecs->world_component(world, scene->platform, SK_TRANSFORM_COMPONENT_TYPE_ID);
	const sk_transform_t* rider_xf = (const sk_transform_t*)ecs->world_component(world, scene->rider, SK_TRANSFORM_COMPONENT_TYPE_ID);
	const f32 platform_top = (platform_xf != NULL) ? platform_xf->position.y + SCENE_PLATFORM_HALF_Y : 0.0f;

	for (i = 0u; i < SCENE_BOXES; ++i) {
		scene_observe_stack_body(ecs, world, scene->boxes[i], i, SCENE_BOX_HALF, frame, obs);
	}
	for (i = 0u; i < SCENE_SPHERES; ++i) {
		scene_observe_stack_body(ecs, world, scene->spheres[i], SCENE_BOXES + i, SCENE_SPHERE_R, frame, obs);
	}
	if (frame == 300u) {
		const sk_transform_t* top = (const sk_transform_t*)ecs->world_component(world, scene->boxes[SCENE_BOXES - 1u], SK_TRANSFORM_COMPONENT_TYPE_ID);
		obs->top_y_300 = (top != NULL) ? top->position.y : 0.0f;
	}
	if (frame == 600u) {
		const sk_transform_t* top = (const sk_transform_t*)ecs->world_component(world, scene->boxes[SCENE_BOXES - 1u], SK_TRANSFORM_COMPONENT_TYPE_ID);
		obs->top_y_600 = (top != NULL) ? top->position.y : 0.0f;
	}
	{
		if (rider_xf != NULL) {
			const f32 clearance = platform_top - (rider_xf->position.y - SCENE_RIDER_HALF);
			if (clearance < obs->min_rider_clearance) {
				obs->min_rider_clearance = clearance;
			}
		}
	}
	{
		const sk_transform_t* char_xf = (const sk_transform_t*)ecs->world_component(world, scene->character, SK_TRANSFORM_COMPONENT_TYPE_ID);
		const sk_character_state_t* char_state = (const sk_character_state_t*)ecs->world_component(world, scene->character, SK_CHARACTER_STATE_COMPONENT_TYPE_ID);
		if (char_xf != NULL) {
			if (char_xf->position.y > obs->char_max_y) {
				obs->char_max_y = char_xf->position.y;
			}
			if (char_xf->position.y < obs->char_min_y) {
				obs->char_min_y = char_xf->position.y;
			}
			if (frame == 90u) {
				obs->char_grounded_90 = (char_state != NULL && char_state->ground_state == SK_JOLT_GROUND_STATE_GROUNDED) ? 1 : 0;
			}
			if (frame == SCENE_FRAMES - 1u) {
				obs->char_grounded_final = (char_state != NULL && char_state->ground_state == SK_JOLT_GROUND_STATE_GROUNDED) ? 1 : 0;
				obs->char_final_x = char_xf->position.x;
				obs->char_final_y = char_xf->position.y;
				obs->platform_y_final = platform_xf != NULL ? platform_xf->position.y : 0.0f;
				if (rider_xf != NULL) {
					obs->rider_y_final = rider_xf->position.y;
				}
				{
					const sk_transform_t* top = (const sk_transform_t*)ecs->world_component(world, scene->boxes[SCENE_BOXES - 1u], SK_TRANSFORM_COMPONENT_TYPE_ID);
					obs->top_y_final = (top != NULL) ? top->position.y : 0.0f;
				}
			}
		}
	}
}

/* Drive one frame: set the kinematic platform pose + character walk input,
 * then step the world exactly like the engine frame loop does. */
static void scene_drive_frame(const sk_entities_api_t* ecs, const sk_jolt_api_t* jolt, sk_world_t* world, const physics_scene_t* scene, u32 frame, scene_observations_t* obs) {
	const f32 t = (f32)frame * SCENE_DT;
	sk_transform_t* platform_xf = (sk_transform_t*)ecs->world_component(world, scene->platform, SK_TRANSFORM_COMPONENT_TYPE_ID);
	if (platform_xf != NULL) {
		platform_xf->position.y = SCENE_PLATFORM_Y0 + SCENE_PLATFORM_AMP * sinf(2.0f * SK_PI * t / SCENE_PLATFORM_PERIOD);
	}
	sk_character_state_t* char_state = (sk_character_state_t*)ecs->world_component(world, scene->character, SK_CHARACTER_STATE_COMPONENT_TYPE_ID);
	if (char_state != NULL) {
		char_state->velocity = sk_vec3(SCENE_WALK_SPEED, 0.0f, 0.0f);
	}
	jolt->step_world(world, SCENE_DT);
	scene_observe_frame(ecs, world, scene, frame, obs);
}

static void scene_run(const sk_entities_api_t* ecs, const sk_jolt_api_t* jolt, sk_world_t* world, const physics_scene_t* scene, scene_observations_t* obs) {
	u32 frame;
	u32 i;
	obs->min_stack_bottom = FLT_MAX;
	obs->min_rider_clearance = FLT_MAX;
	obs->char_min_y = FLT_MAX;
	for (frame = 0u; frame < SCENE_FRAMES; ++frame) {
		scene_drive_frame(ecs, jolt, world, scene, frame, obs);
	}
	obs->settle_frame = 0u;
	for (i = 0u; i < SCENE_BOXES + SCENE_SPHERES; ++i) {
		if (obs->last_moving[i] > obs->settle_frame) {
			obs->settle_frame = obs->last_moving[i];
		}
	}
	/* Residual drift over the last 5 s (stable resting / no jitter). */
	obs->max_stack_drift = 0.0f;
	for (i = 0u; i < SCENE_BOXES + SCENE_SPHERES; ++i) {
		const f32 dx = fabsf(obs->stack_pos_600[i].x - obs->stack_pos_final[i].x);
		const f32 dy = fabsf(obs->stack_pos_600[i].y - obs->stack_pos_final[i].y);
		const f32 dz = fabsf(obs->stack_pos_600[i].z - obs->stack_pos_final[i].z);
		if (dx > obs->max_stack_drift) {
			obs->max_stack_drift = dx;
		}
		if (dy > obs->max_stack_drift) {
			obs->max_stack_drift = dy;
		}
		if (dz > obs->max_stack_drift) {
			obs->max_stack_drift = dz;
		}
	}
}

/* ---- determinism snapshot ---- */

static void scene_snapshot(const sk_entities_api_t* ecs, sk_world_t* world, const physics_scene_t* scene, scene_pose_t* out) {
	u32 i;
	for (i = 0u; i < SCENE_BOXES; ++i) {
		const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, scene->boxes[i], SK_TRANSFORM_COMPONENT_TYPE_ID);
		const sk_rigid_body_state_t* st = (const sk_rigid_body_state_t*)ecs->world_component(world, scene->boxes[i], SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
		scene_pose_t* pose = &out[i];
		memset(pose, 0, sizeof(*pose));
		if (xf != NULL) {
			pose->x = xf->position.x;
			pose->y = xf->position.y;
			pose->z = xf->position.z;
			pose->rx = xf->rotation.x;
			pose->ry = xf->rotation.y;
			pose->rz = xf->rotation.z;
			pose->rw = xf->rotation.w;
		}
		if (st != NULL) {
			pose->vx = st->linear_velocity.x;
			pose->vy = st->linear_velocity.y;
			pose->vz = st->linear_velocity.z;
		}
	}
	for (i = 0u; i < SCENE_SPHERES; ++i) {
		const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, scene->spheres[i], SK_TRANSFORM_COMPONENT_TYPE_ID);
		const sk_rigid_body_state_t* st = (const sk_rigid_body_state_t*)ecs->world_component(world, scene->spheres[i], SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
		scene_pose_t* pose = &out[SCENE_BOXES + i];
		memset(pose, 0, sizeof(*pose));
		if (xf != NULL) {
			pose->x = xf->position.x;
			pose->y = xf->position.y;
			pose->z = xf->position.z;
			pose->rx = xf->rotation.x;
			pose->ry = xf->rotation.y;
			pose->rz = xf->rotation.z;
			pose->rw = xf->rotation.w;
		}
		if (st != NULL) {
			pose->vx = st->linear_velocity.x;
			pose->vy = st->linear_velocity.y;
			pose->vz = st->linear_velocity.z;
		}
	}
	{
		const sk_entity_t extras[3] = {scene->platform, scene->rider, scene->character};
		for (i = 0u; i < 3u; ++i) {
			const sk_transform_t* xf = (const sk_transform_t*)ecs->world_component(world, extras[i], SK_TRANSFORM_COMPONENT_TYPE_ID);
			const sk_rigid_body_state_t* st = (const sk_rigid_body_state_t*)ecs->world_component(world, extras[i], SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
			const sk_character_state_t* cs = (const sk_character_state_t*)ecs->world_component(world, extras[i], SK_CHARACTER_STATE_COMPONENT_TYPE_ID);
			scene_pose_t* pose = &out[SCENE_BOXES + SCENE_SPHERES + i];
			memset(pose, 0, sizeof(*pose));
			if (xf != NULL) {
				pose->x = xf->position.x;
				pose->y = xf->position.y;
				pose->z = xf->position.z;
				pose->rx = xf->rotation.x;
				pose->ry = xf->rotation.y;
				pose->rz = xf->rotation.z;
				pose->rw = xf->rotation.w;
			}
			if (st != NULL) {
				pose->vx = st->linear_velocity.x;
				pose->vy = st->linear_velocity.y;
				pose->vz = st->linear_velocity.z;
			} else if (cs != NULL) {
				pose->vx = cs->velocity.x;
				pose->vy = cs->velocity.y;
				pose->vz = cs->velocity.z;
			}
		}
	}
}

static f32 scene_pose_max_delta(const scene_pose_t* a, const scene_pose_t* b, u32 count) {
	f32 max_delta = 0.0f;
	u32 i;
	for (i = 0u; i < count; ++i) {
		const f32 fields[10] = {fabsf(a[i].x - b[i].x),	  fabsf(a[i].y - b[i].y),	fabsf(a[i].z - b[i].z),	  fabsf(a[i].rx - b[i].rx), fabsf(a[i].ry - b[i].ry),
								fabsf(a[i].rz - b[i].rz), fabsf(a[i].rw - b[i].rw), fabsf(a[i].vx - b[i].vx), fabsf(a[i].vy - b[i].vy), fabsf(a[i].vz - b[i].vz)};
		u32 j;
		for (j = 0u; j < 10u; ++j) {
			if (fields[j] > max_delta) {
				max_delta = fields[j];
			}
		}
	}
	return max_delta;
}

/* ---- expectations ---- */

static i32 scene_check(const scene_observations_t* a, const scene_observations_t* b) {
	i32 ok = 1;
	if (a->settle_frame >= 600u) {
		printf("  FAIL: stack not settled by t=10s (last moving frame %u)\n", a->settle_frame);
		ok = 0;
	}
	if (!(a->max_stack_drift < SCENE_DRIFT_EPS)) {
		printf("  FAIL: stack drift t=10s..t=15s %.6f m >= %.4f m (jitter)\n", (double)a->max_stack_drift, (double)SCENE_DRIFT_EPS);
		ok = 0;
	}
	if (!(a->max_stack_velocity < SCENE_MOVE_EPS)) {
		printf("  FAIL: residual stack velocity %.4f m/s\n", (double)a->max_stack_velocity);
		ok = 0;
	}
	if (!(a->min_stack_bottom >= -SCENE_TUNNEL_MARGIN)) {
		printf("  FAIL: stack body penetrated the floor (min bottom %.4f m)\n", (double)a->min_stack_bottom);
		ok = 0;
	}
	if (!(a->min_rider_clearance >= -SCENE_RIDER_TUNNEL_MARGIN)) {
		printf("  FAIL: rider penetrated the platform (min clearance %.4f m)\n", (double)a->min_rider_clearance);
		ok = 0;
	}
	if (!(fabsf(a->rider_y_final - (a->platform_y_final + SCENE_PLATFORM_HALF_Y + SCENE_RIDER_HALF)) < 0.1f)) {
		printf("  FAIL: rider not resting on the platform at t=15s (rider y %.3f, platform top %.3f)\n", (double)a->rider_y_final,
			   (double)(a->platform_y_final + SCENE_PLATFORM_HALF_Y));
		ok = 0;
	}
	if (a->char_grounded_90 != 1) {
		printf("  FAIL: character not grounded at t=1.5s\n");
		ok = 0;
	}
	if (a->char_grounded_final != 1) {
		printf("  FAIL: character not grounded at t=15s\n");
		ok = 0;
	}
	if (!(a->char_max_y > 0.9f)) {
		printf("  FAIL: character did not climb the stairs (max y %.3f m)\n", (double)a->char_max_y);
		ok = 0;
	}
	if (!(a->char_final_x > 30.0f)) {
		printf("  FAIL: character did not walk far enough (final x %.2f m)\n", (double)a->char_final_x);
		ok = 0;
	}
	if (!(a->char_min_y > -0.05f)) {
		printf("  FAIL: character dropped below the floor (min y %.3f m)\n", (double)a->char_min_y);
		ok = 0;
	}
	if (!(b->max_run_delta < SCENE_DETERMINISM_EPS)) {
		printf("  FAIL: repeat runs diverged (max |pose delta| %.6f m >= %.4f m)\n", (double)b->max_run_delta, (double)SCENE_DETERMINISM_EPS);
		ok = 0;
	}
	return ok;
}

/* ---- report ---- */

static void scene_report(const sk_jolt_api_t* jolt, const scene_observations_t* a, const scene_observations_t* b) {
	printf("============================================================\n");
	printf(" sk-physics-scene — jolt integration end to end (APX-310)\n");
	printf("============================================================\n");
	printf("world: fixed timestep %.6f s (default), substeps %u, gravity (0.00, -9.81, 0.00)\n", (double)jolt->get_fixed_timestep(), jolt->get_substeps());
	printf("scene: ground plane 100x1x100 m, terrain step (top %.2f m), 3 stairs (tops %.2f/%.2f/%.2f m), box tower + spheres (r=%.2f m),\n", (double)SCENE_TERRAIN_TOP,
		   (double)SCENE_STAIR_RISE, (double)(2.0f * SCENE_STAIR_RISE), (double)(3.0f * SCENE_STAIR_RISE), (double)SCENE_SPHERE_R);
	printf("       kinematic platform y(t) = %.1f + %.1f*sin(2*pi*t/%.0f), rider box on top, character walking +X at %.1f m/s\n", (double)SCENE_PLATFORM_Y0,
		   (double)SCENE_PLATFORM_AMP, (double)SCENE_PLATFORM_PERIOD, (double)SCENE_WALK_SPEED);
	printf("simulation: %u frames @ 60 Hz = %.1f s at the default timestep, run twice in one process\n\n", SCENE_FRAMES, (double)(SCENE_FRAMES * SCENE_DT));

	printf("[stack: %u boxes + %u spheres fall and settle]\n", SCENE_BOXES, SCENE_SPHERES);
	printf("  settled by frame %u (all stack bodies asleep, |v| < %.2f m/s)\n", a->settle_frame, (double)SCENE_MOVE_EPS);
	printf("  top box y: t=5s %.4f  t=10s %.4f  t=15s %.4f m\n", (double)a->top_y_300, (double)a->top_y_600, (double)a->top_y_final);
	printf("  residual drift t=10s -> t=15s: %.6f m (stable resting / no jitter)\n", (double)a->max_stack_drift);
	printf("  residual velocity at t=15s: %.6f m/s\n", (double)a->max_stack_velocity);
	printf("  min stack bottom during run: %.4f m (floor top y=0; no tunneling)\n", (double)a->min_stack_bottom);

	printf("\n[kinematic platform + rider]\n");
	printf("  min rider clearance (platform_top - rider_bottom): %.4f m (negative would be penetration)\n", (double)a->min_rider_clearance);
	printf("  final platform y %.4f / rider y %.4f m (rider resting on top)\n", (double)a->platform_y_final, (double)a->rider_y_final);

	printf("\n[character: walks +X at %.1f m/s over terrain + stairs]\n", (double)SCENE_WALK_SPEED);
	printf("  grounded at t=1.5s: %s   grounded at t=15s: %s\n", a->char_grounded_90 ? "yes" : "no", a->char_grounded_final ? "yes" : "no");
	printf("  max height reached: %.3f m (stair tops up to %.2f m)\n", (double)a->char_max_y, (double)(3.0f * SCENE_STAIR_RISE));
	printf("  final x %.2f m, final y %.3f m (walked past the stairs on flat ground)\n", (double)a->char_final_x, (double)a->char_final_y);

	printf("\n[repeat runs: identical scene simulated twice in one process]\n");
	printf("  max |final pose delta| (run B vs run A): %.6f m\n", (double)b->max_run_delta);
	printf("\n");
}

/* ---- entry ---- */

int main(int argc, char* argv[]) {
	sk_app_boot_t boot = sk_app_init(argc, argv);
	sk_app_context_t* ctx = boot.context;
	const sk_app_api_t* app_api = boot.api;
	const sk_entities_api_t* ecs;
	const sk_jolt_api_t* jolt;
	sk_world_t* world;
	physics_scene_t scene_a;
	physics_scene_t scene_b;
	scene_observations_t obs_a;
	scene_observations_t obs_b;
	scene_pose_t poses_a[SCENE_POSE_COUNT];
	scene_pose_t poses_b[SCENE_POSE_COUNT];
	i32 pass;

	if (ctx == NULL) {
		fprintf(stderr, "sk-physics-scene: sk_app_init failed\n");
		return 1;
	}
	ecs = (const sk_entities_api_t*)app_api->get_api(ctx, SK_ENTITIES_API_TYPE_ID);
	jolt = (const sk_jolt_api_t*)app_api->get_api(ctx, SK_JOLT_API_TYPE_ID);
	world = app_api->scene_world(ctx);
	if (ecs == NULL || jolt == NULL || world == NULL || jolt->step_world == NULL) {
		fprintf(stderr, "sk-physics-scene: jolt/entities plugins not loaded (run from the build bin dir so {exe}/plugins is found)\n");
		sk_app_shutdown(ctx);
		return 1;
	}

	memset(&obs_a, 0, sizeof(obs_a));
	memset(&obs_b, 0, sizeof(obs_b));
	scene_init(&scene_a);
	scene_build(ecs, world, &scene_a);
	if (!scene_validate(&scene_a)) {
		fprintf(stderr, "sk-physics-scene: entity spawn failed (physics components not registered?)\n");
		scene_teardown(ecs, world, &scene_a);
		sk_app_shutdown(ctx);
		return 1;
	}
	scene_run(ecs, jolt, world, &scene_a, &obs_a);
	scene_snapshot(ecs, world, &scene_a, poses_a);
	scene_teardown(ecs, world, &scene_a);
	/* A repeat run must start from the same clean state as run A: tear the
	 * Jolt world down and rebuild it so the broadphase tree / body id
	 * allocation is bit-identical to the first run (reusing the live world
	 * leaves tree ordering from the removed run-A bodies behind). */
	jolt->shutdown();
	if (jolt->init(NULL) != 0) {
		fprintf(stderr, "sk-physics-scene: jolt re-init for the repeat run failed\n");
		sk_app_shutdown(ctx);
		return 1;
	}

	scene_init(&scene_b);
	scene_build(ecs, world, &scene_b);
	if (!scene_validate(&scene_b)) {
		fprintf(stderr, "sk-physics-scene: entity spawn failed on repeat run\n");
		scene_teardown(ecs, world, &scene_b);
		sk_app_shutdown(ctx);
		return 1;
	}
	scene_run(ecs, jolt, world, &scene_b, &obs_b);
	scene_snapshot(ecs, world, &scene_b, poses_b);
	obs_b.max_run_delta = scene_pose_max_delta(poses_a, poses_b, SCENE_POSE_COUNT);

	pass = scene_check(&obs_a, &obs_b);
	scene_report(jolt, &obs_a, &obs_b);
	printf("RESULT: %s\n", pass ? "PASS" : "FAIL");

	scene_teardown(ecs, world, &scene_b);
	sk_app_shutdown(ctx);
	return pass ? 0 : 1;
}
