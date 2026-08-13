# UI system design (v1)

Task: **APX-126**. Branch target: `feature/design-and-implement-basic-ui-system`.
Scope of this document: audit of what v2 already provides, how main-branch Dear ImGui is driven (migration reference), and a design for a **retained, flexbox-based UI plugin** that reuses engine abstractions without papering over their gaps.

**v1 is a retained, flexbox-based UI plugin; text renders through the MSDF
pipeline (vendored msdf-atlas-c + median/smoothstep decode) since APX-271.**
Goals are: a usable element tree + layout + draw list for tools/player HUD, a
known migration path off main's ImGui, and a testable surface for a future UI
tester.

---

## 1. Inventory — what exists and can be reused

Paths are relative to the v2 tree root unless noted as **main**.

### 1.1 Plugin registration and lifecycle

| Piece | Location | Reuse for UI |
| --- | --- | --- |
| App context + API registry | `core/app.h`, `app/app.c` | UI registers `sk_ui_api_t` via `set_api(ctx, SK_UI_API_TYPE_ID, &table)`. |
| Multi-impl registry | `sk_app_api_t::{add_impl,remove_impl,impl_count,get_all_impls}` | Future style themes, font providers, or widget packs as `add_impl` tables — same pattern as planned asset handlers. |
| Plugin entry | `sk_plugin_entry_point(context, app_api)` in each `plugins/*/plugin_entry_point.c` | New `plugins/ui/` loads as SHARED, statically links `sk-core`, registers once, returns 0. |
| Load path | `app_api->load_plugin(ctx, path)` | Host (`sk-app` / player / editor) loads UI after window + render device + DXC. |
| Process lifecycle | `sk_app_init` / `sk_app_tick` / `sk_app_run` / `request_shutdown` | Tick owns frame timing (`delta_time`, `fps`, `elapsed_time`). **No frame-phase events on v2 yet** (main had `OnBeginFrame` / `OnRecordRenderCommands`). |
| Coupling rules | `AGENTS.md` | UI couples only through registered APIs + shared headers. No link to other plugin `.c` files. |

**Lifecycle contract for the UI plugin (v1):**

1. Host loads `sk-platform-window`, `sk-render-device` (or `sk-vulkan-render-device`), `sk-dxc-compiler`, then `sk-ui`.
2. UI `entry_point` only registers the API table (no window create, no GPU work at load).
3. Host calls `ui->init(ctx, window, device)` after those APIs are available.
4. Per frame (host-driven until a real phase bus exists): `poll_events` → `ui->begin_frame` → app builds/updates tree → `ui->layout` → `ui->paint` → host encodes draw list via render device → `ui->end_frame`.
5. `ui->shutdown` before device/window destroy.

### 1.2 Render device abstractions

Header: `plugins/render_device/render_device.h` (~1500 lines). Backends: stub `render_device.c`, real work in `plugins/vulkan_render_device/`, recording tests in `plugins/test_render_device/`. Prior surface audit: `docs/render-device-gap-audit.md` (many items closed; still stub-vs-Vulkan completeness).

**Reusable for UI rendering:**

| Capability | API surface | UI use |
| --- | --- | --- |
| Buffers | `create_buffer` / `destroy_buffer` / `buffer_map` / `get_buffer_mapped_data` | Host-visible VB/IB for draw-list batches (`persistent_mapped` preferred). |
| Textures / views / samplers | `create_texture`, `create_texture_view`, `create_sampler`, descs | Font atlas, icon sheets, image widgets. Formats include `RGBA8_UNORM` / `R8_UNORM`. |
| Upload path | `update_buffer`, `copy_buffer_to_texture`, `resource_barrier_*` | Atlas bake and dynamic atlas growth. |
| Shaders | `create_shader(dev, src, src_size, stage)` | **SPIR-V words** (Vulkan backend copies bytes into `u32` words). UI compiles HLSL offline or at init via DXC, then creates shaders. |
| Graphics pipelines | `create_graphics_pipeline` + full `sk_pipeline_desc_t` / blend / raster | Two pipelines: solid+image (RGBA sample) and font (R8 sample → tint). Premultiplied or standard alpha blend. |
| Descriptor sets | `create_descriptor_set` / `update_descriptor_set` / `bind_descriptor_set` | Per-texture bind (batch break on texture change). |
| Render pass / framebuffer | `create_render_pass`, `create_framebuffer`, `begin_render_pass` / `end_render_pass` | UI pass onto swapchain or offscreen UI target. |
| Command encoding | `begin/end_command_buffer`, `bind_pipeline`, `bind_vertex/index_buffer`, `push_constants`, `draw` / `draw_indexed` | Emit batched UI geometry. |
| Viewport / scissor | `set_viewport`, `set_scissor` (`sk_rect2d_t`) | Clip stacks from overflow/scroll regions. **Scissor is implemented in Vulkan** (`vulkan_command_buffer.c`). |
| Swapchain | `create_swapchain` (takes `void_ptr_t window`), acquire/present, extent/format/image queries | Editor-style full-window UI or overlay. |
| Debug labels | `begin/end/insert_debug_label` | `"UI::Paint"` markers. |

**Not needed in UI plugin (owned elsewhere):** adapter selection, ray tracing, AS, multi-queue ownership transfer, dynamic rendering helpers.

### 1.3 Window / platform layer

Header: `plugins/platform_window/platform_window.h`. Backend: GLFW (`platform_window.c`) + nativefiledialog.

| Present today | Notes |
| --- | --- |
| `init` / `shutdown` / `poll_events` | Host must call; no auto-init. |
| `create_window` / `destroy_window` / `window_should_close` | Client size in **screen coordinates** (logical points). |
| `get_window_size` → `sk_extent_t` | **Logical** client size (points) = `get_framebuffer_size / get_window_content_scale`. Not the backend's screen coordinates: GLFW screen coords are points on macOS but **pixels** on Windows, where DPI only shows up in the content scale. |
| `get_cursor_pos` | Cursor in the same **logical** point space as `get_window_size` (pixels ÷ content scale). |
| `get_mouse_button` | Polled button state (`sk_mouse_button_t`). |
| `set_window_key_callback` / `set_window_char_callback` / `set_window_scroll_callback` | Keyboard press/release (`sk_key_t`, `sk_key_mod_flags_t`), composed UTF-8 text entry, and wheel ticks; delivered from `poll_events`. Hosts translate `sk_key_t` → `sk_ui_key_t` (no plugin-to-plugin dependency). |
| `get_framebuffer_size` → `sk_extent_t` | **Physical** framebuffer pixels (swapchain / viewport). |
| `get_window_content_scale` → `sk_content_scale_t` | Separate x/y; **1.0 = 96 DPI baseline**. |
| `get_window_dpi` | Average of content scale x/y (legacy single factor). |
| `set_window_content_scale_callback` | Notified on OS scale change / monitor move (via `poll_events`). |
| Monitors | `get_monitor_count` / `get_primary_monitor` / `get_monitor` / `get_monitor_content_scale`. |
| Conversion helpers | `sk_extent_logical_to_physical` / `sk_physical_to_logical_*` (header, pure math). |
| `is_window_minimized`, maximize, cursor lock, icon, native handle | Enough for basic host loop. |
| File dialogs / message box | Editor tooling, not core UI layout. |

**Missing for UI (see §7):** window resize/focus/close **events** (scale is covered), and **any input** (keys, mouse, text, wheel). GLFW can provide these; the public `sk_platform_window_api_t` does not expose them yet.

Host platform shared libs/clocks live in `core/platform.h` (`sk_platform_api_t`: `lib_open` / `lib_symbol` / `monotonic_seconds`) — used by DXC and plugin load, not by UI layout.

### 1.4 Asset / resource system

| Piece | Status on this branch | UI relevance |
| --- | --- | --- |
| Repository | `core/repository.h` — full RID/UUID store, types, read/write/commit, hierarchy, undo scopes | Stable identity for **Font** / **Texture** / **StyleSheet** resources once types register. |
| Serialization | `core/serialization.h` — binary + JSON archives | Persist styles, layouts, cooked font metadata. |
| Filesystem | `core/filesystem.h` (impl in `app/`) | Raw read of `.ttf`/`.png`/shader source during v1 bootstrap and tests. |
| Asset handlers / importers | **Not on HEAD.** Commit `afd212e` added `core/resource_assets.h` (handler/importer table shapes, ingest/cook contexts, `add_impl` registration) on other lines of work; **this tree has no `resource_assets.h`**. | Font/texture cook pipeline is designed but not landed. v1 must not invent a parallel asset system inside UI. |
| main font path | main `FontImporter` (msdfgen) + `FontHandler` (`.font`) | **Not used by v1** — runtime MSDF bake via msdf-atlas-c replaces it. Reference only. |

**v1 font/texture loading strategy:** load bytes via filesystem (or test fixtures), bake an MSDF atlas with vendored msdf-atlas-c (see `docs/ui-plugin.md` §5). When `resource_assets` lands, Font/Texture handlers own disk formats; UI only consumes `sk_rid_t` + GPU handles resolved by the host/asset layer.

### 1.5 Core containers, math, strings

| Module | What UI uses |
| --- | --- |
| `common.h` | `u8`…`u64`, `f32`/`f64`, `sk_type_id_t`, `SK_HANDLER`, `SK_API`. |
| `allocator.h` | All UI heap through `sk_allocator_t` (default mimalloc). No `malloc` on hot paths. |
| `array.h` | `SK_ARRAY(T)` for children, dirty queues, draw batches, event queues. |
| `hashmap.h` | `SK_HASH_MAP` for id→node, class registry, style cache, glyph cache. |
| `math3d.h` | `sk_vec2_t` for positions/sizes; scalar helpers. **No dedicated color/rect in core** — use `sk_vec4_t` as RGBA and RHI `sk_rect2d_t` for scissor, or UI-local POD types. |
| `path.h` | `sk_str_view_t` (non-owning UTF-8 slice) for labels, class names, event types. |
| `logger.h` | Diagnostics. |
| `test.h` | Unit/integration tests for layout and hit-test (headless). |
| Repository field types | `STRING`, `VEC2`/`VEC4`, `COLOR`, `REFERENCE`, `SUB_OBJECT_LIST` when UI documents are resources. |

**No growable string type in core.** Labels in v1 are `sk_str_view_t` into owner-stable storage, or heap copies owned by the node (`char*` via allocator). A shared string module is a **core** gap if many systems need it.

### 1.6 DXC shader compiler plugin

Header: `plugins/dxc_compiler/dxc_compiler.h`.

```text
app_api->get_api(ctx, SK_DXC_COMPILER_API_TYPE_ID)
→ sk_dxc_compiler_api_t:
    init()
    shutdown()
    compile(entry_point, profile, source, source_size,
            spirv, spirv_capacity, out_spirv_size, log, log_capacity)
```

- Compiles **HLSL from an in-memory string** to **SPIR-V** (`-spirv`, `vulkan1.2`, DX layout flags).
- Profiles e.g. `vs_6_8` / `ps_6_8`.
- Compile-only: no SPIR-V reflection.
- UI init path: embed or load `DrawList2D`-style HLSL (main had `Assets/Shaders/DrawList2D.raster` / `DrawListFont2D.raster`), `compile` → `create_shader` with SPIR-V bytes.

### 1.7 Related plugins (context)

| Plugin | Role |
| --- | --- |
| `entities` | ECS world/systems. v1 UI is **not** required to store the element tree in ECS; editor may later host UI documents as components. |
| `render_graph` | Skeleton only (`init`/`shutdown`). UI does not depend on it for v1; host records UI pass directly. |
| `test_render_device` | Headless/mock RHI for layout + encode tests without GPU. |

### 1.8 Editor tree on v2

`editor/` is essentially empty (CMake only). There is **no Dear ImGui, no RmlUi, no DrawList** in the v2 tree. The migration reference is **main** (C++ engine), documented next.

---

## 2. How main drives Dear ImGui (migration reference)

Source of truth: **main** `Editor/Source/Skore/ImGui/ImGui.cpp` (+ `ImGui.hpp`), with SDL3 + Vulkan backends. v2 has no equivalent yet; this is the path to replace or rehost.

### 2.1 Init (`ImGuiInit`)

1. Resolve editor window: `Graphics::GetWindow()`.
2. Register SDL event hook: `AddSDLEventCallback(SDLProcessEvent)` → `ImGui_ImplSDL3_ProcessEvent`.
3. `scaleFactor = Platform::GetWindowDPI(window)`.
4. Create UI swapchain + render pass + per-image framebuffers (`ImGuiCreateSwapchain`).
5. `ImGui::SetAllocatorFunctions(ImGuiMemAlloc, ImGuiMemFree)` → engine `MemAlloc`/`MemFree`.
6. `ImGui::CreateContext()`; disable ini file; enable docking.
7. `ApplyFonts()`: load TTF/OTF from static content (`DejaVuSans.ttf`, Font Awesome, optional CJK), size `15 * scaleFactor`, rebuild atlas.
8. `SetupDefaultStyle()` + `style.ScaleAllSizes(scaleFactor)`.
9. Backend: `ImGui_ImplSDL3_InitForVulkan` + `ImGui_ImplVulkan_Init` with Vulkan device/queue/descriptor pool/render pass from `VulkanDevice`.
10. Key map: engine `Key` → `ImGuiKey` table (`RegisterKeys`).
11. Event binds:
    - `OnBeginFrame` → `ImGuiNewFrame`
    - `OnRecordRenderCommands` → `ImGuiRecordsCommands`
    - `OnShutdown` → `ImGuiDestroy`
    - `OnWindowResized` → recreate swapchain/framebuffers
    - Plugin/type reload → clear ImGui reflection caches

### 2.2 Per-frame (`ImGuiNewFrame`)

1. Backend `NewFrame` (Vulkan then SDL3).
2. `ImGui::NewFrame()`; `ImGuizmo::BeginFrame()`.
3. Prune stale per-resource editor draw contexts (frame age).

**Input feed:** not a separate ImGui API call path beyond SDL. Platform/SDL events are dual-fed:

- Engine input: `InputHandlerEvents(SDL_Event*)` raises `OnKeyDown`/`OnKeyUp`/`OnMouse*`/`OnTextInput` and updates `Input::*` state.
- ImGui: same SDL events via `ImGui_ImplSDL3_ProcessEvent` (registered callback).

Text input start/stop uses `SDL_StartTextInput` / `SDL_StopTextInput` through `Input::SetTextInputActive`. Capture flags (`WantCaptureMouse` / `WantCaptureKeyboard`) are available from ImGui IO for game-vs-UI routing (editor checks as needed).

### 2.3 Render hook (`ImGuiRecordsCommands`)

1. `ImGui::EndFrame()`.
2. Early-out if window minimized.
3. Active workspace windows `Render(cmd)` (editor panels issue ImGui widgets + may submit 3D viewports into the command buffer).
4. `ImGui::Render()`.
5. `swapchain->AcquireNextImage()`; on out-of-date, resize and re-acquire.
6. `BeginRenderPass` on current swapchain framebuffer; clear dark.
7. `ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmd)`.
8. `EndRenderPass`.

Shutdown tears down swapchain, render pass, `ImGui_ImplVulkan_Shutdown`, `ImGui_ImplSDL3_Shutdown`, `DestroyContext`.

### 2.4 Parallel UI stack on main (not v1 target)

main also has:

- **RmlUi** (`Runtime/Source/Skore/UI/*`, scene components `UIContext` / `UIDocument`) for document/CSS-style UI.
- **DrawList** (`Runtime/Source/Skore/Graphics/DrawList.{hpp,cpp}`) — immediate-style 2D: `AddText` / `AddImage` / `AddRect(Filled)` / scissor stack / `Flush` to GPU with font vs image pipelines.

v1 UI **owns its own retained tree** but the **draw-list + GPU flush model should mirror main's DrawList**, not ImGui's internal draw lists, so the same path can serve HUD and tools without Dear ImGui.

### 2.5 Migration path (high level)

| main ImGui responsibility | v2 owner |
| --- | --- |
| Context + frame bracketing | `sk-ui` (`begin_frame` / `end_frame`) |
| SDL event feed | `platform_window` input events → `sk-ui` input queue (and future engine Input module) |
| Font atlas at DPI | `sk-ui` MSDF text pipeline (msdf-atlas-c) |
| Vulkan render draw data | `sk-ui` draw list → `render_device` command encoding (host or UI encode helper) |
| Docking / editor windows | **Out of v1** — editor keeps temporary ImGui or minimal custom chrome until a later milestone |
| Field reflection widgets | Later editor layer on top of sk-ui primitives |

v1 deliberately **does not** reimplement docking, multi-viewport, ImGuizmo, or the full property-grid field renderer set.

---

## 3. v1 product design

### 3.1 Retained element tree

**Model:** hierarchical nodes with stable identity, not immediate-mode rebuild every frame.

| Concept | Design |
| --- | --- |
| Identity | `sk_ui_node_id_t` = dense generation handle (same spirit as ECS entities / `SK_HANDLER`). Invalid = 0. |
| Tree | Root per `sk_ui_context_t` (one primary context per window in v1). Parent pointer + ordered child array. |
| Node storage | Slot array / freelist in the context; children hold ids, not raw pointers across frames. |
| Types | Tag enum: `Box`, `Text`, `Image`, `Button` (button = box + interaction flags). Custom controls compose these; no deep widget zoo in v1. |
| Lifecycle | `create` / `destroy` (recursive children), `reparent`, `set_child_index`. Destroy frees slot generation. |
| User data | Optional `void*` + type id cookie for host binding. |
| Dirty bits | Per-node flags: `Style`, `Layout`, `Content`, `Transform`, `Subtree`. Mutations set bits; layout/paint clear after work. |
| Invalidation | Changing text/style/class marks self + ancestors for layout; geometry-only style may mark paint only. Window resize marks root layout dirty. |
| Structure vs property | Structural edits (insert/remove child) mark parent layout dirty and rebuild child flex lines. |

**Not in v1:** differential virtual DOM from another language, multi-threaded tree mutation (main-thread only), CSS cascade engine.

### 3.2 Flexbox layout (v1 subset)

Backed by the **vendored Clay engine** (`thirdparty/clay`, v0.14) through the adapter in `plugins/ui/clay_adapter.c` — keeps the plugin C-only and integrates the engine allocator + font metrics without a C++ dependency. The earlier custom pure-C solver (`plugins/ui/layout.c`) was deleted once the Clay adapter covered the whole tree (APX-216). **Not** a full CSS engine.

**In v1 (APX-130):**

- Direction: `row` / `column` / `row-reverse` / `column-reverse`.
- `flex-wrap`: `nowrap` / `wrap` / `wrap-reverse`.
- `justify-content`: `flex-start`, `flex-end`, `center`, `space-between`, `space-around`, `space-evenly`.
- `align-items` / `align-self` / `align-content`: `flex-start`, `flex-end`, `center`, `stretch` (`auto` on self).
- `flex-grow`, `flex-shrink`, `flex-basis` (`auto` | length | %).
- Sizing: `width`/`height` as `auto` | `px` | `%` of parent content box; min/max.
- Padding, margin, border widths (per edge); `row_gap` / `column_gap`.
- Position: flow (`relative`) and **absolute** vs nearest positioned ancestor (padding edge).
- Measure callback for intrinsic content (text/images); layout never inspects fonts.
- Logical units only; `layout_apply_scale` is a separate HiDPI step.

**Still deferred:**

- Overflow scroll widgets, grid, z-index stacking contexts, baseline alignment, aspect-ratio, percentage padding width quirks beyond parent content box.

**Pass order:** measure (callback) → flex resolve top-down → store border/content rects (parent content-relative, logical) → optional `layout_apply_scale` for physical pixels.

### 3.3 Style / class model

| Layer | Behavior |
| --- | --- |
| Inline style | Struct on node (`sk_ui_style_t`): display, flex props, colors, font size, padding/margin, border width/radius (radius paint-only soft in v1: optional; square borders OK). |
| Classes | Interned name → style sheet entry. Node holds list of class ids. |
| Cascade (v1) | Defaults → class list in order → inline overrides. **No** pseudo-classes beyond interaction flags applied as style overrides (`:hover` / `:active` / `:disabled` resolved at style resolve time from input state). |
| Theme | Optional context-level default sheet registered at init. |
| Units | Layout px are **logical** (see HiDPI). |

Style resolve runs when `Style` dirty; produces computed style cached on the node until next invalidation.

### 3.4 Text pipeline (MSDF, since APX-271)

1. **Font face:** TTF/OTF bytes loaded (filesystem or fixture). FreeType is used
   **only** for face loading (`FT_New_Memory_Face`), cmap queries
   (`FT_Get_Char_Index`), and face metrics; glyph rasterization and the R8
   bitmap atlas were retired (APX-271).
2. **MSDF atlas:** `font_msdf_bake` drives msdf-atlas-c (ASCII charset,
   INKTRAP edge coloring, tight power-of-two-square pack, Y_TOP_DOWN) and
   quantizes the RGB32F result to **RGB8** with a **symmetric distance range
   of 2 px** (`{-1,+1}` endpoints, edge at 0.5). One scale-independent atlas
   per face serves every pixel size (see `docs/ui-plugin.md` §5).
3. **Glyph records:** em-normalized advance, plane bounds, and UVs; kerning
   table from a `MSDF_ATLAS_LOAD_KERNING` pass. Missing glyphs resolve to a
   defined .notdef box.
4. **Shaping (v1):** left-to-right UTF-8 walk; **no** HarfBuzz, no complex
   scripts. Newline + soft wrap at spaces to max width.
5. **Metrics:** atlas ascender/descender/line-height (em) × pixel size;
   `measure_text` returns width/height for layout (same path as paint).
6. **Paint:** per-glyph quads sampled as `median(r,g,b)`, converted to a
   signed screen-space distance (`pxRange`/atlas size × `fwidth`), covered
   with `smoothstep(-0.5, 0.5)` and clamped to 1 px minimum range.
   Headless captures evaluate the same formula on the CPU.

**Out of v1:** cooked `.font` atlas assets, MTSDF outlines/glow, rich text
spans beyond a single style per text node.

### 3.5 Draw list and renderer

Mirror main `DrawList` responsibilities inside the UI plugin (or a `ui_draw` module of the same plugin):

```text
sk_ui_draw_list_t
  batches[]: { texture, is_font, vertices[], indices[], scissor }
  commands: reset, add_rect_filled, add_rect, add_image, add_text, push/pop_scissor
  flush → GPU buffers + draw_indexed per batch
```

**Vertex:** `pos: vec2`, `uv: vec2`, `color: u32` (RGBA8 packed) — matches main `DrawListVertex` / `DrawList2D` HLSL.

**GPU path:**

1. Compile VS/PS via DXC at UI init (or load precompiled SPIR-V in tests).
2. Create pipelines: alpha blend, no depth, scissor enabled, triangle list.
3. Per frame after layout: walk tree paint order (painter's algorithm, tree order); push scissor for `overflow:hidden`; emit primitives.
4. `flush(cmd, render_pass_compat, framebuffer_extent)`:
   - ensure host-visible VB/IB capacity
   - map/write or `update_buffer`
   - bind pipeline / descriptor for texture
   - `set_viewport` full target; `set_scissor` per batch
   - `push_constants` ortho projection (pixel space, top-left origin)
   - `draw_indexed`

**Coordinate space:** layout and draw list use **logical pixels**; conversion to framebuffer pixels multiplies by content scale at flush (or build a projection that maps logical → NDC using framebuffer size).

### 3.6 Input / event model

**Ingress (host → UI):** after `poll_events`, host pushes platform events into `ui->queue_event` **or** UI pulls from a future platform input API. v1 API shape:

```text
sk_ui_event_t {
  type: MouseMove | MouseButton | MouseWheel | Key | Text | Focus | ...
  pos_logical: vec2
  button / key / mods / text_utf8 / down / repeat
}
```

**Dispatch:**

1. Hit-test top-most interactive node under cursor (respects scissor/overflow).
2. Capture: mouse-down on a node captures until up (drag).
3. Hover: update `:hover` on enter/leave; dirty style.
4. Bubble path: target → root. Listeners: optional function pointer + user data on node (`on_click`, generic `on_event`).
5. Focus: tab order linear in tree for focusable controls (Button, optional TextField stub); text input requests platform text input when focus enters an editable (platform gap until text IME API exists — v1 can use raw key/text events only).

**Routing to game:** UI reports `wants_mouse` / `wants_keyboard` after dispatch (ImGui-equivalent capture), so the host can skip gameplay input.

### 3.7 HiDPI strategy

| Quantity | Source | Use |
| --- | --- | --- |
| Logical size | `get_window_size` | Layout root, hit-test coordinates. |
| Content scale | `get_window_content_scale` (x/y); `get_window_dpi` averages | Font pixel size, style scale, framebuffer projection. |
| Framebuffer size | `get_framebuffer_size` | Swapchain / viewport in physical pixels. |
| Scale change | `set_window_content_scale_callback` (or poll scale) | Rebuild fonts / dirty layout when DPI changes. |

Rules:

- **Invariant: `logical × content_scale == physical`.** The host lays out at `get_window_size`, then `layout_apply_scale(content_scale)`; if the window API returned pixels as "logical", the scale is applied twice and the UI is drawn `content_scale×` too large (clipped by the window) while hit-testing — which uses unscaled layout rects — lands `content_scale×` off the drawn widget.
- All layout units are logical px.
- Font size for layout is the **logical** `font_size`; the MSDF atlas is
  scale-independent, so paint scales em metrics by `font_size × content_scale`
  (unrounded, min 1 px) without any re-raster. On scale change: mark all
  layout dirty, recreate pipelines only if needed.
- Prefer the content-scale callback; polling `get_window_content_scale` each frame remains valid.

### 3.8 Automation / testability (UI tester foundation)

Shipped as **APX-137** — see **`docs/ui-automation-api.md`** for the full contract the future Selenium-style tester will consume.

| Surface | Purpose |
| --- | --- |
| `query_by_test_id` / `query_by_class` / `query_by_widget` / `query_by_text` (+ `query_all_*`, scoped) | Find elements without scraping pixels. |
| `node_get_abs_rect` / `node_get_computed_style` / `node_is_visible` / `node_is_enabled` / `node_get_visible_text` | Assert layout, style, and state. |
| `action_click` / `action_type_text` / `action_scroll` / `action_focus` | Drive UI via synthesized `input_dispatch` (same path as hosts). |
| `harness_create` / `harness_step(delta)` / stable clock | Headless construct + deterministic frames (no wall time). |
| Optional soft-render RGBA + `harness_pixels` | Golden comparison without GPU. |
| Deterministic font fixture | Embedded TTF under `plugins/ui/testdata/`. |

**Still out of scope:** full scripted recorder, accessibility tree export, complete tester binary.

---

## 4. Proposed plugin surface (sketch)

Not implementing here; shapes for follow-on tasks.

```text
SK_UI_API_TYPE_ID

sk_ui_api_t {
  init / shutdown
  context_create / context_destroy
  begin_frame / end_frame
  set_root_size(logical_w, logical_h, content_scale)

  node_create / node_destroy / node_reparent
  set_style / add_class / remove_class / set_text / set_image
  set_test_id / find_by_test_id

  queue_event / wants_mouse / wants_keyboard
  layout   // process dirty
  paint    // rebuild draw list
  get_draw_list / flush_draw_list(device, cmd, ...)

  // test hooks
  get_layout_rect / dump_tree
}
```

Host owns: window, swapchain, command buffer lifetime, plugin load order.

---

## 5. Honest v1 scope

**In:**

- Retained tree + dirty layout/paint
- Flexbox subset above
- Classes + inline styles + hover/active/disabled
- Bitmap font atlas text (UTF-8 basic)
- Draw list → render_device (Vulkan path)
- DXC-built UI shaders from HLSL source string
- Synthetic input + headless layout tests
- Design-level migration map off main ImGui

**Out:**

- Full Dear ImGui replacement (docking, multi-viewport, demos, property grids, ImGuizmo)
- RmlUi / HTML/CSS compatibility
- Complex text (HarfBuzz, BiDi)
- Scroll views, virtualized lists, text edit completeness (single-line edit optional stretch goal)
- Working around missing platform/input/asset APIs **inside** the UI plugin

---

## 6. Implementation phases (suggested, post-design)

1. Close **blocking gaps** in owning plugins (§7 priority P0).
2. Scaffold `plugins/ui` API + context + node storage + tests.
3. Flex layout + style resolve.
4. Draw list + DXC shaders + Vulkan flush path.
5. Text + atlas.
6. Input dispatch + hover/click.
7. Editor/player host integration (still no full ImGui parity).
8. **APX-138 (done):** sample in-game main menu (`sample_menu_build`) using every v1 widget, style classes only, wired into `sk-player` host loop; headless 1x/2x soft-render goldens and runtime content-scale re-layout / glyph re-raster tests under `plugins/ui/testdata/sample/`.

---

## 7. Abstraction gap list

Every gap is owned by an **existing** module/plugin (or a named new module that is **not** `ui`). The UI plugin must not invent private forks of these.

| ID | Gap | Why UI needs it | Owner (fix here) | Priority |
| --- | --- | --- | --- | --- |
| G1 | **Mostly done:** `sk_key_t` / `sk_key_mod_flags_t` enums + `set_window_key_callback` / `set_window_char_callback` / `set_window_scroll_callback`; ~~no input at all~~. Mouse position/buttons are still **polled** (`get_cursor_pos` / `get_mouse_button`) — no button/move event callbacks yet | UI and future game Input both need a single OS event source (main used SDL events). | **`platform_window`** | P0 |
| G2 | ~~No framebuffer size API~~ **Done:** `get_framebuffer_size` | HiDPI projection and swapchain extent. | **`platform_window`** | P0 |
| G3 | Content-scale callback **done**; still missing resize / focus / close **events** | Avoid missing DPI changes and resize; cleaner than edge-detect alone. | **`platform_window`** | P0 |
| G4 | ~~No monitor list / per-monitor scale~~ **Done:** `get_monitor*` + `get_monitor_content_scale` | Multi-monitor editor placement and DPI. | **`platform_window`** | P2 |
| G5 | ~~Separate content scale x/y~~ **Done:** `get_window_content_scale` | Correct non-uniform scaling (rare but real on some setups). | **`platform_window`** | P2 |
| G6 | No process-wide frame phase / event bus (`OnBeginFrame`, `OnRecordRenderCommands`, …) | Main ImGui hooked these; host currently hard-codes order. UI can be called explicitly in v1, but editor scale wants a bus. | **`sk-app` / core events module** (new small core or app API — **not** ui) | P1 |
| G7 | No engine `Input` module on v2 | Capture routing, text input active, cursor modes shared with gameplay. | **New `input` plugin or `platform_window` input facade** (prefer dedicated **input** plugin later; platform_window remains OS source) | P1 |
| G8 | `resource_assets.h` + manager not on HEAD | Shared Font/Texture load/cook/reload; avoid UI-private file formats long term. | **core `resource_assets` + assets manager (app or plugin)** — land the designed header and runtime | P1 |
| G9 | No Font resource type / importer | Cooked glyph metrics + atlas or source TTF reference for UI and scene text. | **resource assets Font handler/importer** (MSDF atlas cooker; runtime bake via msdf-atlas-c is in) | P1 |
| G10 | No Texture image loader (PNG/etc.) | Image widgets and icons. | **resource assets Texture handler/importer** | P1 |
| G11 | No shared growable string type | Node labels, text input buffers, class name tables. | **`core`** (string module) if multiple systems need it; until then UI-local buffers only | P2 |
| G12 | No `sk_color_t` / UI-friendly rect in core (repo has COLOR field category only) | Consistent color in styles and serialization. | **`core` (`math3d` or small `color` fields in math module)** | P2 |
| G13 | `render_graph` is a stub | Optional later: UI pass declared in graph. Not required for v1. | **`render_graph`** | P3 |
| G14 | No convenience “upload texture from CPU bytes” helper on RHI | UI can sequence barriers + `copy_buffer_to_texture` manually; a Graphics-level helper reduces bugs. | **`render_device` or thin `common-render` helper** (if shared) | P2 |
| G15 | DXC has no reflection | Pipeline layouts hand-authored in UI — acceptable. If shader authoring grows, reflection belongs in a **shader** module, not UI. | **future shader/tools plugin** | P3 |
| G16 | Editor executable / host loop incomplete on v2 | Nowhere to hang ImGui migration until editor bootstrap exists. | **`editor` + `app` host** | P1 |
| G17 | main ImGui still depends on SDL3 + raw Vulkan types | v2 window is GLFW; RHI is abstract. Do **not** reintroduce `ImGui_ImplVulkan` against raw Vk in engine code; go through `render_device`. | **editor migration task** (uses ui + render_device + platform_window) | P1 |

### Short gap list (checklist)

**P0 — block correct UI frame loop**

- [ ] G1 `platform_window`: input events (key/mouse/wheel/text) + enums  
- [x] G2 `platform_window`: framebuffer size  
- [ ] G3 `platform_window`: resize / focus / close events (DPI/content-scale callback **done**)  

**P1 — block production integration**

- [ ] G6 app/core: frame phase events (or documented host call order only)  
- [ ] G7 input module (or platform_window input facade)  
- [ ] G8–G10 resource assets + Font/Texture cooker (MSDF runtime bake is in; no cooked assets yet)  
- [ ] G16–G17 editor host + migration off ImGui_Impl*  

**P2 — quality**

- [x] G4–G5 monitors + non-uniform scale  
- [ ] G11–G12 core string/color  
- [ ] G14 texture upload helper  

**P3 — later**

- [ ] G13 render_graph UI pass  
- [ ] G15 shader reflection  

---

## 8. Summary

v2 already has a **credible GPU and plugin foundation** for a basic UI system: registry lifecycle, a full render-device surface (buffers, textures, pipelines, render passes, scissor, indexed draws), GLFW windowing with DPI query, repository/serialization/filesystem, and DXC HLSL→SPIR-V from a memory string. **What it lacks** is input and HiDPI completeness on the window plugin, an asset handler pipeline on HEAD, frame events, and any UI/ImGui code.

main's editor drives Dear ImGui through **SDL event dual-feed**, **DPI-scaled fonts/styles**, **begin-frame / record-commands event hooks**, and **ImGui_ImplVulkan draw-data** into a dedicated swapchain pass. v1 UI replaces that stack with a **retained flex tree + draw list** encoded via **`sk_render_device_api_t`**, keeps scope small (MSDF text, no full ImGui), and **pushes every abstraction gap to its proper owner** instead of working around it inside the UI plugin.
