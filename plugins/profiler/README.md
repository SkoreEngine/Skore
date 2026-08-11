# Profiler plugin (`sk-profiler`)

Hierarchical CPU/GPU profiler for the v2 C engine. Port of main’s
`Skore::Profiler` to a pure C plugin: process-global `sk_profiler_api_t`,
compile-time instrumentation macros, and optional text / JSON / log reports.

Source of truth for types and contracts: [`profiler.h`](profiler.h).

Migration audit (legacy C++ inventory, risks, non-goals): [`AUDIT.md`](AUDIT.md).

Engine-independent capture core (TLS per-thread rings, overflow policy,
aggregation): [`core/profiler_core.h`](core/profiler_core.h).

---

## 1. Acquire the API table

The plugin registers a process-global table under `SK_PROFILER_API_TYPE_ID`
from `sk_plugin_entry_point`. Hosts never link the plugin binary; they load
plugins (auto via `sk_app_init` scanning `{app}/plugins`) and look up the table:

```c
#include "profiler.h"

const sk_app_api_t* app_api = sk_app_api();
const sk_profiler_api_t* prof =
    (const sk_profiler_api_t*)app_api->get_api(ctx, SK_PROFILER_API_TYPE_ID);

if (prof != NULL) {
    /* CPU-only: pass a zero device. Pass a real device later to attach GPU pools. */
    (void)prof->init(sk_render_device_t_zero());
    prof->set_active(true); /* recording is OFF by default */
}
```

Lifecycle owned by the host (mirrors main’s `App` wiring):

| When | Call |
| ---- | ---- |
| After plugins load | `prof->init(dev)` (zero device = CPU-only; re-call when a device appears) |
| Each frame (main loop) | `prof->begin_frame()` / `prof->end_frame()` |
| Teardown | `prof->shutdown()` then unload plugins |

`sk-app` already brackets each tick with begin/end frame when the table is
present. Recording stays inactive until a UI/tool calls `set_active(true)`.

---

## 2. Public API (`sk_profiler_api_t`)

All table entries are non-NULL after a successful plugin load. **Main-thread
only** for the plugin table (matching v2 default ownership). The optional
engine-independent core under `core/` supports multi-threaded capture via TLS;
that path is used by unit tests and is not the public plugin surface.

### Lifecycle and recording

| Entry | Purpose |
| ----- | ------- |
| `init(dev)` | Attach GPU timestamp query pools when `dev` is non-zero; CPU-only when zero. Idempotent. |
| `shutdown()` | Destroy GPU pools. Safe if init was never called. |
| `begin_frame()` | Advance triple buffers; build tasks from the frame two frames ago (GPU lag). No-op while inactive. |
| `end_frame()` | Frame delimiter counterpart (reserved; currently a no-op on the plugin table). |
| `set_active(bool)` | Master overhead switch. `false` clears tasks + frame stats and stops sampling. |
| `is_active()` | Current recording state (default: inactive). |
| `reset_stats()` | Clear rolling per-task and per-frame stats (keeps task names). |

### Sampling

| Entry | Purpose |
| ----- | ------- |
| `begin_cpu_sample(name, category, color)` | Open a nested CPU zone. Name copied (63+NUL). Category `NULL`/`""` = uncategorized. Color `0` = auto palette from name hash. |
| `end_cpu_sample()` | Close the most recently opened CPU zone (stack pop; no name needed). |
| `begin_gpu_sample(name, category, color, cmd)` | CPU wall stamps + GPU timestamp pair on `cmd` when pools are attached; otherwise a CPU-only sample with `has_gpu=false`. |
| `end_gpu_sample(cmd)` | Close the most recently opened GPU zone on `cmd`. |

Caps: at most `SK_PROFILER_MAX_SAMPLES` (256) zones per frame buffer; further
begins are silently dropped. Triple buffer count: `SK_PROFILER_BUFFER_COUNT` (3).

### Queries

| Entry | Purpose |
| ----- | ------- |
| `get_cpu_tasks(out, count)` | Aggregated CPU task array for the last built frame. |
| `get_gpu_tasks(out, count)` | Same for the GPU task list. |
| `get_cpu_frame_stats()` | Rolling wall-clock frame stats (current/min/max/avg + count). |
| `get_gpu_frame_stats()` | Rolling GPU frame stats (sum of depth-0 GPU task times per read frame). |

`sk_profiler_task_entry_t` fields (times in **seconds**):

| Field | Meaning |
| ----- | ------- |
| `name` / `category` | Zone name and optional category (fixed caps 64 / 32 incl. NUL) |
| `depth` | Nest depth at sample open (0 = root) |
| `color` | Explicit ABGR-ish color, or palette color when 0 was passed |
| `cpu_time` / `gpu_time` | Per-frame totals for this task (same-name samples merge) |
| `cpu_min` / `cpu_max` / `cpu_avg` | Rolling min/max/avg of per-frame CPU totals |
| `gpu_min` / `gpu_max` / `gpu_avg` | Rolling GPU counterparts |
| `cpu_count` / `gpu_count` | Frames in which the task was present (rolling) |
| `cpu_calls` / `gpu_calls` | Call counts in the last built frame |
| `has_gpu` | True when a real GPU timestamp pair was recorded |
| `present` | Seen in the last built frame |

Aggregation: multiple samples with the **same name** merge into one task
(sum times per frame; rolling min/max/avg across frames). Nest order is
reconstructed so children sit under their parent by `depth`.

---

## 3. Instrumentation macros

Call sites pass the resolved table pointer plus a zone name. Macros are
**NULL-safe**: a missing profiler plugin is a no-op.

### Scoped zones (preferred)

```c
SK_PROFILE_CPU_ZONE(prof, "Update");
SK_PROFILE_CPU_ZONE_EX(prof, "Physics", "sim", 0u); /* category + color (0 = auto) */
SK_PROFILE_GPU_ZONE(prof, "TLAS Build", cmd);
SK_PROFILE_GPU_ZONE_EX(prof, "Pass", "render", 0xFF102030u, cmd);
```

On GCC/Clang the zone closes when the enclosing block exits (including early
returns) via `__attribute__((cleanup))`. On MSVC a one-shot for-loop covers the
next statement only — write
`SK_PROFILE_CPU_ZONE(prof, "Update") { ...body... }` or use the explicit pair.

One zone per source line (`__LINE__`-named variable). Names longer than 63
chars and categories longer than 31 chars are truncated.

### Explicit begin/end (portable fallback)

```c
SK_PROFILE_BEGIN_CPU_SAMPLE(prof, "Update", NULL, 0u);
/* ... work ... */
SK_PROFILE_END_CPU_SAMPLE(prof);

SK_PROFILE_BEGIN_GPU_SAMPLE(prof, "Draw", "render", 0u, cmd);
/* ... record ... */
SK_PROFILE_END_GPU_SAMPLE(prof, cmd);

SK_PROFILE_BEGIN_FRAME(prof);
SK_PROFILE_END_FRAME(prof);
```

### Macro summary

| Macro | Expands to |
| ----- | ---------- |
| `SK_PROFILE_CPU_ZONE(api, name)` | Scoped CPU zone (category NULL, color auto) |
| `SK_PROFILE_CPU_ZONE_EX(api, name, category, color)` | Scoped CPU zone with category/color |
| `SK_PROFILE_GPU_ZONE(api, name, cmd)` | Scoped GPU zone (category NULL, color auto) |
| `SK_PROFILE_GPU_ZONE_EX(api, name, category, color, cmd)` | Scoped GPU zone with category/color |
| `SK_PROFILE_BEGIN_CPU_SAMPLE` / `SK_PROFILE_END_CPU_SAMPLE` | Explicit CPU pair |
| `SK_PROFILE_BEGIN_GPU_SAMPLE` / `SK_PROFILE_END_GPU_SAMPLE` | Explicit GPU pair |
| `SK_PROFILE_BEGIN_FRAME` / `SK_PROFILE_END_FRAME` | Frame delimiters through the table |

---

## 4. Enable / disable

### Compile-time (strip call-site overhead)

Root CMake option (default **OFF**):

```bash
# Instrumentation macros are no-ops; no call site references the plugin.
cmake -S . -B build -G Ninja -DSK_ENABLE_PROFILER=OFF

# Real macro bodies; sk-profiler must be present under plugins/ at runtime.
cmake -S . -B build -G Ninja -DSK_ENABLE_PROFILER=ON
```

`SK_ENABLE_PROFILER=ON` defines `SK_PROFILER_ENABLED` for every translation unit.
Without it, every `SK_PROFILE_*` macro reduces to an expression that still
evaluates its arguments (so call-site variables stay “used”) but never calls
into the profiler.

The plugin shared library is always built (tests and hosts can still call the
table directly). The switch only controls whether instrumentation macros emit
work.

### Runtime (cheap early-out)

```c
prof->set_active(true);  /* start recording (player F5 / editor Record) */
prof->set_active(false); /* stop + clear tasks and frame stats */
```

Default is inactive so Release overhead stays near zero when macros are
compiled in but nobody is recording.

---

## 5. Output formats

The profiler does **not** require a third-party backend (no Tracy). Output is
in-memory POD arrays plus optional dumps:

### In-memory (UI / tools)

1. **Task trees** — `get_cpu_tasks` / `get_gpu_tasks` → `sk_profiler_task_entry_t[]`
   with `depth` for indentation. Times in seconds (UI typically multiplies by
   1000 for ms).
2. **Frame stats** — `get_cpu_frame_stats` / `get_gpu_frame_stats` →
   current / min / max / avg / count.

### File and console reports

| Entry | Format | Content |
| ----- | ------ | ------- |
| `dump_report(path)` | Human-readable text | Last built frame CPU/GPU trees (nesting, per-frame total, rolling min/max/avg, call counts, % of frame) + cumulative summary. Times in **milliseconds**. |
| `dump_report_json(path)` | Versioned JSON | Same aggregation as machine-readable document (frame stats, task arrays with name/category/depth/color, calls, times, percentages). |
| `log_report()` | Engine logger | Same text as `dump_report`, emitted through `sk_logger_api` with logger name `"profiler"`. |

All three are safe when inactive or empty (header-only report / document).
Return `0` on success; non-zero if the path is NULL or the file cannot be opened
(`log_report` fails only when the logger is unavailable).

Example JSON shape (abridged):

```json
{
  "version": 1,
  "recording": true,
  "cpu": {
    "frame": { "current_ms": 16.4, "min_ms": 14.0, "max_ms": 22.1, "avg_ms": 16.1, "count": 120 },
    "tasks": [
      { "name": "Update", "category": "sim", "depth": 0, "calls": 1, "total_ms": 4.2, "pct": 25.6 }
    ]
  },
  "gpu": { "frame": { "...": "..." }, "tasks": [] }
}
```

---

## 6. C++ → C migration map

Map from main’s `Skore::Profiler` (`Runtime/Source/Skore/Profiler.{hpp,cpp}`) to
the v2 C plugin. Use this when porting call sites.

### Types and module

| Legacy C++ | New C |
| ---------- | ----- |
| `namespace Skore::Profiler` | `sk_` prefix; types `sk_profiler_*_t` |
| Process-global free functions | `sk_profiler_api_t` table registered under `SK_PROFILER_API_TYPE_ID` |
| `TaskEntry` | `sk_profiler_task_entry_t` |
| `FrameStats` | `sk_profiler_frame_stats_t` |
| `StringView name` | `const_chr_t` (`const char*`); copied into `char[64]` at begin |
| `GPUCommandBuffer* cmd` | `sk_command_buffer_t` (opaque handle) |
| `u32& count` out-ref | `u32* count` out-param |
| `std::chrono::high_resolution_clock` | `sk_platform_api_t::monotonic_seconds` (plugin) / core clock |

### Functions

| Legacy C++ | New C (`sk_profiler_api_t`) |
| ---------- | --------------------------- |
| `Init()` | `init(sk_render_device_t dev)` — pass zero for CPU-only |
| `Shutdown()` | `shutdown()` |
| `BeginFrame()` | `begin_frame()` |
| `EndFrame()` | `end_frame()` |
| `BeginCpuSample(name)` | `begin_cpu_sample(name, category, color)` — pass `NULL, 0u` for parity |
| `EndCpuSample(name)` | `end_cpu_sample()` — name dropped (stack pop) |
| `BeginGpuSample(name, cmd)` | `begin_gpu_sample(name, category, color, cmd)` |
| `EndGpuSample(name, cmd)` | `end_gpu_sample(cmd)` — name dropped |
| `GetCpuTasks(count)` | `get_cpu_tasks(&tasks, &count)` |
| `GetGpuTasks(count)` | `get_gpu_tasks(&tasks, &count)` |
| `GetCpuFrameStats()` | `get_cpu_frame_stats()` |
| `GetGpuFrameStats()` | `get_gpu_frame_stats()` |
| `ResetStats()` | `reset_stats()` |
| `SetActive(bool)` / `IsActive()` | `set_active(bool)` / `is_active()` |
| *(none — UI only on main)* | `dump_report` / `dump_report_json` / `log_report` |

### Macros and RAII helpers

| Legacy C++ | New C |
| ---------- | ----- |
| `SK_SCOPED_CPU_ZONE(name)` | `SK_PROFILE_CPU_ZONE(api, name)` |
| `SK_SCOPED_GPU_ZONE(name, cmd)` | `SK_PROFILE_GPU_ZONE(api, name, cmd)` |
| `ScopedCpuSample` RAII | Scoped macro (GCC/Clang cleanup / MSVC for-loop) or explicit begin/end |
| `ScopedGpuSample` RAII | Same for GPU macros |
| *(no category/color)* | `SK_PROFILE_*_ZONE_EX` / begin with category + color |
| Always linked into Runtime | Compile-time `SK_ENABLE_PROFILER` / `SK_PROFILER_ENABLED` strips macros |

### Behavioral parity notes

- Recording defaults **inactive** (player F5 / editor Record turn it on).
- Triple-buffered frames; GPU task build lags by two frames (query completion).
- Same-name samples merge; nest depth drives tree layout.
- Silent drop after 256 samples per frame buffer.
- Optional Tracy (`SK_ENABLE_TRACY` on main) is **not** ported — do not add a
  third-party profiler dependency.

### Call-site sketch

```c
/* main (C++) */
SK_SCOPED_CPU_ZONE("Update");
Skore::Profiler::BeginGpuSample("Pass", cmd);
/* ... */
Skore::Profiler::EndGpuSample("Pass", cmd);

/* v2 (C) */
const sk_profiler_api_t* prof = /* get_api(...) */;
SK_PROFILE_CPU_ZONE(prof, "Update");
SK_PROFILE_BEGIN_GPU_SAMPLE(prof, "Pass", NULL, 0u, cmd);
/* ... */
SK_PROFILE_END_GPU_SAMPLE(prof, cmd);
```

---

## 7. Tests and build verification

In-source `SK_TEST` bodies (Debug / non-Release with `BUILD_TESTING`):

| Area | Where |
| ---- | ----- |
| Zone begin/end pairing and nesting | `profiler.c`, `core/profiler_core_tests.c`, `profiler_macro_tests.c` |
| Aggregation math (merge, rolling min/max/avg) | `profiler.c`, `core/profiler_core_tests.c` |
| Buffer-overflow policy (thread ring, frame ring, task table, mismatched ends) | `core/profiler_core_tests.c`, `profiler.c` (`profiler_sample_cap_drops_overflow`) |
| Multi-threaded capture, no cross-thread corruption | `core/profiler_core_tests.c` (`profiler_core_worker_threads_record_lockfree`) |
| Disabled-profiling macros compile away | `profiler_macros_match_compile_time_switch` (this TU’s compile mode) + full build with `SK_ENABLE_PROFILER=OFF` |
| Forced-enabled macro bodies | `profiler_macro_tests.c` (defines `SK_PROFILER_ENABLED` even when the option is OFF) |

```bash
# Default: macros disabled
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSK_ENABLE_PROFILER=OFF
cmake --build build --target sk-tests
./build/bin/sk-tests

# Macros enabled
cmake -S . -B build-prof -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSK_ENABLE_PROFILER=ON
cmake --build build-prof --target sk-tests
./build-prof/bin/sk-tests
```

Plugin tests run via `sk_plugin_run_tests` when the host loads `sk-profiler`.

---

## 8. Layout

```text
plugins/profiler/
  README.md                 # this file
  AUDIT.md                  # C++ → v2 migration audit
  CMakeLists.txt            # sk_add_plugin(profiler ...)
  plugin_entry_point.c      # sk_plugin_entry_point → sk_profiler_init
  profiler.h                # public C API + macros
  profiler.c                # table implementation, reports, plugin tests
  profiler_macro_tests.c    # forced SK_PROFILER_ENABLED macro tests
  core/
    profiler_core.h         # engine-independent capture core
    profiler_core.c
    profiler_core_tests.c
```
