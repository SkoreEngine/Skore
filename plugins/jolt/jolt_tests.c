/**
 * @file jolt_tests.c
 * @brief Plugin-local unit tests for the physics module (SK_TESTS builds only).
 *
 * Test code stays in C like every other plugin's tests (the C++ TUs in this
 * plugin never include test.h); the SK_TESTS-only accessors defined in
 * jolt.cpp hand this TU the app registry and the registered sk_jolt_api_t.
 * sk_plugin_run_tests lives here too so the host test scanner (tests/main.c)
 * finds it under the same SK_TESTS guard as the other plugins.
 *
 * Coverage: API registration, settings defaults/resolution, world init →
 * step → shutdown cycles (the leak/ASan surface — every Jolt allocation must
 * be released per cycle), fixed-step rate enforcement via the step callback,
 * the giant-frame spiral-of-death clamp, and the rigid-body integration
 * (APX-320): create/destroy for every shape × motion-type combination,
 * transform/velocity accessor round-trips, handle validation (NULL,
 * use-after-destroy and stale-across-shutdown handles return errors, never
 * crash), and the three motion-type behaviors — a dynamic body falls under
 * gravity and rests on a static floor within a bounded step count, a static
 * body never moves, and a kinematic body holds an explicitly set transform.
 * Character controllers are still stubbed.
 */

/* Release builds strip every symbol below (SK_TESTS undefined); keep the TU
 * non-empty so it is still a valid translation unit under -Wpedantic (ISO C
 * requires at least one declaration; clang-tidy errors on empty TUs). */
typedef int sk_jolt_tests_tu_anchor_t;

#ifdef SK_TESTS

#include "jolt.h"
#include "jolt_components.h"

#include "app.h"
#include "common.h"
#include "test.h"

#include <string.h>

/* Defined in jolt.cpp (SK_TESTS builds only). */
void sk_jolt_test_context(sk_app_context_t** out_context, const sk_app_api_t** out_app_api);
const sk_jolt_api_t* sk_jolt_test_api(void);

/* The plugin test host (tests/main.c) loads each plugin and calls
 * sk_plugin_entry_point before sk_plugin_run_tests, so the registry cached in
 * sk_jolt_init is live by the time these run. */
static void jolt_tests_resolve(const sk_jolt_api_t** out_api, sk_app_context_t** out_context, const sk_app_api_t** out_app_api) {
	sk_jolt_test_context(out_context, out_app_api);
	*out_api = sk_jolt_test_api();
}

SK_TEST(jolt_registers_api_table) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	jolt_tests_resolve(&api, &context, &app_api);

	TEST_ASSERT_NOT_NULL(context);
	TEST_ASSERT_NOT_NULL(app_api);
	TEST_ASSERT_NOT_NULL(api);

	/* The host registry must resolve this plugin's table under its type id,
	 * and it must be the exact table this plugin registered. */
	const sk_jolt_api_t* registered = (const sk_jolt_api_t*)app_api->get_api(context, SK_JOLT_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(registered);
	TEST_ASSERT_EQUAL_PTR((const_ptr_t)api, (const_ptr_t)registered);
}

SK_TEST(jolt_settings_defaults) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_settings_t s;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_NOT_NULL(api->settings_defaults);

	memset(&s, 0xAA, sizeof(s));
	api->settings_defaults(&s);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gravity[0]);
	TEST_ASSERT_EQUAL_FLOAT(-9.81f, s.gravity[1]);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, s.gravity[2]);
	TEST_ASSERT_EQUAL_FLOAT(1.0f / 60.0f, s.fixed_timestep);
	TEST_ASSERT_EQUAL_UINT32(1u, s.substeps);
	TEST_ASSERT_EQUAL_UINT32(65536u, s.max_bodies);
	TEST_ASSERT_EQUAL_UINT32(65536u, s.max_body_pairs);
	TEST_ASSERT_EQUAL_UINT32(10240u, s.max_constraints);
}

/*
 * init/shutdown cycles build and destroy the full Jolt stack (Factory +
 * registered types, 10 MiB TempAllocator, job-system threads, filters,
 * PhysicsSystem). Running many cycles in one process is the leak check: under
 * ASan/LSan any Jolt allocation that survives shutdown is reported. Also
 * covers idempotent shutdown and init-over-live-world replacement.
 */
SK_TEST(jolt_world_init_shutdown_cycles) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	i32 i;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	/* shutdown without init is a no-op. */
	api->shutdown();
	api->shutdown();

	for (i = 0; i < 12; ++i) {
		TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));
		TEST_ASSERT_EQUAL_FLOAT(1.0f / 60.0f, api->get_fixed_timestep());
		TEST_ASSERT_EQUAL_UINT32(1u, api->get_substeps());
		/* Drive a few steps so the job system / broadphase actually run. */
		api->step(0.05f, NULL, NULL);
		api->shutdown();
	}

	/* init over a live world replaces it without leaking the old one. */
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));
	api->shutdown();
	api->shutdown();
}

SK_TEST(jolt_init_honors_settings) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_settings_t s;
	f32 gx = 0.0f;
	f32 gy = 0.0f;
	f32 gz = 0.0f;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	api->settings_defaults(&s);
	s.gravity[0] = 0.0f;
	s.gravity[1] = -20.0f;
	s.gravity[2] = 0.0f;
	s.fixed_timestep = 0.02f;
	s.substeps = 2u;
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));

	api->get_gravity(&gx, &gy, &gz);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, gx);
	TEST_ASSERT_EQUAL_FLOAT(-20.0f, gy);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, gz);
	TEST_ASSERT_EQUAL_FLOAT(0.02f, api->get_fixed_timestep());
	TEST_ASSERT_EQUAL_UINT32(2u, api->get_substeps());

	/* Runtime reconfiguration. */
	api->set_gravity(0.0f, -30.0f, 0.0f);
	api->get_gravity(&gx, &gy, &gz);
	TEST_ASSERT_EQUAL_FLOAT(-30.0f, gy);

	TEST_ASSERT_EQUAL_INT32(0, api->set_fixed_timestep(0.01f));
	TEST_ASSERT_EQUAL_FLOAT(0.01f, api->get_fixed_timestep());
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->set_fixed_timestep(0.0f));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->set_fixed_timestep(-1.0f));
	TEST_ASSERT_EQUAL_FLOAT(0.01f, api->get_fixed_timestep());

	TEST_ASSERT_EQUAL_INT32(0, api->set_substeps(4u));
	TEST_ASSERT_EQUAL_UINT32(4u, api->get_substeps());
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->set_substeps(0u));
	TEST_ASSERT_EQUAL_UINT32(4u, api->get_substeps());

	/* A zeroed settings struct keeps defaults for the numeric caps (zero is
	 * invalid there) but honors explicit gravity. */
	api->shutdown();
	memset(&s, 0, sizeof(s));
	s.gravity[1] = -5.0f;
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));
	api->get_gravity(&gx, &gy, &gz);
	TEST_ASSERT_EQUAL_FLOAT(-5.0f, gy);
	TEST_ASSERT_EQUAL_FLOAT(1.0f / 60.0f, api->get_fixed_timestep());
	TEST_ASSERT_EQUAL_UINT32(1u, api->get_substeps());
	api->shutdown();

	/* Getters report the not-initialized sentinels. */
	TEST_ASSERT_EQUAL_FLOAT(0.0f, api->get_fixed_timestep());
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_substeps());
	api->get_gravity(&gx, &gy, &gz);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, gx);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, gy);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, gz);
}

/* ---- step-callback rate verification ---- */

static u64 g_jolt_step_callbacks = 0u;
static f64 g_jolt_last_physics_time = 0.0;
static u64 g_jolt_last_step_index = 0u;

static void jolt_test_count_steps(void_ptr_t user_data, f64 physics_time, u64 step_index) {
	(void)user_data;
	g_jolt_step_callbacks += 1u;
	g_jolt_last_physics_time = physics_time;
	g_jolt_last_step_index = step_index;
}

/* The step callback must fire exactly once per fixed physics step at the
 * configured rate, with a monotonically advancing physics clock. */
SK_TEST(jolt_step_callback_rate) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_settings_t s;
	i32 i;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	api->settings_defaults(&s);
	s.fixed_timestep = 0.01f;
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));

	/* 50 frames × 0.02 s = 1.0 s simulated at 10 ms/step → exactly 100
	 * callbacks (each frame is well under the 0.25 s frame clamp). */
	g_jolt_step_callbacks = 0u;
	for (i = 0; i < 50; ++i) {
		api->step(0.02f, jolt_test_count_steps, NULL);
	}
	TEST_ASSERT_EQUAL_UINT64(100ull, g_jolt_step_callbacks);
	TEST_ASSERT_EQUAL_UINT64(100ull, g_jolt_last_step_index);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 1.0f, (f32)g_jolt_last_physics_time);

	/* The accumulator carries across frames: 0.5 s more → 150 total. */
	for (i = 0; i < 25; ++i) {
		api->step(0.02f, jolt_test_count_steps, NULL);
	}
	TEST_ASSERT_EQUAL_UINT64(150ull, g_jolt_step_callbacks);
	TEST_ASSERT_EQUAL_UINT64(150ull, g_jolt_last_step_index);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 1.5f, (f32)g_jolt_last_physics_time);

	/* Sub-step frame deltas accumulate: 10 frames × 0.1 s = 100 more steps.
	 * step_index / physics_time are cumulative since init (250 steps, 2.5 s). */
	g_jolt_step_callbacks = 0u;
	for (i = 0; i < 10; ++i) {
		api->step(0.1f, jolt_test_count_steps, NULL);
	}
	TEST_ASSERT_EQUAL_UINT64(100ull, g_jolt_step_callbacks);
	TEST_ASSERT_EQUAL_UINT64(250ull, g_jolt_last_step_index);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 2.5f, (f32)g_jolt_last_physics_time);

	/* A timestep change applies at the new rate (0.2 s at 5 ms → 40 steps). */
	TEST_ASSERT_EQUAL_INT32(0, api->set_fixed_timestep(0.005f));
	g_jolt_step_callbacks = 0u;
	api->step(0.2f, jolt_test_count_steps, NULL);
	TEST_ASSERT_EQUAL_UINT64(40ull, g_jolt_step_callbacks);

	api->shutdown();
}

/* The fixed-step driver must be run-to-run deterministic: identical input
 * frame sequences across two independent init → step → shutdown cycles
 * produce identical step counts, physics clocks, and step indices (the
 * physics world is rebuilt from scratch by init each time, so this also
 * re-verifies the full Jolt teardown/rebuild path per cycle). */
SK_TEST(jolt_step_deterministic_across_runs) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_settings_t s;
	u64 count_a = 0u;
	u64 count_b = 0u;
	u64 index_a = 0u;
	u64 index_b = 0u;
	f64 time_a = 0.0;
	f64 time_b = 0.0;
	i32 i;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	api->settings_defaults(&s);
	s.fixed_timestep = 0.01f;

	/* Run A: 50 frames of 0.02 s = 1.0 s at 10 ms/step (100 steps), then a
	 * partial 0.005 s frame that must only accumulate, not step. */
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));
	g_jolt_step_callbacks = 0u;
	for (i = 0; i < 50; ++i) {
		api->step(0.02f, jolt_test_count_steps, NULL);
	}
	api->step(0.005f, jolt_test_count_steps, NULL);
	count_a = g_jolt_step_callbacks;
	index_a = g_jolt_last_step_index;
	time_a = g_jolt_last_physics_time;
	api->shutdown();

	/* Run B: identical inputs against a freshly constructed world. */
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));
	g_jolt_step_callbacks = 0u;
	for (i = 0; i < 50; ++i) {
		api->step(0.02f, jolt_test_count_steps, NULL);
	}
	api->step(0.005f, jolt_test_count_steps, NULL);
	count_b = g_jolt_step_callbacks;
	index_b = g_jolt_last_step_index;
	time_b = g_jolt_last_physics_time;
	api->shutdown();

	/* Identical step counts and identical physics clocks across runs. */
	TEST_ASSERT_EQUAL_UINT64(count_a, count_b);
	TEST_ASSERT_EQUAL_UINT64(index_a, index_b);
	TEST_ASSERT_EQUAL_FLOAT((f32)time_a, (f32)time_b);
	/* Exact values: 100 steps simulated (the 0.005 s tail stays banked in
	 * the accumulator for the next frame, exactly like run A). */
	TEST_ASSERT_EQUAL_UINT64(100ull, count_b);
	TEST_ASSERT_EQUAL_UINT64(100ull, index_b);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-3f, 1.0f, (f32)time_b);
}

/* Huge host frame deltas must be clamped (no spiral of death): 1000 s of
 * input at 10 ms/step must not queue 100000 steps. */
SK_TEST(jolt_step_clamps_giant_delta) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_settings_t s;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	api->settings_defaults(&s);
	s.fixed_timestep = 0.01f;
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));

	g_jolt_step_callbacks = 0u;
	api->step(1000.0f, jolt_test_count_steps, NULL);
	/* Clamped to 0.25 s per host frame → 25 steps at 10 ms. */
	TEST_ASSERT_EQUAL_UINT64(25ull, g_jolt_step_callbacks);

	/* Non-positive deltas are ignored, not treated as catch-up work. */
	g_jolt_step_callbacks = 0u;
	api->step(0.0f, jolt_test_count_steps, NULL);
	api->step(-0.5f, jolt_test_count_steps, NULL);
	TEST_ASSERT_EQUAL_UINT64(0ull, g_jolt_step_callbacks);

	api->shutdown();
}

/* step() without a live world is a no-op (no crash, no callback). */
SK_TEST(jolt_step_before_init_noop) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	api->shutdown();
	g_jolt_step_callbacks = 0u;
	api->step(1.0f, jolt_test_count_steps, NULL);
	TEST_ASSERT_EQUAL_UINT64(0ull, g_jolt_step_callbacks);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, api->get_fixed_timestep());
}

SK_TEST(jolt_character_stubs) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	/* Character controllers are still stubs: create reports NULL, destroy
	 * tolerates NULL. (Union members are set explicitly: only the first union
	 * member can be brace-initialized portably.) */
	sk_jolt_shape_desc_t capsule = {0};
	capsule.kind = SK_JOLT_SHAPE_CAPSULE;
	capsule.shape.capsule.half_height = 0.75f;
	capsule.shape.capsule.radius = 0.3f;
	TEST_ASSERT_NULL(api->character_create(&capsule, SK_JOLT_OBJECT_LAYER_MOVING));
	api->character_destroy(NULL);
}

/* ---- rigid bodies (APX-320) ---- */

static sk_jolt_body_t* jolt_test_body_box(const sk_jolt_api_t* api, sk_jolt_motion_type_t motion, u32 layer, f32 half) {
	sk_jolt_shape_desc_t desc = {0};
	desc.kind = SK_JOLT_SHAPE_BOX;
	desc.shape.box.half_extent[0] = half;
	desc.shape.box.half_extent[1] = half;
	desc.shape.box.half_extent[2] = half;
	return api->body_create(&desc, motion, layer);
}

static sk_jolt_vec3_t jolt_test_vec3(f32 x, f32 y, f32 z) {
	sk_jolt_vec3_t v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

/* Component-wise |a-b| <= eps on every axis. */
static i32 jolt_test_vec3_near(const sk_jolt_vec3_t* a, const sk_jolt_vec3_t* b, f32 eps) {
	return (a->x - b->x <= eps && b->x - a->x <= eps) && (a->y - b->y <= eps && b->y - a->y <= eps) && (a->z - b->z <= eps && b->z - a->z <= eps);
}

static f32 jolt_test_vec3_len_sq(const sk_jolt_vec3_t* v) {
	return v->x * v->x + v->y * v->y + v->z * v->z;
}

/* Every shape kind × motion-type combination creates a body; invalid
 * parameters fail creation; destroy is idempotent. */
SK_TEST(jolt_body_create_and_destroy) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_shape_desc_t desc = {0};
	const sk_jolt_motion_type_t motions[3] = {SK_JOLT_MOTION_TYPE_STATIC, SK_JOLT_MOTION_TYPE_KINEMATIC, SK_JOLT_MOTION_TYPE_DYNAMIC};
	u32 i;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));

	desc.kind = SK_JOLT_SHAPE_BOX;
	desc.shape.box.half_extent[0] = 0.5f;
	desc.shape.box.half_extent[1] = 0.5f;
	desc.shape.box.half_extent[2] = 0.5f;
	for (i = 0u; i < 3u; ++i) {
		sk_jolt_body_t* body = api->body_create(&desc, motions[i], SK_JOLT_OBJECT_LAYER_MOVING);
		TEST_ASSERT_NOT_NULL(body);
		api->body_destroy(body);
	}

	desc.kind = SK_JOLT_SHAPE_SPHERE;
	desc.shape.sphere.radius = 0.5f;
	for (i = 0u; i < 3u; ++i) {
		sk_jolt_body_t* body = api->body_create(&desc, motions[i], SK_JOLT_OBJECT_LAYER_MOVING);
		TEST_ASSERT_NOT_NULL(body);
		api->body_destroy(body);
	}

	desc.kind = SK_JOLT_SHAPE_CAPSULE;
	desc.shape.capsule.half_height = 0.75f;
	desc.shape.capsule.radius = 0.3f;
	for (i = 0u; i < 3u; ++i) {
		sk_jolt_body_t* body = api->body_create(&desc, motions[i], SK_JOLT_OBJECT_LAYER_MOVING);
		TEST_ASSERT_NOT_NULL(body);
		api->body_destroy(body);
	}

	/* Invalid parameters fail creation instead of asserting or crashing. */
	TEST_ASSERT_NULL(api->body_create(NULL, SK_JOLT_MOTION_TYPE_DYNAMIC, SK_JOLT_OBJECT_LAYER_MOVING));
	TEST_ASSERT_NULL(api->body_create(&desc, (sk_jolt_motion_type_t)42, SK_JOLT_OBJECT_LAYER_MOVING));
	TEST_ASSERT_NULL(api->body_create(&desc, SK_JOLT_MOTION_TYPE_DYNAMIC, SK_JOLT_OBJECT_LAYER_INVALID));
	api->shutdown();
}

/* Position / rotation / linear + angular velocity round-trip through the POD
 * accessors on a dynamic body. */
SK_TEST(jolt_body_transform_accessors) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));

	sk_jolt_body_t* body = jolt_test_body_box(api, SK_JOLT_MOTION_TYPE_DYNAMIC, SK_JOLT_OBJECT_LAYER_MOVING, 0.5f);
	TEST_ASSERT_NOT_NULL(body);

	/* Created at the origin with identity rotation and zero velocity. */
	sk_jolt_vec3_t pos = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_quat_t rot = {0.0f, 0.0f, 0.0f, 0.0f};
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&pos, &(sk_jolt_vec3_t){0.0f, 0.0f, 0.0f}, 1.0e-6f));
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_rotation(body, &rot));
	TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, rot.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, rot.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, rot.z);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 1.0f, rot.w);

	/* Set everything, read everything back. */
	const sk_jolt_vec3_t want_pos = jolt_test_vec3(1.0f, 2.0f, 3.0f);
	const sk_jolt_quat_t want_rot = {0.0f, 0.70710678f, 0.0f, 0.70710678f}; /* 180° about Y */
	const sk_jolt_vec3_t want_lv = jolt_test_vec3(4.0f, 5.0f, 6.0f);
	const sk_jolt_vec3_t want_av = jolt_test_vec3(0.5f, 0.0f, -0.25f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(body, &want_pos));
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_rotation(body, &want_rot));
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_linear_velocity(body, &want_lv));
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_angular_velocity(body, &want_av));

	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&want_pos, &pos, 1.0e-4f));
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_rotation(body, &rot));
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, want_rot.x, rot.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, want_rot.y, rot.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, want_rot.z, rot.z);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, want_rot.w, rot.w);
	sk_jolt_vec3_t lv = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_vec3_t av = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_linear_velocity(body, &lv));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&want_lv, &lv, 1.0e-4f));
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_angular_velocity(body, &av));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&want_av, &av, 1.0e-4f));

	api->body_destroy(body);
	api->shutdown();
}

/* A dynamic body created above a static floor must fall under gravity and
 * come to rest on the floor within a bounded number of fixed steps. */
SK_TEST(jolt_dynamic_body_falls_and_rests) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_settings_t s;
	sk_jolt_vec3_t pos = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_vec3_t vel = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_vec3_t floor_pos = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	i32 i;
	i32 settled = 0;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	api->settings_defaults(&s);
	s.fixed_timestep = 1.0f / 60.0f;
	TEST_ASSERT_EQUAL_INT32(0, api->init(&s));

	/* Floor: static box 20×2×20 centered at y=-1 → top surface at y=0. */
	sk_jolt_shape_desc_t floor_desc = {0};
	floor_desc.kind = SK_JOLT_SHAPE_BOX;
	floor_desc.shape.box.half_extent[0] = 10.0f;
	floor_desc.shape.box.half_extent[1] = 1.0f;
	floor_desc.shape.box.half_extent[2] = 10.0f;
	sk_jolt_body_t* floor = api->body_create(&floor_desc, SK_JOLT_MOTION_TYPE_STATIC, SK_JOLT_OBJECT_LAYER_NON_MOVING);
	TEST_ASSERT_NOT_NULL(floor);
	const sk_jolt_vec3_t floor_start = jolt_test_vec3(0.0f, -1.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(floor, &floor_start));

	/* Dropped box: dynamic 1×1×1, center at y=5 (4.5 above the floor). */
	sk_jolt_body_t* box = jolt_test_body_box(api, SK_JOLT_MOTION_TYPE_DYNAMIC, SK_JOLT_OBJECT_LAYER_MOVING, 0.5f);
	TEST_ASSERT_NOT_NULL(box);
	const sk_jolt_vec3_t drop_start = jolt_test_vec3(0.0f, 5.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(box, &drop_start));

	/* It must fall: after 1 s of simulation it is well below the drop height. */
	for (i = 0; i < 60; ++i) {
		api->step(1.0f / 60.0f, NULL, NULL);
	}
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(box, &pos));
	TEST_ASSERT_TRUE(pos.y < 4.0f);

	/* ...and come to rest on the floor (box center ≈ 0.5 above the floor top)
	 * within a bounded number of fixed steps (30 s = 1800 steps; generous). */
	for (i = 0; i < 1800; ++i) {
		api->step(1.0f / 60.0f, NULL, NULL);
		TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(box, &pos));
		TEST_ASSERT_EQUAL_INT32(0, api->body_get_linear_velocity(box, &vel));
		if (pos.y > 0.4f && pos.y < 0.6f && jolt_test_vec3_len_sq(&vel) < 0.05f * 0.05f) {
			settled = 1;
			break;
		}
	}
	TEST_ASSERT_TRUE(settled);
	TEST_ASSERT_TRUE(pos.y > 0.4f && pos.y < 0.6f);
	TEST_ASSERT_TRUE(jolt_test_vec3_len_sq(&vel) < 0.1f * 0.1f);

	/* The floor itself never moved. */
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(floor, &floor_pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&floor_start, &floor_pos, 1.0e-4f));

	api->body_destroy(box);
	api->body_destroy(floor);
	api->shutdown();
}

/* A static body does not move: gravity and stepping leave its transform
 * untouched, and a repositioned static body stays where it was put. */
SK_TEST(jolt_static_body_does_not_move) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_vec3_t pos = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_vec3_t vel = jolt_test_vec3(1.0f, 1.0f, 1.0f);
	i32 i;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));

	sk_jolt_body_t* body = jolt_test_body_box(api, SK_JOLT_MOTION_TYPE_STATIC, SK_JOLT_OBJECT_LAYER_NON_MOVING, 1.0f);
	TEST_ASSERT_NOT_NULL(body);
	const sk_jolt_vec3_t start = jolt_test_vec3(1.0f, 2.0f, 3.0f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(body, &start));

	/* 2 s of gravity do not move it. */
	for (i = 0; i < 120; ++i) {
		api->step(1.0f / 60.0f, NULL, NULL);
	}
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&start, &pos, 1.0e-4f));
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_linear_velocity(body, &vel));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&vel, &(sk_jolt_vec3_t){0.0f, 0.0f, 0.0f}, 1.0e-6f));

	/* Repositioned static bodies hold the new transform too (no fall). */
	const sk_jolt_vec3_t moved = jolt_test_vec3(0.0f, 10.0f, 0.0f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(body, &moved));
	for (i = 0; i < 60; ++i) {
		api->step(1.0f / 60.0f, NULL, NULL);
	}
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&moved, &pos, 1.0e-4f));

	api->body_destroy(body);
	api->shutdown();
}

/* A kinematic body follows an explicitly set transform: it holds the given
 * position/rotation across steps (no gravity response), including a
 * mid-simulation reposition. */
SK_TEST(jolt_kinematic_body_follows_set_transform) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_vec3_t pos = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_quat_t rot = {0.0f, 0.0f, 0.0f, 0.0f};
	i32 i;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));

	sk_jolt_body_t* body = jolt_test_body_box(api, SK_JOLT_MOTION_TYPE_KINEMATIC, SK_JOLT_OBJECT_LAYER_MOVING, 0.5f);
	TEST_ASSERT_NOT_NULL(body);

	const sk_jolt_vec3_t pos_a = jolt_test_vec3(3.0f, 4.0f, -2.0f);
	const sk_jolt_quat_t rot_a = {0.0f, 0.0f, 0.70710678f, 0.70710678f}; /* 90° about Z */
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(body, &pos_a));
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_rotation(body, &rot_a));

	/* 1 s of simulation (gravity included) leaves the transform untouched. */
	for (i = 0; i < 60; ++i) {
		api->step(1.0f / 60.0f, NULL, NULL);
	}
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&pos_a, &pos, 1.0e-4f));
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_rotation(body, &rot));
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, rot_a.x, rot.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, rot_a.y, rot.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, rot_a.z, rot.z);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, rot_a.w, rot.w);

	/* Mid-simulation reposition: the new transform is held from then on. */
	const sk_jolt_vec3_t pos_b = jolt_test_vec3(-1.0f, -2.0f, 5.0f);
	TEST_ASSERT_EQUAL_INT32(0, api->body_set_position(body, &pos_b));
	for (i = 0; i < 30; ++i) {
		api->step(1.0f / 60.0f, NULL, NULL);
	}
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &pos));
	TEST_ASSERT_TRUE(jolt_test_vec3_near(&pos_b, &pos, 1.0e-4f));

	api->body_destroy(body);
	api->shutdown();
}

/* Handles are validated on every accessor: NULL, use-after-destroy, and
 * stale handles from a previous world return an error instead of crashing
 * (no reading freed records, no aliasing another body). */
SK_TEST(jolt_body_handle_validation) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	sk_jolt_vec3_t v = jolt_test_vec3(0.0f, 0.0f, 0.0f);
	sk_jolt_quat_t q = {0.0f, 0.0f, 0.0f, 1.0f};

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));

	sk_jolt_body_t* body = jolt_test_body_box(api, SK_JOLT_MOTION_TYPE_DYNAMIC, SK_JOLT_OBJECT_LAYER_MOVING, 0.5f);
	TEST_ASSERT_NOT_NULL(body);
	TEST_ASSERT_EQUAL_INT32(0, api->body_get_position(body, &v));

	/* NULL handles and NULL pointer arguments fail cleanly. */
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_position(NULL, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_position(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_position(NULL, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_position(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_rotation(NULL, &q));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_rotation(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_rotation(NULL, &q));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_rotation(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_linear_velocity(NULL, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_linear_velocity(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_linear_velocity(NULL, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_linear_velocity(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_angular_velocity(NULL, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_angular_velocity(body, NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_angular_velocity(NULL, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_angular_velocity(body, NULL));

	/* Use after destroy: every accessor errors instead of crashing. */
	api->body_destroy(body);
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_position(body, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_rotation(body, &q));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_linear_velocity(body, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_angular_velocity(body, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_position(body, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_rotation(body, &q));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_linear_velocity(body, &v));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_set_angular_velocity(body, &v));
	/* Destroying an already-destroyed handle or NULL is a no-op. */
	api->body_destroy(body);
	api->body_destroy(NULL);

	/* A stale handle from a previous world stays invalid after re-init. */
	api->shutdown();
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_position(body, &v));
	TEST_ASSERT_EQUAL_INT32(0, api->init(NULL));
	TEST_ASSERT_NOT_EQUAL_INT32(0, api->body_get_position(body, &v));
	api->shutdown();
}

SK_TEST(jolt_shape_desc_layout) {
	/* The tagged union's active member is chosen by kind; POD fields sit at
	 * their natural offsets (no layout surprises at the C boundary). */
	sk_jolt_shape_desc_t box = {0};
	box.kind = SK_JOLT_SHAPE_BOX;
	box.shape.box.half_extent[0] = 1.0f;
	box.shape.box.half_extent[1] = 2.0f;
	box.shape.box.half_extent[2] = 3.0f;
	TEST_ASSERT_EQUAL_INT32((i32)SK_JOLT_SHAPE_BOX, (i32)box.kind);
	TEST_ASSERT_EQUAL_FLOAT(1.0f, box.shape.box.half_extent[0]);
	TEST_ASSERT_EQUAL_FLOAT(2.0f, box.shape.box.half_extent[1]);
	TEST_ASSERT_EQUAL_FLOAT(3.0f, box.shape.box.half_extent[2]);

	sk_jolt_shape_desc_t capsule = {0};
	capsule.kind = SK_JOLT_SHAPE_CAPSULE;
	capsule.shape.capsule.half_height = 0.75f;
	capsule.shape.capsule.radius = 0.3f;
	TEST_ASSERT_EQUAL_FLOAT(0.75f, capsule.shape.capsule.half_height);
	TEST_ASSERT_EQUAL_FLOAT(0.3f, capsule.shape.capsule.radius);
}

SK_TEST(jolt_components_register_with_ecs) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(context);
	TEST_ASSERT_NOT_NULL(app_api);

	/* The physics components register with the entities API. In the unsorted
	 * test host scan sk-jolt may run before sk-entities; then the entities
	 * table is not registered yet and the full path is covered by the host
	 * test app_init_auto_loads_jolt_plugin (sorted app auto-load) instead.
	 * When entities is live, registering again is idempotent (layout match). */
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)app_api->get_api(context, SK_ENTITIES_API_TYPE_ID);
	if (ecs == NULL) {
		return; /* entities not loaded yet; see app.c integration test */
	}
	TEST_ASSERT_EQUAL_INT32(0, sk_jolt_components_register(ecs));

	/* Cold config component: registered name + layout. */
	sk_component_info_t info;
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("rigid_body_config", info.name);
	TEST_ASSERT_EQUAL_UINT32((u32)sizeof(sk_rigid_body_config_t), info.size);
	TEST_ASSERT_EQUAL_UINT32((u32) _Alignof(sk_rigid_body_config_t), info.align);
	/* One cache line: cold config stays out of the hot per-frame path. */
	TEST_ASSERT_TRUE(sizeof(sk_rigid_body_config_t) <= 64u);

	/* Hot per-frame state component (iterated/written independently). */
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("rigid_body_state", info.name);
	TEST_ASSERT_EQUAL_UINT32((u32)sizeof(sk_rigid_body_state_t), info.size);

	/* Collider components. */
	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_BOX_COLLIDER_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("box_collider", info.name);
	TEST_ASSERT_EQUAL_UINT32((u32)sizeof(sk_box_collider_t), info.size);

	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("sphere_collider", info.name);

	memset(&info, 0, sizeof(info));
	TEST_ASSERT_EQUAL_INT32(0, ecs->component_info(SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID, &info));
	TEST_ASSERT_EQUAL_STRING("capsule_collider", info.name);

	/* A body is composed: spawn an entity with config + hot state + a box
	 * collider and round-trip values through the world storage. */
	sk_world_t* world = ecs->world_create();
	TEST_ASSERT_NOT_NULL(world);
	const sk_type_id_t signature[] = {SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, SK_BOX_COLLIDER_COMPONENT_TYPE_ID};
	sk_entity_t entity = ecs->world_spawn(world, signature, 3u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));

	sk_rigid_body_config_t* cfg = (sk_rigid_body_config_t*)ecs->world_component(world, entity, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(cfg);
	cfg->motion_type = SK_JOLT_MOTION_TYPE_DYNAMIC;
	cfg->mass = 3.0f;
	cfg->friction = 0.8f;
	cfg->gravity_factor = 1.0f;
	cfg->object_layer = SK_JOLT_OBJECT_LAYER_MOVING;
	cfg->flags = SK_RIGID_BODY_FLAG_ALLOW_SLEEPING;
	TEST_ASSERT_NULL(cfg->body); /* opaque handle starts NULL */

	sk_rigid_body_state_t* st = (sk_rigid_body_state_t*)ecs->world_component(world, entity, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(st);
	st->linear_velocity = sk_vec3(1.0f, 2.0f, 3.0f);
	st->angular_velocity = sk_vec3(0.0f, 0.5f, 0.0f);

	sk_box_collider_t* box = (sk_box_collider_t*)ecs->world_component(world, entity, SK_BOX_COLLIDER_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(box);
	box->half_extent = sk_vec3(0.5f, 0.5f, 0.5f);

	/* Read back: the hot state is written/read independently of the config. */
	const sk_rigid_body_state_t* st_r = (const sk_rigid_body_state_t*)ecs->world_component(world, entity, SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(st_r);
	TEST_ASSERT_EQUAL_FLOAT(1.0f, st_r->linear_velocity.x);
	TEST_ASSERT_EQUAL_FLOAT(3.0f, st_r->linear_velocity.z);
	TEST_ASSERT_EQUAL_FLOAT(0.5f, st_r->angular_velocity.y);
	const sk_rigid_body_config_t* cfg_r = (const sk_rigid_body_config_t*)ecs->world_component(world, entity, SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(cfg_r);
	TEST_ASSERT_EQUAL_FLOAT(3.0f, cfg_r->mass);
	TEST_ASSERT_EQUAL_FLOAT(0.8f, cfg_r->friction);
	TEST_ASSERT_EQUAL_UINT32(SK_JOLT_OBJECT_LAYER_MOVING, cfg_r->object_layer);

	TEST_ASSERT_EQUAL_INT32(0, ecs->world_despawn(world, entity));
	ecs->world_destroy(world);
}

/**
 * Plugin-local test entry. Only compiled when SK_TESTS is set (non-Release).
 * Host skips the symbol when missing (Release plugins).
 */
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out);

SK_API i32 sk_plugin_run_tests(sk_test_report_t* out) {
	return sk_test_run_all_status(out);
}

#endif /* SK_TESTS */
