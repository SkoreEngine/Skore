/**
 * @file profiler.c
 * @brief Hierarchical CPU/GPU profiler (v2 C port of main's Skore::Profiler).
 *
 * Singleton module state (one table per loaded plugin, main-thread owned).
 * Design matches main 1:1:
 *
 *  - Triple-buffered per-frame samples (SK_PROFILER_BUFFER_COUNT buffers of
 *    SK_PROFILER_MAX_SAMPLES). begin_frame advances write/read indices,
 *    builds the task list from the buffer written two frames ago (GPU
 *    timestamps are complete by then), then clears the write buffer so it is
 *    recycled this frame.
 *  - CPU samples stamp the platform monotonic clock relative to the frame
 *    start; GPU samples additionally record timestamp query pairs on the
 *    command buffer when GPU pools are attached (init with a render device).
 *  - Same-name samples merge into one sk_profiler_task_entry_t per read
 *    frame; entries carry rolling min/max/avg across frames and a depth so
 *    consumers can render a tree. CPU frame stats = wall clock between
 *    begin_frames; GPU frame stats = sum of depth-0 GPU task times.
 *
 * The CPU path needs no render device; the GPU path resolves
 * sk_render_device_api_t from the app registry at init(dev) time (never
 * linked directly). No third-party profiler dependency (no Tracy).
 */

#include "profiler.h"

#include "app.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

/* ---- module state (plain names; main-thread owned) ---- */

typedef struct sk_profiler_sample_t {
	char name[SK_PROFILER_NAME_CAP];
	char category[SK_PROFILER_CATEGORY_CAP];
	f64 cpu_start;
	f64 cpu_end;
	u32 gpu_query_start;
	u32 gpu_query_end;
	u32 color; /* explicit color, or palette color derived from the name hash when 0 was passed */
	bool has_gpu;
	i32 depth;
} sk_profiler_sample_t;

typedef struct sk_profiler_buffer_t {
	sk_profiler_sample_t samples[SK_PROFILER_MAX_SAMPLES];
	u32 sample_count;
	sk_query_pool_t query_pool;
	u32 query_index;
	bool query_pool_reset;
} sk_profiler_buffer_t;

typedef struct sk_profiler_context_t {
	sk_profiler_buffer_t buffers[SK_PROFILER_BUFFER_COUNT];
	i32 write_idx;
	i32 read_idx;
	i32 sample_stack[SK_PROFILER_MAX_SAMPLES];
	i32 stack_depth;
	sk_profiler_task_entry_t tasks[SK_PROFILER_MAX_SAMPLES];
	u32 task_count;
	f64 frame_time_cur;
	f64 frame_time_min;
	f64 frame_time_max;
	f64 frame_time_avg;
	u32 frame_time_count;
} sk_profiler_context_t;

/* Fixed color palette for auto-coloring from the name hash. */
static const u32 color_palette[] = {
	0xFF9C1ABC, /* turquoise  */
	0xFF60CC2E, /* emerald    */
	0xFFDB9834, /* peterRiver */
	0xFFB6599B, /* amethyst   */
	0xFF0FC4F1, /* sunFlower  */
	0xFF229CE6, /* carrot     */
	0xFF3C4CE7, /* alizarin   */
	0xFF85A016, /* greenSea   */
	0xFF60AE27, /* nephritis  */
	0xFFB98029, /* belizeHole */
	0xFFAD448E, /* wisteria   */
	0xFF129CF3, /* orange     */
	0xFF0054D3, /* pumpkin    */
	0xFFF0ECE1, /* clouds     */
	0xFFC7C3BD, /* silver     */
	0xFF2B39C0, /* pomegranate */
};
static const u32 color_palette_size = (u32)(sizeof(color_palette) / sizeof(color_palette[0]));

static sk_profiler_context_t cpu_ctx;
static sk_profiler_context_t gpu_ctx;

static u32 frame_number;
static f64 frame_start_seconds;
static bool active;
static bool has_last_frame;

static f32 timestamp_period;

static sk_app_context_t* app_context;
static const sk_app_api_t* app_api_table;
static const sk_platform_api_t* platform_api;
static const sk_render_device_api_t* render_api;
static sk_render_device_t device;

/* ---- helpers ---- */

static u32 hash_name(const_chr_t name) {
	u32 hash = 2166136261u;
	while (*name != '\0') {
		hash ^= (u32)(u8)*name++;
		hash *= 16777619u;
	}
	return hash;
}

static u32 color_for_name(const_chr_t name) {
	return color_palette[hash_name(name) % color_palette_size];
}

static f64 elapsed_seconds(void) {
	return platform_api->monotonic_seconds() - frame_start_seconds;
}

static i32 find_task(sk_profiler_context_t* ctx, const_chr_t name) {
	for (u32 i = 0u; i < ctx->task_count; i++) {
		if (strcmp(ctx->tasks[i].name, name) == 0) {
			return (i32)i;
		}
	}
	return -1;
}

static void accumulate_frame_stat(sk_profiler_context_t* ctx, f64 value) {
	if (ctx->frame_time_count == 0u) {
		ctx->frame_time_min = value;
		ctx->frame_time_max = value;
		ctx->frame_time_avg = value;
	} else {
		if (value < ctx->frame_time_min) {
			ctx->frame_time_min = value;
		}
		if (value > ctx->frame_time_max) {
			ctx->frame_time_max = value;
		}
		ctx->frame_time_avg += (value - ctx->frame_time_avg) / (f64)(ctx->frame_time_count + 1u);
	}
	ctx->frame_time_cur = value;
	ctx->frame_time_count++;
}

static void reset_frame_stats(sk_profiler_context_t* ctx) {
	ctx->frame_time_cur = 0.0;
	ctx->frame_time_min = 0.0;
	ctx->frame_time_max = 0.0;
	ctx->frame_time_avg = 0.0;
	ctx->frame_time_count = 0u;
}

static void copy_name(char dst[SK_PROFILER_NAME_CAP], const_chr_t name) {
	size_t len = strlen(name);
	if (len >= SK_PROFILER_NAME_CAP) {
		len = SK_PROFILER_NAME_CAP - 1u;
	}
	memcpy(dst, name, len);
	dst[len] = '\0';
}

static void copy_category(char dst[SK_PROFILER_CATEGORY_CAP], const_chr_t category) {
	if (category == NULL || category[0] == '\0') {
		dst[0] = '\0';
		return;
	}
	size_t len = strlen(category);
	if (len >= SK_PROFILER_CATEGORY_CAP) {
		len = SK_PROFILER_CATEGORY_CAP - 1u;
	}
	memcpy(dst, category, len);
	dst[len] = '\0';
}

/* ---- sampling ---- */

static void begin_sample(sk_profiler_context_t* ctx, const_chr_t name, const_chr_t category, u32 color, sk_command_buffer_t cmd) {
	if (!active) {
		return;
	}

	sk_profiler_buffer_t* buf = &ctx->buffers[ctx->write_idx];
	if (buf->sample_count >= SK_PROFILER_MAX_SAMPLES) {
		return;
	}

	u32 idx = buf->sample_count++;
	sk_profiler_sample_t* sample = &buf->samples[idx];

	copy_name(sample->name, name);
	copy_category(sample->category, category);
	sample->color = (color != 0u) ? color : color_for_name(name);
	sample->cpu_start = elapsed_seconds();
	sample->depth = ctx->stack_depth;

	if (sk_command_buffer_t_is_valid(cmd) && sk_query_pool_t_is_valid(buf->query_pool) && render_api != NULL) {
		if (!buf->query_pool_reset) {
			render_api->reset_query_pool(device, cmd, buf->query_pool, 0u, SK_PROFILER_MAX_SAMPLES * 2u);
			buf->query_pool_reset = true;
		}
		sample->gpu_query_start = buf->query_index++;
		render_api->write_timestamp(device, cmd, buf->query_pool, sample->gpu_query_start);
		sample->has_gpu = true;
	} else {
		sample->has_gpu = false;
	}

	ctx->sample_stack[ctx->stack_depth++] = (i32)idx;
}

static void end_sample(sk_profiler_context_t* ctx, sk_command_buffer_t cmd) {
	if (!active || ctx->stack_depth <= 0) {
		return;
	}

	ctx->stack_depth--;
	i32 idx = ctx->sample_stack[ctx->stack_depth];

	sk_profiler_buffer_t* buf = &ctx->buffers[ctx->write_idx];
	sk_profiler_sample_t* sample = &buf->samples[idx];

	sample->cpu_end = elapsed_seconds();

	if (sk_command_buffer_t_is_valid(cmd) && sample->has_gpu && sk_query_pool_t_is_valid(buf->query_pool) && render_api != NULL) {
		sample->gpu_query_end = buf->query_index++;
		render_api->write_timestamp(device, cmd, buf->query_pool, sample->gpu_query_end);
	}
}

/* ---- task building (read buffer, two frames ago) ---- */

static void build_tasks(sk_profiler_context_t* ctx, bool gpu) {
	sk_profiler_buffer_t* buf = &ctx->buffers[ctx->read_idx];

	u64 gpu_timestamps[SK_PROFILER_MAX_SAMPLES * 2u];
	bool has_gpu_results = false;
	if (buf->query_index > 0u && sk_query_pool_t_is_valid(buf->query_pool) && render_api != NULL) {
		has_gpu_results = render_api->get_query_pool_results(device, buf->query_pool, 0u, buf->query_index, gpu_timestamps, sizeof(gpu_timestamps), sizeof(u64), true) == 0;
	}

	for (u32 i = 0u; i < ctx->task_count; i++) {
		ctx->tasks[i].present = false;
		ctx->tasks[i].cpu_time = 0.0;
		ctx->tasks[i].gpu_time = 0.0;
		ctx->tasks[i].has_gpu = false;
	}

	i32 path_stack[SK_PROFILER_MAX_SAMPLES];

	for (u32 i = 0u; i < buf->sample_count; i++) {
		sk_profiler_sample_t* sample = &buf->samples[i];

		i32 idx = find_task(ctx, sample->name);
		if (idx < 0) {
			if (ctx->task_count >= SK_PROFILER_MAX_SAMPLES) {
				continue;
			}

			/* Insert so children sit directly under their parent (depth order). */
			i32 insert_at = (i32)ctx->task_count;
			if (sample->depth > 0) {
				insert_at = path_stack[sample->depth - 1] + 1;
				while (insert_at < (i32)ctx->task_count && ctx->tasks[insert_at].depth > sample->depth) {
					insert_at++;
				}
			}

			for (i32 j = (i32)ctx->task_count; j > insert_at; j--) {
				ctx->tasks[j] = ctx->tasks[j - 1];
			}
			ctx->task_count++;

			idx = insert_at;
			sk_profiler_task_entry_t* created = &ctx->tasks[idx];
			copy_name(created->name, sample->name);
			copy_category(created->category, sample->category);
			created->color = sample->color;
			created->depth = sample->depth;
			created->cpu_time = 0.0;
			created->gpu_time = 0.0;
			created->cpu_min = 0.0;
			created->cpu_max = 0.0;
			created->cpu_avg = 0.0;
			created->gpu_min = 0.0;
			created->gpu_max = 0.0;
			created->gpu_avg = 0.0;
			created->cpu_count = 0u;
			created->gpu_count = 0u;
			created->has_gpu = false;
			created->present = false;
		}

		sk_profiler_task_entry_t* task = &ctx->tasks[idx];
		task->present = true;
		task->cpu_time += sample->cpu_end - sample->cpu_start;

		if (sample->has_gpu && has_gpu_results) {
			u64 start_tick = gpu_timestamps[sample->gpu_query_start];
			u64 end_tick = gpu_timestamps[sample->gpu_query_end];
			/* Guard against invalid/partial results (end before start). */
			if (end_tick > start_tick) {
				task->gpu_time += (f64)(end_tick - start_tick) * (f64)timestamp_period * 1e-9;
				task->has_gpu = true;
			}
		}

		path_stack[sample->depth] = idx;
	}

	f64 frame_total = 0.0;
	for (u32 i = 0u; i < ctx->task_count; i++) {
		sk_profiler_task_entry_t* task = &ctx->tasks[i];
		if (!task->present) {
			continue;
		}

		if (task->cpu_count == 0u) {
			task->cpu_min = task->cpu_time;
			task->cpu_max = task->cpu_time;
			task->cpu_avg = task->cpu_time;
		} else {
			if (task->cpu_time < task->cpu_min) {
				task->cpu_min = task->cpu_time;
			}
			if (task->cpu_time > task->cpu_max) {
				task->cpu_max = task->cpu_time;
			}
			task->cpu_avg += (task->cpu_time - task->cpu_avg) / (f64)(task->cpu_count + 1u);
		}
		task->cpu_count++;

		if (task->has_gpu) {
			if (task->gpu_count == 0u) {
				task->gpu_min = task->gpu_time;
				task->gpu_max = task->gpu_time;
				task->gpu_avg = task->gpu_time;
			} else {
				if (task->gpu_time < task->gpu_min) {
					task->gpu_min = task->gpu_time;
				}
				if (task->gpu_time > task->gpu_max) {
					task->gpu_max = task->gpu_time;
				}
				task->gpu_avg += (task->gpu_time - task->gpu_avg) / (f64)(task->gpu_count + 1u);
			}
			task->gpu_count++;
		}

		/* Per-frame total accumulates from root (depth-0) entries only. */
		if (task->depth == 0) {
			frame_total += gpu ? task->gpu_time : task->cpu_time;
		}
	}

	/* CPU frame stat is the wall-clock delta (accumulated in begin_frame);
	 * only the GPU profiler derives its frame stat from the task sums. */
	if (gpu) {
		accumulate_frame_stat(ctx, frame_total);
	}
}

static void advance_context(sk_profiler_context_t* ctx, bool gpu) {
	ctx->write_idx = (i32)(frame_number % SK_PROFILER_BUFFER_COUNT);
	/* Read from two frames ago: GPU work is complete by then. */
	ctx->read_idx = (i32)((frame_number + SK_PROFILER_BUFFER_COUNT - 2u) % SK_PROFILER_BUFFER_COUNT);

	if (frame_number >= 2u) {
		build_tasks(ctx, gpu);
	}

	/* Clear the write buffer so it is recycled for this frame. */
	ctx->buffers[ctx->write_idx].sample_count = 0u;
	ctx->buffers[ctx->write_idx].query_index = 0u;
	ctx->buffers[ctx->write_idx].query_pool_reset = false;

	ctx->stack_depth = 0;
}

/* ---- API table entries (static) ---- */

static i32 profiler_init(sk_render_device_t dev);
static void profiler_shutdown(void);
static bool profiler_is_active(void);

static i32 profiler_init(sk_render_device_t dev) {
	profiler_shutdown();

	device = dev;
	render_api = NULL;
	if (sk_render_device_t_is_valid(dev)) {
		render_api = (const sk_render_device_api_t*)app_api_table->get_api(app_context, SK_RENDER_DEVICE_API_TYPE_ID);
		if (render_api != NULL) {
			sk_device_properties_t props = render_api->get_properties(dev);
			timestamp_period = props.limits.timestamp_period;

			for (u32 i = 0u; i < SK_PROFILER_BUFFER_COUNT; i++) {
				sk_query_pool_desc_t desc;
				memset(&desc, 0, sizeof(desc));
				desc.type = SK_QUERY_TYPE_TIMESTAMP;
				desc.query_count = SK_PROFILER_MAX_SAMPLES * 2u;
				desc.debug_name = "ProfilerQueryPool";
				gpu_ctx.buffers[i].query_pool = render_api->create_query_pool(dev, &desc);
			}
		}
	}
	return 0;
}

static void profiler_shutdown(void) {
	for (u32 i = 0u; i < SK_PROFILER_BUFFER_COUNT; i++) {
		if (sk_query_pool_t_is_valid(gpu_ctx.buffers[i].query_pool)) {
			render_api->destroy_query_pool(device, gpu_ctx.buffers[i].query_pool);
		}
		gpu_ctx.buffers[i].query_pool = sk_query_pool_t_zero();
		gpu_ctx.buffers[i].query_index = 0u;
		gpu_ctx.buffers[i].query_pool_reset = false;
	}
	render_api = NULL;
	device = sk_render_device_t_zero();
	timestamp_period = 0.0f;
}

static void profiler_begin_frame(void) {
	if (!active) {
		return;
	}

	advance_context(&cpu_ctx, false);
	advance_context(&gpu_ctx, true);

	f64 now = platform_api->monotonic_seconds();
	if (has_last_frame) {
		/* Wall-clock frame time is the CPU profiler's frame stat. */
		accumulate_frame_stat(&cpu_ctx, now - frame_start_seconds);
	}
	has_last_frame = true;
	frame_start_seconds = now;
	frame_number++;
}

static void profiler_end_frame(void) {
	/* No-op today; kept as the frame delimiter counterpart. */
}

static void profiler_begin_cpu_sample(const_chr_t name, const_chr_t category, u32 color) {
	begin_sample(&cpu_ctx, name, category, color, sk_command_buffer_t_zero());
}

static void profiler_end_cpu_sample(void) {
	end_sample(&cpu_ctx, sk_command_buffer_t_zero());
}

static void profiler_begin_gpu_sample(const_chr_t name, const_chr_t category, u32 color, sk_command_buffer_t cmd) {
	begin_sample(&gpu_ctx, name, category, color, cmd);
}

static void profiler_end_gpu_sample(sk_command_buffer_t cmd) {
	end_sample(&gpu_ctx, cmd);
}

static void profiler_get_cpu_tasks(const sk_profiler_task_entry_t** out, u32* count) {
	if (out != NULL) {
		*out = cpu_ctx.tasks;
	}
	if (count != NULL) {
		*count = cpu_ctx.task_count;
	}
}

static void profiler_get_gpu_tasks(const sk_profiler_task_entry_t** out, u32* count) {
	if (out != NULL) {
		*out = gpu_ctx.tasks;
	}
	if (count != NULL) {
		*count = gpu_ctx.task_count;
	}
}

static sk_profiler_frame_stats_t profiler_get_cpu_frame_stats(void) {
	sk_profiler_frame_stats_t stats;
	stats.current = cpu_ctx.frame_time_cur;
	stats.min = cpu_ctx.frame_time_min;
	stats.max = cpu_ctx.frame_time_max;
	stats.avg = cpu_ctx.frame_time_avg;
	stats.count = cpu_ctx.frame_time_count;
	return stats;
}

static sk_profiler_frame_stats_t profiler_get_gpu_frame_stats(void) {
	sk_profiler_frame_stats_t stats;
	stats.current = gpu_ctx.frame_time_cur;
	stats.min = gpu_ctx.frame_time_min;
	stats.max = gpu_ctx.frame_time_max;
	stats.avg = gpu_ctx.frame_time_avg;
	stats.count = gpu_ctx.frame_time_count;
	return stats;
}

static i32 profiler_dump_report(const_chr_t path) {
	if (path == NULL) {
		return -1;
	}

	FILE* out = fopen(path, "w");
	if (out == NULL) {
		return -1;
	}

	fputs("Skore profiler report\n=====================\n", out);
	fprintf(out, "Recording: %s\n\n", profiler_is_active() ? "active" : "inactive");

	/* CPU section: rolling frame stats + task tree. */
	sk_profiler_frame_stats_t cpu_stats = profiler_get_cpu_frame_stats();
	fprintf(out, "CPU frame: current %.3f ms, min %.3f ms, max %.3f ms, avg %.3f ms (%u frames)\n", cpu_stats.current * 1000.0, cpu_stats.min * 1000.0, cpu_stats.max * 1000.0,
			cpu_stats.avg * 1000.0, cpu_stats.count);
	fputs("CPU tasks:\n", out);

	u32 cpu_count = 0u;
	const sk_profiler_task_entry_t* cpu_tasks = NULL;
	profiler_get_cpu_tasks(&cpu_tasks, &cpu_count);
	for (u32 i = 0u; i < cpu_count; i++) {
		const sk_profiler_task_entry_t* task = &cpu_tasks[i];
		if (!task->present) {
			continue;
		}
		fprintf(out, "%*s%s  cpu %.3f ms (avg %.3f, min %.3f, max %.3f, %u frames, color 0x%08X)", (int)task->depth * 2, "", task->name, task->cpu_time * 1000.0,
				task->cpu_avg * 1000.0, task->cpu_min * 1000.0, task->cpu_max * 1000.0, task->cpu_count, task->color);
		if (task->category[0] != '\0') {
			fprintf(out, " [%s]", task->category);
		}
		if (task->has_gpu) {
			fprintf(out, "  gpu %.3f ms", task->gpu_time * 1000.0);
		}
		fputc('\n', out);
	}

	/* GPU section: rolling frame stats + task tree. */
	sk_profiler_frame_stats_t gpu_stats = profiler_get_gpu_frame_stats();
	fprintf(out, "\nGPU frame: current %.3f ms, min %.3f ms, max %.3f ms, avg %.3f ms (%u frames)\n", gpu_stats.current * 1000.0, gpu_stats.min * 1000.0, gpu_stats.max * 1000.0,
			gpu_stats.avg * 1000.0, gpu_stats.count);
	fputs("GPU tasks:\n", out);

	u32 gpu_count = 0u;
	const sk_profiler_task_entry_t* gpu_tasks = NULL;
	profiler_get_gpu_tasks(&gpu_tasks, &gpu_count);
	for (u32 i = 0u; i < gpu_count; i++) {
		const sk_profiler_task_entry_t* task = &gpu_tasks[i];
		if (!task->present) {
			continue;
		}
		fprintf(out, "%*s%s  gpu %.3f ms (avg %.3f, min %.3f, max %.3f, %u frames, color 0x%08X)", (int)task->depth * 2, "", task->name, task->gpu_time * 1000.0,
				task->gpu_avg * 1000.0, task->gpu_min * 1000.0, task->gpu_max * 1000.0, task->gpu_count, task->color);
		if (task->category[0] != '\0') {
			fprintf(out, " [%s]", task->category);
		}
		fputc('\n', out);
	}

	fclose(out);
	return 0;
}

static void profiler_reset_stats(void) {
	sk_profiler_context_t* contexts[] = {&cpu_ctx, &gpu_ctx};
	for (u32 c = 0u; c < 2u; c++) {
		sk_profiler_context_t* ctx = contexts[c];
		for (u32 i = 0u; i < ctx->task_count; i++) {
			ctx->tasks[i].cpu_min = 0.0;
			ctx->tasks[i].cpu_max = 0.0;
			ctx->tasks[i].cpu_avg = 0.0;
			ctx->tasks[i].gpu_min = 0.0;
			ctx->tasks[i].gpu_max = 0.0;
			ctx->tasks[i].gpu_avg = 0.0;
			ctx->tasks[i].cpu_count = 0u;
			ctx->tasks[i].gpu_count = 0u;
		}
		reset_frame_stats(ctx);
	}
}

static void profiler_set_active(bool value) {
	active = value;
	if (!active) {
		sk_profiler_context_t* contexts[] = {&cpu_ctx, &gpu_ctx};
		for (u32 c = 0u; c < 2u; c++) {
			sk_profiler_context_t* ctx = contexts[c];
			ctx->task_count = 0u;
			reset_frame_stats(ctx);
		}
		has_last_frame = false;
	}
}

static bool profiler_is_active(void) {
	return active;
}

static const sk_profiler_api_t profiler_api = {
	.init = profiler_init,
	.shutdown = profiler_shutdown,
	.begin_frame = profiler_begin_frame,
	.end_frame = profiler_end_frame,
	.begin_cpu_sample = profiler_begin_cpu_sample,
	.end_cpu_sample = profiler_end_cpu_sample,
	.begin_gpu_sample = profiler_begin_gpu_sample,
	.end_gpu_sample = profiler_end_gpu_sample,
	.get_cpu_tasks = profiler_get_cpu_tasks,
	.get_gpu_tasks = profiler_get_gpu_tasks,
	.get_cpu_frame_stats = profiler_get_cpu_frame_stats,
	.get_gpu_frame_stats = profiler_get_gpu_frame_stats,
	.dump_report = profiler_dump_report,
	.reset_stats = profiler_reset_stats,
	.set_active = profiler_set_active,
	.is_active = profiler_is_active,
};

/* ---- registration (called from sk_plugin_entry_point) ---- */

void sk_profiler_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_context = context;
	app_api_table = app_api;
	platform_api = (const sk_platform_api_t*)app_api->get_api(context, SK_PLATFORM_API_TYPE_ID);
	app_api->set_api(context, SK_PROFILER_API_TYPE_ID, &profiler_api);
}

#ifdef SK_TESTS
#include "test.h"

/* Test-only handle to the module's static table (same DLL), so the forced
 * SK_PROFILER_ENABLED macro tests in profiler_macro_tests.c can drive the
 * exact table the plugin registered. */
const sk_profiler_api_t* sk_profiler_test_table(void);

const sk_profiler_api_t* sk_profiler_test_table(void) {
	return &profiler_api;
}

/* Every test starts from a clean recording state. */
static void profiler_test_reset(const sk_profiler_api_t* api) {
	api->set_active(false);
	api->set_active(true);
}

/* ---- CPU profiler behavior ---- */

SK_TEST(profiler_api_table_is_complete) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	TEST_ASSERT_NOT_NULL(api);
	TEST_ASSERT_NOT_NULL(api->init);
	TEST_ASSERT_NOT_NULL(api->shutdown);
	TEST_ASSERT_NOT_NULL(api->begin_frame);
	TEST_ASSERT_NOT_NULL(api->end_frame);
	TEST_ASSERT_NOT_NULL(api->begin_cpu_sample);
	TEST_ASSERT_NOT_NULL(api->end_cpu_sample);
	TEST_ASSERT_NOT_NULL(api->begin_gpu_sample);
	TEST_ASSERT_NOT_NULL(api->end_gpu_sample);
	TEST_ASSERT_NOT_NULL(api->get_cpu_tasks);
	TEST_ASSERT_NOT_NULL(api->get_gpu_tasks);
	TEST_ASSERT_NOT_NULL(api->get_cpu_frame_stats);
	TEST_ASSERT_NOT_NULL(api->get_gpu_frame_stats);
	TEST_ASSERT_NOT_NULL(api->dump_report);
	TEST_ASSERT_NOT_NULL(api->reset_stats);
	TEST_ASSERT_NOT_NULL(api->set_active);
	TEST_ASSERT_NOT_NULL(api->is_active);
}

SK_TEST(profiler_type_id_nonzero) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_PROFILER_API_TYPE_ID, SK_TYPE_ID_ZERO));
}

SK_TEST(profiler_inactive_records_nothing) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	api->set_active(false);
	TEST_ASSERT_FALSE(api->is_active());

	api->begin_cpu_sample("ghost", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame();
	api->begin_frame();

	u32 count = 123u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(0u, count);
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_cpu_frame_stats().count);
	api->set_active(true);
}

SK_TEST(profiler_cpu_zones_nested_build_tasks) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	api->begin_cpu_sample("a", NULL, 0u);
	api->begin_cpu_sample("b", NULL, 0u);
	api->end_cpu_sample();
	api->end_cpu_sample();
	api->begin_frame(); /* frame 1 */
	api->begin_frame(); /* frame 2: builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	TEST_ASSERT_EQUAL_STRING("a", tasks[0].name);
	TEST_ASSERT_EQUAL_INT32(0, tasks[0].depth);
	TEST_ASSERT_TRUE(tasks[0].present);
	TEST_ASSERT_TRUE(tasks[0].cpu_time >= 0.0);
	TEST_ASSERT_EQUAL_STRING("b", tasks[1].name);
	TEST_ASSERT_EQUAL_INT32(1, tasks[1].depth);
	TEST_ASSERT_TRUE(tasks[1].present);
	api->set_active(false);
}

SK_TEST(profiler_read_lag_is_two_frames) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	api->begin_cpu_sample("f0zone", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame(); /* frame 1 */
	api->begin_frame(); /* frame 2: builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("f0zone", tasks[0].name);
	TEST_ASSERT_TRUE(tasks[0].present);

	api->begin_frame(); /* frame 3: builds frame 1 (no samples) */
	count = 0u;
	api->get_cpu_tasks(&tasks, &count);
	/* Task entries persist across frames (rolling stats); the frame-1 build
	 * marks the stale entry not-present. */
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_FALSE(tasks[0].present);
	TEST_ASSERT_EQUAL_UINT32(0u, tasks[0].cpu_time);
	api->set_active(false);
}

SK_TEST(profiler_same_name_samples_merge) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0: two same-name samples sum into one task */
	api->begin_cpu_sample("merged", NULL, 0u);
	api->end_cpu_sample();
	api->begin_cpu_sample("merged", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame(); /* frame 1: one more sample */
	api->begin_cpu_sample("merged", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame(); /* frame 2: builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("merged", tasks[0].name);
	/* cpu_count counts frames the task was present (rolling), not samples. */
	TEST_ASSERT_EQUAL_UINT32(1u, tasks[0].cpu_count);
	TEST_ASSERT_TRUE(tasks[0].cpu_time >= 0.0);

	api->begin_frame(); /* frame 3: builds frame 1 -> second present frame */
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_UINT32(2u, tasks[0].cpu_count);
	api->set_active(false);
}

SK_TEST(profiler_sample_cap_drops_overflow) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	for (u32 i = 0u; i < SK_PROFILER_MAX_SAMPLES + 64u; i++) {
		char name[32];
		(void)snprintf(name, sizeof(name), "zone%u", i);
		api->begin_cpu_sample(name, NULL, 0u);
		api->end_cpu_sample();
	}
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(SK_PROFILER_MAX_SAMPLES, count);
	api->set_active(false);
}

SK_TEST(profiler_frame_stats_accumulate_on_begin_frame) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* first frame: baseline only */
	api->begin_frame();
	api->begin_frame();

	sk_profiler_frame_stats_t stats = api->get_cpu_frame_stats();
	TEST_ASSERT_EQUAL_UINT32(2u, stats.count);
	TEST_ASSERT_TRUE(stats.current >= 0.0);
	TEST_ASSERT_TRUE(stats.min <= stats.current);
	TEST_ASSERT_TRUE(stats.max >= stats.current);
	api->set_active(false);
}

SK_TEST(profiler_reset_stats_clears_rolling_counts) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame();
	api->begin_cpu_sample("stat", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_TRUE(count >= 1u);
	TEST_ASSERT_TRUE(tasks[0].cpu_count >= 1u);

	api->reset_stats();
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_cpu_frame_stats().count);
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_TRUE(count >= 1u);
	TEST_ASSERT_EQUAL_UINT32(0u, tasks[0].cpu_count);
	api->set_active(false);
}

SK_TEST(profiler_set_active_false_clears_state) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame();
	api->begin_cpu_sample("gone", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame();

	api->set_active(false);
	TEST_ASSERT_FALSE(api->is_active());

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(0u, count);
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_cpu_frame_stats().count);
	api->set_active(true);
}

/* ---- GPU profiler behavior ---- */

SK_TEST(profiler_gpu_zones_cpu_only_without_device) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	api->init(sk_render_device_t_zero()); /* CPU-only: no query pools */
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	api->begin_gpu_sample("gzone", NULL, 0u, sk_command_buffer_t_from_u64(0x42u));
	api->end_gpu_sample(sk_command_buffer_t_from_u64(0x42u));
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_gpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("gzone", tasks[0].name);
	TEST_ASSERT_FALSE(tasks[0].has_gpu);
	TEST_ASSERT_TRUE(tasks[0].cpu_time >= 0.0);
	api->set_active(false);
	api->shutdown();
}

/* Minimal fake RHI for the query-pool path: timestamps are fabricated so the
 * GPU times are deterministic (tick N reads back as 1000 + N nanoseconds). */
static sk_device_properties_t mock_get_properties(sk_render_device_t dev) {
	sk_device_properties_t props;
	memset(&props, 0, sizeof(props));
	props.limits.timestamp_period = 1.0f; /* 1 ns per tick */
	(void)dev;
	return props;
}

static sk_query_pool_t mock_create_query_pool(sk_render_device_t dev, const sk_query_pool_desc_t* desc) {
	(void)dev;
	(void)desc;
	return sk_query_pool_t_from_u64(0x1000ull);
}

static void mock_destroy_query_pool(sk_render_device_t dev, sk_query_pool_t pool) {
	(void)dev;
	(void)pool;
}

static void mock_reset_query_pool(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 first_query, u32 query_count) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)first_query;
	(void)query_count;
}

static void mock_write_timestamp(sk_render_device_t dev, sk_command_buffer_t cmd, sk_query_pool_t pool, u32 query) {
	(void)dev;
	(void)cmd;
	(void)pool;
	(void)query;
}

static i32 mock_get_query_pool_results(sk_render_device_t dev, sk_query_pool_t pool, u32 first_query, u32 query_count, void* data, u64 data_size, u64 stride, bool wait) {
	(void)dev;
	(void)pool;
	(void)wait;
	u8* dst = (u8*)data;
	for (u32 i = 0u; i < query_count; i++) {
		u64 offset = (u64)i * stride;
		/* size_t is u64 already on LLP64 (MSVC); the cast would be redundant
		 * there, and on LP64 the value widens implicitly — no cast needed. */
		if (offset + sizeof(u64) > data_size) {
			break;
		}
		u64 ticks = 1000ull + (u64)(first_query + i);
		memcpy(dst + offset, &ticks, sizeof(ticks));
	}
	return 0;
}

SK_TEST(profiler_gpu_zones_read_timestamp_pairs) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	sk_render_device_api_t mock;
	memset(&mock, 0, sizeof(mock));
	mock.get_properties = mock_get_properties;
	mock.create_query_pool = mock_create_query_pool;
	mock.destroy_query_pool = mock_destroy_query_pool;
	mock.reset_query_pool = mock_reset_query_pool;
	mock.write_timestamp = mock_write_timestamp;
	mock.get_query_pool_results = mock_get_query_pool_results;

	/* Install the fake RHI on the profiler's registered context (test-only;
	 * restored afterwards so other plugins see the original registry). */
	void_ptr_t prev = app_api_table->get_api(app_context, SK_RENDER_DEVICE_API_TYPE_ID);
	app_api_table->set_api(app_context, SK_RENDER_DEVICE_API_TYPE_ID, &mock);

	TEST_ASSERT_EQUAL_INT(0, api->init(sk_render_device_t_from_u64(0x1234u)));
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	api->begin_gpu_sample("gpu1", NULL, 0u, sk_command_buffer_t_from_u64(0x77u));
	api->end_gpu_sample(sk_command_buffer_t_from_u64(0x77u));
	api->begin_frame();
	api->begin_frame(); /* builds frame 0: query pair (0,1) -> 1 ns */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_gpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_EQUAL_STRING("gpu1", tasks[0].name);
	TEST_ASSERT_TRUE(tasks[0].has_gpu);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1e-9, tasks[0].gpu_time);
	TEST_ASSERT_TRUE(tasks[0].gpu_count >= 1u);

	sk_profiler_frame_stats_t stats = api->get_gpu_frame_stats();
	TEST_ASSERT_TRUE(stats.count >= 1u);
	TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1e-9, stats.current);

	api->set_active(false);
	api->shutdown();
	app_api_table->set_api(app_context, SK_RENDER_DEVICE_API_TYPE_ID, prev);
}

SK_TEST(profiler_init_shutdown_idempotent) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	TEST_ASSERT_EQUAL_INT(0, api->init(sk_render_device_t_zero()));
	TEST_ASSERT_EQUAL_INT(0, api->init(sk_render_device_t_zero()));
	api->shutdown();
	api->shutdown();
}

/* ---- instrumentation macros (mode depends on the build's compile-time
 * switch; the forced-enabled TU profiler_macro_tests.c always tests the real
 * bodies, and this section asserts whichever mode this TU compiled with). ---- */

SK_TEST(profiler_macros_match_compile_time_switch) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	/* In no-op mode these must compile away and record nothing. */
	SK_PROFILE_CPU_ZONE(api, "macrozone");
	SK_PROFILE_BEGIN_CPU_SAMPLE(api, "explicit", NULL, 0u);
	SK_PROFILE_END_CPU_SAMPLE(api);
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
#if defined(SK_PROFILER_ENABLED)
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	TEST_ASSERT_EQUAL_STRING("macrozone", tasks[0].name);
	TEST_ASSERT_EQUAL_STRING("explicit", tasks[1].name);
#else
	TEST_ASSERT_EQUAL_UINT32(0u, count);
#endif
	api->set_active(false);
}

SK_TEST(profiler_category_and_color_stored) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	api->begin_cpu_sample("catzone", "render", 0xFF102030u);
	api->end_cpu_sample();
	api->begin_cpu_sample("autozone", NULL, 0u); /* auto color + no category */
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 count = 0u;
	const sk_profiler_task_entry_t* tasks = NULL;
	api->get_cpu_tasks(&tasks, &count);
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	TEST_ASSERT_EQUAL_STRING("catzone", tasks[0].name);
	TEST_ASSERT_EQUAL_STRING("render", tasks[0].category);
	TEST_ASSERT_EQUAL_UINT32(0xFF102030u, tasks[0].color);
	TEST_ASSERT_EQUAL_STRING("autozone", tasks[1].name);
	TEST_ASSERT_EQUAL_STRING("", tasks[1].category);
	TEST_ASSERT_TRUE(tasks[1].color != 0u); /* auto palette color derived from the name */
	api->set_active(false);
}

SK_TEST(profiler_dump_report_writes_text) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	/* NULL path is rejected without crashing. */
	TEST_ASSERT_TRUE(api->dump_report(NULL) != 0);

	api->begin_frame(); /* frame 0 */
	api->begin_cpu_sample("dumpzone", "io", 0u);
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	const char* path = "sk_profiler_dump_test.txt";
	TEST_ASSERT_EQUAL_INT(0, api->dump_report(path));

	FILE* f = fopen(path, "r");
	TEST_ASSERT_NOT_NULL(f);
	bool found_zone = false;
	bool found_category = false;
	char line[256];
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strstr(line, "dumpzone") != NULL) {
			found_zone = true;
		}
		if (strstr(line, "[io]") != NULL) {
			found_category = true;
		}
	}
	fclose(f);
	(void)remove(path);
	TEST_ASSERT_TRUE(found_zone);
	TEST_ASSERT_TRUE(found_category);
	api->set_active(false);
}

#endif /* SK_TESTS */
