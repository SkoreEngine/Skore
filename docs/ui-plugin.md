# UI plugin usage guide (v1)

**Audience:** anyone building UI with the `sk-ui` plugin.  
**API source of truth:** `plugins/ui/ui.h` (`sk_ui_api_t`).  
**Related docs:** design notes in `docs/ui-system-design.md`, automation contract in `docs/ui-automation-api.md`, editor dual-stack in `docs/ui-editor-migration.md`.

v1 is a **retained, flexbox-based** element tree with style classes, a CPU draw list, FreeType bitmap text, a small widget set, synthetic input routing, and a headless automation harness. It is **not** a full Dear ImGui replacement and **does not** use msdfgen.

---

## 1. Obtaining the API and creating a UI root

### 1.1 Load and look up

The plugin registers a static `sk_ui_api_t` under `SK_UI_API_TYPE_ID` from `sk_plugin_entry_point` → `sk_ui_init`. Hosts never call UI internals; they only use the registry:

```c
#include "app.h"
#include "ui.h"

/* After sk_app_init (and after sk-ui is loaded from {app_folder}/plugins): */
sk_app_context_t* app_ctx = /* from sk_app_init */;
const sk_app_api_t* app_api = sk_app_api();
const sk_ui_api_t* ui =
    (const sk_ui_api_t*)app_api->get_api(app_ctx, SK_UI_API_TYPE_ID);

if (ui == NULL) {
    /* Plugin missing or not loaded yet. */
    return -1;
}
if (ui->init() != 0) {
    return -1;
}
```

In-process plugin unit tests use the same table via the plugin-local export (they still call through `sk_ui_api_t` function pointers).

### 1.2 Context = one root

Each `sk_ui_context_t` owns **one** retained tree. The root is created automatically as a `SK_UI_NODE_KIND_BOX` with full dirty flags:

```c
sk_ui_context_t* ctx = ui->context_create(NULL); /* NULL = process default allocator */
if (ctx == NULL) {
    return -1;
}
sk_ui_node_t root = ui->context_root(ctx);
/* root is always alive while ctx is live; it cannot be destroyed. */

/* … build tree, style, layout, paint … */

ui->context_destroy(ctx);
ui->shutdown();
```

**Host responsibilities (not done inside `context_create`):**

- Load plugins in a sensible order (window / render device / DXC before GPU UI encode).
- Own the window and per-frame: `poll_events` → feed input → `style_resolve` → `layout` → `layout_apply_scale` → `paint` → optional GPU `renderer_prepare` / `renderer_encode`.
- Call `context_destroy` before device/window tear-down; then `ui->shutdown()`.

There is **no** `begin_frame` / `end_frame` on the v1 table — the host (or `harness_step`) sequences the passes explicitly. See the player sample (`player/main.c`) for a real host loop.

---

## 2. Building an element tree

### 2.1 Nodes and handles

Nodes are stable generation handles (`sk_ui_node_t`). `SK_UI_NODE_INVALID` / `sk_ui_node_is_valid` / `node_alive` are the liveness checks. Structural kinds:

| Kind | Use |
| --- | --- |
| `SK_UI_NODE_KIND_BOX` | Containers, panels, most widgets |
| `SK_UI_NODE_KIND_TEXT` | Labels / text paint |
| `SK_UI_NODE_KIND_IMAGE` | Host texture quads |
| `SK_UI_NODE_KIND_BUTTON` | Button (box + interaction defaults) |

Prefer **widget factories** (§7) for product UI. Low-level tree APIs are for custom composition and layout tests:

```c
sk_ui_node_t root = ui->context_root(ctx);
sk_ui_node_t panel = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);
sk_ui_node_t title = ui->node_create(ctx, SK_UI_NODE_KIND_TEXT, panel);
sk_ui_node_t orphan = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, SK_UI_NODE_INVALID);

(void)ui->node_set_id(ctx, panel, "settings-panel");
(void)ui->node_set_prop_str(ctx, title, "text", "Settings");
(void)ui->node_insert_child(ctx, panel, orphan, 0u); /* reparent orphan under panel */

/* Hierarchy helpers: node_parent, node_child_count, node_child_at,
   node_remove_child, node_set_child_index, node_reparent, node_destroy. */
```

### 2.2 Identity, classes, properties

| Surface | API | Notes |
| --- | --- | --- |
| User / test id | `node_set_id` / `node_get_id` / `find_by_id` | Unique per context; automation uses the same ids |
| Style classes | `node_add_class` / `node_remove_class` / `node_has_class` | Ordered list; cascade later-class wins |
| Props | `node_set_prop_i32` / `_f32` / `_str`, `node_get_prop`, `node_clear_prop` | Widgets store model data here (`"text"`, `"checked"`, …) |
| Host binding | `node_set_user_data` / `node_get_user_data` | Opaque pointer + type cookie; not freed by UI |

### 2.3 Dirty flags

Mutations mark `SK_UI_DIRTY_STYLE` / `LAYOUT` / `PAINT` on the node **and ancestors**. A clean parent for a flag means the whole subtree is clean for that flag. Passes clear their bits:

1. `style_resolve` → clears `STYLE`
2. `layout` → clears `LAYOUT`
3. `paint` → clears `PAINT` (on rebuild)

---

## 3. Style classes and inline styles

The style system is **not CSS**. It is a named class registry + per-node class list + inline overrides + interaction state variants.

### 3.1 Cascade order (per property)

```text
default → earlier class (base → hover → focused → active → disabled)
        → later class (same state order)
        → inline
        → text inherit (font_family / font_size / color only)
```

State variants apply only for bits set in `node_get_state` (`SK_UI_STATE_HOVER`, `ACTIVE`, `FOCUSED`, `DISABLED`). Input routing updates those flags automatically for interactive nodes.

### 3.2 Register a class and attach it

Only fields with bits set in `sk_ui_style_props_t.mask` participate. Layout fields live under `props.layout` and use the same length helpers as direct layout style.

```c
sk_ui_style_props_t base;
sk_ui_style_props_t hover;

memset(&base, 0, sizeof(base));
base.mask = SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_PADDING | SK_UI_SP_FLEX_DIRECTION |
            SK_UI_SP_ROW_GAP | SK_UI_SP_WIDTH;
base.background_color = sk_ui_rgba(0.15f, 0.17f, 0.22f, 1.0f);
base.layout.padding.left = 12.0f;
base.layout.padding.top = 12.0f;
base.layout.padding.right = 12.0f;
base.layout.padding.bottom = 12.0f;
base.layout.flex_direction = SK_UI_FLEX_COLUMN;
base.layout.row_gap = 8.0f;
base.layout.width = sk_ui_pt(280.0f);

if (ui->style_class_register(ctx, "card", &base) != 0) {
    return -1;
}

memset(&hover, 0, sizeof(hover));
hover.mask = SK_UI_SP_BACKGROUND_COLOR;
hover.background_color = sk_ui_rgba(0.20f, 0.23f, 0.30f, 1.0f);
(void)ui->style_class_set_variant(ctx, "card", SK_UI_STATE_HOVER, &hover);

sk_ui_node_t card = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, ui->context_root(ctx));
(void)ui->node_add_class(ctx, card, "card");

/* Highest cascade layer — optional: */
sk_ui_style_props_t inline_s;
memset(&inline_s, 0, sizeof(inline_s));
inline_s.mask = SK_UI_SP_CORNER_RADIUS | SK_UI_SP_OPACITY;
inline_s.corner_radius = 6.0f;
inline_s.opacity = 1.0f;
(void)ui->node_set_inline_style(ctx, card, &inline_s);

/* Or merge without wiping other inline fields: */
(void)ui->node_merge_inline_style(ctx, card, &inline_s);

if (ui->style_resolve(ctx) != 0) {
    return -1;
}
```

`widgets_register_defaults` (called from `context_create`) installs the v1 widget class names (`SK_UI_CLASS_PANEL`, `SK_UI_CLASS_BUTTON`, …) with hover/active/focused/disabled variants. You may override those classes with `style_class_register` / `style_class_set_variant`.

### 3.3 Style property mask bits

| Mask | Field | Notes |
| --- | --- | --- |
| `SK_UI_SP_FLEX_*` / size / min / max / padding / margin / border / gaps / position / insets | `props.layout.*` | Fed to the flex solver after resolve |
| `SK_UI_SP_BACKGROUND_COLOR` | `background_color` | RGBA 0..1 |
| `SK_UI_SP_BORDER_COLOR` | `border_color` | Paint |
| `SK_UI_SP_CORNER_RADIUS` | `corner_radius` | Paint (soft corners) |
| `SK_UI_SP_OPACITY` | `opacity` | Multiplies paint alpha |
| `SK_UI_SP_FONT_FAMILY` | `font_family` | Inherits; name string for host/font lookup |
| `SK_UI_SP_FONT_SIZE` | `font_size` | Logical px; inherits |
| `SK_UI_SP_COLOR` | `color` | Text color; inherits |

Helpers: `SK_UI_SP_LAYOUT_MASK`, `SK_UI_SP_PAINT_MASK`, `SK_UI_SP_INHERIT_MASK`.

---

## 4. Flexbox layout (supported property set)

Layout runs in **logical units** (window client points). HiDPI is a separate step: `layout_apply_scale(scale_x, scale_y)`.

### 4.1 Supported properties

| Property | Values / type |
| --- | --- |
| `flex_direction` | `ROW`, `ROW_REVERSE`, `COLUMN` (default), `COLUMN_REVERSE` |
| `flex_wrap` | `NOWRAP`, `WRAP`, `WRAP_REVERSE` |
| `justify_content` | `FLEX_START`, `FLEX_END`, `CENTER`, `SPACE_BETWEEN`, `SPACE_AROUND`, `SPACE_EVENLY` |
| `align_items` | `FLEX_START`, `FLEX_END`, `CENTER`, `STRETCH` (default stretch) |
| `align_self` | `AUTO` (inherit items) + same as items |
| `align_content` | multi-line: start / end / center / stretch |
| `flex_grow` / `flex_shrink` / `flex_basis` | grow ≥ 0, shrink default 1, basis `auto` / pt / % |
| `width` / `height` / min / max | `sk_ui_auto()`, `sk_ui_pt(v)`, `sk_ui_percent(v)` (0..100 of parent content) |
| `padding` / `margin` / `border` | `sk_ui_edges_t` (border widths layout-only) |
| `row_gap` / `column_gap` | logical px |
| `position` | `RELATIVE` (in-flow) or `ABSOLUTE` (out of flex flow) |
| `left` / `top` / `right` / `bottom` | absolute offsets vs nearest positioned ancestor |

**Not in v1 layout:** CSS Grid, baseline alignment, aspect-ratio, z-index stacking contexts, percentage padding quirks beyond parent content box, Yoga as a dependency (solver is pure C in `layout.c`).

### 4.2 Lengths and defaults

```c
sk_ui_length_t fixed = sk_ui_pt(120.0f);
sk_ui_length_t half = sk_ui_percent(50.0f);
sk_ui_length_t auto_sz = sk_ui_auto();
(void)fixed;
(void)half;
(void)auto_sz;
```

New nodes start with column flex, stretch items, grow 0 / shrink 1, auto sizes. Prefer reading then writing so you do not zero out defaults:

```c
sk_ui_layout_style_t ls;
sk_ui_node_t row = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, ui->context_root(ctx));

(void)ui->node_get_layout_style(ctx, row, &ls);
ls.flex_direction = SK_UI_FLEX_ROW;
ls.justify_content = SK_UI_JUSTIFY_SPACE_BETWEEN;
ls.align_items = SK_UI_ALIGN_CENTER;
ls.column_gap = 8.0f;
ls.width = sk_ui_percent(100.0f);
ls.height = sk_ui_pt(40.0f);
ls.padding.left = 8.0f;
ls.padding.right = 8.0f;
(void)ui->node_set_layout_style(ctx, row, &ls);

sk_ui_node_t a = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, row);
sk_ui_node_t b = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, row);
(void)ui->node_get_layout_style(ctx, a, &ls);
ls.width = sk_ui_pt(64.0f);
ls.height = sk_ui_pt(24.0f);
ls.flex_grow = 0.0f;
(void)ui->node_set_layout_style(ctx, a, &ls);
(void)ui->node_get_layout_style(ctx, b, &ls);
ls.width = sk_ui_pt(64.0f);
ls.height = sk_ui_pt(24.0f);
ls.flex_grow = 1.0f; /* take remaining main-axis space */
(void)ui->node_set_layout_style(ctx, b, &ls);

if (ui->style_resolve(ctx) != 0) {
    return -1;
}
if (ui->layout(ctx, 800.0f, 600.0f) != 0) {
    return -1;
}
if (ui->layout_apply_scale(ctx, 1.0f, 1.0f) != 0) {
    return -1;
}

sk_ui_rect_t border;
(void)ui->node_get_layout_rect(ctx, b, &border, NULL);
/* border is parent-content-relative logical box */
```

### 4.3 Absolute positioning example

```c
sk_ui_layout_style_t ls;
sk_ui_node_t root = ui->context_root(ctx);
sk_ui_node_t badge = ui->node_create(ctx, SK_UI_NODE_KIND_BOX, root);

(void)ui->node_get_layout_style(ctx, badge, &ls);
ls.position = SK_UI_POSITION_ABSOLUTE;
ls.right = sk_ui_pt(8.0f);
ls.top = sk_ui_pt(8.0f);
ls.width = sk_ui_pt(16.0f);
ls.height = sk_ui_pt(16.0f);
(void)ui->node_set_layout_style(ctx, badge, &ls);
```

### 4.4 Intrinsic measure (text / images)

The solver never inspects fonts. Install a measure callback when auto-sized content needs an intrinsic size (widgets and paint install defaults where needed; custom trees may set one):

```c
static void my_measure(sk_ui_context_t* ctx, sk_ui_node_t node,
                       const sk_ui_measure_constraint_t* c,
                       sk_ui_size_t* out_size, void_ptr_t user) {
    (void)ctx;
    (void)node;
    (void)user;
    out_size->width = (c->width_mode == SK_UI_MEASURE_EXACTLY) ? c->width : 100.0f;
    out_size->height = 20.0f;
}

ui->set_measure_fn(ctx, my_measure, NULL);
```

---

## 5. Text and fonts

### 5.1 Model

- **CPU path:** FreeType raster + `stb_rect_pack` atlas (R8 pages). Opaque `sk_ui_font_system_t` / `sk_ui_font_t`.
- **Logical font size** comes from computed style (`font_size`). Physical raster size:

```c
u32 px = sk_ui_font_pixel_size(/* logical */ 15.0f, /* content_scale */ 2.0f);
/* px == 30 */
(void)px;
```

- **Shaping (v1):** UTF-8 left-to-right codepoint walk; no HarfBuzz, BiDi, or RTL. Soft wrap uses spaces when wrap is enabled on labels.
- **Paint:** pass fonts into `paint` via `sk_ui_paint_params_t`. Without fonts, text nodes still layout/measure but emit no glyph quads.

### 5.2 Load a face and paint text

```c
sk_ui_font_system_t* fonts = ui->font_system_create(NULL, 512u, 512u);
if (fonts == NULL) {
    return -1;
}

/* From memory (tests / embedded fixtures): */
const u8* ttf_bytes = /* … */;
u32 ttf_size = /* … */;
sk_ui_font_t* font = ui->font_load_memory(fonts, ttf_bytes, ttf_size);

/* Or from disk via engine filesystem: */
/* sk_ui_font_t* font = ui->font_load_path(fonts, sk_filesystem_api(), "fonts/UI.ttf"); */

if (font == NULL) {
    ui->font_system_destroy(fonts);
    return -1;
}

sk_ui_node_t label = ui->widget_label(ctx, ui->context_root(ctx), "Hello", "lbl-hello");
sk_ui_style_props_t text_style;
memset(&text_style, 0, sizeof(text_style));
text_style.mask = SK_UI_SP_FONT_SIZE | SK_UI_SP_COLOR;
text_style.font_size = 16.0f;
text_style.color = sk_ui_rgba(0.95f, 0.95f, 0.98f, 1.0f);
(void)ui->node_set_inline_style(ctx, label, &text_style);

(void)ui->style_resolve(ctx);
(void)ui->layout(ctx, 320.0f, 240.0f);
(void)ui->layout_apply_scale(ctx, 2.0f, 2.0f);

sk_ui_paint_params_t paint_params;
memset(&paint_params, 0, sizeof(paint_params));
paint_params.font_system = fonts;
paint_params.font = font;
if (ui->paint(ctx, &paint_params) != 0) {
    return -1;
}
const sk_ui_draw_list_t* dl = ui->get_draw_list(ctx);
(void)dl;

ui->font_destroy(font);
ui->font_system_destroy(fonts);
```

Diagnostics: `font_get_metrics`, `font_glyph_index`, `font_get_glyph`, `font_atlas_get_page`, `font_cache_stats`.

GPU upload of atlas pages is owned by `sk_ui_renderer_t` (`renderer_prepare`), not the font system.

---

## 6. Events and callbacks

### 6.1 Ingress vs routed events

| Layer | Type | Role |
| --- | --- | --- |
| Host / tester input | `sk_ui_input_event_t` + `input_dispatch` | Raw move/button/wheel/key/text |
| Node callbacks | `sk_ui_event_t` + `sk_ui_node_callbacks_t` | Routed enter/leave/move/down/up/click/wheel/key/text/focus |

`input_dispatch` expands platform-shaped input into capture/target/bubble phases, hover enter/leave, pointer capture, click synthesis, tab focus, and `wants_mouse` / `wants_keyboard`.

```c
static void on_click(sk_ui_context_t* ctx, sk_ui_node_t node,
                     sk_ui_event_t* event, void_ptr_t user) {
    i32* clicks = (i32*)user;
    (void)ctx;
    (void)node;
    if (event->phase == SK_UI_EVENT_PHASE_TARGET) {
        *clicks += 1;
        event->consumed = 1; /* stop remaining bubble */
    }
}

sk_ui_node_t btn = ui->widget_button(ctx, ui->context_root(ctx), "OK", "btn-ok");
sk_ui_node_callbacks_t cbs;
i32 clicks = 0;
memset(&cbs, 0, sizeof(cbs));
cbs.on_click = on_click;
cbs.user = &clicks;
(void)ui->node_set_callbacks(ctx, btn, &cbs);

/* After layout so hit-test geometry is valid: */
sk_ui_input_event_t in;
memset(&in, 0, sizeof(in));
in.kind = SK_UI_INPUT_POINTER_MOVE;
in.x = 20.0f;
in.y = 20.0f;
(void)ui->input_dispatch(ctx, &in);

memset(&in, 0, sizeof(in));
in.kind = SK_UI_INPUT_POINTER_BUTTON;
in.x = 20.0f;
in.y = 20.0f;
in.button = SK_UI_POINTER_BUTTON_LEFT;
in.down = 1;
(void)ui->input_dispatch(ctx, &in);
in.down = 0;
(void)ui->input_dispatch(ctx, &in);
```

### 6.2 Callback table

| Field | Fired for |
| --- | --- |
| `on_event` | Every type (after the typed handler if both set) |
| `on_pointer_enter` / `leave` / `move` / `down` / `up` | Pointer path |
| `on_click` | Synthesized click (down+up on same target) |
| `on_wheel` | Wheel |
| `on_key_down` / `on_key_up` | Keys (focus target) |
| `on_text_input` | UTF-8 text (focus target) |
| `on_focus_in` / `on_focus_out` | Focus changes |
| `user` | Shared user pointer for all handlers |

Phases: `SK_UI_EVENT_PHASE_CAPTURE` → `TARGET` → `BUBBLE`. Set `event->consumed` to stop further delivery.

### 6.3 Hit-test, focus, clip, pointer-events

```c
sk_ui_node_t hit = ui->hit_test(ctx, x, y);
(void)ui->node_set_clip_children(ctx, scroll, 1);          /* scissor + hit clip */
(void)ui->node_set_pointer_events(ctx, overlay, SK_UI_POINTER_EVENTS_NONE);
(void)ui->node_set_focusable(ctx, field, 1);
(void)ui->focus_set(ctx, field);
(void)ui->focus_advance(ctx, 0);  /* tab forward; non-zero = reverse */
i32 mouse = ui->wants_mouse(ctx);
i32 keys = ui->wants_keyboard(ctx);
(void)hit;
(void)mouse;
(void)keys;
```

Well-known keys: `SK_UI_KEY_TAB`, `ENTER`, `ESCAPE`, `SPACE`, `BACKSPACE`, `DELETE`, arrows, `HOME`, `END`. Modifiers: `SK_UI_MOD_SHIFT` / `CTRL` / `ALT` / `SUPER`.

---

## 7. Widget catalog

Factories create nodes with a default style class, prop `"widget"` type string, and a stable test id (auto `ui-button-1` style when `id` is NULL/empty). Default classes are registered by `widgets_register_defaults` (already called from `context_create`).

| Factory | Kind / class | `widget` prop | Notes |
| --- | --- | --- | --- |
| `widget_panel` | BOX / `ui-panel` | `panel` | Styled container |
| `widget_view` | BOX / `ui-view` | `view` | Lightweight container |
| `widget_label` | TEXT / `ui-label` | `label` | Text + wrap/align |
| `widget_button` | BUTTON / `ui-button` | `button` | Focusable; use `on_click` |
| `widget_checkbox` | BOX / `ui-checkbox` | `checkbox` | Toggle + `on_change` |
| `widget_slider` | BOX / `ui-slider` | `slider` | Drag value + `on_change` |
| `widget_text_input` | BOX / `ui-text-input` | `text_input` | Single-line edit, focusable |
| `widget_scroll_view` | BOX / `ui-scroll-view` | `scroll_view` | Clipped; content child |
| `widget_image` | IMAGE / `ui-image` | `image` | Host `texture_id` |

Content under a scroll view uses an internal child (`widget` = `scroll_content`, class `ui-scroll-content`). Parent children there via `scroll_view_content`.

### 7.1 Panel / view

```c
sk_ui_node_t root = ui->context_root(ctx);
sk_ui_node_t panel = ui->widget_panel(ctx, root, "main-panel");
sk_ui_node_t row = ui->widget_view(ctx, panel, "toolbar");
(void)row;
```

No widget-specific props beyond class styling and normal node APIs.

### 7.2 Label

| Prop / API | Meaning |
| --- | --- |
| prop `"text"` | Visible string |
| prop `"wrap"` | `0` off, `1` on (default on) |
| prop `"text_align"` / `"vertical_align"` | `0` start, `1` center, `2` end |
| `label_set_text` / `label_get_text` | Text accessors |
| `label_set_wrap` / `label_set_align` | Wrap and alignment |

```c
sk_ui_node_t lab = ui->widget_label(ctx, panel, "Volume", "lbl-volume");
(void)ui->label_set_wrap(ctx, lab, 1);
(void)ui->label_set_align(ctx, lab, 0, 1);
```

Callbacks: none built-in; use `node_set_callbacks` if needed.

### 7.3 Button

| Prop / API | Meaning |
| --- | --- |
| prop `"text"` | Label |
| `button_set_label` | Set label text |
| `button_set_disabled` | Sets/clears `SK_UI_STATE_DISABLED` |
| focusable | default on |

```c
static void save_clicked(sk_ui_context_t* c, sk_ui_node_t n, sk_ui_event_t* e, void_ptr_t u) {
    (void)c;
    (void)n;
    (void)u;
    if (e->type == SK_UI_EVENT_CLICK) {
        /* host save path */
        e->consumed = 1;
    }
}

sk_ui_node_t save = ui->widget_button(ctx, panel, "Save", "btn-save");
sk_ui_node_callbacks_t cbs;
memset(&cbs, 0, sizeof(cbs));
cbs.on_click = save_clicked;
(void)ui->node_set_callbacks(ctx, save, &cbs);
(void)ui->button_set_disabled(ctx, save, 0);
```

### 7.4 Checkbox

| Prop / API | Meaning |
| --- | --- |
| prop `"checked"` | `0` / `1` |
| `checkbox_set_checked` / `checkbox_get_checked` | Model |
| `checkbox_set_on_change` | `sk_ui_widget_bool_fn` after toggle |

```c
static void on_check(sk_ui_context_t* c, sk_ui_node_t n, i32 value, void_ptr_t user) {
    (void)c;
    (void)n;
    *(i32*)user = value;
}

i32 enabled = 1;
sk_ui_node_t cb = ui->widget_checkbox(ctx, panel, 1, "chk-enabled");
(void)ui->checkbox_set_on_change(ctx, cb, on_check, &enabled);
```

### 7.5 Slider

| Prop / API | Meaning |
| --- | --- |
| `"min"` / `"max"` / `"value"` | Range and value (f32) |
| `slider_set_value` / `slider_get_value` | Value |
| `slider_set_range` | Update min/max |
| `slider_set_on_change` | `sk_ui_widget_float_fn` while dragging / setting |

```c
static void on_vol(sk_ui_context_t* c, sk_ui_node_t n, f32 value, void_ptr_t user) {
    (void)c;
    (void)n;
    *(f32*)user = value;
}

f32 volume = 0.5f;
sk_ui_node_t sl = ui->widget_slider(ctx, panel, 0.0f, 1.0f, 0.5f, "sld-volume");
(void)ui->slider_set_on_change(ctx, sl, on_vol, &volume);
```

### 7.6 Text input

| Prop / API | Meaning |
| --- | --- |
| `"text"` | Current string |
| `"caret"` / `"sel_start"` / `"sel_end"` | Edit state (codepoints) |
| `text_input_set_text` / `get_text` | Model |
| `text_input_get_caret` / `set_selection` | Caret / selection |
| `text_input_insert` / `delete_selection` | Edit |
| `text_input_copy` / `cut` / `paste` | Clipboard (needs `set_clipboard_fns`) |

Single-line only. Keys: arrows, home/end, backspace/delete, Ctrl/Cmd+C/X/V/A. Paste/cut/copy require host clipboard hooks:

```c
static i32 clip_get(void_ptr_t user, char* buf, u32 cap, u32* out_len) {
    const char* src = (const char*)user;
    u32 n = 0u;
    u32 i;
    while (src[n] != '\0') {
        ++n;
    }
    if (n + 1u > cap) {
        return -1;
    }
    for (i = 0u; i < n; ++i) {
        buf[i] = src[i];
    }
    buf[n] = '\0';
    *out_len = n;
    return 0;
}

static i32 clip_set(void_ptr_t user, const_chr_t text) {
    (void)user;
    (void)text;
    return 0;
}

char clip_store[256];
ui->set_clipboard_fns(ctx, clip_get, clip_set, clip_store);

sk_ui_node_t field = ui->widget_text_input(ctx, panel, "player", "inp-name");
(void)ui->text_input_set_text(ctx, field, "hero");
```

### 7.7 Scroll view

| Prop / API | Meaning |
| --- | --- |
| `"scroll_x"` / `"scroll_y"` | Scroll offsets |
| `"content_width"` / `"content_height"` | Content extent for clamp / bars |
| `scroll_view_content` | Child node to parent content under |
| `scroll_view_set_scroll` / `get_scroll` | Offsets |
| `scroll_view_set_content_size` | Content size |

Wheel and drag update scroll; children outside the content box are clipped for hit-test and paint.

```c
sk_ui_node_t sv = ui->widget_scroll_view(ctx, panel, "log-scroll");
sk_ui_node_t content = ui->scroll_view_content(ctx, sv);
(void)ui->widget_label(ctx, content, "line 1", "log-0");
(void)ui->scroll_view_set_content_size(ctx, sv, 0.0f, 400.0f);
(void)ui->scroll_view_set_scroll(ctx, sv, 0.0f, 0.0f);
```

### 7.8 Image

| Prop / API | Meaning |
| --- | --- |
| prop `"texture_id"` | Host image index for `SK_UI_DRAW_TEX_IMAGE` |
| `image_set_texture` | Update id |

The GPU encoder looks up `texture_id` in `sk_ui_renderer_encode_info_t.images`.

```c
sk_ui_node_t icon = ui->widget_image(ctx, panel, 0, "img-icon");
(void)ui->image_set_texture(ctx, icon, 3);
```

### 7.9 Sample menu (reference scene)

`sample_menu_build` builds an in-game main menu using every v1 widget and style classes only (see `player/main.c` and soft-render goldens under `plugins/ui/testdata/sample/`).

```c
(void)ui->sample_menu_register_styles(ctx); /* optional; also called from build */
sk_ui_node_t menu = ui->sample_menu_build(ctx, ui->context_root(ctx));
f32 mw = 0.0f, mh = 0.0f;
ui->sample_menu_logical_size(&mw, &mh); /* 320 x 240 */
(void)menu;
```

---

## 8. Per-frame pipeline (host)

Recommended order (matches `harness_step` and the player host):

```text
poll_events / synthesize input
  → input_dispatch (0..n events)
  → style_resolve
  → layout(logical_w, logical_h)
  → layout_apply_scale(content_scale_x, content_scale_y)
  → paint(&font_params)
  → [GPU] renderer_prepare (outside render pass)
  → [GPU] begin_render_pass → renderer_encode → end_render_pass
```

```c
(void)ui->style_resolve(ctx);
(void)ui->layout(ctx, (f32)logical_w, (f32)logical_h);
(void)ui->layout_apply_scale(ctx, scale_x, scale_y);

sk_ui_paint_params_t pp;
memset(&pp, 0, sizeof(pp));
pp.font_system = fonts;
pp.font = font;
(void)ui->paint(ctx, &pp);

const sk_ui_draw_list_t* list = ui->get_draw_list(ctx);
/* list->vertices / indices / commands — physical pixels, ready for upload */
(void)list;
```

Optional GPU path (needs render device + DXC + compatible render pass):

```c
sk_ui_renderer_desc_t rd;
memset(&rd, 0, sizeof(rd));
rd.device_api = device_api;
rd.device = device;
rd.dxc = dxc_api;
rd.render_pass = ui_pass;
rd.allocator = NULL;

sk_ui_renderer_t* renderer = ui->renderer_create(&rd);
if (renderer == NULL) {
    return -1;
}

sk_ui_renderer_prepare_info_t prep;
memset(&prep, 0, sizeof(prep));
prep.cmd = transfer_cmd;
prep.draw_list = ui->get_draw_list(ctx);
prep.font_system = fonts;
(void)ui->renderer_prepare(renderer, &prep);

sk_ui_renderer_encode_info_t enc;
memset(&enc, 0, sizeof(enc));
enc.cmd = pass_cmd;
enc.draw_list = prep.draw_list;
enc.target_width = fb_w;
enc.target_height = fb_h;
enc.images.views = host_views;
enc.images.count = host_view_count;
(void)ui->renderer_encode(renderer, &enc);

ui->renderer_destroy(renderer);
```

---

## 9. Testing with the automation harness

Full contract: **`docs/ui-automation-api.md`**. Summary for authors of unit/widget tests:

### 9.1 Headless harness

```c
sk_ui_harness_desc_t desc;
memset(&desc, 0, sizeof(desc));
desc.width = 800.0f;
desc.height = 600.0f;
desc.content_scale = 1.0f;
desc.soft_render = 1; /* RGBA8 goldens */
desc.allocator = NULL;

sk_ui_harness_t* h = ui->harness_create(&desc);
if (h == NULL) {
    return -1;
}
sk_ui_context_t* ctx = ui->harness_context(h);

sk_ui_node_t root = ui->context_root(ctx);
sk_ui_node_t btn = ui->widget_button(ctx, root, "Go", "btn-go");

if (ui->harness_step(h, 1.0f / 60.0f) != 0) {
    ui->harness_destroy(h);
    return -1;
}

sk_ui_node_t found = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "btn-go");
/* found == btn when ids match */
(void)ui->action_click(ctx, found);
(void)ui->harness_step(h, 1.0f / 60.0f);

f64 t = ui->harness_time(h);
u32 frame = ui->harness_frame_index(h);
const u8* rgba = ui->harness_pixels(h);
u32 pw = 0u, ph = 0u;
ui->harness_pixel_size(h, &pw, &ph);
(void)btn;
(void)t;
(void)frame;
(void)rgba;
(void)pw;
(void)ph;

ui->harness_destroy(h);
```

**Rules:**

- Advance time only with `harness_step(delta)` — never wall clocks for UI timing.
- Actions (`action_click`, `action_type_text`, `action_scroll`, `action_focus`) go through `input_dispatch`.
- Queries: `query_by_test_id` / `query_by_class` / `query_by_widget` / `query_by_text` (+ `query_all_*`).
- Accessors: `node_is_visible`, `node_is_enabled`, `node_get_visible_text`, `node_get_abs_rect`, `node_get_computed_style`.

### 9.2 Recommended test pattern

1. Create harness (`soft_render` if asserting pixels).
2. Build widgets with **stable test ids**.
3. `harness_step(delta)` — style + layout + scale + paint (+ soft-render).
4. Query and assert visibility / layout / text / state.
5. `action_*` then `harness_step` again; assert model and optional pixels / draw list.
6. Optional: `harness_set_font` + fixture TTF under `plugins/ui/testdata/` for text goldens.

Shipped coverage lives under `#ifdef SK_TESTS` in `plugins/ui/*.c` (layout, style, input, widgets, automation, sample menu). GPU goldens: `tests/integration/ui_render.c` and `scripts/regen-ui-goldens.sh`.

### 9.3 Golden image comparison (review and bless)

Use `cpu_image_compare` / `cpu_image_compare_golden` on `sk_ui_api_t` (APX-229) after capture or soft-render:

```c
sk_ui_image_compare_params_t p;
sk_ui_image_compare_stats_t stats;
memset(&p, 0, sizeof(p));
p.channel_tolerance = 2;      /* per-channel abs delta still “equal” */
p.max_diff_fraction = 0.0f;   /* fraction of pixels allowed beyond tolerance */
p.name = "my_scene";          /* failure artifact base name */
p.update_golden = 0;          /* never default to 1 */

i32 rc = ui->cpu_image_compare_golden(&img, "plugins/ui/testdata/my_scene.png",
                                      &p, sk_filesystem_api(), &stats);
/* OK / MISMATCH / SIZE_MISMATCH / MISSING_GOLDEN */
```

**On mismatch** the helper writes three PNGs under the shared test-artifact root (`SK_TEST_ARTIFACT_DIR` or `build/test-artifacts`): `{name}_actual.png`, `{name}_expected.png`, and `{name}_diff.png` (dim actual + red failing pixels). Logs include differ count, max channel delta, and the bounding box of the differing region. Size mismatches fail immediately with a clear message (no silent pixel scan).

**Review and bless (update goldens):**

1. Open the three failure artifacts and confirm the change is intentional.
2. Regenerate deliberately — **never automatic**:
   - `params.update_golden = 1`, or
   - `SK_UI_REGEN_GOLDENS=1` (same env used by `scripts/regen-ui-goldens.sh`).
3. Re-run the test so the golden PNG under `plugins/ui/testdata/` (or your path) is overwritten.
4. Visually check the new golden, then commit the PNG with the code change.

Do not leave `update_golden=1` or the env set in CI; blessing is opt-in only.

### 9.4 Structural image assertions (beyond goldens)

Golden comparison needs a committed golden and fails on any pixel difference. For layout / theme regressions that keep individual pixels moving, use the structural assertion APIs (APX-230) on `sk_ui_api_t` — they check **structure** and need no golden. Regions are half-open pixel rects `{x0, y0, x1, y1}` clamped to the image; colors are `sk_ui_color_match_t` (per-channel abs tolerance, optional alpha).

| Assertion | API | Catches |
| --- | --- | --- |
| Uniform fill | `cpu_image_assert_solid` | A panel/button that is no longer a solid block (torn fill, wrong tint) |
| Coverage | `cpu_image_assert_coverage` | A widget that shrank, bled, or vanished (fraction of matching pixels out of range) |
| Bounding box | `cpu_image_find_bbox` + `cpu_image_assert_bbox` | Drift and misalignment — a shifted widget keeps its colors, so pixel sampling misses it |
| Dominant colors | `cpu_image_histogram` + `cpu_image_assert_histogram` | Wrong theming (unexpected colors) and missing widgets (expected colors absent), with approximate proportions |
| Region hash | `cpu_image_region_hash` + `cpu_image_assert_region_hash` | Cheap change detection: stable FNV-1a 64-bit per region; run before heavier asserts |

```c
sk_ui_region_t r = { 0, 0, 320, 240 };
sk_ui_color_match_t c = { 0 }; /* r=g=b=a=0, tolerance=0 */
c.r = 20; c.g = 24; c.b = 28; c.a = 255; c.tolerance = 2;

/* 1. A status bar is uniformly dark gray. */
TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK,
    ui->cpu_image_assert_solid(&img, r, &c, NULL));

/* 2. The play button fills ~25% of its region. */
TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK,
    ui->cpu_image_assert_coverage(&img, r, &c, 0.20f, 0.30f, NULL));

/* 3. The accent bar still sits at its exact position/size. */
sk_ui_bbox_expected_t e = { 0 };
e.min_x = 40; e.min_y = 16; e.max_x = 279; e.max_y = 23;
e.position_tolerance = 1; e.size_tolerance = 1; e.min_pixels = 200;
TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK,
    ui->cpu_image_assert_bbox(&img, r, &c, &e, NULL));

/* 4. Theme check: dark gray ~70%, accent ~25%, rest <= 5%. */
sk_ui_hist_expectation_t ex[2];
sk_ui_color_match_t accent = c; accent.r = 220; accent.g = 120; accent.b = 0; /* orange */
ex[0].color = c; ex[0].min_fraction = 0.65f; ex[0].max_fraction = 0.75f;
ex[1].color = accent; ex[1].min_fraction = 0.20f; ex[1].max_fraction = 0.30f;
TEST_ASSERT_EQUAL_INT(SK_UI_IMAGE_ASSERT_OK,
    ui->cpu_image_assert_histogram(&img, r, NULL, ex, 2, NULL));

/* 5. Cheap per-frame change gate. */
u64 h = 0;
ui->cpu_image_region_hash(&img, r, 0, &h);
if (h != last_hash) { /* region changed → run the heavy asserts */ }
```

**Every failure logs the actual measured values** (nonmatching count, fraction, bbox + deltas, dominant colors with proportions, hash) — a CI log alone is actionable. Return codes: `SK_UI_IMAGE_ASSERT_OK` (0), `FAIL` (1), `ERROR` (-1, bad args such as a degenerate region). NULL `params`/`out_stats` use defaults / are optional.

---

## 10. v1 limitations (deliberately absent)

These are **not bugs** — they are out of scope for the basic UI system. Each row points at the intended follow-up.

| Absent in v1 | Why / current behavior | Intended follow-up |
| --- | --- | --- |
| **msdfgen / MSDF font atlases** | v1 uses FreeType **bitmap** glyphs into an R8 atlas. Sharp scaling is content-scale re-raster, not distance fields. | Optional Font cooker / resource handler with MSDF (or multi-channel SDF) when high-quality scalable UI text is required; keep bitmap path for tools/tests. Design note: `docs/ui-system-design.md` §3.4. |
| **Complex text shaping / BiDi / RTL** | UTF-8 LTR codepoint walk only; no HarfBuzz, no bidirectional reordering, no complex scripts, no required kerning. | Integrate a shaping library (HarfBuzz) behind the font measure/paint path; add direction + locale to computed style; extend text input caret model for clusters. |
| **Standalone UI tester runtime** | Automation **API + headless harness** ship inside `sk_ui_api_t` (`query_*`, `action_*`, `harness_*`). There is no separate Selenium-style driver binary or scripted recorder. | Build an external tester process/CLI that loads plugins and drives `harness_*` / `input_dispatch` (contract in `docs/ui-automation-api.md`). |
| **Full ImGui feature parity** (docking, multi-viewport, tables, menus, tree views, property grids, ImGuizmo, demos) | v1 widgets are panel/view/label/button/checkbox/slider/text_input/scroll_view/image only. Editor still uses a dual stack (`docs/ui-editor-migration.md`). | Port panels incrementally onto sk-ui; keep docking/chrome on ImGui (or a future dock host) until a dedicated layout-shell milestone; do not block product UI on parity. |
| **Runtime style file loading** | Classes are registered in C via `style_class_register` / variants. No `.css` / JSON theme load path, no hot-reload of sheets from disk. | Theme resource type + serializer (repository / assets) and a loader that fills the class registry; optional editor theme browser. |
| **Shaders as assets rather than source literals** | UI GPU renderer compiles **embedded HLSL strings** through DXC at `renderer_create`. No cooked SPIR-V asset or shader graph. | Move DrawList2D HLSL into the asset pipeline (precompile or load SPIR-V), register as shaders via render device; keep DXC path for dev iteration. |

### 10.1 Additional known gaps (shorter list)

- No `begin_frame` / `end_frame` on the API table — host sequences passes (or use `harness_step`).
- No CSS Grid, baseline align, or z-index stacking beyond tree paint order.
- Text input is **single-line**; rich text / multi-style runs are out of scope.
- Image widgets bind host texture **ids**; PNG load/cook belongs to resource assets, not UI.
- Platform window input may still need host translation into `sk_ui_input_event_t` until a shared input module is complete.

---

## 11. Quick reference

| Task | Entry points |
| --- | --- |
| Get API | `get_api(ctx, SK_UI_API_TYPE_ID)` |
| Root | `context_create` → `context_root` |
| Tree | `node_create` / widget factories / hierarchy APIs |
| Style | `style_class_register`, `node_add_class`, `node_set_inline_style`, `style_resolve` |
| Layout | `node_set_layout_style` or style masks → `layout` → `layout_apply_scale` |
| Fonts | `font_system_create`, `font_load_*`, `paint` params |
| Input | `input_dispatch`, `node_set_callbacks`, focus APIs |
| Widgets | `widget_*`, typed setters, `*_set_on_change` |
| GPU | `renderer_create` / `prepare` / `encode` |
| Tests | `harness_*`, `query_*`, `action_*` |

**Header:** `plugins/ui/ui.h`  
**Plugin:** `plugins/ui/` (SHARED, statically links `sk-core`, FreeType, stb_rect_pack)  
**Sample host:** `player/main.c` (`sample_menu_build`)  
**Editor proof:** `editor/console_panel.c` + `editor/editor_ui_host.c`
