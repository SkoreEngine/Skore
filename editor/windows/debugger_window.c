/**
 * @file debugger_window.c
 * @brief Debugger window (APX-374): mock statistics + profiler on the v2 shell.
 *
 * Port of main-branch Skore::DebuggerWindow (migration manifest §3.2): a
 * BottomRight / All-workspaces window with Statistics / CPU Profiler / GPU
 * Profiler tabs. The v2 engine has no Profiler singleton, so the window
 * carries a small **session mock profiler**: a deterministic LCG frame-time
 * generator + a fixed task tree with per-frame variance, advanced by
 * `tick` whenever recording is active and not paused. The Statistics tab
 * summarizes the mock frame/CPU/memory/VRAM values; the profiler tabs show
 * the toolbar (Record/Stop, Pause/Resume, Clear, scale combo), a frame-stats
 * line, the task tree and a mock frame-time chart (colored cells per
 * history frame). The ops table exposes feed_frame / set_tasks so real data
 * can replace the mock without UI changes.
 *
 * Public entry points go through the `sk_editor_debugger_ops_t` table
 * registered with add_impl (window-table-pattern.md §7).
 */

#include "debugger_window.h"

#include "allocator.h"

#include <stdio.h>
#include <string.h>

#define SK_EDITOR_DEBUGGER_STATE_TYPE_ID SK_TYPE_ID("sk.editor.debugger.state", 0x3e5f7a9b0c2d4e6fULL, 0x8a1c3e5f7092b4d6ULL)

static const sk_editor_scale_entry_t k_scale_entries[SK_EDITOR_DEBUGGER_SCALE_COUNT] = {
	{"2ms", 0.002f}, {"4ms", 0.004f}, {"8ms", 0.008f}, {"16ms (60fps)", 1.0f / 60.0f}, {"33ms (30fps)", 1.0f / 30.0f}, {"66ms (15fps)", 1.0f / 15.0f},
};

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct debugger_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;

	/* Mock profiler view state (C++ ProfilerView). */
	i32 active;
	i32 paused;
	i32 scale_index;
	f32 max_frame_time;

	/* Mock frame stats (C++ Profiler::FrameStats). */
	f32 frame_current_ms;
	f32 frame_avg_ms;
	f32 frame_min_ms;
	f32 frame_max_ms;
	u32 frame_count;

	/* Mock tasks (C++ Profiler::TaskEntry list; tree via depth). */
	sk_editor_debugger_task_t tasks[SK_EDITOR_DEBUGGER_MAX_TASKS];
	u32 task_count;

	/* Frame-time history ring (C++ ProfilerView::history). */
	f32 history[SK_EDITOR_DEBUGGER_HISTORY];
	u32 history_write;
	u32 history_count;
	u32 history_revision;
	u32 last_chart_revision;

	/* Deterministic LCG for the mock generator. */
	u32 lcg;

	/* Selected tab: 0 Statistics, 1 CPU Profiler, 2 GPU Profiler. */
	i32 selected_tab;
	i32 last_built_tab;

	/* sk-ui handles; live only while a ui context hosts the window chrome. */
	sk_ui_node_t root;
	sk_ui_node_t tab_bar;
	sk_ui_node_t content_host;
	sk_ui_node_t tree;
	sk_ui_node_t chart;
	sk_ui_item_t tree_items[SK_EDITOR_DEBUGGER_MAX_TASKS];
	sk_ui_item_array_t tree_arr;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ui_ctx;
} debugger_state_t;

typedef struct debugger_class_state_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
} debugger_class_state_t;

static void debugger_init(sk_editor_window_t* window);
static void debugger_draw(sk_editor_window_t* window, i32* open);
static void debugger_destroy(sk_editor_window_t* window);

static sk_editor_window_t debugger_window = {
	.title = "Debugger",
	.dock_id = "sk.editor_window.debugger",
	.dock_position = SK_EDITOR_DOCK_BOTTOM_RIGHT,
	.order = 20,
	.workspace_mask = SK_EDITOR_WORKSPACE_ALL,
	.init = debugger_init,
	.draw = debugger_draw,
	.render = NULL,
	.destroy = debugger_destroy,
};

static debugger_class_state_t* debugger_class(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (debugger_class_state_t*)app_api->get_api(app_context, SK_EDITOR_DEBUGGER_STATE_TYPE_ID);
}

/* ------------------------------------------------------------------ */
/*  Mock profiler (ui-independent; deterministic for tests)           */
/* ------------------------------------------------------------------ */

static u32 dgb_rand(debugger_state_t* state) {
	state->lcg = state->lcg * 1664525u + 1013904223u;
	return state->lcg;
}

static f32 dgb_frand(debugger_state_t* state) {
	return (f32)((dgb_rand(state) >> 8) & 0xFFFFFFu) / (f32)0xFFFFFFu;
}

static void dgb_build_mock_tasks(debugger_state_t* state, f32 frame_ms) {
	sk_editor_debugger_task_t* t;
	u32 count = 0u;
	f32 update_ms = frame_ms * (0.40f + 0.15f * dgb_frand(state));
	f32 render_ms = frame_ms * (0.35f + 0.10f * dgb_frand(state));

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "Frame");
	t->depth = 0u;
	t->cpu_ms = frame_ms;
	t->gpu_ms = frame_ms;
	t->color = 0xFF60C060u;
	t->present = 1;

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "Update");
	t->depth = 1u;
	t->cpu_ms = update_ms;
	t->gpu_ms = 0.0f;
	t->color = 0xFF6090E0u;
	t->present = 1;

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "Physics");
	t->depth = 2u;
	t->cpu_ms = update_ms * (0.35f + 0.2f * dgb_frand(state));
	t->gpu_ms = 0.0f;
	t->color = 0xFFE0B060u;
	t->present = 1;

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "Scripts");
	t->depth = 2u;
	t->cpu_ms = update_ms * (0.45f + 0.2f * dgb_frand(state));
	t->gpu_ms = 0.0f;
	t->color = 0xFFB080E0u;
	t->present = 1;

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "Render");
	t->depth = 1u;
	t->cpu_ms = render_ms * 0.30f;
	t->gpu_ms = render_ms;
	t->color = 0xFFE06060u;
	t->present = 1;

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "Scene");
	t->depth = 2u;
	t->cpu_ms = 0.0f;
	t->gpu_ms = render_ms * (0.6f + 0.2f * dgb_frand(state));
	t->color = 0xFF60C0C0u;
	t->present = 1;

	t = &state->tasks[count++];
	(void)snprintf(t->name, sizeof(t->name), "%s", "UI");
	t->depth = 1u;
	t->cpu_ms = frame_ms * 0.08f;
	t->gpu_ms = frame_ms * 0.05f;
	t->color = 0xFFC0C060u;
	t->present = 1;

	state->task_count = count;
}

static void dgb_push_history(debugger_state_t* state, f32 frame_ms) {
	state->history[state->history_write] = frame_ms;
	state->history_write = (state->history_write + 1u) % SK_EDITOR_DEBUGGER_HISTORY;
	if (state->history_count < SK_EDITOR_DEBUGGER_HISTORY) {
		state->history_count += 1u;
	}
	state->history_revision += 1u;
}

static void dgb_feed_frame(debugger_state_t* state, f32 frame_ms) {
	if (frame_ms < 0.0f) {
		frame_ms = 0.0f;
	}
	state->frame_current_ms = frame_ms;
	if (state->frame_count == 0u) {
		state->frame_min_ms = frame_ms;
		state->frame_max_ms = frame_ms;
	} else {
		if (frame_ms < state->frame_min_ms) {
			state->frame_min_ms = frame_ms;
		}
		if (frame_ms > state->frame_max_ms) {
			state->frame_max_ms = frame_ms;
		}
	}
	state->frame_avg_ms = state->frame_count == 0u ? frame_ms : (state->frame_avg_ms * (f32)state->frame_count + frame_ms) / (f32)(state->frame_count + 1u);
	state->frame_count += 1u;
	dgb_push_history(state, frame_ms);
	dgb_build_mock_tasks(state, frame_ms);
}

static void dgb_tick(debugger_state_t* state) {
	f32 frame_ms;
	if (state == NULL || state->active == 0 || state->paused != 0) {
		return;
	}
	/* Mock frame: ~16.7ms base with a small deterministic wobble. */
	frame_ms = 16.0f + 3.0f * dgb_frand(state);
	if (dgb_frand(state) < 0.05f) {
		frame_ms += 8.0f + 6.0f * dgb_frand(state); /* occasional spike */
	}
	dgb_feed_frame(state, frame_ms);
}

/* ------------------------------------------------------------------ */
/*  Widget styles                                                     */
/* ------------------------------------------------------------------ */

static void dgb_apply_panel_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_ROW_GAP | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.padding.left = 6.0f;
	p.layout.padding.top = 4.0f;
	p.layout.padding.right = 6.0f;
	p.layout.padding.bottom = 4.0f;
	p.layout.row_gap = 2.0f;
	p.background_color = sk_ui_rgba(0.08f, 0.09f, 0.11f, 0.96f);
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void dgb_apply_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 8.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void dgb_apply_stat_row_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_PADDING;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_shrink = 0.0f;
	p.layout.padding.left = 2.0f;
	p.layout.padding.top = 0.0f;
	p.layout.padding.right = 2.0f;
	p.layout.padding.bottom = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

static void dgb_apply_stat_label_style(const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t node) {
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_COLOR | SK_UI_SP_FONT_SIZE | SK_UI_SP_WIDTH | SK_UI_SP_FLEX_SHRINK;
	p.color = sk_ui_rgba(0.78f, 0.79f, 0.86f, 1.0f);
	p.font_size = 11.0f;
	p.layout.width = sk_ui_pt(120.0f);
	p.layout.flex_shrink = 0.0f;
	(void)ui->node_set_inline_style(ctx, node, &p);
}

/* ------------------------------------------------------------------ */
/*  Statistics tab                                                    */
/* ------------------------------------------------------------------ */

static void dgb_format_bytes(char* out, u32 cap, f64 bytes) {
	const f64 KB = 1024.0;
	const f64 MB = KB * 1024.0;
	const f64 GB = MB * 1024.0;
	if (bytes >= GB) {
		(void)snprintf(out, cap, "%.2f GB", bytes / GB);
	} else if (bytes >= MB) {
		(void)snprintf(out, cap, "%.2f MB", bytes / MB);
	} else if (bytes >= KB) {
		(void)snprintf(out, cap, "%.2f KB", bytes / KB);
	} else {
		(void)snprintf(out, cap, "%.0f B", bytes);
	}
}

static void dgb_build_statistics(debugger_state_t* state, sk_ui_node_t content) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	f32 fps = state->frame_current_ms > 0.001f ? 1000.0f / state->frame_current_ms : 0.0f;
	f32 cpu_total;
	f32 cpu_usage;
	f64 working;
	f64 peak;
	f64 sys_used;
	f64 sys_total;
	f64 vram_used;
	f64 vram_total;
	char buf[160];
	char buf2[64];

	/* Mock platform values derived from the mock frame (deterministic). */
	cpu_usage = 8.0f + 10.0f * (f32)((dgb_rand(state) >> 8) & 0xFFu) / 255.0f;
	cpu_total = cpu_usage;
	working = 1.1e9 + 0.2e9 * (double)((dgb_rand(state) >> 8) & 0xFFu) / 255.0;
	peak = working + 0.3e9;
	sys_total = 16.0e9;
	sys_used = 6.0e9 + 2.0e9 * (double)((dgb_rand(state) >> 8) & 0xFFu) / 255.0;
	vram_total = 8.0e9;
	vram_used = 1.8e9 + 0.9e9 * (double)((dgb_rand(state) >> 8) & 0xFFu) / 255.0;

#define DGB_STAT(_label, _value)                                \
	do {                                                        \
		sk_ui_node_t row = ui->widget_view(ctx, content, NULL); \
		sk_ui_node_t lbl;                                       \
		(void)row;                                              \
		dgb_apply_stat_row_style(ui, ctx, row);                 \
		lbl = ui->widget_label(ctx, row, _label, NULL);         \
		dgb_apply_stat_label_style(ui, ctx, lbl);               \
		(void)ui->widget_label(ctx, row, _value, NULL);         \
	} while (0)

	(void)ui->widget_text_colored(ctx, content, "Frame", sk_ui_rgba(0.78f, 0.79f, 0.86f, 1.0f), NULL);
	(void)snprintf(buf, sizeof(buf), "%.1f", (double)fps);
	DGB_STAT("FPS", buf);
	(void)snprintf(buf, sizeof(buf), "%.2f ms", (double)state->frame_current_ms);
	DGB_STAT("Frame time", buf);

	(void)ui->widget_text_colored(ctx, content, "Process", sk_ui_rgba(0.78f, 0.79f, 0.86f, 1.0f), NULL);
	(void)snprintf(buf, sizeof(buf), "%.1f%%  (%.2f/core, 4 cores)", (double)cpu_total, (double)(cpu_usage) / 4.0);
	DGB_STAT("CPU", buf);
	dgb_format_bytes(buf2, sizeof(buf2), working);
	(void)snprintf(buf, sizeof(buf), "%s  (peak %s)", buf2, (dgb_format_bytes(buf2, sizeof(buf2), peak), buf2));
	DGB_STAT("Working set", buf);
	dgb_format_bytes(buf2, sizeof(buf2), sys_used);
	(void)snprintf(buf, sizeof(buf), "%s / %s", buf2, (dgb_format_bytes(buf2, sizeof(buf2), sys_total), buf2));
	DGB_STAT("System memory", buf);

	(void)ui->widget_text_colored(ctx, content, "GPU memory", sk_ui_rgba(0.78f, 0.79f, 0.86f, 1.0f), NULL);
	dgb_format_bytes(buf2, sizeof(buf2), vram_used);
	(void)snprintf(buf, sizeof(buf), "%s / %s", buf2, (dgb_format_bytes(buf2, sizeof(buf2), vram_total), buf2));
	DGB_STAT("Dedicated (VRAM)", buf);

	(void)ui->widget_text_colored(ctx, content, "Rendering", sk_ui_rgba(0.78f, 0.79f, 0.86f, 1.0f), NULL);
	DGB_STAT("Opaque drawcalls", "312");
	DGB_STAT("Transparent drawcalls", "24");
	DGB_STAT("Shadow drawcalls", "48 (per cascade)");
	DGB_STAT("Total drawcalls", "384");
	DGB_STAT("Instances", "1500");
	DGB_STAT("Render pipelines", "17");

#undef DGB_STAT
}

/* ------------------------------------------------------------------ */
/*  Profiler tab (toolbar + stats + tree + chart)                     */
/* ------------------------------------------------------------------ */

static void dgb_on_record_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	debugger_state_t* state = (debugger_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	state->active = state->active == 0 ? 1 : 0;
	event->consumed = 1;
}

static void dgb_on_pause_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	debugger_state_t* state = (debugger_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	state->paused = state->paused == 0 ? 1 : 0;
	event->consumed = 1;
}

static void dgb_on_clear_click(sk_ui_context_t* ctx, sk_ui_node_t node, sk_ui_event_t* event, void_ptr_t user) {
	debugger_state_t* state = (debugger_state_t*)user;
	(void)ctx;
	(void)node;
	if (event == NULL || event->type != SK_UI_EVENT_CLICK || state == NULL) {
		return;
	}
	state->history_count = 0u;
	state->history_write = 0u;
	state->history_revision += 1u;
	state->task_count = 0u;
	state->frame_count = 0u;
	state->frame_avg_ms = 0.0f;
	state->frame_min_ms = 0.0f;
	state->frame_max_ms = 0.0f;
	event->consumed = 1;
}

static void dgb_rebuild_tree(debugger_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	if (ui == NULL) {
		return;
	}
	state->tree_arr.items = state->tree_items;
	state->tree_arr.count = state->task_count;
	state->tree_arr.revision = state->history_revision;
	for (i = 0u; i < state->task_count; ++i) {
		sk_ui_item_t* item = &state->tree_items[i];
		u64 parent = 0u;
		i32 d = (i32)state->tasks[i].depth;
		i32 j;
		for (j = (i32)i - 1; j >= 0; --j) {
			if ((i32)state->tasks[j].depth < d) {
				parent = (u64)j + 1u;
				break;
			}
		}
		sk_ui_item_set(item, (u64)i + 1u, parent, state->tasks[i].name, 0u);
	}
	if (sk_ui_node_is_valid(state->tree)) {
		(void)ui->item_bind_set_array(ctx, state->tree, &state->tree_arr);
		(void)ui->item_bind_sync(ctx, state->tree);
	}
}

/* Rebuild the mock chart: one colored cell per history frame, height by
 * frame time vs the selected scale (green < 60fps budget, yellow, red). */
static void dgb_rebuild_chart(debugger_state_t* state, sk_ui_node_t chart) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	u32 i;
	if (ui == NULL) {
		return;
	}
	/* Clear the chart children (single rows view). */
	{
		sk_ui_node_t rows = ui->find_by_id(ctx, "debugger.chart.rows");
		if (sk_ui_node_is_valid(rows) && ui->node_alive(ctx, rows)) {
			(void)ui->node_destroy(ctx, rows);
		}
		rows = ui->widget_view(ctx, chart, "debugger.chart.rows");
		{
			sk_ui_style_props_t p;
			memset(&p, 0, sizeof(p));
			p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR |
					 SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
			p.layout.flex_direction = SK_UI_FLEX_ROW;
			p.layout.width = sk_ui_percent(100.0f);
			p.layout.height = sk_ui_pt(90.0f);
			p.layout.align_items = SK_UI_ALIGN_FLEX_END;
			p.layout.column_gap = 2.0f;
			p.layout.padding.left = 6.0f;
			p.layout.padding.top = 6.0f;
			p.layout.padding.right = 6.0f;
			p.layout.padding.bottom = 6.0f;
			p.background_color = sk_ui_rgba(0.05f, 0.055f, 0.07f, 1.0f);
			p.border_color = sk_ui_rgba(0.20f, 0.21f, 0.25f, 1.0f);
			p.layout.border.left = 1.0f;
			p.layout.border.top = 1.0f;
			p.layout.border.right = 1.0f;
			p.layout.border.bottom = 1.0f;
			(void)ui->node_set_inline_style(ctx, rows, &p);
		}
	}
	if (state->history_count == 0u) {
		(void)ui->widget_text_disabled(ctx, ui->find_by_id(ctx, "debugger.chart.rows"), "No frames recorded. Press Record to start.", "debugger.chart.empty");
		return;
	}
	for (i = 0u; i < state->history_count; ++i) {
		u32 idx = (state->history_write + SK_EDITOR_DEBUGGER_HISTORY - state->history_count + i) % SK_EDITOR_DEBUGGER_HISTORY;
		f32 ms = state->history[idx];
		f32 frac = state->max_frame_time > 0.001f ? ms / state->max_frame_time : 0.0f;
		f32 h = 6.0f + frac * 78.0f;
		sk_ui_node_t cell;
		sk_ui_style_props_t p;
		char id[32];
		if (h > 84.0f) {
			h = 84.0f;
		}
		(void)snprintf(id, sizeof(id), "debugger.chart.cell.%u", i);
		cell = ui->widget_dummy(ctx, ui->find_by_id(ctx, "debugger.chart.rows"), 8.0f, h, id);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
		p.layout.width = sk_ui_pt(8.0f);
		p.layout.height = sk_ui_pt(h);
		if (frac < 0.6f) {
			p.background_color = sk_ui_rgba(0.30f, 0.70f, 0.35f, 1.0f);
		} else if (frac < 1.0f) {
			p.background_color = sk_ui_rgba(0.85f, 0.70f, 0.25f, 1.0f);
		} else {
			p.background_color = sk_ui_rgba(0.85f, 0.30f, 0.30f, 1.0f);
		}
		(void)ui->node_set_inline_style(ctx, cell, &p);
	}
}

static void dgb_build_profiler_tab(debugger_state_t* state, sk_ui_node_t content, i32 gpu) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t toolbar;
	sk_ui_node_t stats_line;
	sk_ui_node_t body;
	sk_ui_node_t left;
	sk_ui_node_t chart_host;
	sk_ui_style_props_t p;
	sk_ui_node_callbacks_t cbs;
	char buf[160];

	/* Toolbar: Record/Stop, Pause/Resume, Clear, scale combo. */
	toolbar = ui->widget_view(ctx, content, "debugger.toolbar");
	dgb_apply_row_style(ui, ctx, toolbar);
	{
		sk_ui_node_t btn = ui->widget_button(ctx, toolbar, state->active != 0 ? "Stop" : "Record", "debugger.record");
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = dgb_on_record_click;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, btn, &cbs);
	}
	{
		sk_ui_node_t btn = ui->widget_button(ctx, toolbar, state->paused != 0 ? "Resume" : "Pause", "debugger.pause");
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = dgb_on_pause_click;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, btn, &cbs);
	}
	{
		sk_ui_node_t btn = ui->widget_button(ctx, toolbar, "Clear", "debugger.clear");
		memset(&cbs, 0, sizeof(cbs));
		cbs.on_click = dgb_on_clear_click;
		cbs.user = state;
		(void)ui->node_set_callbacks(ctx, btn, &cbs);
	}
	{
		sk_ui_node_t combo = ui->widget_begin_combo(ctx, toolbar, "Scale", k_scale_entries[state->scale_index].label, 0u, "debugger.scale");
		(void)combo;
		/* Scale selection is session state edited via the ops table; the
		 * combo preview mirrors the C++ scale combo (MOCK popup). */
	}

	/* Frame stats line. */
	stats_line = ui->widget_label(ctx, content, "", "debugger.stats");
	(void)snprintf(buf, sizeof(buf), "Frame: %.2f ms   avg %.2f   min %.2f   max %.2f   (%u samples)", (double)state->frame_current_ms, (double)state->frame_avg_ms,
				   (double)state->frame_min_ms, (double)state->frame_max_ms, state->frame_count);
	(void)ui->label_set_text(ctx, stats_line, buf);

	/* Body: left task tree + right chart. */
	body = ui->widget_view(ctx, content, "debugger.body");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_COLUMN_GAP;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.column_gap = 6.0f;
	(void)ui->node_set_inline_style(ctx, body, &p);

	left = ui->widget_view(ctx, body, "debugger.tree.host");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(50.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, left, &p);
	state->tree = ui->widget_tree(ctx, left, &state->tree_arr, "debugger.tree");
	dgb_rebuild_tree(state);

	chart_host = ui->widget_view(ctx, body, "debugger.chart.host");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(50.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.height = sk_ui_percent(100.0f);
	(void)ui->node_set_inline_style(ctx, chart_host, &p);
	(void)ui->widget_label(ctx, chart_host, gpu != 0 ? "GPU" : "CPU", "debugger.chart.label");
	state->chart = chart_host;
	dgb_rebuild_chart(state, chart_host);
}

/* ------------------------------------------------------------------ */
/*  Tab content switching                                             */
/* ------------------------------------------------------------------ */

static void dgb_build_selected_tab(debugger_state_t* state) {
	const sk_ui_api_t* ui = state->ui;
	sk_ui_context_t* ctx = state->ui_ctx;
	sk_ui_node_t content;
	if (ui == NULL || !sk_ui_node_is_valid(state->content_host)) {
		return;
	}
	content = ui->find_by_id(ctx, "debugger.content");
	if (sk_ui_node_is_valid(content) && ui->node_alive(ctx, content)) {
		(void)ui->node_destroy(ctx, content);
	}
	content = ui->widget_view(ctx, state->content_host, "debugger.content");
	dgb_apply_panel_style(ui, ctx, content);
	state->tree = SK_UI_NODE_INVALID;
	state->chart = SK_UI_NODE_INVALID;
	if (state->selected_tab == 0) {
		dgb_build_statistics(state, content);
	} else {
		dgb_build_profiler_tab(state, content, state->selected_tab == 2 ? 1 : 0);
	}
	state->last_built_tab = state->selected_tab;
}

/* ------------------------------------------------------------------ */
/*  Chrome build + per-frame sync                                     */
/* ------------------------------------------------------------------ */

static void dgb_build_ui(debugger_state_t* state, const sk_ui_api_t* ui, sk_ui_context_t* ctx, sk_ui_node_t parent) {
	sk_ui_style_props_t p;

	state->ui = ui;
	state->ui_ctx = ctx;

	state->root = ui->widget_view(ctx, parent, "debugger.content.root");
	if (!sk_ui_node_is_valid(state->root)) {
		return;
	}
	dgb_apply_panel_style(ui, ctx, state->root);

	/* Tab bar: Statistics / CPU Profiler / GPU Profiler. */
	state->tab_bar = ui->widget_tab_bar(ctx, state->root, "debugger.tabbar");
	(void)ui->tab_bar_bind_selected(ctx, state->tab_bar, &state->selected_tab);
	(void)ui->widget_tab(ctx, state->tab_bar, "Statistics", "debugger.tab.stats");
	(void)ui->widget_tab(ctx, state->tab_bar, "CPU Profiler", "debugger.tab.cpu");
	(void)ui->widget_tab(ctx, state->tab_bar, "GPU Profiler", "debugger.tab.gpu");

	state->content_host = ui->widget_view(ctx, state->root, "debugger.content.host");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW | SK_UI_SP_MIN_HEIGHT;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.min_height = sk_ui_pt(72.0f);
	(void)ui->node_set_inline_style(ctx, state->content_host, &p);

	state->last_built_tab = -1;
	dgb_build_selected_tab(state);
}

static i32 debugger_sync(debugger_state_t* state) {
	if (state == NULL || state->ui == NULL) {
		return -1;
	}
	/* Advance the mock profiler (session mock; no-op when inactive/paused). */
	dgb_tick(state);
	/* Rebuild the tab content on tab switch. */
	if (state->selected_tab != state->last_built_tab) {
		dgb_build_selected_tab(state);
	}
	/* Refresh chart + tree only when new frames landed (avoid per-frame rebuilds). */
	if (state->selected_tab != 0 && sk_ui_node_is_valid(state->tree)) {
		if (sk_ui_node_is_valid(state->chart) && state->history_revision != state->last_chart_revision) {
			dgb_rebuild_chart(state, state->chart);
			state->last_chart_revision = state->history_revision;
		}
		dgb_rebuild_tree(state);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Window lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void debugger_init(sk_editor_window_t* window) {
	const sk_allocator_t* alloc = sk_allocator_default();
	debugger_class_state_t* cls = (debugger_class_state_t*)window->user_data;
	debugger_state_t* state;
	if (cls == NULL) {
		window->user_data = NULL;
		return;
	}
	state = (debugger_state_t*)alloc->alloc(alloc->instance, sizeof(debugger_state_t));
	if (state == NULL) {
		window->user_data = NULL;
		return;
	}
	memset(state, 0, sizeof(*state));
	state->app_context = cls->app_context;
	state->app_api = cls->app_api;
	state->lcg = 0x12345678u;
	state->active = 1; /* C++ profiler default records */
	state->scale_index = 3;
	state->max_frame_time = k_scale_entries[3].max_frame_time;
	state->selected_tab = 0;
	state->last_built_tab = -1;
	dgb_feed_frame(state, 16.7f); /* seed one frame so the UI is not empty */
	window->user_data = state;
}

static void debugger_draw(sk_editor_window_t* window, i32* open) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	sk_editor_workspace_t* ws;
	sk_ui_node_t chrome;
	sk_ui_node_t content;
	sk_ui_node_t root;
	*open = 1;
	if (state == NULL) {
		*open = 0;
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (ctx == NULL) {
		return;
	}
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ui == NULL) {
		return;
	}
	chrome = ui->find_by_id(ctx, window->dock_id);
	if (!sk_ui_node_is_valid(chrome)) {
		return;
	}
	content = ui->editor_window_content(ctx, chrome);
	if (!sk_ui_node_is_valid(content)) {
		return;
	}
	root = ui->find_by_id(ctx, "debugger.content.root");
	if (!sk_ui_node_is_valid(root)) {
		dgb_build_ui(state, ui, ctx, content);
	}
	state->ui = ui;
	state->ui_ctx = ctx;
	(void)debugger_sync(state);
}

static void debugger_destroy(sk_editor_window_t* window) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_workspace_t* ws;
	sk_ui_context_t* ctx;
	const sk_ui_api_t* ui;
	if (state == NULL) {
		return;
	}
	ws = sk_editor_workspace_active(state->app_context, state->app_api);
	ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	ui = (const sk_ui_api_t*)state->app_api->get_api(state->app_context, SK_UI_API_TYPE_ID);
	if (ctx != NULL && ui != NULL && sk_ui_node_is_valid(state->root) && ui->node_alive(ctx, state->root)) {
		(void)ui->node_destroy(ctx, state->root);
	}
	state->root = SK_UI_NODE_INVALID;
	alloc->free(alloc->instance, state);
	window->user_data = NULL;
}

/* ------------------------------------------------------------------ */
/*  Ops table (add_impl; never direct symbols)                        */
/* ------------------------------------------------------------------ */

static sk_editor_window_t* debugger_open(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return sk_editor_window_open(app_context, app_api, SK_EDITOR_WINDOW_DEBUGGER);
}

static i32 debugger_ops_is_active(const sk_editor_window_t* window) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	return state != NULL ? state->active : 0;
}

static void debugger_ops_set_active(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 active) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->active = active != 0 ? 1 : 0;
	}
}

static i32 debugger_ops_is_paused(const sk_editor_window_t* window) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	return state != NULL ? state->paused : 0;
}

static void debugger_ops_set_paused(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 paused) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->paused = paused != 0 ? 1 : 0;
	}
}

static void debugger_ops_reset_stats(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		state->history_count = 0u;
		state->history_write = 0u;
		state->history_revision += 1u;
		state->task_count = 0u;
		state->frame_count = 0u;
		state->frame_avg_ms = 0.0f;
		state->frame_min_ms = 0.0f;
		state->frame_max_ms = 0.0f;
	}
}

static void debugger_ops_get_frame_stats(const sk_editor_window_t* window, sk_editor_frame_stats_t* out) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	if (out != NULL) {
		memset(out, 0, sizeof(*out));
		if (state != NULL) {
			out->current_ms = state->frame_current_ms;
			out->avg_ms = state->frame_avg_ms;
			out->min_ms = state->frame_min_ms;
			out->max_ms = state->frame_max_ms;
			out->sample_count = state->frame_count;
		}
	}
}

static void debugger_ops_tick(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		dgb_tick(state);
	}
}

static u32 debugger_ops_get_task_count(const sk_editor_window_t* window) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	return state != NULL ? state->task_count : 0u;
}

static i32 debugger_ops_get_task(const sk_editor_window_t* window, u32 index, sk_editor_debugger_task_t* out) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	if (state == NULL || index >= state->task_count || out == NULL) {
		return -1;
	}
	*out = state->tasks[index];
	return 0;
}

static i32 debugger_ops_get_scale_index(const sk_editor_window_t* window) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	return state != NULL ? state->scale_index : 0;
}

static void debugger_ops_set_scale_index(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 index) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL && index >= 0 && (u32)index < (u32)SK_EDITOR_DEBUGGER_SCALE_COUNT) {
		state->scale_index = index;
		state->max_frame_time = k_scale_entries[index].max_frame_time;
	}
}

static const_chr_t debugger_ops_scale_label(i32 index) {
	if (index < 0 || (u32)index >= (u32)SK_EDITOR_DEBUGGER_SCALE_COUNT) {
		return NULL;
	}
	return k_scale_entries[index].label;
}

static f32 debugger_ops_scale_max_frame_time(i32 index) {
	if (index < 0 || (u32)index >= (u32)SK_EDITOR_DEBUGGER_SCALE_COUNT) {
		return 0.0f;
	}
	return k_scale_entries[index].max_frame_time;
}

static u32 debugger_ops_get_history_count(const sk_editor_window_t* window) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	return state != NULL ? state->history_count : 0u;
}

static f32 debugger_ops_get_history_at(const sk_editor_window_t* window, u32 index) {
	const debugger_state_t* state = (const debugger_state_t*)window->user_data;
	if (state == NULL || index >= state->history_count) {
		return 0.0f;
	}
	return state->history[(state->history_write + SK_EDITOR_DEBUGGER_HISTORY - state->history_count + index) % SK_EDITOR_DEBUGGER_HISTORY];
}

static void debugger_ops_feed_frame(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, f32 frame_ms) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state != NULL) {
		dgb_feed_frame(state, frame_ms);
	}
}

static void debugger_ops_set_tasks(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_editor_debugger_task_t* tasks, u32 count) {
	debugger_state_t* state = (debugger_state_t*)window->user_data;
	(void)app_context;
	(void)app_api;
	if (state == NULL || tasks == NULL || count > SK_EDITOR_DEBUGGER_MAX_TASKS) {
		return;
	}
	memcpy(state->tasks, tasks, sizeof(sk_editor_debugger_task_t) * count);
	state->task_count = count;
}

static const sk_editor_debugger_ops_t debugger_ops = {
	debugger_open,
	debugger_ops_is_active,
	debugger_ops_set_active,
	debugger_ops_is_paused,
	debugger_ops_set_paused,
	debugger_ops_reset_stats,
	debugger_ops_get_frame_stats,
	debugger_ops_tick,
	debugger_ops_get_task_count,
	debugger_ops_get_task,
	debugger_ops_get_scale_index,
	debugger_ops_set_scale_index,
	debugger_ops_scale_label,
	debugger_ops_scale_max_frame_time,
	debugger_ops_get_history_count,
	debugger_ops_get_history_at,
	debugger_ops_feed_frame,
	debugger_ops_set_tasks,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

void sk_editor_debugger_register(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	debugger_class_state_t* cls = debugger_class(app_context, app_api);
	if (cls != NULL) {
		debugger_window.user_data = cls;
		return;
	}
	cls = (debugger_class_state_t*)alloc->alloc(alloc->instance, sizeof(debugger_class_state_t));
	if (cls == NULL) {
		return;
	}
	memset(cls, 0, sizeof(*cls));
	cls->app_context = app_context;
	cls->app_api = app_api;
	app_api->set_api(app_context, SK_EDITOR_DEBUGGER_STATE_TYPE_ID, cls);

	debugger_window.type_id = SK_EDITOR_WINDOW_DEBUGGER;
	debugger_window.user_data = cls;
	app_api->add_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &debugger_window);
	app_api->add_impl(app_context, SK_EDITOR_DEBUGGER_OPS_TYPE_ID, &debugger_ops);
}

void sk_editor_debugger_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	const sk_allocator_t* alloc = sk_allocator_default();
	debugger_class_state_t* cls = debugger_class(app_context, app_api);
	if (cls == NULL) {
		return;
	}
	app_api->remove_impl(app_context, SK_EDITOR_WINDOW_IMPL_TYPE_ID, &debugger_window);
	app_api->remove_impl(app_context, SK_EDITOR_DEBUGGER_OPS_TYPE_ID, &debugger_ops);
	app_api->set_api(app_context, SK_EDITOR_DEBUGGER_STATE_TYPE_ID, NULL);
	alloc->free(alloc->instance, cls);
	if (debugger_window.user_data == cls) {
		debugger_window.user_data = NULL;
	}
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "test.h"

/* Ops-table path (no ui): seeded mock profiler, record/pause/clear, frame
 * stats + history, task listing and scale combos. */
SK_TEST(editor_debugger_window_ops_mock_profiler) {
	sk_app_boot_t boot = sk_app_init(0, NULL);
	sk_app_context_t* app = boot.context;
	const sk_editor_api_t* editor;
	const sk_editor_debugger_ops_t* ops;
	sk_editor_window_t* window;
	sk_editor_frame_stats_t fs;
	sk_editor_debugger_task_t task;
	u32 i;

	TEST_ASSERT_NOT_NULL(app);
	sk_editor_bind_tables(app, boot.api);
	editor = (const sk_editor_api_t*)boot.api->get_api(app, SK_EDITOR_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(editor);

	TEST_ASSERT_NULL(sk_editor_debugger_ops(app, boot.api));
	sk_editor_debugger_register(app, boot.api);
	sk_editor_debugger_register(app, boot.api);
	TEST_ASSERT_EQUAL_UINT(1u, boot.api->impl_count(app, SK_EDITOR_DEBUGGER_OPS_TYPE_ID));

	ops = sk_editor_debugger_ops(app, boot.api);
	TEST_ASSERT_NOT_NULL(ops);

	window = ops->open(app, boot.api);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Debugger", window->title);
	TEST_ASSERT_EQUAL_PTR(window, editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_DEBUGGER));
	TEST_ASSERT_EQUAL_INT(SK_EDITOR_DOCK_BOTTOM_RIGHT, window->dock_position);
	TEST_ASSERT_EQUAL_INT(20, window->order);
	TEST_ASSERT_TRUE(sk_editor_workspace_mask_contains(window->workspace_mask, SK_EDITOR_WORKSPACE_SCENE));

	/* Defaults: recording on, not paused, scale 16ms(60fps), one seeded frame. */
	TEST_ASSERT_EQUAL_INT(1, ops->is_active(window));
	TEST_ASSERT_EQUAL_INT(0, ops->is_paused(window));
	TEST_ASSERT_EQUAL_INT(3, ops->get_scale_index(window));
	TEST_ASSERT_EQUAL_STRING("16ms (60fps)", ops->scale_label(3));
	TEST_ASSERT_TRUE(ops->scale_max_frame_time(3) > 0.015f);
	ops->get_frame_stats(window, &fs);
	TEST_ASSERT_EQUAL_UINT(1u, fs.sample_count);
	TEST_ASSERT_TRUE(fs.current_ms > 0.0f);
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_history_count(window));

	/* The seeded frame built the mock task tree. */
	TEST_ASSERT_TRUE(ops->get_task_count(window) >= 5u);
	TEST_ASSERT_EQUAL_INT(0, ops->get_task(window, 0u, &task));
	TEST_ASSERT_EQUAL_STRING("Frame", task.name);
	TEST_ASSERT_EQUAL_UINT(0u, task.depth);
	TEST_ASSERT_TRUE(task.present != 0);

	/* Pause stops the mock; tick() advances only while recording. */
	ops->set_paused(app, boot.api, window, 1);
	TEST_ASSERT_EQUAL_INT(1, ops->is_paused(window));
	ops->tick(app, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(1u, ops->get_history_count(window));

	ops->set_paused(app, boot.api, window, 0);
	ops->tick(app, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_history_count(window));
	ops->get_frame_stats(window, &fs);
	TEST_ASSERT_EQUAL_UINT(2u, fs.sample_count);

	/* Stop recording: tick becomes a no-op. */
	ops->set_active(app, boot.api, window, 0);
	TEST_ASSERT_EQUAL_INT(0, ops->is_active(window));
	ops->tick(app, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(2u, ops->get_history_count(window));

	/* feed_frame is the real-data hook: pushes into history + stats. */
	ops->feed_frame(app, boot.api, window, 8.0f);
	TEST_ASSERT_EQUAL_UINT(3u, ops->get_history_count(window));
	ops->get_frame_stats(window, &fs);
	TEST_ASSERT_EQUAL_UINT(3u, fs.sample_count);
	TEST_ASSERT_TRUE(ops->get_history_at(window, 0u) >= 0.0f);
	for (i = 0u; i < ops->get_history_count(window); ++i) {
		TEST_ASSERT_TRUE(ops->get_history_at(window, i) > 0.0f);
	}

	/* Clear wipes history + stats + tasks. */
	ops->reset_stats(app, boot.api, window);
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_history_count(window));
	TEST_ASSERT_EQUAL_UINT(0u, ops->get_task_count(window));
	ops->get_frame_stats(window, &fs);
	TEST_ASSERT_EQUAL_UINT(0u, fs.sample_count);

	editor->window_close(app, boot.api, window);
	TEST_ASSERT_NULL(editor->window_by_type(app, boot.api, SK_EDITOR_WINDOW_DEBUGGER));
	TEST_ASSERT_NOT_NULL(sk_editor_debugger_ops(app, boot.api));

	sk_editor_debugger_shutdown(app, boot.api);
	TEST_ASSERT_NULL(sk_editor_debugger_ops(app, boot.api));
	sk_app_shutdown(app);
}

#endif /* SK_TESTS */
