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
 * the giant-frame spiral-of-death clamp, and the still-stubbed body /
 * character entry points.
 */

/* Release builds strip every symbol below (SK_TESTS undefined); keep the TU
 * non-empty so it is still a valid translation unit under -Wpedantic (ISO C
 * requires at least one declaration; clang-tidy errors on empty TUs). */
typedef int sk_jolt_tests_tu_anchor_t;

#ifdef SK_TESTS

#include "jolt.h"

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

SK_TEST(jolt_body_character_stubs) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;

	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	/* Stub create entry points report "not implemented" via NULL; destroy
	 * tolerates NULL. (Union members are set explicitly: only the first union
	 * member can be brace-initialized portably.) */
	sk_jolt_shape_desc_t box = {0};
	box.kind = SK_JOLT_SHAPE_BOX;
	box.shape.box.half_extent[0] = 0.5f;
	box.shape.box.half_extent[1] = 0.5f;
	box.shape.box.half_extent[2] = 0.5f;
	TEST_ASSERT_NULL(api->body_create(&box, SK_JOLT_MOTION_TYPE_DYNAMIC, SK_JOLT_OBJECT_LAYER_MOVING));
	api->body_destroy(NULL);

	sk_jolt_shape_desc_t capsule = {0};
	capsule.kind = SK_JOLT_SHAPE_CAPSULE;
	capsule.shape.capsule.half_height = 0.75f;
	capsule.shape.capsule.radius = 0.3f;
	TEST_ASSERT_NULL(api->character_create(&capsule, SK_JOLT_OBJECT_LAYER_MOVING));
	api->character_destroy(NULL);
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

/**
 * Plugin-local test entry. Only compiled when SK_TESTS is set (non-Release).
 * Host skips the symbol when missing (Release plugins).
 */
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out);

SK_API i32 sk_plugin_run_tests(sk_test_report_t* out) {
	return sk_test_run_all_status(out);
}

#endif /* SK_TESTS */
