#pragma once

/**
 * @file debugger_window.h
 * @brief Debugger window (APX-374): v2 migration.
 *
 * Port of main-branch Skore::DebuggerWindow (migration manifest §3.2) onto
 * the v2 editor shell: a BottomRight / All-workspaces window with three tabs
 * — Statistics, CPU Profiler, GPU Profiler — mirroring the C++ chrome.
 *
 * The v2 engine has no `Profiler` singleton and no platform/GPU memory
 * queries, so **all data is MOCK**: the window runs a small session mock
 * profiler (deterministic LCG frame timings + a fixed task tree with
 * per-frame variance) that the toolbar controls (Record/Stop, Pause/Resume,
 * Clear, scale combo) and that the Statistics tab summarizes (FPS, frame
 * time, CPU %, memory, VRAM). The mock runs only while "active" and not
 * paused, so the charts animate in the shell exactly like the C++ profiler;
 * the ops table can feed real frame/task data later without UI changes.
 *
 * Public entry points (SetActive / IsActive / ResetStats / frame stats /
 * task listing / scale) follow the APX-365 pattern: never exported as free
 * symbols. The window publishes one process-lifetime
 * `sk_editor_debugger_ops_t` table registered with `app_api->add_impl`
 * under SK_EDITOR_DEBUGGER_OPS_TYPE_ID; callers look it up via
 * `sk_editor_debugger_ops(ctx, api)` and call through the pointers.
 */

#include "editor_window.h"
#include "main_windows.h"
#include "window_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SK_EDITOR_DEBUGGER_OPS_TYPE_ID SK_TYPE_ID("sk.editor.debugger.ops", 0x90b2d4f6e8a0c2e4ULL, 0x1d3f5b7d9f0b2d4fULL)

/** Max tracked mock profiler tasks (C++ ProfilerView::MaxTasks). */
#define SK_EDITOR_DEBUGGER_MAX_TASKS 64u
/** Mock frame history ring (C++ MaxHistoryFrames 300; smaller session mock). */
#define SK_EDITOR_DEBUGGER_HISTORY 96u
/** Max characters of one task name. */
#define SK_EDITOR_DEBUGGER_NAME_CAP 64u

/** Frame stats snapshot (C++ Profiler::FrameStats). */
typedef struct sk_editor_frame_stats_t {
	f32 current_ms;
	f32 avg_ms;
	f32 min_ms;
	f32 max_ms;
	u32 sample_count;
} sk_editor_frame_stats_t;

/** One mock profiler task entry (C++ Profiler::TaskEntry subset). */
typedef struct sk_editor_debugger_task_t {
	char name[SK_EDITOR_DEBUGGER_NAME_CAP];
	u32 depth;
	f32 cpu_ms;
	f32 gpu_ms;
	u32 color; /* RGBA */
	i32 present;
} sk_editor_debugger_task_t;

/** Scale combo entries (C++ scaleLabels/scaleValues). */
typedef struct sk_editor_scale_entry_t {
	const_chr_t label;
	f32 max_frame_time;
} sk_editor_scale_entry_t;

/** Mock scale options; index 3 = "16ms (60fps)" default. */
#define SK_EDITOR_DEBUGGER_SCALE_COUNT 6u

/**
 * Debugger window public table. Looked up via add_impl; never call the
 * implementations by symbol. Every function takes the app context / api pair
 * (plus the window instance for per-instance queries).
 */
typedef struct sk_editor_debugger_ops_t {
	/** Open (or focus) the Debugger window. */
	sk_editor_window_t* (*open)(sk_app_context_t* app_context, const sk_app_api_t* app_api);

	/** Non-zero when the mock profiler is recording (C++ Profiler::IsActive). */
	i32 (*is_active)(const sk_editor_window_t* window);

	/** Start / stop recording (C++ Profiler::SetActive; toolbar Record/Stop). */
	void (*set_active)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 active);

	/** Non-zero when the profiler view is paused (C++ ProfilerView::paused). */
	i32 (*is_paused)(const sk_editor_window_t* window);

	/** Pause / resume the view (toolbar Pause/Resume). */
	void (*set_paused)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 paused);

	/** Clear history + tasks and reset stats (toolbar Clear; C++ ResetStats). */
	void (*reset_stats)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Copy the current frame stats (mock). */
	void (*get_frame_stats)(const sk_editor_window_t* window, sk_editor_frame_stats_t* out);

	/** Advance the mock profiler by one frame (session mock; no-op when inactive/paused). */
	void (*tick)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window);

	/** Number of mock tasks (C++ taskCount). */
	u32 (*get_task_count)(const sk_editor_window_t* window);

	/** Copy task @p index into @p out (C++ tasks[i]). */
	i32 (*get_task)(const sk_editor_window_t* window, u32 index, sk_editor_debugger_task_t* out);

	/** Current scale combo index (C++ ProfilerView::scaleIndex). */
	i32 (*get_scale_index)(const sk_editor_window_t* window);

	/** Set the scale combo index (C++ scaleLabels selection). */
	void (*set_scale_index)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, i32 index);

	/** Scale option label at @p index. */
	const_chr_t (*scale_label)(i32 index);

	/** Scale option max-frame-time at @p index. */
	f32 (*scale_max_frame_time)(i32 index);

	/** Number of frames in the history ring. */
	u32 (*get_history_count)(const sk_editor_window_t* window);

	/** Frame time (ms) of history entry @p index (oldest first). */
	f32 (*get_history_at)(const sk_editor_window_t* window, u32 index);

	/** Feed one frame time (ms) into the history + stats (real-data hook). */
	void (*feed_frame)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, f32 frame_ms);

	/** Replace the mock task list (real-data hook; count 0 keeps the mock). */
	void (*set_tasks)(sk_app_context_t* app_context, const sk_app_api_t* app_api, sk_editor_window_t* window, const sk_editor_debugger_task_t* tasks, u32 count);
} sk_editor_debugger_ops_t;

/** First registered Debugger ops table, or NULL before register. */
SK_FINLINE const sk_editor_debugger_ops_t* sk_editor_debugger_ops(sk_app_context_t* app_context, const sk_app_api_t* app_api) {
	return (const sk_editor_debugger_ops_t*)sk_editor_window_ops_lookup(app_context, app_api, SK_EDITOR_DEBUGGER_OPS_TYPE_ID);
}

/**
 * Register the Debugger window impl + ops table on @p app_context. Idempotent
 * per context. Call before sk_editor_windows_register_impls so the real
 * impl wins window_open over the APX-330 scaffold.
 * Pair with sk_editor_debugger_shutdown in tests.
 */
void sk_editor_debugger_register(sk_app_context_t* app_context, const sk_app_api_t* app_api);

/**
 * Drop the ops/window impls and free per-context class state. No-op when the
 * window was never registered on this context. Close open instances first.
 */
void sk_editor_debugger_shutdown(sk_app_context_t* app_context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
