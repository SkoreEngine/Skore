# Profiler migration audit (C++ main → v2 C plugin)

Task: **APX-197**. Branch: **v2** (this tree). Reference (read-only): `main`
`Runtime/Source/Skore/Profiler.{hpp,cpp}` and consumers under Runtime, Player,
Editor. Destination: future **`plugins/profiler/`** (this directory).

**Scope:** written audit only. **No implementation code**, no Tracy / third-party
profiling dependency, no behavioral changes to the engine or existing plugins.

**Goal:** migrate the first-party CPU/GPU hierarchical profiler to a v2 C plugin
that matches existing plugin conventions (`sk_plugin_entry_point`, API table
registration, `sk_add_plugin` build).

---

## Method

1. Read `Profiler.hpp` / `Profiler.cpp` on `main` (full public surface + private
   implementation).
2. Trace call sites: `App` frame loop, `SK_SCOPED_*_ZONE` instrumentation,
   `ProfilerOverlayPass` (player F5 overlay), `DebuggerWindow` (editor UI).
3. Compare against v2 plugin conventions (`platform_window`, `entities`,
   `render_device`, `render_graph`, …) and host load path in `app/app.c`.
4. List C++-only idioms and propose C replacements that fit AGENTS.md.

---

## 1. Legacy C++ profiler (`main`)

### 1.1 Source locations

| Path | Role |
| ---- | ---- |
| `Runtime/Source/Skore/Profiler.hpp` | Public API, macros, `TaskEntry` / `FrameStats` |
| `Runtime/Source/Skore/Profiler.cpp` | Implementation (anonymous namespace state) |
| `Runtime/Source/Skore/Graphics/Pipeline/ProfilerOverlayPass.{hpp,cpp}` | Player F5 on-screen tree overlay |
| `Editor/Source/Skore/Window/DebuggerWindow.{hpp,cpp}` | Editor ImGui CPU/GPU tabs + history chart |
| `Runtime/Source/Skore/App.cpp` | `Init` / `Shutdown` / `BeginFrame` / `EndFrame` |
| Root `CMakeLists.txt` | `option(SK_ENABLE_TRACY … OFF)` optional Tracy compile path |
| `ThirdParty/tracy/**` | Optional third-party (must **not** be ported) |

Profiler sources are compiled into the shared `SkoreRuntime` library via the
runtime GLOB of `Source/*.{hpp,cpp}`. There is no separate profiler CMake target
on main.

**Not in scope for this migration (vendored third-party profilers on main):**
Jolt `ThirdParty/JoltPhysics/.../Profiler.*`, RmlUi `Profiling.*`, and the full
Tracy tree. The first-party surface is only `Skore::Profiler` + the two UI
consumers above.

### 1.2 Public API and macros

Namespace: `Skore::Profiler` (`Profiler.hpp`).

#### Macros (call-site instrumentation)

```c
#define SK_SCOPED_GPU_ZONE(name, cmd) \
    Skore::Profiler::ScopedGpuSample SK_CONCAT(scopedZone_, __LINE__)(name, cmd)
#define SK_SCOPED_CPU_ZONE(name) \
    Skore::Profiler::ScopedCpuSample SK_CONCAT(scopedZone_, __LINE__)(name)
```

`SK_CONCAT` comes from `Common.hpp` (`x##y` token paste). Macros expand to RAII
stack objects that call Begin/End in ctor/dtor.

#### RAII helpers

| Type | Members | Behavior |
| ---- | ------- | -------- |
| `ScopedCpuSample` | `StringView name` | ctor → `BeginCpuSample`; dtor → `EndCpuSample` |
| `ScopedGpuSample` | `StringView name`, `GPUCommandBuffer* cmd` | ctor → `BeginGpuSample`; dtor → `EndGpuSample` |

#### Free functions (`SK_API`)

| Function | Purpose |
| -------- | ------- |
| `Init()` | Read GPU `timestampPeriod`; create 3 timestamp query pools (GPU context only) |
| `Shutdown()` | Destroy GPU query pools |
| `BeginCpuSample(StringView name)` | Push nested CPU sample on write buffer |
| `EndCpuSample(StringView name)` | Pop stack; `name` is **ignored** (stack-based nesting) |
| `BeginGpuSample(StringView name, GPUCommandBuffer* cmd)` | CPU wall stamp + `WriteTimestamp` start |
| `EndGpuSample(StringView name, GPUCommandBuffer* cmd)` | CPU wall stamp + `WriteTimestamp` end; `name` ignored |
| `BeginFrame()` | If active: advance triple buffers, build tasks from N−2, accumulate wall-clock frame stat |
| `EndFrame()` | Empty (no-op today) |
| `GetCpuTasks(u32& count)` / `GetGpuTasks(u32& count)` | Pointer to built `TaskEntry[]` + count |
| `GetCpuFrameStats()` / `GetGpuFrameStats()` | Rolling frame min/max/avg/current |
| `ResetStats()` | Clear per-task and frame rolling stats (both contexts) |
| `SetActive(bool)` / `IsActive()` | Master overhead switch |

### 1.3 Data structures

#### Public

```text
TaskEntry {
  f64 cpuTime, gpuTime;
  f64 cpuMin, cpuMax, cpuAvg;
  f64 gpuMin, gpuMax, gpuAvg;
  u32 cpuCount, gpuCount;
  u32 color;            // ABGR-ish palette entry from name hash
  char name[64];
  i32 depth;            // nest depth at sample open
  bool hasGPU;
  bool present;         // seen this read frame
}

FrameStats {
  f64 current, min, max, avg;
  u32 count;
}
```

#### Private (`.cpp` anonymous namespace)

| Struct / constant | Meaning |
| ----------------- | ------- |
| `MaxSamples = 256` | Cap samples per frame buffer and task list |
| `BufferCount = 3` | Triple-buffer frames |
| `Sample` | Per open/close zone: name[64], cpuStart/End (seconds from frame start), gpu query indices, hasGPU, depth |
| `Buffer` | `Sample[256]`, sampleCount, `GPUQueryPool*`, queryIndex, queryPoolReset |
| `ProfilerContext` | 3 buffers, writeIdx/readIdx, sample stack, tasks[256], rolling frame stats |
| `cpuCtx` / `gpuCtx` | Independent contexts (CPU zones never write GPU queries) |
| `colorPalette[16]` | Fixed colors; FNV-1a style hash of name → palette index |
| Globals | `frameNumber`, `frameStart` (`chrono` time_point), `active`, `hasLastFrame`, `timestampPeriod` |

Aggregation model:

- Multiple samples with the **same name** merge into one `TaskEntry` (sum times
  per frame; rolling min/max/avg across frames).
- Nest order is reconstructed so children sit under their parent in the task
  array (`pathStack` / insert-at-depth logic in `BuildTasks`).
- CPU frame stat = wall clock between successive `BeginFrame` calls.
- GPU frame stat = sum of **depth-0** GPU task times for that read buffer.

### 1.4 Timing sources

| Domain | Source | Unit |
| ------ | ------ | ---- |
| CPU sample intervals | `std::chrono::high_resolution_clock` relative to `frameStart` | seconds (`f64`) |
| CPU frame total | Same clock delta between `BeginFrame` calls | seconds |
| GPU sample intervals | Timestamp query pool (`QueryType::Timestamp`) | ticks × `device.limits.timestampPeriod` × 1e−9 → seconds |
| Query pool size | `MaxSamples * 2` queries per buffer (start+end per zone) | — |
| Read lag | `readIdx = (frameNumber + BufferCount - 2) % 3` | GPU results from **two frames ago** |

CPU samples always stamp wall time even when recording a GPU zone (GPU path
still records `cpuStart`/`cpuEnd` so the GPU panel can show CPU record cost).

### 1.5 Thread handling

**None.** The profiler uses process-global `ProfilerContext` state with no
mutex, no TLS, no per-thread sample rings.

Implications:

- Safe only when all Begin/End and BeginFrame/EndFrame run on the **same
  thread** (the main / render-record thread in current main usage).
- Concurrent instrumentation from worker threads would race on
  `sampleCount` / stack / buffers.
- Migration should document main-thread ownership to match v2 AGENTS.md
  defaults, or later add explicit TLS + merge if workers need zones.

### 1.6 Overhead-control switches

| Switch | Effect |
| ------ | ------ |
| `SetActive(false)` / default `active = false` | `BeginSample` / `BeginFrame` early-out; clears tasks + frame stats; resets `hasLastFrame` |
| `SetActive(true)` | Enables recording (player F5 / editor Record) |
| `MaxSamples` hard cap | Drop new samples when write buffer full (silent) |
| Optional `SK_ENABLE_TRACY` | Extra Tracy `TracyMessageL` / `FrameMark` only — **orthogonal**; first-party profiler does not require it |

There is no sampling rate, no per-category filter, and no compile-time strip of
the first-party macros (zones always call into the DLL; inactive is the cheap
path).

### 1.7 Output / report format

The profiler does **not** write a file or wire protocol. Output is in-memory
POD arrays consumed by UI:

1. **Task tree** — `GetCpuTasks` / `GetGpuTasks` → `TaskEntry[]` with `depth`
   for indentation; times in **seconds** (UI multiplies by 1000 for ms).
2. **Frame stats** — `GetCpuFrameStats` / `GetGpuFrameStats` →
   current/min/max/avg (+ count) for header lines.
3. **Player overlay** (`ProfilerOverlayPass`) — F5 toggles `SetActive` + draws
   two panels (CPU then GPU) via `DrawList`: name swatches, CPU columns,
   GPU columns on GPU panel; header
   `"%s FPS … frame/avg/min/max ms"`.
4. **Editor debugger** (`DebuggerWindow`) — ImGui tabs “CPU Profiler” /
   “GPU Profiler”: Record/Stop, Reset stats, hierarchical table, and a
   300-frame history chart of root segments (local UI state, not part of
   `Profiler` itself).

Call-site instrumentation found on main (non-exhaustive of future zones):

| File | Zones |
| ---- | ----- |
| `Scene/Scene.cpp` | ExecuteEvents, Update, OnFixedUpdate, OnUpdate |
| `Scene/Physics.cpp` | character controllers, fixed update, write-back, collision events, pending bodies |
| `Navigation/Navigation.cpp` | NavMesh Update |
| `Graphics/RenderResourceCache.cpp` | Flush |
| `Graphics/RenderSceneObjects.cpp` | Begin (CPU); TLAS Update (GPU) |

Lifecycle wiring (`App.cpp`): `Profiler::Init()` once after first update path
has graphics; each frame `BeginFrame` before work / `EndFrame` after; 
`Shutdown` on destroy.

---

## 2. v2 C engine plugin architecture

### 2.1 Layout and build

```text
plugins/
  CMakeLists.txt              # add_subdirectory per plugin
  <name>/
    CMakeLists.txt            # sk_add_plugin(...) + extra links
    plugin_entry_point.c      # exported entry (+ optional tests)
    <name>.h                  # public types + sk_*_api_t + TYPE_ID
    <name>.c                  # static table + sk_*_init registration
```

Root `CMakeLists.txt` → `add_subdirectory(plugins)`. Each plugin
`CMakeLists.txt` calls `sk_add_plugin` from `cmake/cmake_functions.cmake`:

```cmake
sk_add_plugin(<short-name> SOURCES ${sources})
# Creates:
#   sk-<short-name>        SHARED  → {runtime}/plugins/sk-<short-name>.{so,dll,dylib}
#   sk-<short-name>-lib    INTERFACE headers for consumers/tests
# Links PRIVATE sk-core (static). PREFIX "" so Unix matches unprefixed name.
# Non-Release: SK_TESTS + link sk-test for in-DLL Unity registry.
```

To add a plugin: create the directory, wire `sk_add_plugin`, and
`add_subdirectory` in `plugins/CMakeLists.txt`. Output lands next to the
executable under `plugins/` for auto-load.

### 2.2 Registration entry point (exact contract)

Every plugin exports:

```c
SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api);
#ifdef SK_TESTS
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out);
#endif
```

Host (`app/app.c` → `sk_app_load_plugin_impl`):

1. `plat->lib_open(path)`
2. `plat->lib_symbol(lib, "sk_plugin_entry_point")`
3. `entry(context, sk_app_api())` — must return `0` on success
4. Keep library handle until bootstrap shutdown

Bootstrap auto-scans `{app_folder}/plugins` (fallback `{cwd}/plugins`), skips
shared libs that do **not** export `sk_plugin_entry_point` (e.g. vendored DXC
runtime copied into the same folder).

### 2.3 Example plugins (two+)

#### A. `plugins/platform_window/`

| Item | Detail |
| ---- | ------ |
| CMake name | `sk_add_plugin(platform-window …)` → `sk-platform-window` |
| Entry | `plugin_entry_point.c` → `sk_platform_window_init(context, app_api)` |
| Register | `app_api->set_api(context, SK_PLATFORM_WINDOW_API_TYPE_ID, &platform_window_api)` |
| API table | `sk_platform_window_api_t` — window create/poll/dialogs + **`init` / `shutdown`** members |
| Lifecycle | Load-time: register table only. Host (or tests) call `api->init` / `api->shutdown` when needed. **No per-frame hook** in the plugin entry. |
| Logging | Uses core logger where needed; no special sink |
| Memory | GLFW/NFD-owned; engine allocations via default patterns in module |
| Tests | `sk_plugin_run_tests` → `sk_test_run_all_status` under `SK_TESTS` |

#### B. `plugins/entities/`

| Item | Detail |
| ---- | ------ |
| CMake name | `sk_add_plugin(entities …)` → `sk-entities` |
| Entry | `plugin_entry_point.c` → `sk_entities_init` |
| Register | `set_api(..., SK_ENTITIES_API_TYPE_ID, &entities_api)` |
| API table | Large `sk_entities_api_t` (world, queries, systems, commands) — pure table, no free-function mirrors |
| Lifecycle | Registration only at entry; no global init/shutdown on the table today |
| Memory | `sk_allocator_default()` for chunks, archetypes, command buffers |
| Logging | None required for core ECS paths |

#### C. `plugins/render_graph/` (third reference)

Same entry pattern; `sk_render_graph_init` registers `sk_render_graph_api_t`
including a **`shutdown`** table entry (module-level). Graph instances own
arenas/pools via `sk_allocator_t`.

### 2.4 Lifecycle hooks (what exists / what does not)

| Hook | Who owns it | Notes |
| ---- | ----------- | ----- |
| Process load | Host | `lib_open` + `sk_plugin_entry_point` |
| Registration | Plugin entry | `set_api` / sometimes `add_impl` |
| Module init/shutdown | Optional API members | e.g. `platform_window.init/shutdown`, `render_graph.shutdown` — **host-called**, not auto |
| Per-frame | **Not automatic** | Host `sk_app_tick` updates timing only; no plugin frame callback list. Callers invoke API functions (or ECS systems) each frame. |
| Unload | Host bootstrap shutdown | `lib_close` all handles; plugins do not receive an unload callback today |

Implication for profiler: the plugin should **register** `sk_profiler_api_t` at
entry. The host (or a small player/editor bootstrap) must call
`begin_frame` / `end_frame` and optional `init` after a render device exists —
mirroring main’s `App` wiring, not inventing a hidden global constructor.

### 2.5 Memory and logging conventions

| Concern | Convention |
| ------- | ---------- |
| Heap | `sk_allocator_t` / `sk_allocator_default()` (mimalloc backend); allocate at init / known boundaries, not hot-path ad-hoc growth |
| Fixed buffers | Prefer fixed caps (main profiler already uses 256 samples / triple buffer — good fit) |
| Logging | `sk_logger_api()` + named logger or `sk_log_*` helpers; host logger name `"app"` exists on context |
| Errors | `i32` status (`0` success); no exceptions; trust callers for null/invariant bugs |
| Cross-plugin | Headers + type ids only; never link plugin A to plugin B |

GPU timestamp support on v2 is already sketched on the RHI surface
(`plugins/render_device/render_device.h`): `create_query_pool`,
`write_timestamp`, `get_query_pool_results`, device limit
`timestamp_period`. Vulkan / test backends implement or stub these; the
profiler plugin should depend on the **render_device API table** via
`get_api`, not by linking `sk-vulkan-render-device`.

### 2.6 Host timing available for CPU path

v2 already exposes:

- `sk_platform_api_t::monotonic_seconds` (Unix `CLOCK_MONOTONIC`, Win
  `QueryPerformanceCounter`)
- `sk_app_api_t::delta_time` / `elapsed_time` / `fps` on the bootstrapped
  context

Prefer **platform monotonic seconds** for sample stamps (same role as main’s
`high_resolution_clock`), not wall/calendar time.

---

## 3. C++ features without a clean C equivalent → replacement idioms

| C++ on main | Why it does not map 1:1 | v2 C replacement |
| ----------- | ------------------------ | ---------------- |
| `namespace Skore::Profiler` | No namespaces | `sk_` prefix; types `sk_profiler_*_t`; module table `sk_profiler_api_t` |
| `ScopedCpuSample` / `ScopedGpuSample` RAII | No destructors | Keep explicit `begin_*` / `end_*` on the API table; optional macros that open a for-loop or nested block with `__attribute__((cleanup))` **or** simply document paired calls. Prefer macros that expand to begin + a unique end label only if needed — simplest portable form: `SK_SCOPED_CPU_ZONE` becomes a statement macro that is **not** RAII, or a GNU cleanup helper behind `#if` with a manual fallback. **Recommended:** ship `begin/end` + a C99 macro using a for-loop guard (`for (int _once = (begin(), 1); _once; _once = (end(), 0))`) so call sites stay one-liners without C++. |
| `StringView` | C++ class | `const char*` + length, or `sk_str_view_t` (`path.h`); sample names are copied into `char[64]` at begin — C can take `const_chr_t` only |
| `std::chrono::high_resolution_clock` | C++ chrono | `sk_platform_api()->monotonic_seconds()`; store frame start as `f64` |
| Member functions / free functions in namespace | C++ linkage | Single `static const sk_profiler_api_t` filled with `static` functions; register via `set_api` |
| Default arguments / references (`u32& count`) | C++ refs | Out-params: `void get_cpu_tasks(const sk_profiler_task_entry_t** out, u32* count)` |
| Classes for overlay (`ProfilerOverlayPass`) | C++ inheritance / lambdas | Out of scope for first profiler plugin slice: UI can stay host-side later; plugin only owns timing + task buffers |
| Optional Tracy (`#ifdef SK_ENABLE_TRACY`) | Third-party | **Do not port.** No Tracy, no third-party profiler dependency in this migration |
| Process-global C++ statics in `.cpp` | Works in C too | Same pattern OK inside plugin `.c` **or** heap instance owned by `init` if multi-context ever matters; document single global for parity |
| `strncpy` + `StringView::Data/Size` | C++ string view | `memcpy`/`strncpy` from C string with explicit max 63 + NUL |
| `GPUCommandBuffer*` OOP | C++ RHI objects | Opaque `sk_command_buffer_t` + `sk_render_device_api_t*` function pointers |
| `std::memcpy` in editor | C++ | `memcpy` from `<string.h>` |

Nothing in the **core algorithm** (triple buffer, nest stack, name merge,
palette hash, rolling stats, GPU query lag) requires C++. The only
ergonomics gap is RAII scoped zones; the for-loop macro or explicit
begin/end fully covers it.

---

## 4. Proposed v2 shape (documentation only — not implemented)

Suggested future layout (this directory):

```text
plugins/profiler/
  AUDIT.md                 # this file
  CMakeLists.txt           # sk_add_plugin(profiler SOURCES ...)
  plugin_entry_point.c     # sk_plugin_entry_point → sk_profiler_init
  profiler.h               # sk_profiler_api_t, TaskEntry, FrameStats, TYPE_ID, zone macros
  profiler.c               # contexts, triple buffer, register set_api
```

Suggested registration (now frozen by APX-198; zone begins take an optional
category label and color, and a text report/dump entry point was added):

```c
#define SK_PROFILER_API_TYPE_ID SK_TYPE_ID("sk.profiler_api", /* cmake fills hashes */)

typedef struct sk_profiler_api_t {
  void (*init)(void);              /* after render device available; may no-op CPU-only */
  void (*shutdown)(void);
  void (*begin_frame)(void);
  void (*end_frame)(void);
  void (*begin_cpu_sample)(const_chr_t name, const_chr_t category, u32 color); /* category NULL/color 0 optional */
  void (*end_cpu_sample)(void);    /* drop unused name param */
  void (*begin_gpu_sample)(const_chr_t name, const_chr_t category, u32 color, sk_command_buffer_t cmd);
  void (*end_gpu_sample)(sk_command_buffer_t cmd);
  void (*get_cpu_tasks)(const sk_profiler_task_entry_t** out, u32* count);
  void (*get_gpu_tasks)(const sk_profiler_task_entry_t** out, u32* count);
  sk_profiler_frame_stats_t (*get_cpu_frame_stats)(void);
  sk_profiler_frame_stats_t (*get_gpu_frame_stats)(void);
  i32 (*dump_report)(const_chr_t path);  /* text report of last built frame; later tasks add formats */
  void (*reset_stats)(void);
  void (*set_active)(bool active);
  bool (*is_active)(void);
} sk_profiler_api_t;
```

Host responsibilities (later tasks):

1. Load plugin (auto-scan already).
2. After render device is up: `api->init()` (create query pools via RHI).
3. Each frame: `begin_frame` / `end_frame` around sim+render record.
4. Optional UI: reimplement overlay/debugger against the table (separate work).

**Explicit non-goals for the migration stream:** Tracy, chrome://tracing export,
multi-thread TLS rings (unless a later task requires them), and coupling the
profiler plugin binary to vulkan-specific code (use `sk_render_device_api_t`
only).

---

## 5. Risk / dependency notes

| Topic | Finding |
| ----- | ------- |
| External consumers of C++ profiler | Same-repo only (Runtime, Player overlay, Editor debugger). No separate product tree. |
| RHI readiness | Query-pool and timestamp entry points exist on v2 RHI surface; real GPU results need a live device backend. |
| Active-by-default | Main defaults **inactive** until F5/Record — preserve that to keep Release overhead near zero. |
| Silent sample drop | At 256 zones/frame, further begins are ignored — document the cap. |
| `End*Sample(name)` unused name | Stack pop only; C API can omit the name on end. |
| Thread model | Main-thread only; matches v2 default ownership. |

---

## 6. Verdict

- First-party profiler on `main` is a **compact hierarchical CPU/GPU sampler**
  with a clear POD report surface — well suited to a pure C plugin.
- v2 already has the **plugin registration, build, allocator, logging, clocks,
  and RHI query hooks** needed for a faithful port.
- C++-only pieces are **RAII scopes, namespaces, StringView, chrono, and
  optional Tracy** — all have straightforward C or “do not port” answers.
- Proceed with `sk-profiler` under `plugins/profiler/` in a follow-up
  implementation task; this audit is documentation only.

---

## 7. Follow-up status

| Task | Result |
| ---- | ------ |
| APX-198 | C API contract frozen: `sk_profiler_api_t` with zone category/color, `dump_report` entry point, `_EX` macros. |
| APX-199 (core) | Engine-independent core implemented under `plugins/profiler/core/` (`profiler_core.h` / `profiler_core.c` / `profiler_core_tests.c`): platform monotonic high-res clock (Win32 QPC, POSIX CLOCK_MONOTONIC, injectable for tests), per-thread TLS zone buffers with lock-free registration and allocation-free hot path, bounded ring buffers with counted non-crashing overflow (per-thread per-frame cap, frame ring keep-newest, task table cap, mismatched ends), nested depth tracking (dropped begins keep depth consistent), and per-zone accumulation (per-frame calls/total + rolling min/max/avg) with frame wall-time stats. Standalone: no app context, plugin registry or render device dependency; uses sk-core allocator + logger only. Wiring into the plugin table is a later task. |
| APX-199/200 | Plugin lifecycle integrated (host begin/end frame in `sk_app_tick`), compile-switchable `SK_PROFILE_*` macros, player frame zone. |
| APX-201 | Report path live: `dump_report` (text), `dump_report_json` (versioned JSON), `log_report` (console/log) with per-frame + cumulative aggregation (call counts, total/min/max/avg, % of frame, nesting). Engine call sites instrumented: main-loop phases (`app tick` → `tick timing`), ECS (`ecs spawn` / `ecs despawn` / `ecs scheduler run`), render graph (`rg begin` / `rg compile` / `rg execute`). Verified end to end with `SK_ENABLE_PROFILER=ON` (see `app_profiler_report_end_to_end`). Still no Tracy / third-party backend. |
| APX-202 | Tests + docs: core/plugin/macro `SK_TEST` coverage for zone begin/end nesting, aggregation math, bounded overflow policy, multi-threaded TLS capture (no cross-thread corruption), and compile-time macro strip (`SK_ENABLE_PROFILER` OFF/ON). Plugin README documents API, macros, enable/disable, report formats, and C++→C migration map. Full v2 build verified with profiling enabled and disabled. |
