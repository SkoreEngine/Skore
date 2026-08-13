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
#include "logger.h"
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
	f64 frame_wall_time; /* wall-clock time of the frame this buffer recorded */
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
		ctx->tasks[i].cpu_calls = 0u;
		ctx->tasks[i].gpu_calls = 0u;
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
			created->cpu_calls = 0u;
			created->gpu_calls = 0u;
		}

		sk_profiler_task_entry_t* task = &ctx->tasks[idx];
		task->present = true;
		task->cpu_time += sample->cpu_end - sample->cpu_start;
		task->cpu_calls++;

		if (sample->has_gpu && has_gpu_results) {
			u64 start_tick = gpu_timestamps[sample->gpu_query_start];
			u64 end_tick = gpu_timestamps[sample->gpu_query_end];
			/* Guard against invalid/partial results (end before start). */
			if (end_tick > start_tick) {
				task->gpu_time += (f64)(end_tick - start_tick) * (f64)timestamp_period * 1e-9;
				task->has_gpu = true;
				task->gpu_calls++;
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
		f64 delta = now - frame_start_seconds;
		/* Wall-clock frame time is the CPU profiler's frame stat. Tag the
		 * buffer that recorded the just-ended frame so reports can express
		 * task times as a percentage of their own frame's wall time. */
		accumulate_frame_stat(&cpu_ctx, delta);
		cpu_ctx.buffers[(cpu_ctx.write_idx + SK_PROFILER_BUFFER_COUNT - 1) % SK_PROFILER_BUFFER_COUNT].frame_wall_time = delta;
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

/* ---- reporting (text + JSON + log; dump_report / dump_report_json / log_report) ---- */

/* Line sink: renderers emit complete lines through this callback so the same
 * text renderer backs the file dump and the console/log form. */
typedef void (*sk_profiler_report_emit_fn)(void* user, const_chr_t line);

static void report_emit_to_file(void* user, const_chr_t line) {
	fputs(line, (FILE*)user);
	fputc('\n', (FILE*)user);
}

static const sk_logger_api_t* profiler_host_logger_api(void) {
	if (app_api_table == NULL || app_context == NULL) {
		return NULL;
	}
	return app_api_table->logger_api(app_context);
}

static sk_logger_context_t* profiler_host_logger_ctx(void) {
	if (app_api_table == NULL || app_context == NULL) {
		return NULL;
	}
	return app_api_table->logger_context(app_context);
}

static void report_emit_to_log(void* user, const_chr_t line) {
	const sk_logger_api_t* logger_api = profiler_host_logger_api();
	if (logger_api == NULL) {
		return;
	}
	sk_log_info(logger_api, (sk_logger_t*)user, "%s", line);
}

static f64 report_pct_of(f64 part, f64 total) {
	return (total > 0.0) ? (part / total) * 100.0 : 0.0;
}

/* Which side of a task entry a section renders (cpu_* vs gpu_* fields). */
typedef struct sk_profiler_report_side_t {
	bool gpu;
	f64 frame_time;		  /* the built frame's own time (seconds); %% reference */
	f64 frame_total_time; /* cumulative reference: avg frame time x frames */
} sk_profiler_report_side_t;

/* Cumulative-summary ordering: largest cumulative total first (stable
 * tie-break on task index). Insertion sort: at most SK_PROFILER_MAX_SAMPLES
 * entries, no external sort dependency. */
static void report_sort_order(u32* order, u32 count, const sk_profiler_task_entry_t* tasks, bool gpu) {
	for (u32 i = 1u; i < count; i++) {
		u32 key = order[i];
		f64 key_total = (gpu ? tasks[key].gpu_avg : tasks[key].cpu_avg) * (f64)(gpu ? tasks[key].gpu_count : tasks[key].cpu_count);
		i32 j = (i32)i - 1;
		while (j >= 0) {
			u32 other = order[j];
			f64 other_total = (gpu ? tasks[other].gpu_avg : tasks[other].cpu_avg) * (f64)(gpu ? tasks[other].gpu_count : tasks[other].cpu_count);
			/* -Wfloat-equal is on: express equality without ==. */
			bool equal = !(other_total > key_total) && !(key_total > other_total);
			if (other_total > key_total || (equal && other < key)) {
				order[j + 1] = order[j];
				j--;
			} else {
				break;
			}
		}
		order[j + 1] = key;
	}
}

/* One per-frame task-tree line: nesting indent, name, total, % of frame,
 * per-frame call count, rolling count/min/max/avg, cumulative total + % of
 * the average frame. */
static void report_task_line(char* line, u32 line_cap, const sk_profiler_task_entry_t* task, const sk_profiler_report_side_t* side) {
	f64 total = side->gpu ? task->gpu_time : task->cpu_time;
	f64 tmin = side->gpu ? task->gpu_min : task->cpu_min;
	f64 tmax = side->gpu ? task->gpu_max : task->cpu_max;
	f64 tavg = side->gpu ? task->gpu_avg : task->cpu_avg;
	u32 calls = side->gpu ? task->gpu_calls : task->cpu_calls;
	u32 count = side->gpu ? task->gpu_count : task->cpu_count;
	f64 cumulative = tavg * (f64)count;

	int n = snprintf(line, line_cap, "%*s%s  %s %9.3f ms  %6.2f%% frame  (calls %u, %u frames, avg %7.3f, min %7.3f, max %7.3f)  cum %9.3f ms (%6.2f%%)", (int)task->depth * 2, "",
					 task->name, side->gpu ? "gpu" : "cpu", total * 1000.0, report_pct_of(total, side->frame_time), calls, count, tavg * 1000.0, tmin * 1000.0, tmax * 1000.0,
					 cumulative * 1000.0, report_pct_of(cumulative, side->frame_total_time));
	if (n >= 0 && task->category[0] != '\0' && (u32)n + 2u < line_cap) {
		snprintf(line + n, line_cap - (u32)n, "  [%s]", task->category);
	}
}

static void report_render_side_text(sk_profiler_report_emit_fn emit, void* user, const_chr_t side_name, const sk_profiler_task_entry_t* tasks, u32 task_count,
									const sk_profiler_report_side_t* side) {
	char line[512];
	u32 present_count = 0u;

	snprintf(line, sizeof(line), "%s tasks (last built frame, %% of frame):", side_name);
	emit(user, line);
	for (u32 i = 0u; i < task_count; i++) {
		if (!tasks[i].present) {
			continue;
		}
		report_task_line(line, (u32)sizeof(line), &tasks[i], side);
		emit(user, line);
		present_count++;
	}
	if (present_count == 0u) {
		emit(user, "  (no tasks in the last built frame)");
	}

	/* Cumulative summary: every task ever recorded, largest total first. */
	u32 order[SK_PROFILER_MAX_SAMPLES];
	u32 order_count = 0u;
	for (u32 i = 0u; i < task_count; i++) {
		u32 count = side->gpu ? tasks[i].gpu_count : tasks[i].cpu_count;
		if (count > 0u) {
			order[order_count++] = i;
		}
	}
	report_sort_order(order, order_count, tasks, side->gpu);

	snprintf(line, sizeof(line), "Cumulative %s summary (all recorded frames, %% of avg frame):", side_name);
	emit(user, line);
	if (order_count == 0u) {
		emit(user, "  (no tasks recorded)");
	}
	for (u32 i = 0u; i < order_count; i++) {
		const sk_profiler_task_entry_t* task = &tasks[order[i]];
		f64 tmin = side->gpu ? task->gpu_min : task->cpu_min;
		f64 tmax = side->gpu ? task->gpu_max : task->cpu_max;
		f64 tavg = side->gpu ? task->gpu_avg : task->cpu_avg;
		u32 count = side->gpu ? task->gpu_count : task->cpu_count;
		f64 cumulative = tavg * (f64)count;
		snprintf(line, sizeof(line), "  %-24s total %9.3f ms   avg %7.3f   min %7.3f   max %7.3f   %4u frames   %6.2f%% of avg frame", task->name, cumulative * 1000.0,
				 tavg * 1000.0, tmin * 1000.0, tmax * 1000.0, count, report_pct_of(cumulative, side->frame_total_time));
		emit(user, line);
	}
}

static void profiler_render_text(sk_profiler_report_emit_fn emit, void* user) {
	char line[512];

	emit(user, "Skore profiler report");
	emit(user, "=====================");
	snprintf(line, sizeof(line), "Recording: %s", profiler_is_active() ? "active" : "inactive");
	emit(user, line);

	/* CPU section: rolling frame stats + per-frame tree + cumulative summary.
	 * The per-frame %% reference is the built frame's own wall time (the
	 * rolling "current" stat belongs to a newer, possibly unbuilt frame). */
	sk_profiler_frame_stats_t cpu_stats = profiler_get_cpu_frame_stats();
	snprintf(line, sizeof(line), "CPU frame: current %.3f ms, min %.3f ms, max %.3f ms, avg %.3f ms (%u frames)", cpu_stats.current * 1000.0, cpu_stats.min * 1000.0,
			 cpu_stats.max * 1000.0, cpu_stats.avg * 1000.0, cpu_stats.count);
	emit(user, line);

	u32 cpu_count = 0u;
	const sk_profiler_task_entry_t* cpu_tasks = NULL;
	profiler_get_cpu_tasks(&cpu_tasks, &cpu_count);
	sk_profiler_report_side_t cpu_side;
	cpu_side.gpu = false;
	cpu_side.frame_time = cpu_ctx.buffers[cpu_ctx.read_idx].frame_wall_time;
	cpu_side.frame_total_time = cpu_stats.avg * (f64)cpu_stats.count;
	report_render_side_text(emit, user, "CPU", cpu_tasks, cpu_count, &cpu_side);

	/* GPU section. */
	sk_profiler_frame_stats_t gpu_stats = profiler_get_gpu_frame_stats();
	snprintf(line, sizeof(line), "\nGPU frame: current %.3f ms, min %.3f ms, max %.3f ms, avg %.3f ms (%u frames)", gpu_stats.current * 1000.0, gpu_stats.min * 1000.0,
			 gpu_stats.max * 1000.0, gpu_stats.avg * 1000.0, gpu_stats.count);
	emit(user, line);

	u32 gpu_count = 0u;
	const sk_profiler_task_entry_t* gpu_tasks = NULL;
	profiler_get_gpu_tasks(&gpu_tasks, &gpu_count);
	sk_profiler_report_side_t gpu_side;
	gpu_side.gpu = true;
	gpu_side.frame_time = gpu_stats.current;
	gpu_side.frame_total_time = gpu_stats.avg * (f64)gpu_stats.count;
	report_render_side_text(emit, user, "GPU", gpu_tasks, gpu_count, &gpu_side);
}

/* JSON: escape a string into a buffer (quotes, backslashes, control chars). */
static void report_json_escape(char* dst, u32 dst_cap, const_chr_t src) {
	u32 o = 0u;
	for (const_chr_t p = src; *p != '\0' && o + 6u < dst_cap; p++) {
		unsigned char c = (unsigned char)*p;
		switch (c) {
		case '"':
			dst[o++] = '\\';
			dst[o++] = '"';
			break;
		case '\\':
			dst[o++] = '\\';
			dst[o++] = '\\';
			break;
		case '\n':
			dst[o++] = '\\';
			dst[o++] = 'n';
			break;
		case '\r':
			dst[o++] = '\\';
			dst[o++] = 'r';
			break;
		case '\t':
			dst[o++] = '\\';
			dst[o++] = 't';
			break;
		default:
			if (c < 0x20u) {
				dst[o++] = '\\';
				dst[o++] = 'u';
				o += (u32)snprintf(dst + o, dst_cap - o, "%04x", (unsigned)c);
			} else {
				dst[o++] = (char)c;
			}
		}
	}
	dst[o] = '\0';
}

static void report_json_task(FILE* out, const sk_profiler_task_entry_t* task, const sk_profiler_report_side_t* side, bool last) {
	char name[SK_PROFILER_NAME_CAP * 2u + 2u];
	char category[SK_PROFILER_CATEGORY_CAP * 2u + 2u];
	f64 total = side->gpu ? task->gpu_time : task->cpu_time;
	f64 tmin = side->gpu ? task->gpu_min : task->cpu_min;
	f64 tmax = side->gpu ? task->gpu_max : task->cpu_max;
	f64 tavg = side->gpu ? task->gpu_avg : task->cpu_avg;
	u32 calls = side->gpu ? task->gpu_calls : task->cpu_calls;
	u32 count = side->gpu ? task->gpu_count : task->cpu_count;
	f64 cumulative = tavg * (f64)count;

	report_json_escape(name, (u32)sizeof(name), task->name);
	report_json_escape(category, (u32)sizeof(category), task->category);

	fprintf(out,
			"\t\t{\"name\":\"%s\",\"category\":\"%s\",\"depth\":%d,\"color\":%u,\"present\":%s,\"calls\":%u,\"count\":%u,\"frame_total_ms\":%.6f,\"min_ms\":%.6f,\"max_ms\":%.6f,"
			"\"avg_ms\":%.6f,\"frame_pct\":%.6f,\"cumulative_ms\":%.6f,\"cumulative_pct\":%.6f}%s\n",
			name, category, task->depth, task->color, task->present ? "true" : "false", calls, count, total * 1000.0, tmin * 1000.0, tmax * 1000.0, tavg * 1000.0,
			report_pct_of(total, side->frame_time), cumulative * 1000.0, report_pct_of(cumulative, side->frame_total_time), last ? "" : ",");
}

static void report_render_side_json(FILE* out, const_chr_t side_name, const sk_profiler_task_entry_t* tasks, u32 task_count, const sk_profiler_report_side_t* side,
									sk_profiler_frame_stats_t stats) {
	fprintf(out, "\t\"%s\": {\n", side_name);
	fprintf(out, "\t\t\"frame_stats_ms\": {\"current\":%.6f,\"min\":%.6f,\"max\":%.6f,\"avg\":%.6f,\"count\":%u},\n", stats.current * 1000.0, stats.min * 1000.0,
			stats.max * 1000.0, stats.avg * 1000.0, stats.count);
	fprintf(out, "\t\t\"tasks\": [\n");
	u32 emitted = 0u;
	for (u32 i = 0u; i < task_count; i++) {
		report_json_task(out, &tasks[i], side, i + 1u == task_count);
		emitted++;
	}
	if (emitted == 0u) {
		fprintf(out, "\t\t\t{\"name\":\"\",\"category\":\"\",\"depth\":0,\"color\":0,\"present\":false,\"calls\":0,\"count\":0,\"frame_total_ms\":0.0,\"min_ms\":0.0,\"max_ms\":0."
					 "0,\"avg_ms\":0.0,\"frame_pct\":0.0,\"cumulative_ms\":0.0,\"cumulative_pct\":0.0}\n");
	}
	fprintf(out, "\t\t]\n\t}\n");
}

static void profiler_render_json(FILE* out) {
	fprintf(out, "{\n");
	fprintf(out, "\t\"format\": \"skore.profiler.report\",\n");
	fprintf(out, "\t\"version\": 1,\n");
	fprintf(out, "\t\"recording\": %s,\n", profiler_is_active() ? "true" : "false");

	sk_profiler_frame_stats_t cpu_stats = profiler_get_cpu_frame_stats();
	u32 cpu_count = 0u;
	const sk_profiler_task_entry_t* cpu_tasks = NULL;
	profiler_get_cpu_tasks(&cpu_tasks, &cpu_count);
	sk_profiler_report_side_t cpu_side;
	cpu_side.gpu = false;
	cpu_side.frame_time = cpu_ctx.buffers[cpu_ctx.read_idx].frame_wall_time;
	cpu_side.frame_total_time = cpu_stats.avg * (f64)cpu_stats.count;
	report_render_side_json(out, "cpu", cpu_tasks, cpu_count, &cpu_side, cpu_stats);
	fprintf(out, ",\n");

	sk_profiler_frame_stats_t gpu_stats = profiler_get_gpu_frame_stats();
	u32 gpu_count = 0u;
	const sk_profiler_task_entry_t* gpu_tasks = NULL;
	profiler_get_gpu_tasks(&gpu_tasks, &gpu_count);
	sk_profiler_report_side_t gpu_side;
	gpu_side.gpu = true;
	gpu_side.frame_time = gpu_stats.current;
	gpu_side.frame_total_time = gpu_stats.avg * (f64)gpu_stats.count;
	report_render_side_json(out, "gpu", gpu_tasks, gpu_count, &gpu_side, gpu_stats);
	fprintf(out, "}\n");
}

static i32 profiler_dump_report(const_chr_t path) {
	if (path == NULL) {
		return -1;
	}

	FILE* out = fopen(path, "w");
	if (out == NULL) {
		return -1;
	}
	profiler_render_text(report_emit_to_file, out);
	fclose(out);
	return 0;
}

static i32 profiler_dump_report_json(const_chr_t path) {
	if (path == NULL) {
		return -1;
	}

	FILE* out = fopen(path, "w");
	if (out == NULL) {
		return -1;
	}
	profiler_render_json(out);
	fclose(out);
	return 0;
}

static i32 profiler_log_report(void) {
	const sk_logger_api_t* logger_api = profiler_host_logger_api();
	sk_logger_context_t* log_ctx = profiler_host_logger_ctx();
	sk_logger_t* log;
	if (logger_api == NULL || log_ctx == NULL) {
		return -1;
	}
	log = logger_api->create_logger(log_ctx, "profiler");
	if (log == NULL) {
		return -1;
	}
	profiler_render_text(report_emit_to_log, log);
	logger_api->destroy_logger(log_ctx, log);
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
	.dump_report_json = profiler_dump_report_json,
	.log_report = profiler_log_report,
	.reset_stats = profiler_reset_stats,
	.set_active = profiler_set_active,
	.is_active = profiler_is_active,
};

/* ---- registration (called from sk_plugin_entry_point) ---- */

/**
 * Register the profiler API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_profiler_init(sk_app_context_t* context, const sk_app_api_t* app_api);

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
	TEST_ASSERT_NOT_NULL(api->dump_report_json);
	TEST_ASSERT_NOT_NULL(api->log_report);
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
 * bodies, and this section asserts whichever mode this TU compiled with).
 * When SK_PROFILER_ENABLED is off, every SK_PROFILE_* macro must compile to a
 * no-op that still evaluates its arguments (so call-site variables stay used)
 * and never records a sample. ---- */

SK_TEST(profiler_macros_match_compile_time_switch) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	/* Keep locals live so the disabled macro bodies must evaluate them. */
	const_chr_t category = "test";
	u32 color = 0x11223344u;
	sk_command_buffer_t cmd = sk_command_buffer_t_from_u64(0x77u);

	api->begin_frame(); /* frame 0 — host delimiter, not the macro */
	/* Exercise every sample SK_PROFILE_* macro. Distinct names avoid merge
	 * ambiguity when asserting category/color. Frame macros are checked
	 * after deactivate so they cannot advance the triple buffer mid-record. */
	{
		SK_PROFILE_CPU_ZONE(api, "macrozone");
		SK_PROFILE_CPU_ZONE_EX(api, "macroex", category, color);
		SK_PROFILE_GPU_ZONE(api, "gmacro", cmd);
		SK_PROFILE_GPU_ZONE_EX(api, "gmacroex", category, color, cmd);
		SK_PROFILE_BEGIN_CPU_SAMPLE(api, "explicit", category, color);
		SK_PROFILE_END_CPU_SAMPLE(api);
		SK_PROFILE_BEGIN_GPU_SAMPLE(api, "gexplicit", category, color, cmd);
		SK_PROFILE_END_GPU_SAMPLE(api, cmd);
	}
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	u32 cpu_count = 0u;
	u32 gpu_count = 0u;
	const sk_profiler_task_entry_t* cpu_tasks = NULL;
	const sk_profiler_task_entry_t* gpu_tasks = NULL;
	api->get_cpu_tasks(&cpu_tasks, &cpu_count);
	api->get_gpu_tasks(&gpu_tasks, &gpu_count);
#if defined(SK_PROFILER_ENABLED)
	/* Real bodies: three CPU samples + three GPU samples (CPU-only without a
	 * device). First CPU entry is the plain zone; EX carries category/color. */
	TEST_ASSERT_EQUAL_UINT32(3u, cpu_count);
	TEST_ASSERT_EQUAL_UINT32(3u, gpu_count);
	TEST_ASSERT_EQUAL_STRING("macrozone", cpu_tasks[0].name);
	TEST_ASSERT_EQUAL_STRING("macroex", cpu_tasks[1].name);
	TEST_ASSERT_EQUAL_STRING(category, cpu_tasks[1].category);
	TEST_ASSERT_EQUAL_UINT32(color, cpu_tasks[1].color);
	TEST_ASSERT_EQUAL_STRING("explicit", cpu_tasks[2].name);
#else
	/* Disabled: every macro compiled away — no samples, no side effects. */
	TEST_ASSERT_EQUAL_UINT32(0u, cpu_count);
	TEST_ASSERT_EQUAL_UINT32(0u, gpu_count);
	(void)cpu_tasks;
	(void)gpu_tasks;
	(void)color;
#endif
	/* Frame macros must also compile (and be no-ops when inactive / disabled). */
	api->set_active(false);
	SK_PROFILE_BEGIN_FRAME(api);
	SK_PROFILE_END_FRAME(api);
	TEST_ASSERT_EQUAL_UINT32(0u, api->get_cpu_frame_stats().count);
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
	bool found_pct = false;
	bool found_cumulative = false;
	char line[256];
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strstr(line, "dumpzone") != NULL) {
			found_zone = true;
		}
		if (strstr(line, "[io]") != NULL) {
			found_category = true;
		}
		if (strstr(line, "% frame") != NULL || strstr(line, "% of frame") != NULL) {
			found_pct = true;
		}
		if (strstr(line, "Cumulative") != NULL) {
			found_cumulative = true;
		}
	}
	fclose(f);
	(void)remove(path);
	TEST_ASSERT_TRUE(found_zone);
	TEST_ASSERT_TRUE(found_category);
	TEST_ASSERT_TRUE(found_pct);
	TEST_ASSERT_TRUE(found_cumulative);
	api->set_active(false);
}

SK_TEST(profiler_dump_report_json_writes_machine_readable) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	/* NULL path is rejected without crashing. */
	TEST_ASSERT_TRUE(api->dump_report_json(NULL) != 0);

	api->begin_frame(); /* frame 0 */
	api->begin_cpu_sample("jsonzone", "net", 0u);
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	const char* path = "sk_profiler_dump_test.json";
	TEST_ASSERT_EQUAL_INT(0, api->dump_report_json(path));

	FILE* f = fopen(path, "r");
	TEST_ASSERT_NOT_NULL(f);
	bool found_format = false;
	bool found_zone = false;
	bool found_frame_pct = false;
	bool found_cumulative_pct = false;
	char line[512];
	while (fgets(line, sizeof(line), f) != NULL) {
		if (strstr(line, "\"format\"") != NULL && strstr(line, "skore.profiler.report") != NULL) {
			found_format = true;
		}
		if (strstr(line, "\"name\":\"jsonzone\"") != NULL) {
			found_zone = true;
		}
		if (strstr(line, "\"frame_pct\"") != NULL) {
			found_frame_pct = true;
		}
		if (strstr(line, "\"cumulative_pct\"") != NULL) {
			found_cumulative_pct = true;
		}
	}
	fclose(f);
	(void)remove(path);
	TEST_ASSERT_TRUE(found_format);
	TEST_ASSERT_TRUE(found_zone);
	TEST_ASSERT_TRUE(found_frame_pct);
	TEST_ASSERT_TRUE(found_cumulative_pct);
	api->set_active(false);
}

/* Capture sink for log_report: records every emitted line. */
typedef struct sk_profiler_capture_sink_t {
	char lines[64][128];
	u32 count;
} sk_profiler_capture_sink_t;

static void profiler_capture_print(void_ptr_t user_data, sk_logger_type_t level, const_chr_t logger_name, const_chr_t message) {
	sk_profiler_capture_sink_t* cap = (sk_profiler_capture_sink_t*)user_data;
	(void)level;
	(void)logger_name;
	if (cap->count < 64u) {
		(void)snprintf(cap->lines[cap->count], 128u, "%s", message);
		cap->count++;
	}
}

SK_TEST(profiler_log_report_emits_console_form) {
	const sk_profiler_api_t* api = sk_profiler_test_table();
	profiler_test_reset(api);

	api->begin_frame(); /* frame 0 */
	api->begin_cpu_sample("logzone", NULL, 0u);
	api->end_cpu_sample();
	api->begin_frame();
	api->begin_frame(); /* builds frame 0 */

	const sk_logger_api_t* logger_api = profiler_host_logger_api();
	sk_logger_context_t* log_ctx = profiler_host_logger_ctx();
	TEST_ASSERT_NOT_NULL(logger_api);
	TEST_ASSERT_NOT_NULL(log_ctx);
	sk_profiler_capture_sink_t cap;
	memset(&cap, 0, sizeof(cap));
	sk_log_sink_t sink;
	memset(&sink, 0, sizeof(sink));
	sink.user_data = &cap;
	sink.print = profiler_capture_print;
	TEST_ASSERT_EQUAL_INT(0, logger_api->add_sink(log_ctx, &sink));

	TEST_ASSERT_EQUAL_INT(0, api->log_report());
	TEST_ASSERT_EQUAL_INT(0, logger_api->remove_sink(log_ctx, &sink));

	TEST_ASSERT_TRUE(cap.count > 0u);
	bool found_header = false;
	bool found_zone = false;
	for (u32 i = 0u; i < cap.count; i++) {
		if (strstr(cap.lines[i], "Skore profiler report") != NULL) {
			found_header = true;
		}
		if (strstr(cap.lines[i], "logzone") != NULL) {
			found_zone = true;
		}
	}
	TEST_ASSERT_TRUE(found_header);
	TEST_ASSERT_TRUE(found_zone);
	api->set_active(false);
}

#endif /* SK_TESTS */
