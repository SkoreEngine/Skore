/**
 * @file profiler_macro_tests.c
 * @brief Instrumentation-macro tests compiled with SK_PROFILER_ENABLED forced
 *        on, so the real macro bodies are exercised even when the global
 *        SK_ENABLE_PROFILER option is OFF (the default build mode).
 *
 * The no-op macro bodies are covered by the mode-aware test in profiler.c
 * (profiler_macros_match_compile_time_switch) and by every call site in the
 * default build. This TU is an empty translation unit in Release (SK_TESTS
 * strips the test bodies).
 */

#ifndef SK_PROFILER_ENABLED
#define SK_PROFILER_ENABLED 1
#endif

#include "profiler.h"

#ifdef SK_TESTS
#include "test.h"

#include <string.h>

/* Same DLL: the module's static table, exposed for tests by profiler.c. */
const sk_profiler_api_t* sk_profiler_test_table(void);

static i32 find_task(const sk_profiler_task_entry_t* tasks, u32 count, const_chr_t name) {
	for (u32 i = 0u; i < count; i++) {
		if (strcmp(tasks[i].name, name) == 0) {
			return (i32)i;
		}
	}
	return -1;
}

SK_TEST(profiler_macro_cpu_zone_scopes_and_merges) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	api->set_active(false);
	api->set_active(true);
	api->begin_frame(); /* frame 0 */
						/* GCC/Clang cleanup scopes the zone to the enclosing block. MSVC's
	 * for-loop form covers only the next statement — use the brace body. */
#if defined(__GNUC__) || defined(__clang__)
	{
		SK_PROFILE_CPU_ZONE(api, "outer");
		{
			SK_PROFILE_CPU_ZONE(api, "inner");
		} /* inner must end here: "after" opens at depth 1, not 2 */
		SK_PROFILE_CPU_ZONE(api, "after");
		SK_PROFILE_CPU_ZONE(api, "outer"); /* merges into the outer task */
	}
#else
	SK_PROFILE_CPU_ZONE(api, "outer") {
		SK_PROFILE_CPU_ZONE(api, "inner") {} /* inner must end here: "after" opens at depth 1, not 2 */
		SK_PROFILE_CPU_ZONE(api, "after") {}
		SK_PROFILE_CPU_ZONE(api, "outer") { /* merges into the outer task */ }
	}
#endif
	api->begin_frame(); /* frame 1 */
	api->begin_frame(); /* frame 2: builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(3u, count);

	i32 outer = find_task(tasks, count, "outer");
	i32 inner = find_task(tasks, count, "inner");
	i32 after = find_task(tasks, count, "after");
	TEST_ASSERT_TRUE(outer >= 0);
	TEST_ASSERT_TRUE(inner >= 0);
	TEST_ASSERT_TRUE(after >= 0);

	TEST_ASSERT_EQUAL_INT32(0, tasks[outer].depth);
	/* Both samples in one frame: merged into a single task (cpu_count counts
	 * present frames, so one frame -> 1). */
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[outer].cpu_count);
	TEST_ASSERT_EQUAL_INT32(1, tasks[inner].depth);
	TEST_ASSERT_EQUAL_INT32(1, tasks[after].depth); /* proves inner's end ran in its block */
	TEST_ASSERT_TRUE(tasks[outer].cpu_time >= 0.0);
	api->set_active(false);
}

SK_TEST(profiler_macro_explicit_begin_end_pair) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	api->set_active(false);
	api->set_active(true);
	api->begin_frame(); /* frame 0 */

	SK_PROFILE_BEGIN_CPU_SAMPLE(api, "explicit", NULL, 0u);
	SK_PROFILE_END_CPU_SAMPLE(api);
	SK_PROFILE_BEGIN_GPU_SAMPLE(api, "gpuonly", NULL, 0u, sk_command_buffer_t_from_u64(0x42u));
	SK_PROFILE_END_GPU_SAMPLE(api, sk_command_buffer_t_from_u64(0x42u));

	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("explicit", tasks[0].name);
	TEST_ASSERT_TRUE(tasks[0].cpu_time >= 0.0);

	api->get_gpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("gpuonly", tasks[0].name);
	/* No render device attached: CPU stamps only. */
	TEST_ASSERT_FALSE(tasks[0].has_gpu);
	TEST_ASSERT_TRUE(tasks[0].cpu_time >= 0.0);
	api->set_active(false);
}

SK_TEST(profiler_macro_ex_zone_carries_category_color) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	api->set_active(false);
	api->set_active(true);
	api->begin_frame(); /* frame 0 */
	{
		SK_PROFILE_CPU_ZONE_EX(api, "exzone", "scene", 0x11223344u);
	}
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("exzone", tasks[0].name);
	TEST_ASSERT_EQUAL_STRING("scene", tasks[0].category);
	TEST_ASSERT_EQUAL_UINT32(0x11223344u, tasks[0].color);
	api->set_active(false);
}

SK_TEST(profiler_macro_gpu_zone_scopes) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	api->set_active(false);
	api->set_active(true);
	api->begin_frame(); /* frame 0 */
	{
		sk_command_buffer_t cmd = sk_command_buffer_t_from_u64(0x55u);
		SK_PROFILE_GPU_ZONE(api, "gzone", cmd);
	}
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_gpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("gzone", tasks[0].name);
	TEST_ASSERT_TRUE(tasks[0].cpu_time >= 0.0);
	api->set_active(false);
}

#endif /* SK_TESTS */
