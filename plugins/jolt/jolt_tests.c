/**
 * @file jolt_tests.c
 * @brief Plugin-local unit tests for the physics module (SK_TESTS builds only).
 *
 * Test code stays in C like every other plugin's tests (the C++ TUs in this
 * plugin never include test.h); the SK_TESTS-only accessors defined in
 * jolt.cpp hand this TU the app registry and the registered sk_jolt_api_t.
 * sk_plugin_run_tests lives here too so the host test scanner (tests/main.c)
 * finds it under the same SK_TESTS guard as the other plugins.
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

SK_TEST(jolt_stubs_callable) {
	const sk_jolt_api_t* api = NULL;
	sk_app_context_t* context = NULL;
	const sk_app_api_t* app_api = NULL;
	jolt_tests_resolve(&api, &context, &app_api);
	TEST_ASSERT_NOT_NULL(api);

	/* init/shutdown are idempotent no-ops that must not crash. */
	TEST_ASSERT_EQUAL_INT32(0, api->init());
	TEST_ASSERT_EQUAL_INT32(0, api->init());
	api->shutdown();
	api->shutdown();

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
