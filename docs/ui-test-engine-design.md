# Code-driven UI test engine design (imgui_test_engine equivalent)

Task: **APX-258**. Goal: **ui integration test improvements**.

This document specs a **code-driven UI test engine** for sk-ui, analogous in
role to Dear ImGui’s `imgui_test_engine`: tests are ordinary C functions that
locate widgets, inject input, step frames under test control, and assert
model/layout/pixel state — without a real OS event loop or human driver.

It is **not** a Selenium binary, scripted recorder, or accessibility tree.
Those remain out of scope (see `docs/ui-automation-api.md` §1).

**Status:** design complete (APX-258). Foundation implemented (APX-259): item
registry, id/path lookup, `test_engine_step` / `yield_frames` / `run_until`
with timeout errors. Synthetic input injection implemented (APX-260): mouse
move/hover, click (all buttons + double-click), press/release, drag with
intermediate motion frames, scroll wheel, keys with modifiers, and text entry
— all via `input_dispatch`.

---

## 0. Executive summary

| Topic | Decision |
| --- | --- |
| Item reference (primary) | **Stable string test ids** on nodes (`node_set_id` / widget `id` param) via `query_by_test_id` / `find_by_id` |
| Item reference (secondary) | Class / widget type / exact visible text (`query_by_*`); optional **id path** built from ancestor ids (not ImGui label paths) |
| Frame control | Engine owns the frame loop: `style_resolve → layout → layout_apply_scale → paint → (soft-render)` via existing `harness_step` |
| Input | Always through `input_dispatch` (actions already do this) |
| Registration | `SK_UI_TEST` macros → process-local registry; runner creates harness, invokes body, tears down |
| Non-test core changes | **Minimal** — stable identity already exists (see §7). No scope-change escalation for identity. |

**Prerequisite layers already shipped**

| Layer | Location | Role |
| --- | --- | --- |
| Tree + ids | `plugins/ui/ui.c`, `ui_internal.h` | Generation handles, `id_map`, `node_set_id` / `find_by_id` |
| Widgets | `plugins/ui/widgets.c` | Factories assign explicit or auto ids; prop `"widget"`; model getters |
| Input | `plugins/ui/input.c` | Hit-test, focus, `input_dispatch` |
| Automation API | `plugins/ui/automation.c`, `docs/ui-automation-api.md` | `query_*`, `action_*`, `harness_*` |
| Soft-render / goldens | `automation.c`, `image_compare.c`, `image_structure.c` | Pixels + structural asserts |
| GPU capture harness | `tests/integration/ui_capture_harness.*` | One-shot offscreen Vulkan frames |
| Unit registration | `core/test.h` | `SK_TEST` constructor registry |

The **test engine** sits **above** the automation harness: multi-step scenarios,
waits, named tests, and a unified assertion surface. It reuses — does not
replace — `harness_step` / `query_*` / `action_*`.

```text
  SK_UI_TEST body  (or integration SK_TEST)
        │
        ▼
  sk_ui_test_engine_t   ← NEW (this design)
        │  open/step/yield/assert/close
        ▼
  sk_ui_api_t  query_* / action_* / harness_*   ← EXISTING (APX-137)
        │
        ├─► find_by_id, node classes, props, widget model
        ├─► style_resolve / layout / paint
        └─► input_dispatch  (single ingress; never bypass)
```

---

## 1. Goals and non-goals

### 1.1 Goals

1. **Code-driven scenarios** — click, type, scroll, focus, multi-frame waits,
   assert widget model and layout without wall clocks or a window.
2. **Stable item addressing** — primary reference is explicit test ids; document
   when label/text lookup is acceptable.
3. **Test-controlled frames** — no GLFW/OS loop; engine advances time and
   pipeline phases explicitly.
4. **Clear injection points** — list every non-test production file that must
   change for implementation, and what can stay pure test-side.
5. **Assertion surface** — item existence, visibility, enabled, text, values,
   geometry, optional soft-render / structural image checks.
6. **Registration / lifecycle** — discoverable tests, isolated harness per test
   (or explicit shared context when intentional).

### 1.2 Non-goals (v1 engine)

- ImGui ID stack / `PushID` / `##` hash paths (retained UI is not immediate-mode).
- Coroutine/fiber test yield (imgui_test_engine’s stack-switch style). Use
  explicit `engine_step` / `engine_yield_frames` calls instead.
- Full scripted recorder / playback DSL.
- External Selenium-compatible process.
- Driving live editor/player windows from the engine (optional later host mode;
  v1 is headless harness only).
- Replacing GPU integration capture (`ui_capture_harness`) — that remains the
  golden/GPU path; the engine is the **interaction** path.

---

## 2. How tests reference UI items

### 2.1 Primary: stable item ids (required for production-quality tests)

**Mechanism (already implemented)**

| Concept | Implementation |
| --- | --- |
| Id storage | `ui_node_slot_t.id` + `sk_ui_context_t.id_map` (`ui_internal.h`) |
| Set / get | `node_set_id` / `node_get_id` (`ui.c`) |
| Lookup | `find_by_id` → O(1) hash; `query_by_test_id(ctx, scope, id)` for scoped |
| Widget factories | Last arg is optional `id`; see `ui_widget_assign_id` in `widgets.c` |

```c
sk_ui_node_t btn = ui->widget_button(ctx, parent, "Save", "btn-save");
sk_ui_node_t n   = ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "btn-save");
/* n == btn */
```

**Contract**

- Test ids are **unique per context** (`node_set_id` rejects duplicates with `-2`).
- **Explicit ids are the stable address.** Prefer semantic names:
  `player-click`, `console-clear`, `menu-play`.
- Auto-ids when `id == NULL` (`"ui-button-1"`, … via `widget_id_seq`) exist so
  every node remains findable, but they are **not stable** across rebuilds,
  factory call reordering, or partial tree rebuilds. **Do not write tests that
  depend on auto-ids.**
- Editor / player production UI already passes explicit ids (e.g.
  `player/main.c`, `editor/console_panel.c`); tests should mirror that.

**Identity status (escalation gate)**

| Question | Answer |
| --- | --- |
| Does the widget system have stable item identity? | **Yes.** |
| Scope change required before implementation? | **No** for identity. |
| Escalation? | **Not required** for adding a test engine on top of existing ids. |

If a future widget factory omits id assignment entirely, that would regress
automation and **must** be treated as a bug, not as “use label paths only.”

### 2.2 Secondary: class, widget type, visible text

| Query | Use when | Risk |
| --- | --- | --- |
| `query_by_class` / `query_all_by_class` | Structural role (`ui-button`) under a scope | Multiple matches |
| `query_by_widget` / `query_all_by_widget` | Prop `"widget"` (`"button"`, `"text_input"`, …) | Multiple matches |
| `query_by_text` / `query_all_by_text` | Exact prop `"text"` | Ambiguous; brittle on copy changes |

**Engine policy:** resolve by test id first. Use text/class only inside a
**scoped** parent that is itself id-stable, or when counting matches.

### 2.3 Not supported: ImGui label paths

imgui_test_engine often addresses items via the ImGui ID stack and labels
(`"Open##file"`, `"//Window/Button"`). sk-ui is **retained-mode**:

- There is **no** per-frame ID stack, no `##` disambiguation, no automatic
  label→id hash.
- Visible labels are props, not identity.

**Do not** implement ImGui label-path resolution as the primary addressing
scheme. That would be a different product.

### 2.4 Optional engine convenience: id paths

**Proposal (test-engine layer only, no tree rewrite):** a path of slash-separated
**node ids** from root or a scope:

```text
"player-card/player-actions/player-click"
```

Resolution algorithm:

1. Split on `/` (no empty segments; no `//` ImGui root magic).
2. Starting from `scope` (or context root), walk children (or use `find_by_id`
   for the last segment when ids are globally unique — preferred fast path).
3. Because ids are already globally unique in a context, path resolution is
   **optional sugar** for readability; `query_by_test_id(..., "player-click")`
   remains the canonical form.

**No production change** required for paths if implemented as pure query sugar
in the engine file.

### 2.5 Handles after structural changes

`sk_ui_node_t` is slot + generation. Destroying a node invalidates the handle.
After rebuild:

1. Re-query by test id (never cache handles across destroy).
2. Engine helpers always re-resolve by id before action/assert unless the test
   holds a handle known still live.

---

## 3. Frame stepping under test control

### 3.1 Problem with a real event loop

Hosts (`player/main.c`, `editor/main.c`) run:

```text
while (running) {
  poll_events();                 // OS / GLFW
  // translate → input_dispatch
  style_resolve → layout → layout_apply_scale → paint
  // GPU present
}
```

Wall time, display refresh, and event ordering make UI interaction tests flaky.

### 3.2 Existing deterministic step (foundation)

`ui_harness_step_impl` in `plugins/ui/automation.c` already implements one
controlled frame:

1. Advance stable clock: `time_sec += delta`, `frame_index++` (no wall clock).
2. `style_resolve`
3. `layout(width, height)` (logical)
4. `layout_apply_scale(content_scale, content_scale)`
5. `paint` (optional font from `harness_set_font`)
6. Optional soft-render into RGBA8 (`harness_pixels`)

Contract: **tests must never use OS clocks** for UI timing — only harness
deltas. Documented in `docs/ui-automation-api.md` §5–6.

### 3.3 Engine frame model

The test engine **does not** introduce a second pipeline. It owns:

| Engine op | Behavior |
| --- | --- |
| `engine_open(desc)` | `harness_create`; store `ui` + harness + ctx |
| `engine_step(e, dt)` | `harness_step` |
| `engine_yield_frames(e, n, dt)` | Loop `n` steps (default `dt = 1/60`) |
| `engine_time` / `engine_frame` | Forward harness clock |
| `engine_close(e)` | `harness_destroy` |

**Actions and steps are separate:** `action_click` synthesizes input
immediately via `input_dispatch` but does **not** advance style/layout/paint.
Typical pattern:

```text
step  → layout valid for hit-test
click → input_dispatch (handlers, state flags)
step  → style (hover/active), layout, paint, soft-render
assert
```

This matches how production hosts process input before the next full frame.

### 3.4 Multi-frame waits (without fibers)

imgui_test_engine often yields mid-test across frames via coroutines. sk-ui v1
uses **explicit waits**:

```c
/* Wait until node exists and is visible, or timeout frames. */
i32 sk_ui_test_wait_visible(sk_ui_test_engine_t* e, const_chr_t test_id,
                            u32 max_frames, f32 dt);

/* Wait until checkbox is checked, text equals, etc. */
i32 sk_ui_test_wait_checkbox(sk_ui_test_engine_t* e, const_chr_t test_id,
                             i32 checked, u32 max_frames, f32 dt);
```

Each wait: `for (i = 0; i < max_frames; ++i) { if (cond) return 0; engine_step; }`
then fail. No threads, no wall `sleep`.

### 3.5 Optional host-driven mode (later)

A future mode could attach to a live `sk_ui_context_t` owned by player/editor
and only inject input + force one layout/paint without owning create/destroy.
**v1 is harness-owned only.** Host mode would need a documented “frame barrier”
callback from the host (injection list §7.2) — not required for headless
integration improvements.

---

## 4. Injection points in the existing UI core

### 4.1 Principle

Prefer **zero behavioral change** to production paths. The automation layer
already shares `input_dispatch` with hosts. The test engine should be additive
files + thin `sk_ui_api_t` entry points (appended at the **end** of the vtable —
see versioning note in `docs/ui-automation-api.md` §9).

### 4.2 Required vs optional non-test changes

#### A. Stable identity — **no change required** (escalation: clear)

| File | Current state | Engine need |
| --- | --- | --- |
| `plugins/ui/ui.c` | `node_set_id`, `find_by_id`, unique `id_map` | Use as-is |
| `plugins/ui/ui_internal.h` | `id`, `id_map`, `widget_id_seq` | Use as-is |
| `plugins/ui/widgets.c` | `ui_widget_assign_id` on all factories | Use as-is; keep assigning ids |

**Flag:** If implementation discovers a widget factory that **skips**
`ui_widget_assign_id` / `node_set_id`, that is a **bugfix** in `widgets.c`,
not a redesign. Adding a brand-new parallel identity system would be a **scope
change** and must be escalated — **do not do that**.

#### B. Frame pipeline — **no change required** for headless engine

| File | Role |
| --- | --- |
| `plugins/ui/automation.c` | `harness_step` pipeline already correct |
| `plugins/ui/style.c` | `style_resolve` |
| `plugins/ui/clay_adapter.c` / layout | `layout` |
| `plugins/ui/paint.c` | `paint` / draw list |
| `plugins/ui/input.c` | `input_dispatch`, hit-test, focus |

#### C. Likely small production extensions (non-test engine code)

These are the **only** production-side deltas expected for a useful v1 engine.
Each is a small additive API, not a rewrite.

| # | File | Change | Why |
| --- | --- | --- | --- |
| 1 | `plugins/ui/ui.h` | Append test-engine types + function pointers on `sk_ui_api_t` (or a nested `sk_ui_test_engine_api_t` registered separately — prefer **append to `sk_ui_api_t`** for one table) | Hosts/tests obtain engine via same `get_api` |
| 2 | `plugins/ui/ui.c` | Wire new function pointers in the static API table | Registration of implementations |
| 3 | `plugins/ui/automation.c` **or new** `plugins/ui/test_engine.c` | Implement engine open/step/wait/assert helpers; may call existing `ui_*_impl` | Keeps automation file from growing unbounded; new TU preferred |
| 4 | `plugins/ui/ui_internal.h` | Declare `ui_test_engine_*_impl` if split TU | Match existing pattern (`ui_action_*_impl`, `ui_harness_*_impl`) |
| 5 | `plugins/ui/CMakeLists.txt` | No special case if GLOB; confirm new `.c` is picked up | `file(GLOB_RECURSE …)` already covers `plugins/ui/*.c` |
| 6 | `plugins/ui/input.c` (optional) | `action_key` helper: synthesize `SK_UI_INPUT_KEY` at focused node | Engine completeness; actions today cover click/type/scroll/focus only |
| 7 | `plugins/ui/automation.c` (optional) | `action_key` / `action_double_click` next to existing actions | Same input_dispatch path |
| 8 | `plugins/ui/widgets.c` (optional, policy only) | Debug assert or test that every factory assigns an id; document prefixes | Hardens identity contract; not a new id system |

**Not required for v1**

| File | Why skip |
| --- | --- |
| `plugins/ui/paint.c` | No ImGui-style “item status” table needed; retained tree is queryable after layout |
| `plugins/ui/clay_adapter.c` | Layout solver unchanged |
| `plugins/ui/render.c` / `capture.c` | GPU path stays on integration harness |
| `player/main.c` / `editor/*` | Headless engine does not inject into live hosts |
| `core/test.h` | Can reuse `SK_TEST` without core changes; optional `SK_UI_TEST` can live in `ui.h` under `SK_TESTS` |
| `plugins/platform_window/*` | No OS events in headless mode |

#### D. Test-only / integration files (expected)

| File | Change |
| --- | --- |
| `plugins/ui/test_engine.c` (new) | Engine implementation + unit tests under `#ifdef SK_TESTS` |
| `tests/integration/ui_test_engine.c` (new, optional) | Multi-step interaction scenarios on harness (no GPU) or combined with capture |
| `tests/integration/CMakeLists.txt` | Add source if new integration TU |
| `docs/ui-automation-api.md` | Short “see also” link to this design after implementation |
| `docs/ui-plugin.md` | Point “standalone tester” follow-up at engine when shipped |

### 4.3 Injection diagram

```text
Production (unchanged control flow)
  Host poll_events → input_dispatch → style → layout → paint → GPU

Headless test engine (v1)
  engine_open
    └─ harness_create → context_create
  test builds tree (widget_* with explicit ids)
  engine_step
    └─ harness_step → style → layout → scale → paint → soft-render
  action_* / engine_click_id
    └─ input_dispatch only
  engine_step again
  assert_* / Unity TEST_ASSERT_*
  engine_close
    └─ harness_destroy
```

**Hard rule:** no second input path, no test-only hit-test bypass, no writing
widget model fields from asserts without going through public setters/actions.

---

## 5. Test registration and lifecycle

### 5.1 Two tiers

| Tier | Mechanism | Where | Use |
| --- | --- | --- | --- |
| Unit / in-plugin | `SK_TEST` (existing) or `SK_UI_TEST` (thin wrapper) | `plugins/ui/*.c` under `#ifdef SK_TESTS` | Fast, no GPU, soft-render optional |
| Integration | `SK_TEST` in `tests/integration/` | `sk-integration-tests` binary | GPU capture, multi-plugin load |

v1 engine targets **tier 1** first (in-plugin + pure harness). Tier 2 can call
the same engine APIs after loading `sk-ui`.

### 5.2 Proposed `SK_UI_TEST` (optional sugar)

```c
/* plugins/ui/ui_test.h or bottom of ui.h under SK_TESTS */
#define SK_UI_TEST(name) SK_TEST(ui_te_##name)
```

Bodies remain ordinary Unity tests. The engine does **not** need a separate
process-wide registry unless we want filtering (`--ui-test=pattern`). Optionaling
can wait; `sk_test_register` already lists names.

### 5.3 Lifecycle (per test)

```text
1. SETUP
   - sk_ui_test_engine_desc_t: width, height, content_scale, soft_render, font?
   - engine = ui->test_engine_create(&desc)   // or harness_create + wrapper
   - ctx = engine_context(engine)
   - optional: widgets_register_defaults, sample_menu_build, custom tree

2. RUN
   - engine_step(engine, 1/60)     // establish layout
   - query / action / wait loops
   - engine_step after interactions
   - assertions (Unity + engine helpers)

3. TEARDOWN
   - engine_destroy(engine)        // destroys context, pixels
   - no shared global UI context across tests
```

**Isolation:** each test gets a fresh context. Shared statics in widget code
must not leak (already true for `context_create`).

### 5.4 Failure reporting

- Prefer Unity (`TEST_ASSERT_*`) so plugin `sk_plugin_run_tests` aggregates
  failures.
- Engine helpers return `0` / non-zero and may `TEST_FAIL_MESSAGE` when
  compiled under `SK_TESTS` with an active Unity context, **or** only return
  codes for use outside Unity — pick **return codes + caller asserts** for
  clarity (matches existing `action_click` style).

### 5.5 Example test shape

```c
SK_TEST(ui_te_click_increments_counter)
{
	const sk_ui_api_t* ui = ui_get_api_table();
	sk_ui_test_engine_desc_t d = {0};
	sk_ui_test_engine_t* e;
	sk_ui_context_t* ctx;
	sk_ui_node_t btn, lbl;
	i32 clicks = 0;

	d.width = 400.0f;
	d.height = 300.0f;
	d.soft_render = 0;
	e = ui->test_engine_create(&d);
	TEST_ASSERT_NOT_NULL(e);
	ctx = ui->test_engine_context(e);

	/* Build scene with stable ids */
	btn = ui->widget_button(ctx, ui->context_root(ctx), "Go", "te-go");
	lbl = ui->widget_label(ctx, ui->context_root(ctx), "0", "te-count");
	/* wire on_click → clicks++ and update label text … */

	TEST_ASSERT_EQUAL_INT(0, ui->test_engine_step(e, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->test_engine_click(e, "te-go"));
	TEST_ASSERT_EQUAL_INT(0, ui->test_engine_step(e, 1.0f / 60.0f));
	TEST_ASSERT_EQUAL_INT(1, clicks);
	TEST_ASSERT_EQUAL_STRING("1", ui->node_get_visible_text(ctx,
	    ui->query_by_test_id(ctx, SK_UI_NODE_INVALID, "te-count")));

	ui->test_engine_destroy(e);
}
```

---

## 6. Assertion surface

### 6.1 Layers (compose; do not duplicate)

| Layer | API | Asserts |
| --- | --- | --- |
| L0 Unity | `TEST_ASSERT_*` | Generic |
| L1 Automation accessors | `node_is_visible`, `node_is_enabled`, `node_get_visible_text`, `node_get_abs_rect`, `node_get_state`, `node_get_computed_style` | Item state after layout |
| L2 Widget model | `checkbox_get_checked`, `slider_get_value`, `text_input_get_text`, `scroll_view_get_scroll` | Control values |
| L3 Actions outcome | click count callbacks, focus_get | Interaction |
| L4 Soft pixels | `harness_pixels` + `cpu_image_compare` / `_golden` | Visual goldens (CPU) |
| L5 Structure | `cpu_image_assert_bbox`, `_histogram`, `_coverage`, `_region_hash` | Diagnosable visual |
| L6 GPU capture | `sk_ui_capture_harness_capture` | Integration goldens (Vulkan) |

The **test engine** standardizes L1–L3 (and optional L4) behind id-keyed
helpers so tests do not re-plumb query + null checks every time.

### 6.2 Proposed engine assertion helpers

All resolve `test_id` via `query_by_test_id`; return `0` ok, non-zero fail.
Callers wrap with `TEST_ASSERT_EQUAL_INT(0, …)`.

| Helper | Meaning |
| --- | --- |
| `test_assert_exists(e, id)` | Live node with id |
| `test_assert_not_exists(e, id)` | No live node |
| `test_assert_visible(e, id)` / `_hidden` | `node_is_visible` |
| `test_assert_enabled(e, id)` / `_disabled` | `node_is_enabled` |
| `test_assert_text(e, id, utf8)` | Exact `node_get_visible_text` |
| `test_assert_focused(e, id)` | `focus_get` equals node |
| `test_assert_checkbox(e, id, checked)` | `checkbox_get_checked` |
| `test_assert_slider(e, id, value, eps)` | `slider_get_value` |
| `test_assert_input_text(e, id, utf8)` | `text_input_get_text` |
| `test_assert_scroll(e, id, x, y, eps)` | `scroll_view_get_scroll` |
| `test_assert_rect(e, id, x, y, w, h, eps)` | Abs border box after layout |
| `test_assert_class(e, id, class_name)` | `node_has_class` |
| `test_assert_hover` / `_active` (optional) | State flags bits |

### 6.3 Id-keyed actions (thin wrappers)

| Helper | Implementation |
| --- | --- |
| `test_click(e, id)` | resolve → `action_click` |
| `test_type(e, id, text)` | resolve → `action_type_text` |
| `test_scroll(e, id, sx, sy)` | resolve → `action_scroll` |
| `test_focus(e, id)` | resolve → `action_focus` |
| `test_key(e, key, down, mods)` (optional) | new `action_key` → `input_dispatch` |

### 6.4 Pixel asserts

Engine does not reimplement goldens. When `soft_render != 0`:

```c
const u8* px = ui->harness_pixels(ui->test_engine_harness(e));
/* cpu_image_compare_golden / cpu_image_assert_bbox … */
```

Or expose `test_engine_pixels` / `test_engine_draw_list` as pass-throughs.

### 6.5 What not to assert

- Auto-generated ids (`ui-button-N`).
- Exact pointer equality of nodes across destroy/recreate.
- Wall-clock durations.
- Unscoped `query_by_text` for unique identity in large trees.

---

## 7. Explicit non-test engine code change checklist

### 7.1 Identity escalation (mandatory review item)

| Check | Result |
| --- | --- |
| Stable item identity present? | **YES** — `node_set_id` / `id_map` / widget `id` params / `query_by_test_id` |
| Label-path system present? | **NO** — not required; secondary text/class queries only |
| Scope change to add identity? | **NO — do not escalate for identity** |
| Escalate if… | Someone proposes replacing string ids with ImGui-style hashed stacks, or removing ids from widgets |

### 7.2 Complete production (non-test) touch list for implementation

Files that may receive **non-`#ifdef SK_TESTS`** code:

1. **`plugins/ui/ui.h`** — types: `sk_ui_test_engine_t`, `sk_ui_test_engine_desc_t`; API pointers: create/destroy/context/step/clock/click-by-id/assert helpers (or a minimal set + macros in a test header).
2. **`plugins/ui/ui.c`** — fill new vtable slots; no behavior change to existing slots.
3. **`plugins/ui/test_engine.c`** (new) — engine implementation.
4. **`plugins/ui/ui_internal.h`** — impl declarations for the new TU.
5. **`plugins/ui/automation.c`** — only if actions are extended (`action_key`) or engine is embedded here instead of a new file.
6. **`plugins/ui/input.c`** — only if key synthesis needs shared helpers beyond what actions can do with public `input_dispatch`.
7. **`plugins/ui/widgets.c`** — only for identity policy hardening (every factory must keep calling `ui_widget_assign_id`); **no new identity scheme**.
8. **`plugins/ui/CMakeLists.txt`** — only if GLOB is replaced later; today GLOB picks up new `.c` automatically.
9. **`plugins/ui/plugin_entry_point.c`** — **no change** expected (API table is static in `ui.c`).

Files that must **not** be rewritten for this feature:

- `clay_adapter.c`, `paint.c`, `render.c`, `capture.c`, `font.c`, `style.c` (except if a bug is found unrelated to addressing).
- `player/main.c`, `editor/*` for v1 headless engine.
- `core/*` (unless adding a generic test filter later — out of scope).

### 7.3 Test-side files (implementation phase)

- `plugins/ui/test_engine.c` — `#ifdef SK_TESTS` scenarios (click, type, wait, multi-step).
- Optional `tests/integration/ui_test_engine_interactions.c` — longer flows.
- Update `docs/ui-automation-api.md` with a “Test engine” section pointing here.

---

## 8. Relationship to existing docs and harnesses

| Doc / component | Relationship |
| --- | --- |
| `docs/ui-automation-api.md` | **Contract** for query/action/harness — engine is a consumer |
| `docs/ui-plugin.md` § automation | User-facing API sketch; engine fills “standalone tester runtime” gap partially (in-process) |
| `docs/ui-snapshot-integration-tests.md` | GPU PNG inventory — orthogonal; use for visual reg, not click scripts |
| `tests/integration/ui_capture_harness.*` | One-frame GPU capture; engine is multi-frame interaction |
| imgui_test_engine (upstream) | Conceptual peer: register tests, find items, step frames, assert — **not** a port of its ID stack or coroutine runtime |

---

## 9. Phased implementation plan (post-design)

| Phase | Deliverable | Non-test files |
| --- | --- | --- |
| **P0** | This design (APX-258) | docs only |
| **P1** | `test_engine.c` + create/step/destroy + id-keyed click/type + assert_exists/visible/text | `ui.h`, `ui.c`, `ui_internal.h`, new `.c` |
| **P2** | Waits, checkbox/slider/input asserts, scroll | same + maybe `action_key` in automation/input |
| **P3** | Soft-render pass-through helpers; sample multi-step tests for sample_menu / player tree fixtures | test code + optional widgets policy |
| **P4** | Optional host-attached mode | host injection API (new design spike; escalate if hosts need frame hooks) |

---

## 10. Verification checklist (this design task)

| Criterion | Met? |
| --- | --- |
| Item reference: stable ids vs label paths | §2 — ids primary; no ImGui label paths; text/class secondary |
| Frame stepping under test control | §3 — builds on `harness_step`; no real event loop |
| Injection points in UI core | §4, §7.2 — concrete files |
| Registration / lifecycle | §5 |
| Assertion surface | §6 |
| Every non-test engine change listed | §7.2 |
| Stable identity missing? | **No** — flagged as present; **no escalation** |
| Concrete file-level touch points | §4.2, §7.2, §7.3 |

### 10.1 File-level touch point index (quick)

**Production (implementation):**

- `plugins/ui/ui.h`
- `plugins/ui/ui.c`
- `plugins/ui/ui_internal.h`
- `plugins/ui/test_engine.c` (new)
- `plugins/ui/automation.c` (optional actions)
- `plugins/ui/input.c` (optional key helper)
- `plugins/ui/widgets.c` (identity policy only)

**Already sufficient (read/use, no identity rewrite):**

- `plugins/ui/ui.c` (`id_map`, `node_set_id`)
- `plugins/ui/widgets.c` (`ui_widget_assign_id`)
- `plugins/ui/automation.c` (`query_*`, `action_*`, `harness_*`)
- `plugins/ui/input.c` (`input_dispatch`)
- `plugins/ui/style.c`, `paint.c`, layout path

**Tests / docs:**

- `plugins/ui/test_engine.c` (`SK_TEST`s)
- `tests/integration/*` (optional)
- `docs/ui-test-engine-design.md` (this file)
- `docs/ui-automation-api.md` (link after implement)

---

## 11. Open questions (non-blocking for P1)

1. **Separate API type id** (`SK_UI_TEST_ENGINE_API_TYPE_ID`) vs append to `sk_ui_api_t`? Recommendation: **append** for simplicity; one table.
2. **Should `SK_UI_TEST` live in core?** Recommendation: no — keep under UI / `SK_TESTS` only.
3. **Double-click / drag actions?** Defer to P2; compose from pointer events if needed.
4. **Host mode for editor console panel e2e?** Valuable later; requires explicit host frame barrier — separate task.

---

## 12. References

- Dear ImGui Test Engine (conceptual): register tests, item query, frame yield, asserts.
- In-tree: `docs/ui-automation-api.md`, `docs/ui-plugin.md`, `docs/ui-system-design.md` §3.8.
- Sources: `plugins/ui/{ui.h,ui.c,ui_internal.h,automation.c,widgets.c,input.c}`, `core/test.h`, `tests/integration/ui_capture_harness.h`, `player/main.c`, `editor/editor_ui_host.c`.
