/**
 * @file profiler_core_tests.c
 * @brief Unit tests for the engine-independent profiler core.
 *
 * Determinism: most tests drive a fake clock (config.clock.now) so zone
 * durations and frame wall times are exact. The multi-threaded tests use the
 * real platform clock and assert structural invariants (counts, nesting,
 * per-thread attribution) instead of exact times.
 */

#include "profiler_core.h"

#ifdef SK_TESTS

#include "test.h"
#include "thread.h"

#include <stdio.h>
#include <string.h>

/* ---- deterministic fake clock ---- */

static f64 test_clock_now;

static f64 fake_clock_now(void) {
	return test_clock_now;
}

static void fake_clock_set(f64 seconds) {
	test_clock_now = seconds;
}

/* ---- helpers ---- */

static void core_config_test(sk_profiler_core_config_t* config) {
	sk_profiler_core_config_default(config);
	config->clock.now = fake_clock_now;
}

static sk_profiler_core_t* core_new(sk_profiler_core_config_t* config) {
	sk_profiler_core_t* core = sk_profiler_core_create(config);
	TEST_ASSERT_NOT_NULL(core);
	sk_profiler_core_set_active(core, true);
	return core;
}

static void core_done(sk_profiler_core_t* core) {
	sk_profiler_core_destroy(core);
}

static i32 find_task(const sk_profiler_core_task_t* tasks, u32 count, const_chr_t name) {
	for (u32 i = 0u; i < count; i++) {
		if (strcmp(tasks[i].name, name) == 0) {
			return (i32)i;
		}
	}
	return -1;
}

static void record_one_frame(sk_profiler_core_t* core, f64 begin_at, f64 zone_begin, f64 zone_end, f64 frame_end, const_chr_t name) {
	fake_clock_set(begin_at);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(zone_begin);
	sk_profiler_core_begin_zone(core, name, NULL, 0u);
	fake_clock_set(zone_end);
	sk_profiler_core_end_zone(core);
	fake_clock_set(frame_end);
	sk_profiler_core_end_frame(core);
}

/* ---- clock ---- */

SK_TEST(profiler_core_clock_default_is_high_resolution_monotonic) {
	sk_profiler_clock_t clock;
	sk_profiler_clock_default(&clock);
	TEST_ASSERT_NOT_NULL(clock.now);

	f64 first = clock.now();
	TEST_ASSERT_TRUE(first > 0.0);
	f64 second = clock.now();
	TEST_ASSERT_TRUE(second >= first);
	TEST_ASSERT_TRUE(second > 0.0);
}

SK_TEST(profiler_core_config_defaults_are_sane) {
	sk_profiler_core_config_t config;
	sk_profiler_core_config_default(&config);
	TEST_ASSERT_TRUE(config.max_threads > 0u);
	TEST_ASSERT_TRUE(config.zones_per_thread > 0u);
	TEST_ASSERT_TRUE(config.frame_zones > 0u);
	TEST_ASSERT_TRUE(config.max_tasks > 0u);
	TEST_ASSERT_NULL(config.allocator);
	TEST_ASSERT_NULL(config.clock.now);
}

SK_TEST(profiler_core_zeroed_config_uses_defaults) {
	sk_profiler_core_config_t config;
	memset(&config, 0, sizeof(config));
	sk_profiler_core_t* core = sk_profiler_core_create(&config);
	TEST_ASSERT_NOT_NULL(core);
	TEST_ASSERT_FALSE(sk_profiler_core_is_active(core));
	sk_profiler_core_set_active(core, true);
	record_one_frame(core, 1.0, 1.0, 1.5, 2.0, "defaults");
	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("defaults", tasks[0].name);
	sk_profiler_core_destroy(core);
}

/* ---- lifecycle ---- */

SK_TEST(profiler_core_init_shutdown_roundtrip) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = sk_profiler_core_create(&config);
	TEST_ASSERT_NOT_NULL(core);
	TEST_ASSERT_FALSE(sk_profiler_core_is_active(core));
	sk_profiler_core_set_active(core, true);
	TEST_ASSERT_TRUE(sk_profiler_core_is_active(core));
	sk_profiler_core_shutdown(core);
	sk_profiler_core_shutdown(core); /* double shutdown is a no-op */
	sk_profiler_core_t* defaults = sk_profiler_core_create(NULL);
	TEST_ASSERT_NOT_NULL(defaults);
	sk_profiler_core_destroy(defaults);
	sk_profiler_core_destroy(core);
}

SK_TEST(profiler_core_inactive_records_nothing) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = sk_profiler_core_create(&config);
	TEST_ASSERT_NOT_NULL(core);

	fake_clock_set(0.0);
	sk_profiler_core_begin_zone(core, "ghost", NULL, 0u);
	sk_profiler_core_end_zone(core);
	sk_profiler_core_begin_frame(core);
	sk_profiler_core_end_frame(core);

	u32 count = 123u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(0u, count);
	u32 zone_count = 123u;
	const sk_profiler_core_zone_t* zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_EQUAL_UINT32(0u, zone_count);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_frame_stats(core).count);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_dropped_zones(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_mismatched_ends(core));
	TEST_ASSERT_NOT_NULL(tasks);
	TEST_ASSERT_NOT_NULL(zones);
	sk_profiler_core_destroy(core);
}

/* ---- single-thread behavior (fake clock) ---- */

SK_TEST(profiler_core_single_zone_accumulates) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	record_one_frame(core, 0.0, 1.0, 1.5, 2.0, "zone");

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("zone", tasks[0].name);
	TEST_ASSERT_EQUAL_STRING("", tasks[0].category);
	TEST_ASSERT_TRUE(tasks[0].present);
	TEST_ASSERT_EQUAL_INT32(0, tasks[0].depth);
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].calls);
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].frames);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, tasks[0].total);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, tasks[0].min);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, tasks[0].max);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, tasks[0].avg);

	u32 zone_count = 0u;
	const sk_profiler_core_zone_t* zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_EQUAL_UINT32(1u, zone_count);
	TEST_ASSERT_EQUAL_STRING("zone", zones[0].name);
	TEST_ASSERT_FALSE(zones[0].open);
	TEST_ASSERT_EQUAL_UINT32(0u, zones[0].thread_index);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, zones[0].start);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.5, zones[0].end);

	sk_profiler_core_frame_stats_t stats = sk_profiler_core_frame_stats(core);
	TEST_ASSERT_EQUAL_UINT32(1u, stats.count);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0, stats.current); /* wall = end_frame - begin_frame */
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0, sk_profiler_core_frame_wall_time(core));
	core_done(core);
}

SK_TEST(profiler_core_nested_zones_track_depth) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(1.0);
	sk_profiler_core_begin_zone(core, "outer", "sim", 0xFF102030u);
	fake_clock_set(1.2);
	sk_profiler_core_begin_zone(core, "inner", NULL, 0u);
	fake_clock_set(1.3);
	sk_profiler_core_begin_zone(core, "leaf", NULL, 0u);
	fake_clock_set(1.4);
	sk_profiler_core_end_zone(core);
	fake_clock_set(1.5);
	sk_profiler_core_end_zone(core);
	fake_clock_set(2.5);
	sk_profiler_core_end_zone(core);
	fake_clock_set(3.0);
	sk_profiler_core_end_frame(core);

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(3u, count);

	i32 outer = find_task(tasks, count, "outer");
	i32 inner = find_task(tasks, count, "inner");
	i32 leaf = find_task(tasks, count, "leaf");
	TEST_ASSERT_TRUE(outer >= 0);
	TEST_ASSERT_TRUE(inner >= 0);
	TEST_ASSERT_TRUE(leaf >= 0);
	TEST_ASSERT_EQUAL_INT32(0, tasks[outer].depth);
	TEST_ASSERT_EQUAL_INT32(1, tasks[inner].depth);
	TEST_ASSERT_EQUAL_INT32(2, tasks[leaf].depth);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.5, tasks[outer].total); /* 1.0 -> 2.5 */
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.3, tasks[inner].total); /* 1.2 -> 1.5 */
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.1, tasks[leaf].total);  /* 1.3 -> 1.4 */
	TEST_ASSERT_EQUAL_STRING("sim", tasks[outer].category);
	TEST_ASSERT_EQUAL_UINT32(0xFF102030u, tasks[outer].color);
	TEST_ASSERT_EQUAL_STRING("", tasks[inner].category);
	core_done(core);
}

SK_TEST(profiler_core_same_name_zones_merge) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	for (u32 i = 0u; i < 3u; i++) {
		fake_clock_set(1.0 + (f64)i);
		sk_profiler_core_begin_zone(core, "merged", NULL, 0u);
		fake_clock_set(1.0 + (f64)i + 0.25);
		sk_profiler_core_end_zone(core);
	}
	fake_clock_set(5.0);
	sk_profiler_core_end_frame(core);

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("merged", tasks[0].name);
	TEST_ASSERT_EQUAL_UINT32(3u, tasks[0].calls);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.75, tasks[0].total);
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].frames);
	core_done(core);
}

SK_TEST(profiler_core_rolling_stats_across_frames) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	record_one_frame(core, 0.0, 1.0, 2.0, 5.0, "stat");		/* total 1.0, wall 5.0 */
	record_one_frame(core, 10.0, 11.0, 14.0, 20.0, "stat"); /* total 3.0, wall 10.0 */

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_UINT32(2u, tasks[0].frames);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, tasks[0].min);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 3.0, tasks[0].max);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0, tasks[0].avg);

	sk_profiler_core_frame_stats_t stats = sk_profiler_core_frame_stats(core);
	TEST_ASSERT_EQUAL_UINT32(2u, stats.count);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 5.0, stats.min);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 10.0, stats.max);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 7.5, stats.avg);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 10.0, stats.current);
	core_done(core);
}

SK_TEST(profiler_core_absent_task_not_present_second_frame) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	record_one_frame(core, 0.0, 1.0, 1.5, 2.0, "seen");
	fake_clock_set(3.0);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(4.0);
	sk_profiler_core_end_frame(core); /* empty frame: task stays, present=false */

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_FALSE(tasks[0].present);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, tasks[0].total);
	TEST_ASSERT_EQUAL_UINT32(0u, tasks[0].calls);
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].frames); /* rolling count unchanged */
	core_done(core);
}

/* ---- overflow policies (bounded, non-crashing) ---- */

SK_TEST(profiler_core_thread_ring_overflow_drops_newest) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	config.zones_per_thread = 4u;
	config.frame_zones = 64u;
	sk_profiler_core_t* core = core_new(&config);

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	for (u32 i = 0u; i < 6u; i++) {
		char name[16];
		(void)snprintf(name, sizeof(name), "z%u", i);
		fake_clock_set(1.0 + (f64)i);
		sk_profiler_core_begin_zone(core, name, NULL, 0u);
		fake_clock_set(1.0 + (f64)i + 0.1);
		sk_profiler_core_end_zone(core);
	}
	fake_clock_set(8.0);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(2u, sk_profiler_core_dropped_zones(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_mismatched_ends(core));

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(4u, count); /* first four zones recorded */
	TEST_ASSERT_TRUE(find_task(tasks, count, "z0") >= 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "z3") >= 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "z4") < 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "z5") < 0);
	core_done(core);
}

SK_TEST(profiler_core_dropped_begin_keeps_depth_consistent) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	config.zones_per_thread = 2u;
	config.frame_zones = 64u;
	sk_profiler_core_t* core = core_new(&config);

	/* Frame 1: two zones fit, the third begin is dropped (per-frame cap) and
	 * its end is consumed as a phantom; the following ends pair correctly. */
	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(1.0);
	sk_profiler_core_begin_zone(core, "a", NULL, 0u);
	fake_clock_set(1.1);
	sk_profiler_core_begin_zone(core, "b", NULL, 0u);
	fake_clock_set(1.2);
	sk_profiler_core_begin_zone(core, "c", NULL, 0u); /* dropped: buffer full */
	fake_clock_set(1.3);
	sk_profiler_core_end_zone(core); /* closes b */
	fake_clock_set(1.4);
	sk_profiler_core_end_zone(core); /* closes a */
	fake_clock_set(1.5);
	sk_profiler_core_end_zone(core); /* phantom: end of the dropped c */
	fake_clock_set(2.0);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(1u, sk_profiler_core_dropped_zones(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_mismatched_ends(core));

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	i32 a = find_task(tasks, count, "a");
	i32 b = find_task(tasks, count, "b");
	TEST_ASSERT_TRUE(a >= 0 && b >= 0);
	TEST_ASSERT_EQUAL_INT32(0, tasks[a].depth);
	TEST_ASSERT_EQUAL_INT32(1, tasks[b].depth);

	/* Frame 2 (buffer drained): depth restarts at 0 and nests again — the
	 * drop in frame 1 left no stale nesting state behind. */
	fake_clock_set(3.0);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(4.0);
	sk_profiler_core_begin_zone(core, "d", NULL, 0u);
	fake_clock_set(4.1);
	sk_profiler_core_begin_zone(core, "e", NULL, 0u);
	fake_clock_set(4.2);
	sk_profiler_core_end_zone(core);
	fake_clock_set(4.3);
	sk_profiler_core_end_zone(core);
	fake_clock_set(5.0);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_dropped_zones(core) - 1u); /* still exactly one drop total */
	tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(4u, count);
	i32 d = find_task(tasks, count, "d");
	i32 e = find_task(tasks, count, "e");
	TEST_ASSERT_TRUE(d >= 0 && e >= 0);
	TEST_ASSERT_EQUAL_INT32(0, tasks[d].depth);
	TEST_ASSERT_EQUAL_INT32(1, tasks[e].depth);
	core_done(core);
}

SK_TEST(profiler_core_mismatched_end_counted) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	sk_profiler_core_end_zone(core); /* no matching begin */
	sk_profiler_core_begin_zone(core, "x", NULL, 0u);
	sk_profiler_core_end_zone(core);
	sk_profiler_core_end_zone(core); /* second end: mismatch */
	fake_clock_set(1.0);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(2u, sk_profiler_core_mismatched_ends(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_dropped_zones(core));

	u32 count = 0u;
	sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count); /* x recorded despite the noise */
	core_done(core);
}

SK_TEST(profiler_core_task_table_overflow_drops_new_names) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	config.max_tasks = 2u;
	config.frame_zones = 64u;
	sk_profiler_core_t* core = core_new(&config);

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	const char* names[] = {"alpha", "beta", "gamma", "delta"};
	for (u32 i = 0u; i < 4u; i++) {
		fake_clock_set(1.0 + (f64)i);
		sk_profiler_core_begin_zone(core, names[i], NULL, 0u);
		fake_clock_set(1.0 + (f64)i + 0.1);
		sk_profiler_core_end_zone(core);
	}
	fake_clock_set(6.0);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(2u, sk_profiler_core_dropped_tasks(core));

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	TEST_ASSERT_TRUE(find_task(tasks, count, "alpha") >= 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "beta") >= 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "gamma") < 0);

	/* The dropped names still appear in the frame zone list. */
	u32 zone_count = 0u;
	const sk_profiler_core_zone_t* zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_EQUAL_UINT32(4u, zone_count);
	TEST_ASSERT_EQUAL_STRING("gamma", zones[2].name);
	core_done(core);
}

SK_TEST(profiler_core_frame_ring_keeps_newest) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	config.frame_zones = 3u;
	sk_profiler_core_t* core = core_new(&config);

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	for (u32 i = 0u; i < 5u; i++) {
		char name[16];
		(void)snprintf(name, sizeof(name), "f%u", i);
		fake_clock_set(1.0 + (f64)i);
		sk_profiler_core_begin_zone(core, name, NULL, 0u);
		fake_clock_set(1.0 + (f64)i + 0.1);
		sk_profiler_core_end_zone(core);
	}
	fake_clock_set(7.0);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(2u, sk_profiler_core_frame_overwrites(core));

	u32 zone_count = 0u;
	const sk_profiler_core_zone_t* zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_EQUAL_UINT32(3u, zone_count);
	TEST_ASSERT_EQUAL_STRING("f2", zones[0].name); /* oldest kept record */
	TEST_ASSERT_EQUAL_STRING("f3", zones[1].name);
	TEST_ASSERT_EQUAL_STRING("f4", zones[2].name);

	/* Task accumulation reads the frame ring (the frame's zone set), so the
	 * overwritten zones are absent from this frame's stats too. */
	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(3u, count);
	TEST_ASSERT_TRUE(find_task(tasks, count, "f2") >= 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "f4") >= 0);
	TEST_ASSERT_TRUE(find_task(tasks, count, "f0") < 0);
	core_done(core);
}

/* ---- frame / zone lifecycle ---- */

SK_TEST(profiler_core_open_zone_spans_frames) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	/* Frame 1: "open" is still open at end_frame -> stays in the thread ring. */
	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(1.0);
	sk_profiler_core_begin_zone(core, "open", NULL, 0u);
	fake_clock_set(2.0);
	sk_profiler_core_end_frame(core);

	u32 zone_count = 0u;
	const sk_profiler_core_zone_t* zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_EQUAL_UINT32(0u, zone_count);

	/* Frame 2: the zone closes inside it and lands there. */
	fake_clock_set(2.5);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(3.0);
	sk_profiler_core_end_zone(core);
	fake_clock_set(4.0);
	sk_profiler_core_end_frame(core);

	zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_EQUAL_UINT32(1u, zone_count);
	TEST_ASSERT_EQUAL_STRING("open", zones[0].name);
	TEST_ASSERT_FALSE(zones[0].open);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0, zones[0].end - zones[0].start);

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].frames);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0, tasks[0].total);
	core_done(core);
}

SK_TEST(profiler_core_reset_stats_keeps_names) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	record_one_frame(core, 0.0, 1.0, 2.0, 5.0, "keep");
	sk_profiler_core_reset_stats(core);

	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_frame_stats(core).count);
	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("keep", tasks[0].name);
	TEST_ASSERT_EQUAL_UINT32(0u, tasks[0].frames);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, tasks[0].avg);
	TEST_ASSERT_TRUE(tasks[0].present); /* per-frame data survives */

	record_one_frame(core, 10.0, 11.0, 12.0, 15.0, "keep");
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].frames); /* rolling restarted */
	core_done(core);
}

SK_TEST(profiler_core_set_active_false_clears_state) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	record_one_frame(core, 0.0, 1.0, 1.5, 2.0, "gone");
	sk_profiler_core_set_active(core, false);

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(0u, count);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_frame_stats(core).count);

	/* Zones recorded while inactive are ignored. */
	sk_profiler_core_begin_zone(core, "ghost", NULL, 0u);
	sk_profiler_core_end_zone(core);
	sk_profiler_core_begin_frame(core);
	sk_profiler_core_end_frame(core);
	tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(0u, count);

	/* Recording resumes after reactivation. */
	sk_profiler_core_set_active(core, true);
	record_one_frame(core, 5.0, 6.0, 6.5, 7.0, "back");
	tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("back", tasks[0].name);
	core_done(core);
}

SK_TEST(profiler_core_name_and_category_truncated) {
	sk_profiler_core_config_t config;
	core_config_test(&config);
	sk_profiler_core_t* core = core_new(&config);

	char long_name[80];
	char long_category[48];
	memset(long_name, 'n', sizeof(long_name) - 1u);
	long_name[sizeof(long_name) - 1u] = '\0';
	memset(long_category, 'c', sizeof(long_category) - 1u);
	long_category[sizeof(long_category) - 1u] = '\0';

	fake_clock_set(0.0);
	sk_profiler_core_begin_frame(core);
	fake_clock_set(1.0);
	sk_profiler_core_begin_zone(core, long_name, long_category, 0u);
	fake_clock_set(1.5);
	sk_profiler_core_end_zone(core);
	fake_clock_set(2.0);
	sk_profiler_core_end_frame(core);

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_UINT32(SK_PROFILER_CORE_NAME_CAP - 1u, (u32)strlen(tasks[0].name));
	TEST_ASSERT_EQUAL_UINT32(SK_PROFILER_CORE_CATEGORY_CAP - 1u, (u32)strlen(tasks[0].category));
	TEST_ASSERT_EQUAL_CHAR('\0', tasks[0].name[SK_PROFILER_CORE_NAME_CAP - 1u]);
	TEST_ASSERT_EQUAL_CHAR('\0', tasks[0].category[SK_PROFILER_CORE_CATEGORY_CAP - 1u]);
	core_done(core);
}

/* ---- multi-threaded capture (real clock, lock-free registration) ---- */

typedef struct worker_args_t {
	sk_profiler_core_t* core;
	u32 id;
} worker_args_t;

static i32 worker_record_zones(void_ptr_t arg) {
	worker_args_t* args = (worker_args_t*)arg;
	char name[16];
	(void)snprintf(name, sizeof(name), "worker%u", args->id);

	/* Mix nesting and sequential zones; every zone closes before returning. */
	for (u32 i = 0u; i < 100u; i++) {
		sk_profiler_core_begin_zone(args->core, name, NULL, 0u);
		sk_profiler_core_begin_zone(args->core, "workerChild", NULL, 0u);
		sk_profiler_core_end_zone(args->core);
		sk_profiler_core_end_zone(args->core);
	}
	return 0;
}

SK_TEST(profiler_core_worker_threads_record_lockfree) {
	sk_profiler_core_config_t config;
	sk_profiler_core_config_default(&config); /* real clock; defaults */
	config.max_threads = 8u;
	sk_profiler_core_t* core = sk_profiler_core_create(&config);
	TEST_ASSERT_NOT_NULL(core);
	sk_profiler_core_set_active(core, true);

	sk_profiler_core_begin_frame(core);

	worker_args_t args[3];
	sk_thread_t* threads[3];
	for (u32 i = 0u; i < 3u; i++) {
		args[i].core = core;
		args[i].id = i;
		threads[i] = sk_thread_create(worker_record_zones, &args[i]);
		TEST_ASSERT_NOT_NULL(threads[i]);
	}
	for (u32 i = 0u; i < 3u; i++) {
		TEST_ASSERT_EQUAL_INT(0, sk_thread_join(threads[i]));
		sk_thread_destroy(threads[i]);
	}

	/* Main-thread zone, recorded between the worker batches. */
	sk_profiler_core_begin_zone(core, "main", NULL, 0u);
	sk_profiler_core_end_zone(core);
	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_dropped_zones(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_mismatched_ends(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_thread_overflows(core));

	u32 count = 0u;
	const sk_profiler_core_task_t* tasks = sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(5u, count); /* main + 3 workers + workerChild */
	i32 main_task = find_task(tasks, count, "main");
	i32 child_task = find_task(tasks, count, "workerChild");
	TEST_ASSERT_TRUE(main_task >= 0);
	TEST_ASSERT_TRUE(child_task >= 0);
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[main_task].calls);
	TEST_ASSERT_EQUAL_UINT32(300u, tasks[child_task].calls); /* merged across workers */
	for (u32 i = 0u; i < 3u; i++) {
		char name[16];
		(void)snprintf(name, sizeof(name), "worker%u", i);
		i32 idx = find_task(tasks, count, name);
		TEST_ASSERT_TRUE(idx >= 0);
		TEST_ASSERT_EQUAL_UINT32(100u, tasks[idx].calls);
		TEST_ASSERT_TRUE(tasks[idx].total >= 0.0);
	}

	/* Per-thread attribution: each worker zone name maps to exactly one
	 * thread index; workers got distinct indices 1..3, main got 0 (the
	 * recording thread drains first, so its zone leads the list). */
	u32 zone_count = 0u;
	const sk_profiler_core_zone_t* zones = sk_profiler_core_frame_zones(core, &zone_count);
	TEST_ASSERT_TRUE(zone_count >= 301u);				 /* 300 worker + 1 main (no drops) */
	TEST_ASSERT_EQUAL_UINT32(0u, zones[0].thread_index); /* main zone recorded first */
	for (u32 i = 0u; i < zone_count; i++) {
		TEST_ASSERT_TRUE(zones[i].thread_index <= 3u);
	}
	/* The three worker names each carry one of the worker thread indices. */
	u32 worker_indices[3] = {0u, 0u, 0u};
	bool saw[4] = {false, false, false, false};
	for (u32 i = 0u; i < zone_count; i++) {
		const_chr_t name = zones[i].name;
		if (name[0] == 'w' && name[6] >= '0' && name[6] <= '2' && name[7] == '\0') {
			u32 id = (u32)(name[6] - '0');
			worker_indices[id] = zones[i].thread_index;
			saw[zones[i].thread_index] = true;
		}
	}
	TEST_ASSERT_TRUE(worker_indices[0] != 0u && worker_indices[1] != 0u && worker_indices[2] != 0u);
	TEST_ASSERT_TRUE(worker_indices[0] != worker_indices[1] && worker_indices[1] != worker_indices[2] && worker_indices[0] != worker_indices[2]);
	TEST_ASSERT_TRUE(saw[1] && saw[2] && saw[3]);

	sk_profiler_core_destroy(core);
}

SK_TEST(profiler_core_thread_cap_overflow_is_counted) {
	sk_profiler_core_config_t config;
	sk_profiler_core_config_default(&config);
	config.max_threads = 1u; /* recording thread only */
	sk_profiler_core_t* core = sk_profiler_core_create(&config);
	TEST_ASSERT_NOT_NULL(core);
	sk_profiler_core_set_active(core, true);

	sk_profiler_core_begin_frame(core);

	worker_args_t args;
	args.core = core;
	args.id = 7u;
	sk_thread_t* thread = sk_thread_create(worker_record_zones, &args);
	TEST_ASSERT_NOT_NULL(thread);
	TEST_ASSERT_EQUAL_INT(0, sk_thread_join(thread));
	sk_thread_destroy(thread);

	sk_profiler_core_end_frame(core);

	TEST_ASSERT_EQUAL_UINT32(1u, sk_profiler_core_thread_overflows(core));
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_dropped_zones(core));

	/* The overflowed thread recorded nothing; the core stays healthy. */
	u32 count = 0u;
	sk_profiler_core_tasks(core, &count);
	TEST_ASSERT_EQUAL_UINT32(0u, count);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_profiler_core_mismatched_ends(core));
	sk_profiler_core_destroy(core);
}

#endif /* SK_TESTS */
