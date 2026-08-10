# Render graph plugin API (`sk-render-graph`)

Concise host guide for the C plugin that replaced the C++ `RenderGraph` on v2.
Source of truth for types and contracts: `plugins/render_graph/render_graph.h`.
Host frame helper: `plugins/render_graph/render_pipeline.h`.

Related:

- Migration audit / feature inventory: `docs/render-graph-migration-audit.md`
- Feature ↔ test map: `docs/render-graph-test-coverage.md`
- Final verification + parity (APX-157): `docs/render-graph-verification.md`

---

## 1. Acquire the fn table

The plugin registers a **process-global** `sk_render_graph_api_t` under
`SK_RENDER_GRAPH_API_TYPE_ID`. Hosts never link the plugin binary; they load
plugins (auto via `sk_app_init` scanning `{app}/plugins`) and look up the table:

```c
const sk_app_api_t* app_api = sk_app_api();
const sk_render_graph_api_t* rg =
    (const sk_render_graph_api_t*)app_api->get_api(ctx, SK_RENDER_GRAPH_API_TYPE_ID);
/* or: sk_render_graph_api_from_app(ctx, app_api); */

if (rg == NULL || rg->create == NULL) {
    /* plugin missing or incomplete */
}
if (rg->init() != 0) {
    /* module init failed */
}
```

Optional: call `rg->shutdown()` on process teardown. Every table entry is
non-null after a successful plugin load (asserted by smoke tests).

---

## 2. Fn table surface (groups)

| Group | Entry points |
| ----- | ------------ |
| Module | `init`, `shutdown` |
| Graph object | `create`, `create_with_config`, `destroy` |
| Resources | `create_texture`, `create_buffer`, `create_view`, `import_textures`, `create_instance`, `get_instance` |
| Pass declare | `add_pass`, `pass_read` / `pass_write` / `pass_read_write` / `pass_resolve`, `pass_stage`, `pass_set_side_effects` |
| Pass bind | `pass_set_pipeline`, `pass_set_descriptor_set`, `pass_set_record`, `pass_set_resize`, `pass_set_constants`, `pass_dispatch`, `pass_dispatch_indirect`, `pass_trace_rays` |
| Accessors | `get_texture`, `get_prev_texture`, `get_texture_view`, `get_buffer`, `get_prev_buffer` |
| Outputs | `set_color_output`, `set_depth_output`, `set_output_size`, `get_output_size`, `set_current_output_index` |
| Frame | `begin`, `end`, `compile`, `execute` |
| Memory / debug | `set_heap_allocator`, `get_heap_allocator`, `get_memory_stats`, `get_last_error`, `topology_build_count` |
| Introspection | `get_pass_count` / `get_pass_info`, `get_resource_*`, `get_pass_dep_info`, `get_edge_*` |
| Compile results | `is_compiled`, `get_compiled_pass_*`, `get_culled_pass_*`, `get_resource_lifetime`, `get_alias_*` |
| Execute results | `get_barrier_count`, `get_barrier_info` |

Opaque types: `sk_render_graph_t*` (owned by module until `destroy`),
`sk_rg_pass_t*` (valid only for the current frame after `add_pass`).

---

## 3. Lifecycle

```text
create / create_with_config   (once; retains pools + arena capacity)
    │
    ▼
┌─ begin(scene)               reset arena offset + free-lists; mark in_frame
│     declare resources       create_* / import_*
│     add_pass + deps         pass_read/write/…, stage, side-effects, record
│     set outputs / size      set_color_output, set_output_size, …
│  [optional] compile()       topo order, cull, lifetimes, alias plan
│     execute(cmd)            compile if needed → barriers → record → restore
│  [or] end()                 leave frame without recording (build-only)
└─ (repeat per frame)
    │
    ▼
destroy
```

**Rules**

1. Declare only between `begin` and `end`/`execute`. Outside that window →
   `SK_RG_ERR_NOT_IN_FRAME`.
2. Rebuild the pass/resource list **every frame** (same as C++ main). Pools and
   the frame arena **retain capacity**; `begin` resets usage, not reservation.
3. `execute` ends the frame (same as `end`). Results (barriers, compiled order)
   stay valid until the next `begin`.
4. `compile` may be called mid-frame or after `end`; results are valid until the
   next `begin`. `execute` compiles automatically when the graph is not compiled.
5. Prefer the host helper for the usual loop:

```c
sk_render_pipeline_context_t pipeline;
sk_render_pipeline_context_create(&pipeline, rg, device, my_build_fn, user);
/* each frame: */
sk_render_pipeline_context_execute(&pipeline, cmd, scene);
/* teardown: */
sk_render_pipeline_context_destroy(&pipeline);
```

That is `begin → build_fn → execute` (ports `RenderPipelineContext::Execute`).

---

## 4. Capacity configuration and tuning

```c
typedef struct sk_rg_memory_config_t {
    u64 frame_arena_bytes;   /* linear scratch per frame (reset at begin) */
    u32 pass_capacity;       /* pass free-list slots */
    u32 resource_capacity;   /* resource free-list slots */
    u32 edge_capacity;       /* producer→consumer edges */
    u32 barrier_capacity;    /* barriers recorded at execute */
} sk_rg_memory_config_t;
```

**Defaults** (zero fields or `create()` without config):

| Field | Default |
| ----- | ------- |
| `frame_arena_bytes` | 64 KiB |
| `pass_capacity` | 64 |
| `resource_capacity` | 128 |
| `edge_capacity` | 256 |
| `barrier_capacity` | 256 |

**Tuning**

1. Start with defaults or size to your worst-case pass/resource/edge counts
   plus ~25–50% headroom.
2. Run a representative warm-up frame (or a few), then read `get_memory_stats`:
   - `*_high_water` → raise the matching capacity if close to `*_capacity`.
   - `frame_arena_high_water` → size `frame_arena_bytes` ≥ high-water with margin.
3. Growth is allowed **only outside a frame** (`begin` … `end`/`execute`).
   Mid-frame overflow returns `SK_RG_ERR_OUT_OF_SPACE` (no silent `malloc`).
4. After warm-up, capacities are retained across frames — steady state should
   show flat `heap_alloc_count` and `growth_events`.

---

## 5. Allocation contract

| When | Heap allowed? | Mechanism |
| ---- | ------------- | --------- |
| `create` / `create_with_config` | Yes | Arena, pools, compile scratch tables |
| Between frames (not in_frame) | Yes | Capacity growth only when requested and out-of-frame |
| Inside `begin` … `end`/`execute` | **No** | Frame arena + free-lists only |
| Steady-state `compile` / `execute` after warm-up | **No** | Pre-sized working sets |

**How to keep it**

- Pre-size with `create_with_config` for production pipelines.
- Use **fn + userdata** for record/constants/resize (no per-frame closures).
- Keep resource **names** as stable string literals or storage that outlives the
  declare phase (names are non-owning views).
- Do not grow host-side vectors/maps in the build callback every frame without
  retained capacity on the host side either.
- Use `set_heap_allocator` only in tests (counting allocator); restore default
  before production graphs live on another heap.
- Check `get_memory_stats` after warm-up: `heap_alloc_count` and `growth_events`
  must not increase across steady frames.

Violations mid-frame surface as `SK_RG_ERR_OUT_OF_SPACE` / `SK_RG_ERR_GROW_BLOCKED`
via `get_last_error` — never as a silent allocation.

---

## 6. Error codes (`sk_rg_result_t`)

| Code | Meaning |
| ---- | ------- |
| `SK_RG_OK` (0) | Success |
| `SK_RG_ERR_OUT_OF_SPACE` | Capacity exceeded while in-frame (or fixed path) |
| `SK_RG_ERR_GROW_BLOCKED` | Growth attempted while a frame is active |
| `SK_RG_ERR_OOM` | Heap alloc failed during allowed out-of-frame growth |
| `SK_RG_ERR_NOT_IN_FRAME` | Build op outside `begin`…`end`/`execute` |
| `SK_RG_ERR_DUPLICATE_NAME` | Resource or pass name already declared this frame |
| `SK_RG_ERR_WRITE_READ_ONLY` | Write / RW against imported read-only resource |
| `SK_RG_ERR_INVALID_PASS` | Dependency on stale/invalid pass handle |
| `SK_RG_ERR_UNKNOWN_RESOURCE` | Dependency names a resource not declared |
| `SK_RG_ERR_INVALID_ARGUMENT` | NULL desc, empty name, or bad argument |
| `SK_RG_ERR_CYCLE` | Pass dependency graph has a cycle (`compile`) |
| `SK_RG_ERR_INVALID_STATE` | `compile` with no/invalid graph state |

Many declare helpers are `void` and stash the last error on the graph
(`get_last_error`). `compile` returns the code directly. Memory-only aliases
`SK_RG_MEMORY_*` map to the same numeric values for older call sites.

---

## 7. Worked example

Minimal lighting → composite chain through the host pipeline context (same
pattern as `tests/integration/render_graph.c` and the player acquire path):

```c
#include "app.h"
#include "render_device.h"
#include "render_graph.h"
#include "render_pipeline.h"

typedef struct {
    u32 w, h;
} my_frame_user_t;

static void my_record(sk_rg_pass_t* pass, void_ptr_t scene,
                      sk_command_buffer_t cmd, void_ptr_t user) {
    (void)pass;
    (void)scene;
    (void)cmd;
    (void)user;
    /* Bind pipelines / draw / dispatch via sk_render_device_api_t as needed. */
}

static void my_build(const sk_render_graph_api_t* api, sk_render_graph_t* g,
                     void_ptr_t user) {
    my_frame_user_t* u = (my_frame_user_t*)user;
    sk_rg_texture_desc_t tex = {0};
    sk_rg_extent_t extent = {u->w, u->h};
    sk_rg_pass_t* lighting;
    sk_rg_pass_t* composite;

    tex.format = SK_PIXEL_FORMAT_RGBA8_UNORM;
    tex.extent.width = u->w;
    tex.extent.height = u->h;
    tex.extent.depth = 1u;
    tex.scale_x = tex.scale_y = 1.0f;
    tex.mip_levels = tex.array_layers = tex.samples = 1u;

    api->set_output_size(g, extent);
    api->create_texture(g, "Color", &tex);
    api->set_color_output(g, "Color");

    lighting = api->add_pass(g, "Lighting", SK_RG_PASS_COMPUTE);
    composite = api->add_pass(g, "Composite", SK_RG_PASS_COMPUTE);
    api->pass_write(lighting, "Color");
    api->pass_read(composite, "Color");
    api->pass_write(composite, "Color");
    api->pass_set_side_effects(composite, 1); /* never cull present/output */
    api->pass_set_record(lighting, my_record, u);
    api->pass_set_record(composite, my_record, u);
}

void run_frames(sk_app_context_t* ctx, sk_render_device_t device,
                sk_command_buffer_t cmd) {
    const sk_render_graph_api_t* rg =
        sk_render_graph_api_from_app(ctx, sk_app_api());
    sk_render_pipeline_context_t pipeline;
    my_frame_user_t user = {.w = 1280u, .h = 720u};
    sk_rg_memory_stats_t stats;
    u32 heap_warm;
    u32 i;

    rg->init();
    sk_render_pipeline_context_create(&pipeline, rg, device, my_build, &user);

    /* Warm-up (may realize physical resources once). */
    sk_render_pipeline_context_execute(&pipeline, cmd, NULL);
    rg->get_memory_stats(pipeline.graph, &stats);
    heap_warm = stats.heap_alloc_count;

    for (i = 0u; i < 100u; ++i) {
        sk_render_pipeline_context_execute(&pipeline, cmd, NULL);
        rg->get_memory_stats(pipeline.graph, &stats);
        /* Steady state: no graph-heap growth. */
        assert(stats.heap_alloc_count == heap_warm);
    }

    sk_render_pipeline_context_destroy(&pipeline);
    rg->shutdown();
}
```

**Standalone** path (PreviewGenerator-style, no pipeline context):

```c
graph = rg->create(device);
rg->begin(graph, NULL);
/* declare… */
rg->execute(graph, cmd);
rg->destroy(graph);
```

---

## 8. What is intentionally out of scope

| Legacy (C++ main) | C plugin status |
| ----------------- | --------------- |
| AccelerationStructure resource kind | Not on `sk_rg_resource_kind_t` yet |
| Scene/camera UBO + auto descriptor sets (§1.8) | Deferred conveniences; bind DS explicitly |
| Multi-queue / async compute | Same as legacy: **single command buffer / queue** |
| RID-based shader/pipeline lookup | RHI is RID-free; pass full handles/descs |
| Built-in Bloom/shadow/forward passes | Register as `sk_rg_build_fn` when scene ports land |

Dead-pass culling **is** implemented on v2 (improvement over main, which ran
every registered pass). Mark external effects with
`pass_set_side_effects(pass, 1)`.
