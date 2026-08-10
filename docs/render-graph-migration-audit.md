# Render graph migration audit (C++ main → v2 C plugin)

Task: **APX-148**. Branch: **v2**. Reference (read-only): `main` tree
`Runtime/Source/Skore/Graphics/RenderGraph.{hpp,cpp}` and call sites under
`Runtime/`, `Player/`, `Editor/`, plus tests. Destination scaffold on v2:
`plugins/render_graph/` (stub `init`/`shutdown` only as of this audit).

**Scope:** documentation and a proposed C API sketch only. **No behavioral
changes** to the engine or plugins in this task.

---

## Method

1. Read `RenderGraph.hpp` / `RenderGraph.cpp` on `main` (full public surface +
   private implementation).
2. Trace construct / `Begin` / `BuildRenderGraph` / `Execute` call sites across
   Runtime, Player, Editor, and unit/GPU tests.
3. Compare against v2 plugin conventions (`platform_window`, `entities`,
   `dxc_compiler`, `render_device`, `test_render_device`) and the existing
   `render_graph` scaffold.
4. Cross-check RHI surface used by the graph against
   `plugins/render_device/render_device.h` and
   `docs/render-device-gap-audit.md`.

---

## Escalation notes

| Question | Finding |
| -------- | ------- |
| Consumers of the C++ graph **outside this repo**? | **No.** All production consumers live in the same GitHub repo (`SkoreEngine/Skore`) on **`main`**: Runtime pipelines, Player, Editor (scene view, properties, preview/thumbnail). There is no separate product tree depending on this header. |
| Does the **RHI abstraction itself** need to change for migration? | **Mostly no for barriers/aliasing.** v2 already exposes `create_memory`, `create_aliased_texture`, texture/buffer barriers, and `memory_barrier`. **Design adaptation is required** for main’s RID-centric helpers the graph uses today (`CreateDescriptorSet(RID, variant, set)`, shader RID on passes, `Resources::FindByPath`). v2’s RHI policy is “no RID inside the RHI — pass full data” (see render-device gap audit §F). The C graph should resolve shaders/pipelines/descriptor layouts **outside** or via full descs, not by reintroducing RID lookup into the RHI. That is an API design choice for the migration, not a hard requirement to change existing RHI entry points. |

**Verdict:** proceed with a C `sk-render-graph` plugin on v2; do not block on
external consumers. Coordinate auto-descriptor / pipeline creation with the
non-RID RHI model rather than expanding RHI with RID reads.

---

## 1. C++ render graph feature inventory (`main`)

Source of truth: `Runtime/Source/Skore/Graphics/RenderGraph.hpp` (~444 lines)
and `RenderGraph.cpp` (~2737 lines). Companion: `RenderPipeline.{hpp,cpp}`
owns the usual `Begin → Build → Execute` frame loop.

### 1.1 Pass types

| Enum `RenderGraphPassType` | Builder entry | Default dispatch |
| -------------------------- | ------------- | ---------------- |
| `Compute` | `AddComputePass(name, shaderPath\|RID)` | `Dispatch` / `DispatchIndirect` (or custom `Render` callback) |
| `Graphics` | `AddGraphicsPass(name)` | `Render` callback inside `BeginRenderPass`/`EndRenderPass` |
| `Raytrace` | `AddRaytracePass(name, RID)` | `TraceRays` |
| `Transfer` | `AddPass(name)` | custom `Render` only (no auto pipeline) |

Pass builder is fluent on `RenderGraphPass&`:

- Access: `Write` / `Read` / `WriteRead` / `Resolve` (MSA resolve attachment).
- Descriptors: `DescriptorSet(set, GPUDescriptorSet*)`.
- Viewport: `InvertViewport` (Vulkan Y-flip).
- Flags: `RequireJitter`, `RequireMotionVector` (camera/TAA metadata for callers).
- Clears: `ClearColor(name, Vec4)` mutates the **resource desc** clear color.
- Ordering hint: `Stage(i32)` (see `RenderStage` constants in `RenderPipeline.hpp`:
  Culling=100 … Swapchain=1300).
- Push constants: `Constants<T>(fn)` → fixed ≤256-byte stack scratch at execute.
- Resize hook: `Resize(fn)` called when output size changes.
- Record hook: `Render(fn(pass, scene, cmd))`.
- Dispatch: `Dispatch(x,y,z)` / `Dispatch(Extent3D)` / `DispatchIndirect(buffer)`.
- Rays: `TraceRays(w,h,d)` stores dimensions in the same `dispatch*` fields.

### 1.2 Resource kinds

Internal `RenderGraph::Resource::Kind`:

| Kind | How created | Lifetime notes |
| ---- | ----------- | -------------- |
| `Texture` | `Create(name, RenderGraphTextureDesc)` | Transient GPU textures; optional **ping-pong** (`textures[0/1]`), **persistent**, scale-from-output (`extent` 0 → `outputSize * scale`), cubemap, samples, mips, clear values |
| `Buffer` | `Create(name, RenderGraphBufferDesc)` | Size + usage flags; **hostVisible** / **persistentMapped**; **perFrame** or **pingPong** → `SK_FRAMES_IN_FLIGHT` copies |
| `View` | `CreateView(name, RenderGraphViewDesc)` | Named subresource view of another graph texture (mip/layer range); barriers apply to parent subresources |
| `Imported` | `Import(name, Span<GPUTexture*>, ResourceState)` | External textures (typically swapchain images); tracked per index; restored to `importedState` at end of `Execute` |
| `Instance` | `CreateInstance` / `CreateInstance<T>` | CPU blackboard blob (`operator new`); not a GPU resource |
| `AccelerationStructure` | `Create(name, RenderGraphAccelStructDesc)` | Holds external `GPUTopLevelAS*` pointer (not owned as GPU alloc by the graph) |

Usage flags for textures/buffers are **inferred** from pass accesses
(`InferTextureUsage` / `InferBufferUsage`) unless the desc already sets usage
bits (then OR’d). Graphics write → RT or depth-stencil; compute/RT write → UAV;
read → SRV; transfer → copy src/dst; etc.

Named outputs:

- `SetColorOutput` / `SetDepthOutput` (exclude from memory aliasing).
- `SetOutputAttachments` = import + mark as color output name.
- `SetCurrentOutputIndex` selects which imported/swapchain image is active.
- `SetOutputSize` / `GetOutputSize`; textures with zero extent follow output size.

### 1.3 Dependency declaration

- Each pass stores `Dependency { nameStorage, name view, access }` with
  `RenderGraphAccess`: `Read` | `Write` | `ReadWrite`.
- Views alias to their parent texture name for **sort edges**.
- Writer→reader, writer→writer, and reader→writer (W after R) edges built during
  `SortPasses`.
- Stable topological order with **stage** as a tie-break among indegree-0 nodes.
- Graph signature (pass names, types, stages, deps, resolves) cached; if
  unchanged, reuses `cachedSortedPassIndices` (avoids full rebuild).
- Cycles: `SK_ASSERT` and abort sort.

**There is no unused-pass culling.** Every pass added this frame is sorted and
executed. “Culling” in the pipeline sense is a **scene pass stage**
(`RenderStage::Culling` / `CullingPass`), not graph dead-code elimination.
`camera.cullingMask` is only scene-buffer data for GPU culling shaders.

### 1.4 Resource lifetime and GC

- Frame ring: `Begin` advances `currentFrame = (currentFrame+1) % SK_FRAMES_IN_FLIGHT`,
  bumps `frameGeneration`, returns all pass objects to `passPool`, clears the
  active pass list.
- Resources stamp `lastUsed = frameGeneration` on create/import and when a pass
  depends on them during `Execute`.
- `CollectUnusedResources` (from `Begin`): if any non-instance, non-aliased
  resource has `frameGeneration - lastUsed >= SK_FRAMES_IN_FLIGHT`, wait idle,
  destroy GPU objects, erase from the resource map.
- Resize (`sizeChanged`): destroy **output-following** textures/views, recreate
  on next `Execute` when `resourcesDirty`.
- Scene buffer + per-frame scene descriptor sets created once
  (`CreateSceneResources`).

### 1.5 Memory aliasing

- Transient **textures** only (not buffers, not ping-pong, not persistent, not
  color/depth outputs).
- Lifetime = first/last pass index that touches the resource; eligible only if
  the first use **writes and does not read** (pure producer start).
- `ComputeRenderGraphAliasPlan`: size-desc sort, place into heaps with
  non-overlapping lifetimes and matching `memoryTypeBits`; first-fit offsets.
- `BuildAliasGroup`: `CreateMemory` heaps + `CreateAliasedTexture` at offsets;
  marks `Resource::aliased`.
- At execute, first pass that touches each aliased resource emits a full
  `MemoryBarrier()` once per such pass (activation fence between alias tenants).
- Aliased texture states reset to `Undefined` at the start of each `Execute`.
- `AnalyzeMemoryAliasing()` public for tests/debug reporting (standalone vs
  aliased bytes).

### 1.6 Barriers / transitions

Per dependency before pass body:

| Resource | Target state (simplified) | Sync scope from pass type |
| -------- | ------------------------- | ------------------------- |
| Graphics write color | `ColorAttachment` | `Graphics` |
| Graphics write depth | `DepthStencilAttachment` | `Graphics` |
| Graphics read | `ShaderReadOnly` / depth RO | `Graphics` |
| Compute / raytrace | RO → `ShaderReadOnly`, W/RW → `General` | `Compute` / `Raytrace` |
| Transfer | copy src/dst/general | `Transfer` |
| Buffer | similar RO/General/copy | same scopes |

- Texture tracking is **per subresource** (mip × layer): state, scope, lastWrite.
- Barriers batch when the whole view range shares old state/scope/need; else
  per-subresource.
- Hazard: barrier even if state unchanged when `lastWrite` or this access writes
  (write-after-write / layout-stable hazards).
- Buffers: single state/scope/lastWrite per frame slot.
- After all passes: imported textures transitioned back to `importedState`.

### 1.7 Execution / command recording

`Execute(GPUCommandBuffer* cmd)`:

1. Ensure scene resources; `SortPasses`.
2. If resized → destroy output-following resources; recreate textures if dirty
   (includes alias rebuild).
3. On resize, invoke pass `resizeFn`s.
4. `CreateRenderPasses` — build/cached graphics pipelines, render passes,
   framebuffers from write attachments + resolves.
5. `UpdateSceneBuffer` — camera, frustum, jitter, instance count, TLAS bind.
6. For each sorted pass: alias memory barrier if needed → transition deps →
   optional begin render pass + viewport/scissor → bind pipeline + descriptor
   sets + auto DS for compute/RT without `renderFn` → push constants →
   `renderFn` or auto dispatch/trace → end render pass.
7. Restore imported resource states.

**Single command buffer, single queue of recording.** No multi-queue splits, no
async compute schedule, no secondary CB recording inside the graph. Parallelism
is only whatever the GPU does within one submitted buffer.

### 1.8 Scene / camera / descriptor conveniences

- `UpdateCamera(...)` fills `CameraData` (matrices, jitter period, frustum,
  culling mask).
- Per-frame `GlobalSceneBuffer` UBO + large scene descriptor set layout
  (instance SSBO, shadows, env maps, optional TLAS, …).
- `GetDescriptorSet(shader, variant, set)` and auto-bind for compute/RT from
  pass dependency order matching shader bindings.
- `GetOrCreatePipeline(key, factory)` cache.
- Pipeline / DS / render pass / framebuffer hash caches on the graph object.

### 1.9 Debug / validation

- Logger `"Skore::RenderGraph"` (alias plan debug line).
- `SK_ASSERT` for cyclic deps, push-constant size > 256, bad cached indices.
- `GetTopologyBuildCount()` for tests.
- Resource `debugName` set from graph names on create.
- Unit tests (`Tests/Source/Runtime/RenderGraphTests.cpp`) and GPU-style tests
  (`Tests/Source/GPU/RenderGraphRenderTest.cpp`) against `TestRenderDevice`.
- No graph-level validation layer (no automatic “missing producer” hard fail
  beyond sort heuristics; null resources are skipped in places).

### 1.10 What the graph is **not**

- Not a multi-queue / async timeline API.
- Not a pass culler (unused passes still run if registered).
- Not a general allocator for arbitrary CPU frame data beyond the instance
  blackboard and fixed push-constant scratch.
- Not independent of `Scene` / resource RID system for default pipelines
  (shader paths resolve via `Resources::FindByPath`).

---

## 2. Call sites that construct or execute the graph

### 2.1 Central frame API

```text
RenderPipelineContext::Execute(cmd, scene)
  → renderGraph->Begin(scene)
  → pipeline->BuildRenderGraph(*renderGraph)   // rebuilds pass list every frame
  → renderGraph->Execute(cmd)
```

`RenderPipelineContext` constructs `Alloc<RenderGraph>()` once and owns it
until destroy (`RenderPipeline.cpp`).

### 2.2 Production / tools (same repo, `main`)

| Site | Role |
| ---- | ---- |
| `Player/Source/Skore/Main.cpp` | Creates `RenderPipelineContext` for `PlayerRenderPipeline`; each record-commands event: set swapchain image index → `pipelineContext->Execute(cmd, scene)`. Sets output attachments/size on swapchain create/resize. |
| `Runtime/.../RenderPipeline.cpp` | Default discovery of `DefaultPipelinePass` types via reflection; `BuildRenderGraph` fan-out. |
| `Runtime/.../Pipeline/*Pass.cpp` | Bloom, cascade shadow, culling, forward, light setup, post-process, profiler overlay, etc. — each `BuildRenderGraph(RenderGraph&)`. |
| `Editor/.../SceneViewRenderPipeline.cpp` | Extends default pipeline with selection/outline/tools passes. |
| `Editor/.../SceneViewWindow.cpp` / `PropertiesWindow.cpp` | Resize output, `UpdateCamera`, read color output texture for UI. |
| `Editor/.../PreviewGenerator.cpp` | Standalone `RenderGraph` member: `Begin` → local `BuildRenderGraph` → `Execute` for thumbnails (does **not** go through `RenderPipelineContext`). |

### 2.3 Tests (`main`)

- `Tests/Source/Runtime/RenderGraphTests.cpp` — unit tests with
  `TestRenderDevice` (usage inference, sort, barriers, alias plan, frame ring, …).
- `Tests/Source/GPU/RenderGraphRenderTest.cpp` — larger render-path tests.

### 2.4 v2 today

- `plugins/render_graph/` registers a **stub** API (`init`/`shutdown` only).
- **No** player/editor call sites construct or execute a real graph yet
  (`player/main.c` is still minimal).

---

## 3. Plugin struct-fn-table conventions (v2)

References used: `platform_window`, `entities`, `dxc_compiler` (siblings of
`render_graph` / `render_device`). Also `AGENTS.md` module rules.

### 3.1 Declaration

```c
/* public header: <module>.h */
#define SK_<MODULE>_API_TYPE_ID SK_TYPE_ID("sk.<module>_api", 0x…ULL, 0x…ULL)

typedef struct sk_<module>_api_t {
    i32  (*init)(void);           /* optional; domain-specific */
    void (*shutdown)(void);       /* optional */
    /* … function pointers only; one global table per process … */
} sk_<module>_api_t;

void sk_<module>_init(sk_app_context_t* context, const sk_app_api_t* app_api);
```

Rules from project norms:

- **`sk_*_api_t` only** for the single process-global module table.
- Hosts obtain the table **only** via `app_api->get_api(ctx, SK_*_API_TYPE_ID)`.
- **No** free-function mirrors of table entries and **no** public
  `sk_*_get_api()` for plugins.
- Public headers stay C (`extern "C"`), `sk_` prefixes, **no** `sk_` file-name
  prefix (`render_graph.h`, not `sk_render_graph.h`).
- Opaque objects: `typedef struct sk_foo_t sk_foo_t;` or `SK_HANDLER(sk_foo_t)`
  for device-like handles. Layout of the API struct itself is public.

### 3.2 Versioning

- Type id is a **content hash** of the string name via `SK_TYPE_ID("sk.…", lo, hi)`.
- CMake expands one-arg `SK_TYPE_ID("name")` to the three-arg form; checked-in
  headers usually already have lo/hi literals.
- **No** separate `version` field in the tables today. ABI stability is
  “append carefully / bump type id string if breaking.” Migration should keep
  that pattern unless a version field is deliberately introduced later.

### 3.3 Population

In the `.c` file:

```c
static i32 foo_init_impl(void) { … }
/* static implementations for every table slot */

static const sk_foo_api_t foo_api = {
    foo_init_impl,
    foo_shutdown_impl,
    /* positional initializers matching header order */
};

void sk_foo_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
    app_api->set_api(context, SK_FOO_API_TYPE_ID, (const_ptr_t)&foo_api);
}
```

- Table is **static const** (or static with fixed function pointers).
- Implementations are **`static`** in the TU; only `sk_*_init` and the entry
  point are exported as needed.

### 3.4 Registration / load

`plugin_entry_point.c` (every plugin):

```c
SK_API int sk_plugin_entry_point(sk_app_context_t* context, const sk_app_api_t* app_api) {
    sk_<module>_init(context, app_api);
    return 0;
}

#ifdef SK_TESTS
SK_API i32 sk_plugin_run_tests(sk_test_report_t* out) {
    return sk_test_run_all_status(out);
}
#endif
```

- Host `sk_app_init` scans `{exe}/plugins` and `dlopen`s each shared library,
  resolves `sk_plugin_entry_point`, passes context + `sk_app_api()`.
- Return `0` on success; non-zero fails load.

### 3.5 Opaque handles, context, userdata

| Pattern | Examples |
| ------- | -------- |
| `SK_HANDLER(sk_render_device_t)` etc. | RHI: `u64` wrapper + `_from_ptr` / `_to_ptr` / `_is_valid` |
| `typedef void_ptr_t sk_window_t` | platform_window (raw backend pointer) |
| Fully opaque `typedef struct sk_world_t sk_world_t` | entities: create returns pointer; destroy frees |
| Callbacks with `void_ptr_t user_data` | file dialogs (`sk_path_callback_t`) |

Render graph on main used C++ objects + `std::function`. C port should prefer:

- opaque `sk_render_graph_t*` (or handler) owned by the module,
- function pointers + `void* user_data` instead of `std::function`,
- string views as `const char*` + length or null-terminated names consistent
  with neighboring APIs.

### 3.6 Error / result conventions

- Recoverable: `i32` **0 = success**, non-zero = failure; or domain enums
  (`sk_device_result_t`).
- Invalid handle: zero / NULL.
- **No** defensive null checks on “must be valid” params (AGENTS.md).
- Plugin entry returns `0`; module `init` may return failure for runtime load
  issues (DXC runtime missing, GLFW init fail).

### 3.7 Naming and file layout

```text
plugins/<snake_name>/
  CMakeLists.txt          # sk_add_plugin(<kebab-name> SOURCES …)
  plugin_entry_point.c    # sk_plugin_entry_point (+ optional run_tests)
  <module>.h              # public API table + types
  <module>.c              # impl + static table + SK_TEST block
```

CMake target names: `sk-<kebab>-plugin` SHARED → output
`sk-<kebab>-plugin.so` / `.dll` / `.dylib` under `bin/plugins`; INTERFACE
`sk-<kebab>-lib` for headers.

Register under `plugins/CMakeLists.txt` with `add_subdirectory(...)`.
`render_graph` is already listed.

### 3.8 Build addition checklist (already done for scaffold)

1. `plugins/render_graph/CMakeLists.txt` → `sk_add_plugin(render-graph …)`.
2. `plugins/CMakeLists.txt` → `add_subdirectory(render_graph)`.
3. Link extra deps on `sk-render-graph` if needed (e.g. `sk-render-device-lib`
   for headers only — **do not** link another plugin’s SHARED binary; couple
   via registered APIs at runtime).

---

## 4. RHI test plugin: discovery, run, helpers

Plugin: `plugins/test_render_device/` — registers a **full**
`sk_render_device_api_t` under the **same** `SK_RENDER_DEVICE_API_TYPE_ID` as
the real/stub RHI (last loaded plugin wins for that type id). Pure CPU mock
ported from main’s `TestRenderDevice`.

### 4.1 How tests are discovered and run

1. Sources compiled with `SK_TESTS` (non-Release + `BUILD_TESTING` via
   `sk_target_enable_tests` inside `sk_add_plugin`).
2. `SK_TEST(name) { … }` in the `.c` registers via constructor
   (`sk_test_register`) into a **plugin-local** Unity registry (`sk-test`).
3. `sk_plugin_run_tests` → `sk_test_run_all_status`.
4. Host `tests/main.c`:
   - runs host core/app tests in-process;
   - scans plugins dir, `lib_open`, calls `sk_plugin_entry_point`, then
     `sk_plugin_run_tests` if present;
   - missing test export → skip (Release).

Assertions: Unity macros via `test.h` → `TEST_ASSERT_*`,
`TEST_ASSERT_EQUAL_INT`, `TEST_ASSERT_TRUE`, etc.

### 4.2 What the test device offers (behavior + implicit helpers)

Not a separate assert library — **observable mock state** used by tests:

| Capability | Behavior |
| ---------- | -------- |
| Buffer storage | CPU backing store; `map` / `update_buffer` round-trip |
| Texture subresource states | Per-(mip, layer); start `Undefined` |
| Barrier history | Records transitions; **`mismatch_count`** if declared `old_state` ≠ tracked (except `Undefined` = discard) |
| Command stats | draw / dispatch / trace_rays / render_pass / copy / clear / barrier / bind counts; reset on begin |
| Memory aliasing | `create_memory` + `create_aliased_texture` record alias ownership |
| Lifecycle | Device destroy frees all owned objects |
| Swapchain | Fake images, acquire advances index |
| Queues | `submit_count` |

Embedded `SK_TEST`s cover API table completeness, init, handles, buffer
round-trip, barriers (including mismatch), command stats, lifecycle, destroy
ownership.

**For graph migration tests:** load `test_render_device` (or call its API table
directly in-process), create a graph, `execute`, then assert barrier history /
cmd stats / resource states — same pattern as main’s `RenderGraphTests` +
`TestRenderDevice`.

---

## 5. Current allocation behavior of the C++ graph

Goal: know what **heap-allocates per frame** so the C port can avoid it on hot
paths (AGENTS.md: no implicit heap on per-frame paths).

### 5.1 One-time / rare (acceptable if documented)

| Site | What |
| ---- | ---- |
| `RenderPipelineContext` ctor | `Alloc<RenderGraph>()`, pipeline object |
| First `CreateSceneResources` | scene buffer + `SK_FRAMES_IN_FLIGHT` descriptor sets |
| First touch of a resource name | HashMap insert; GPU texture/buffer create |
| Pipeline / DS / RP / FB caches | grow until stable |
| Alias heaps | rebuild when topology/resources dirty |
| `CreateInstance` | `operator new(size)` once per name/size |
| Pass pool cold start | `Alloc<RenderGraphPass>()` until pool covers max passes/frame |

### 5.2 Per-frame (hot) — current C++ behavior

| Site | Heap? | Notes |
| ---- | ----- | ----- |
| `Begin` pass list clear + pool return | Usually no new pass alloc | Reuses `passPool` |
| `BuildRenderGraph` every frame | **Yes, often** | Re-adds all passes; each `AddDependency` may grow `Array` and **allocate `String` nameStorage**; `std::function` for `Render`/`Constants`/`Resize` typically **heap-allocates the callable** (captures) every frame |
| `SortPasses` cache hit | Low | Reorder from cached indices; scratch arrays may already have capacity |
| `SortPasses` cache miss | **Yes** | Temporary `Array`/`HashMap` for edges, writers, readers, etc. |
| `Execute` when not dirty | Moderate | Scratch `passActivatesAliasScratch` resize (capacity reuse); no texture recreate |
| `Execute` when dirty / resize | **Yes** + GPU | Destroy/recreate, alias plan arrays, views |
| `CollectUnusedResources` | Occasional | `toRemove` strings; GPU destroy after idle wait |
| Push constants | No | 256-byte stack buffer |
| Resource name lookup | HashMap | No per-lookup alloc if key is view into existing storage |

**Bottom line for migration:** main rebuilds the **entire pass graph every
frame** with fluent builders and `std::function`s — that is the dominant
per-frame CPU heap traffic. GPU resources and caches are mostly stable.
A C port should:

1. Prefer **retained** pass/dependency storage (reset counts, reuse arrays).
2. Replace `std::function` with **fn + userdata** (no per-frame closure alloc).
3. Intern or reuse resource name strings; avoid per-edge heap strings.
4. Keep topology cache; pre-size edge/indegree scratch.
5. Keep pass object pool (already present on main).

---

## 6. Proposed C API sketch (non-binding)

Target: expand `plugins/render_graph/render_graph.h` in a later task. This
sketch maps main’s features to v2 conventions without RID-inside-RHI.

```c
#pragma once
#include "app.h"
#include "common.h"
#include "render_device.h" /* handles + resource state enums */

#ifdef __cplusplus
extern "C" {
#endif

#define SK_RENDER_GRAPH_API_TYPE_ID \
  SK_TYPE_ID("sk.render_graph_api", 0xc77df474a355fb80ULL, 0x11b457b7c17d0220ULL)

/* ---- enums (mirror main) ---- */

typedef enum sk_rg_pass_type_t {
  SK_RG_PASS_COMPUTE = 0,
  SK_RG_PASS_GRAPHICS = 1,
  SK_RG_PASS_RAYTRACE = 2,
  SK_RG_PASS_TRANSFER = 3,
} sk_rg_pass_type_t;

typedef enum sk_rg_access_t {
  SK_RG_ACCESS_READ = 0,
  SK_RG_ACCESS_WRITE = 1,
  SK_RG_ACCESS_READ_WRITE = 2,
} sk_rg_access_t;

/* ---- opaque objects ---- */

typedef struct sk_render_graph_t sk_render_graph_t;
typedef struct sk_rg_pass_t sk_rg_pass_t;

/* ---- descs (POD; strings are non-owning views for the declare phase) ---- */

typedef struct sk_rg_texture_desc_t {
  sk_pixel_format_t format;
  sk_extent3d_t extent; /* 0,0 → follow output * scale */
  f32 scale_x, scale_y;
  u32 array_layers, samples, mip_levels;
  u32 usage; /* sk_resource_usage bits; 0 = infer */
  f32 clear_color[4];
  f32 clear_depth;
  i32 cubemap, ping_pong, persistent;
} sk_rg_texture_desc_t;

typedef struct sk_rg_buffer_desc_t {
  u64 size;
  u32 usage;
  i32 host_visible, persistent_mapped, per_frame, ping_pong;
} sk_rg_buffer_desc_t;

typedef struct sk_rg_view_desc_t {
  const_chr_t texture_name;
  sk_texture_view_type_t view_type;
  u32 base_mip_level, mip_level_count;
  u32 base_array_layer, array_layer_count;
} sk_rg_view_desc_t;

/* Record callback: no std::function — caller supplies userdata. */
typedef void (*sk_rg_record_fn)(sk_rg_pass_t* pass, void_ptr_t scene,
                                sk_command_buffer_t cmd, void_ptr_t user);
typedef void (*sk_rg_resize_fn)(sk_render_graph_t* graph, sk_extent_t extent,
                                void_ptr_t user);
typedef void (*sk_rg_constants_fn)(sk_render_graph_t* graph, void_ptr_t dst,
                                   void_ptr_t user);

typedef struct sk_render_graph_api_t {
  /* module lifecycle (plugin-global) */
  i32 (*init)(void);
  void (*shutdown)(void);

  /* graph object */
  sk_render_graph_t* (*create)(sk_render_device_t device);
  void (*destroy)(sk_render_graph_t* graph);

  /* declare resources (names valid until next begin or retained intern pool) */
  void (*create_texture)(sk_render_graph_t* g, const_chr_t name,
                         const sk_rg_texture_desc_t* desc);
  void (*create_buffer)(sk_render_graph_t* g, const_chr_t name,
                        const sk_rg_buffer_desc_t* desc);
  void (*create_view)(sk_render_graph_t* g, const_chr_t name,
                      const sk_rg_view_desc_t* desc);
  void (*import_textures)(sk_render_graph_t* g, const_chr_t name,
                          const sk_texture_t* textures, u32 count,
                          sk_resource_state_t state);
  void* (*create_instance)(sk_render_graph_t* g, const_chr_t name, usize size);
  void* (*get_instance)(sk_render_graph_t* g, const_chr_t name);

  /* passes */
  sk_rg_pass_t* (*add_pass)(sk_render_graph_t* g, const_chr_t name,
                            sk_rg_pass_type_t type);
  void (*pass_read)(sk_rg_pass_t* p, const_chr_t name);
  void (*pass_write)(sk_rg_pass_t* p, const_chr_t name);
  void (*pass_read_write)(sk_rg_pass_t* p, const_chr_t name);
  void (*pass_resolve)(sk_rg_pass_t* p, const_chr_t name);
  void (*pass_stage)(sk_rg_pass_t* p, i32 stage);
  void (*pass_set_pipeline)(sk_rg_pass_t* p, sk_pipeline_t pipeline);
  void (*pass_set_descriptor_set)(sk_rg_pass_t* p, u32 set, sk_descriptor_set_t ds);
  void (*pass_set_record)(sk_rg_pass_t* p, sk_rg_record_fn fn, void_ptr_t user);
  void (*pass_set_resize)(sk_rg_pass_t* p, sk_rg_resize_fn fn, void_ptr_t user);
  void (*pass_set_constants)(sk_rg_pass_t* p, u32 size, u32 stage_mask,
                             sk_rg_constants_fn fn, void_ptr_t user);
  void (*pass_dispatch)(sk_rg_pass_t* p, u32 x, u32 y, u32 z);
  void (*pass_dispatch_indirect)(sk_rg_pass_t* p, sk_buffer_t indirect);
  void (*pass_trace_rays)(sk_rg_pass_t* p, u32 w, u32 h, u32 d);

  /* accessors */
  sk_texture_t (*get_texture)(const sk_render_graph_t* g, const_chr_t name);
  sk_texture_t (*get_prev_texture)(const sk_render_graph_t* g, const_chr_t name);
  sk_texture_view_t (*get_texture_view)(const sk_render_graph_t* g, const_chr_t name);
  sk_buffer_t (*get_buffer)(const sk_render_graph_t* g, const_chr_t name);
  sk_buffer_t (*get_prev_buffer)(const sk_render_graph_t* g, const_chr_t name);

  /* outputs / frame */
  void (*set_color_output)(sk_render_graph_t* g, const_chr_t name);
  void (*set_depth_output)(sk_render_graph_t* g, const_chr_t name);
  void (*set_output_size)(sk_render_graph_t* g, sk_extent_t size);
  sk_extent_t (*get_output_size)(const sk_render_graph_t* g);
  void (*set_current_output_index)(sk_render_graph_t* g, u32 index);

  void (*begin)(sk_render_graph_t* g, void_ptr_t scene /* optional opaque */);
  void (*execute)(sk_render_graph_t* g, sk_command_buffer_t cmd);

  /* debug / tests */
  u32 (*topology_build_count)(const sk_render_graph_t* g);
  /* optional: analyze_aliasing report struct later */
} sk_render_graph_api_t;

void sk_render_graph_init(sk_app_context_t* context, const sk_app_api_t* app_api);

#ifdef __cplusplus
}
#endif
```

### 6.1 Design notes for implementers

1. **Retain-and-reset:** `begin` clears pass list into a pool; dependency arrays
   keep capacity; names interned in a frame arena or stable string table.
2. **RHI coupling:** graph calls `sk_render_device_api_t` obtained from the app
   registry (same pattern as other plugins). Do not link `vulkan_render_device`.
3. **Pipelines:** callers create pipelines via RHI and pass handles into
   `pass_set_pipeline` — or a later helper builds from full
   `sk_*_pipeline_desc_t` without RID.
4. **Scene buffer / auto DS:** port as optional convenience later; first
   milestone can require explicit descriptor binds (matches main’s graphics
   passes that use `Render` callbacks).
5. **Stages:** keep numeric stages compatible with main’s `RenderStage` values
   so pipeline ports stay readable.
6. **Tests:** unit tests under `#ifdef SK_TESTS` in `render_graph.c` using
   `test_render_device`’s API table for barriers/aliasing; mirror main’s
   `RenderGraphTests` cases incrementally.

### 6.2 Suggested implementation phases (out of scope here)

1. Resources + passes + sort + execute with manual barriers/record callbacks.
2. Graphics render-pass/framebuffer cache.
3. Texture memory aliasing + activation barriers.
4. Import/swapchain path + output size resize.
5. Scene/camera buffer helpers (if still desired under non-RID rules).
6. Integration tests with Vulkan plugin + triangle/graph smoke.

---

## 7. File index (main reference)

| Path on `main` | Role |
| -------------- | ---- |
| `Runtime/Source/Skore/Graphics/RenderGraph.hpp` | Public C++ API |
| `Runtime/Source/Skore/Graphics/RenderGraph.cpp` | Implementation |
| `Runtime/Source/Skore/Graphics/RenderPipeline.{hpp,cpp}` | Frame loop + pass discovery |
| `Runtime/Source/Skore/Graphics/Pipeline/*` | Built-in graph builders |
| `Player/Source/Skore/Main.cpp` | Game player execute path |
| `Editor/Source/Skore/Scene/SceneViewRenderPipeline.*` | Editor view graph |
| `Editor/Source/Skore/Utils/PreviewGenerator.cpp` | Standalone graph |
| `Tests/Source/Runtime/RenderGraphTests.cpp` | Unit tests |
| `Tests/Source/GPU/RenderGraphRenderTest.cpp` | GPU-oriented tests |

| Path on `v2` | Role |
| ------------ | ---- |
| `plugins/render_graph/*` | Scaffold destination plugin |
| `plugins/render_device/render_device.h` | RHI surface graph will call |
| `plugins/test_render_device/*` | Mock RHI for graph unit tests |
| `docs/render-device-gap-audit.md` | Related RHI gap list |
| `docs/render-graph-migration-audit.md` | This document |

---

## 8. Summary

The main C++ render graph is a **frame-rebuilt, single-queue, name-based**
frame graph with texture/buffer/view/import/instance resources, topological
pass sort (stage-aware), subresource barriers, transient texture memory
aliasing, and rich pipeline/scene helpers. It is consumed only inside this
repo’s `main` branch (Player, Editor, Runtime). v2 already has a plugin
scaffold and an RHI rich enough for barriers and aliasing; the migration should
follow **struct-fn-table** plugin conventions, replace `std::function`/per-frame
string growth with retained pools, and keep pipelines/descriptors
**RID-free** at the RHI boundary. No code behavior was changed in this audit.
