# UI automation API and headless harness

Task: **APX-137**. This document is the **contract** a future Selenium-style UI tester will consume. It describes the query surface, element accessors, programmatic actions, deterministic frame stepping, and the headless harness — not a full scripted recorder or accessibility tree.

Public types and entry points live in `plugins/ui/ui.h` as members of `sk_ui_api_t`. Hosts obtain the table via the app registry:

```c
const sk_ui_api_t* ui =
    (const sk_ui_api_t*)app_api->get_api(ctx, SK_UI_API_TYPE_ID);
```

In-process unit tests use the same table through the plugin-local export.

---

## 1. Goals and non-goals

**In scope**

| Area | Purpose |
| --- | --- |
| Query API | Find nodes by test id, style class, widget type, or visible text; optional subtree scope |
| Accessors | Layout rect, computed style, visibility, enabled state, visible text |
| Actions | Click, type text, scroll, focus — via the **same** `input_dispatch` path as real hosts |
| Frame stepping | Advance one frame with a supplied delta on a **stable clock** (no wall time) |
| Headless harness | Construct a UI context, drive it, optionally soft-render RGBA for goldens |

**Out of scope (later)**

- Full scripted recorder / playback format
- Accessibility tree export
- A complete Selenium-compatible driver binary (this API is what that driver will call)
- GPU offscreen capture (soft-render is CPU draw-list rasterization)

---

## 2. Stable identity and queries

### 2.1 Test ids

Node user ids (`node_set_id` / `node_get_id` / `find_by_id`) **are** the automation test ids. Widget factories accept an optional `id` and auto-generate one (`ui-button-1`, …) when omitted.

```c
sk_ui_node_t btn = ui->widget_button(ctx, parent, "Save", "btn-save");
sk_ui_node_t n = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "btn-save");
/* n == btn */
```

`query_by_test_id(ctx, scope, test_id)`:

- Resolves the unique id in the context.
- If `scope` is a live node, returns the match only when it is `scope` or a descendant.
- `scope == SK_UI_NODE_INVALID` searches the whole tree (equivalent to `find_by_id` when the id exists).

### 2.2 Style class

```c
sk_ui_node_t n = ui->query_by_class(ctx, scope, SK_UI_CLASS_BUTTON);
u32 count = ui->query_all_by_class(ctx, scope, SK_UI_CLASS_BUTTON, out, max_out);
```

Default class names (stable): `ui-panel`, `ui-view`, `ui-label`, `ui-button`, `ui-checkbox`, `ui-slider`, `ui-text-input`, `ui-scroll-view`, `ui-image`.

### 2.3 Widget type

Widgets set prop `"widget"` to a short type string: `"panel"`, `"view"`, `"label"`, `"button"`, `"checkbox"`, `"slider"`, `"text_input"`, `"scroll_view"`, `"image"`, `"scroll_content"`.

```c
sk_ui_node_t n = ui->query_by_widget(ctx, scope, "text_input");
```

### 2.4 Visible text

Exact match on prop `"text"` (labels, buttons, text inputs).

```c
sk_ui_node_t n = ui->query_by_text(ctx, panel, "Save");
```

### 2.5 Scoped walks and `query_all_*`

- All queries walk **preorder** under the scope root (`scope` if alive, else context root).
- `query_all_*` writes up to `max_out` handles into `out` and returns the **total** match count (may exceed `max_out`). Pass `out == NULL` or `max_out == 0` to count only.

---

## 3. Element accessors

| API | Meaning |
| --- | --- |
| `node_get_abs_rect` / `node_get_layout_rect` | Computed layout geometry (logical units; abs rect is hit-test space) |
| `node_get_computed_style` | Last `style_resolve` result (colors, opacity, layout fields, font) |
| `node_get_state` | Interaction flags: hover / active / focused / disabled |
| `node_is_visible` | Alive, not `hidden==1`, computed opacity &gt; 0, non-empty abs border box after layout |
| `node_is_enabled` | Alive and `SK_UI_STATE_DISABLED` clear |
| `node_get_visible_text` | Prop `"text"`, or `""` |

Callers should run layout (or `harness_step`) before relying on visibility or abs rects.

---

## 4. Programmatic actions

Actions **must not** bypass routing. They synthesize `sk_ui_input_event_t` values and call `input_dispatch`, so widget handlers, capture, focus, and state flags behave as for a real pointer/keyboard.

| Action | Implementation |
| --- | --- |
| `action_click(ctx, node)` | Pointer move → left button down → left button up at absolute center |
| `action_type_text(ctx, node, utf8)` | `focus_set` then `SK_UI_INPUT_TEXT` with the string |
| `action_scroll(ctx, node, sx, sy)` | `SK_UI_INPUT_WHEEL` at absolute center |
| `action_focus(ctx, node)` | `focus_set` (FOCUS_OUT / FOCUS_IN, focused state bit) |

Preconditions:

- Node is alive.
- For click/scroll: layout has produced a non-empty abs rect (run `layout` or `harness_step` first).
- For type/focus: node is focusable and not disabled.

Return `0` on success, non-zero on failure.

---

## 5. Deterministic frame stepping and clock

Tests **must not** use wall clocks for UI timing. The harness owns a monotonic counter:

```text
time_sec += delta_seconds   // only advanced by harness_step
frame_index += 1
```

```c
ui->harness_step(h, 1.0f / 60.0f);
f64 t = ui->harness_time(h);       /* sum of deltas since create */
f32 d = ui->harness_last_delta(h);
u32 f = ui->harness_frame_index(h);
```

Negative deltas are clamped to `0`. Zero delta still runs a full frame (style/layout/paint) and increments `frame_index`.

---

## 6. Headless harness

### 6.1 Create / destroy

```c
sk_ui_harness_desc_t desc = {0};
desc.width = 800.0f;          /* logical; default 800 if <= 0 */
desc.height = 600.0f;         /* logical; default 600 if <= 0 */
desc.content_scale = 1.0f;    /* default 1 if <= 0 */
desc.soft_render = 1;         /* optional RGBA8 buffer */
desc.allocator = NULL;        /* default process allocator */

sk_ui_harness_t* h = ui->harness_create(&desc);
sk_ui_context_t* ctx = ui->harness_context(h);
/* build tree with widget_* / node_* */
ui->harness_step(h, 1.0f / 60.0f);
/* query / action / assert */
ui->harness_destroy(h);
```

No window, no GPU, no OS event loop.

### 6.2 `harness_step` pipeline

Each successful step:

1. Advance stable clock (`time_sec`, `last_delta`, `frame_index`).
2. `style_resolve`
3. `layout(width, height)` (logical root size)
4. `layout_apply_scale(content_scale, content_scale)`
5. `paint` (optional font params from `harness_set_font`)
6. If soft-render: CPU-rasterize the draw list into an RGBA8 buffer

### 6.3 Soft-render (golden comparison)

When `soft_render != 0`:

| API | Notes |
| --- | --- |
| `harness_pixels` | Row-major RGBA8, pitch = width×4; valid until next step/destroy/resize |
| `harness_pixel_size` | Physical size ≈ `round(logical * content_scale)` |
| `harness_draw_list` | Last paint list (clips, meshes) for structural asserts |

Soft-render is intentional for headless goldens; GPU capture remains a separate integration path (`tests/integration/ui_render.c`).

Assert pixels with `cpu_image_compare` / `cpu_image_compare_golden` (APX-229): per-channel tolerance, max differing-pixel fraction, size-mismatch fail-fast, and on failure `{name}_actual` / `_expected` / `_diff` PNGs plus differ count, max channel delta, and bbox. **Blessing is opt-in only** (`params.update_golden` or `SK_UI_REGEN_GOLDENS=1`); review the three artifacts, rewrite the golden, then commit. See `docs/ui-plugin.md` §9.3.

### 6.4 Fonts for text goldens

```c
sk_ui_font_system_t* fs = ui->font_system_create(NULL, 256, 256);
sk_ui_font_t* font = ui->font_load_memory(fs, ttf_bytes, ttf_size);
ui->harness_set_font(h, fs, font);
/* … steps … */
ui->harness_destroy(h);
ui->font_destroy(font);
ui->font_system_destroy(fs);
```

Ownership of font objects stays with the caller.

---

## 7. Recommended test pattern

```c
/* 1. Create harness with soft_render if asserting pixels */
/* 2. Build widgets with stable test ids */
/* 3. harness_step(delta) — layout + paint + clock */
/* 4. query_by_test_id / class / widget / text */
/* 5. Assert accessors (visible, enabled, layout, style, text) */
/* 6. action_click / action_type_text / action_scroll / action_focus */
/* 7. harness_step again; assert model + optional pixels / draw list */
```

End-to-end coverage shipped with the plugin (under `#ifdef SK_TESTS` in `plugins/ui/automation.c`):

- Scoped queries by id, class, widget type, and text
- Visibility / enabled / layout / computed style accessors
- Click button → callback fired + interaction state
- Type into `TextInput` → model value + soft-render path
- Scroll `ScrollView` → scroll offset + clip commands
- Deterministic clock (fixed deltas, no wall time)

---

## 8. Relationship to lower layers

```text
Future UI tester
      │
      ▼
sk_ui_api_t  query_* / action_* / harness_*     ← this contract
      │
      ├─► find_by_id, node classes, props
      ├─► style_resolve / layout / paint
      └─► input_dispatch  (single ingress for hosts and automation)
```

Do not add a second “test-only” input path. Automation and production hosts share `input_dispatch`.

---

## 9. Versioning notes

- Breaking changes to function pointer order or semantics of `sk_ui_api_t` require a coordinated consumer update (the table is a flat C vtable).
- Prefer new entry points at the **end** of `sk_ui_api_t` when extending.
- Widget type strings and default class names are part of the automation surface; rename only with a migration note.
