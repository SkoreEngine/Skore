/**
 * @file jolt.cpp
 * @brief Physics module implementation (C++ behind the C-only jolt.h surface).
 *
 * Scaffold stage: only the module registration (sk_jolt_init, invoked from
 * sk_plugin_entry_point) and empty stubs are implemented. Every table entry
 * is a no-op or an explicit "not created" NULL return — no Jolt API is called
 * yet, and this TU intentionally does not include any Jolt header. The C++
 * language, the vendored Jolt link (target_link_libraries(sk-jolt PRIVATE
 * jolt)), and the layer/shape/motion constants in jolt.h are the scaffolding
 * for the real integration that follows.
 *
 * Unit tests live in jolt_tests.c (C, like every other plugin's tests); this
 * TU only exposes a SK_TESTS-only accessor for them.
 */

#include "jolt.h"

#include "app.h"
#include "common.h"

#include <type_traits>

/* Host app registry cached at plugin load so in-plugin tests (and later the
 * integration) can resolve this plugin's own table. */
static sk_app_context_t* g_jolt_app_context = nullptr;
static const sk_app_api_t* g_jolt_app_api = nullptr;

/* ---- empty stubs (no Jolt API calls at this stage) ---- */

static i32 jolt_init_impl() noexcept {
	return 0;
}

static void jolt_shutdown_impl() noexcept {}

static sk_jolt_body_t* jolt_body_create_impl(const sk_jolt_shape_desc_t*, sk_jolt_motion_type_t, u32) noexcept {
	return nullptr;
}

static void jolt_body_destroy_impl(sk_jolt_body_t*) noexcept {}

static sk_jolt_character_t* jolt_character_create_impl(const sk_jolt_shape_desc_t*, u32) noexcept {
	return nullptr;
}

static void jolt_character_destroy_impl(sk_jolt_character_t*) noexcept {}

/* ---- API table (table-only; no public free-function mirrors) ---- */

static const sk_jolt_api_t jolt_api = {
	jolt_init_impl, jolt_shutdown_impl, jolt_body_create_impl, jolt_body_destroy_impl, jolt_character_create_impl, jolt_character_destroy_impl,
};

/**
 * Register the physics API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_jolt_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_jolt_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	g_jolt_app_context = context;
	g_jolt_app_api = app_api;
	app_api->set_api(context, SK_JOLT_API_TYPE_ID, &jolt_api);
}

/* ---- compile-time checks on the C boundary (all builds) ---- */

/* The public surface must stay C-compatible (POD, no C++ machinery): a C
 * translation unit includes jolt.h, so the structs must be trivial and
 * standard-layout for a C++ host to pass them to the C table, and the motion
 * type must stay a plain C enum. */
static_assert(std::is_trivial<sk_jolt_shape_desc_t>::value && std::is_standard_layout<sk_jolt_shape_desc_t>::value);
static_assert(std::is_enum<sk_jolt_motion_type_t>::value && std::is_trivial<sk_jolt_motion_type_t>::value);

/* Motion type values mirror JPH::EMotionType; layer/group constants mirror
 * the vendored Jolt defaults so later integration maps 1:1. */
static_assert((int)SK_JOLT_MOTION_TYPE_STATIC == 0 && (int)SK_JOLT_MOTION_TYPE_KINEMATIC == 1 && (int)SK_JOLT_MOTION_TYPE_DYNAMIC == 2);
static_assert(SK_JOLT_OBJECT_LAYER_INVALID == 0xFFFFu);
static_assert(SK_JOLT_COLLISION_GROUP_INVALID == 0xFFFFFFFFu && SK_JOLT_COLLISION_MASK_ALL == 0xFFFFFFFFu);

#ifdef SK_TESTS
/* Test-only hook (SK_TESTS plugin builds only; never in a public header):
 * hands the plugin-local C test TU (jolt_tests.c) the app registry cached at
 * plugin load and this plugin's registered table, so the tests can verify the
 * registration landed and exercise the stubs. extern "C" keeps the names
 * unmangled for the C caller within the same shared library. */
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
