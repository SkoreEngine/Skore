# UI Clay layout call-site audit (APX-237)

**Task:** APX-237 — acceptance gate for replacing the custom layout implementation with Clay.  
**Goal branch:** `feature/replace-custom-layout-implementation-on`  
**Scope:** Verify no UI code still calls the custom flex solver; report unmigrated call sites (if any) with the Clay limitation that blocked them (APX-215). Do **not** migrate new surfaces in this task.

---

## Verdict

**(a) Confirmed: no UI call site remains on the custom layout implementation.**

The custom pure-C solver (`plugins/ui/layout.c`, public entry point `ui_layout_impl`) was deleted in APX-216. Every production and test layout pass goes through the public `sk_ui_api_t.layout` function pointer, which is wired exclusively to `ui_clay_layout_impl` in `plugins/ui/clay_adapter.c`. There is no dual path, no stub, and no residual dead-code definition of the old solver.

Unmigrated layout call sites that still need the old solver: **none**. Remaining Clay gaps are handled by documented best-effort mapping and engine-side paint/hit-test (see §4), not by leaving surfaces on `layout.c`.

---

## 1. Custom layout entry points (historical)

Source of truth for the deleted solver: git parent of `0af2ff1` (`plugins/ui/layout.c`).

| Symbol | Role | Status on this branch |
| ------ | ---- | --------------------- |
| `ui_layout_impl` | Public API-table layout entry (whole-tree flex solve) | **Gone** — file deleted; no declaration in `ui_internal.h`; no definition; no references in `.c`/`.h` |
| `ui_layout_node` | Recursive flex node layout | **Gone** (was `static` in `layout.c`) |
| `ui_layout_flex_children` | Flex line placement | **Gone** (was `static`) |
| `ui_layout_absolute_child` | Absolute-position children | **Gone** (was `static`) |
| `ui_walk_clear_layout` | Dirty clear walk | **Gone** (was `static`) |
| `ui_call_measure` / grow-shrink / place helpers | Internal flex math | **Gone** (were `static`) |
| `ui_layout_style_init_default` | Style defaults | **Moved** to `ui.c` (API surface, not the solver) |
| `ui_node_set/get_layout_style_impl` | Style get/set | **Moved** to `ui.c` |
| `ui_node_get_layout_rect(_scaled)_impl` | Rect queries | **Moved** to `ui.c` |
| `ui_set_measure_fn_impl` | Host measure install | **Moved** to `ui.c` |
| `ui_layout_apply_scale_impl` / `ui_layout_get_content_scale_impl` | HiDPI scale step | **Moved** to `ui.c` (still not a flex solver) |

`plugins/ui/layout.c` is absent from the tree and from `plugins/ui/CMakeLists.txt` (`file(GLOB_RECURSE … *.c)` has nothing named `layout.c` to pick up).

---

## 2. Search method and results

Searches run against first-party sources (`plugins/`, `editor/`, `player/`, `tests/`, `docs/`, `core/`, `app/`), excluding `thirdparty/`:

| Pattern | Result |
| ------- | ------ |
| `ui_layout_impl` | **Zero** matches in `.c`/`.h` |
| `ui_layout_node`, `ui_layout_flex_children`, `ui_walk_clear_layout`, `ui_distribute_grow`, `UI_LAYOUT_EPS`, `UI_LAYOUT_MAX_FLEX` | **Zero** matches |
| `layout.c` as a source/include | Mentions only in docs/comments stating the file was **deleted** (`clay_adapter.c` header, `docs/ui-system-design.md`) |
| `ui_clay_layout_impl` | Definition in `clay_adapter.c`; declaration in `ui_internal.h`; **sole** API-table assignment in `ui.c` |

### API table wiring (`plugins/ui/ui.c`)

```text
…,
ui_set_measure_fn_impl,
ui_clay_layout_impl,          /* sk_ui_api_t.layout */
ui_layout_apply_scale_impl,
ui_layout_get_content_scale_impl,
…
```

There is no compile-time switch, function pointer swap, or fallback that reintroduces `ui_layout_impl`.

### How call sites invoke layout

Hosts, widgets, automation, and unit/integration tests call the public table only:

- `ui->layout(ctx, width, height)` (unit tests, automation harness, clay_adapter tests, style/input/paint/widget tests)
- Indirect frame path through automation (`automation.c` harness layout + scale)

None of those files include a private solver header or call `ui_clay_layout_impl` / `ui_layout_impl` by name (the adapter entry is internal to the plugin and bound once in the API table).

Style structs such as `sk_ui_layout_style_t` / `props.layout.height` are **layout property data**, not calls into the custom solver.

---

## 3. Surface migration coverage (prior tasks; no new migration here)

| Surface class | Task | Layout path |
| ------------- | ---- | ----------- |
| Whole retained tree / public `layout()` | APX-214 / APX-216 | `ui_clay_layout_impl` only |
| Panels | APX-232 | Clay adapter + stable IDs |
| Widgets (button, checkbox, slider, text_input, label, scroll_view, image) | APX-233 | Clay adapter + stable IDs |
| Menus (bar, item, popup, dropdown, context, submenu) | APX-234 | Clay adapter + floating |
| Docking / editor windows | APX-235 | Clay adapter + stable IDs |
| Scroll / clip / wrap helpers | APX-236 | Clay clip + stable IDs |

Per prior commit messages on this branch, inventory surfaces were not left on the custom path solely because of Clay limitations; those gaps are best-effort mapped or kept on the engine paint/hit-test path by design.

---

## 4. Unmigrated call sites (APX-215)

**None.** No production or test call site still invokes the custom layout entry points.

Engine-side responsibilities that intentionally remain outside Clay’s full ownership (documented in `clay_adapter.c`; **not** residual `layout.c` call sites):

| Behavior | Why not fully Clay-owned | Impact |
| -------- | ------------------------ | ------ |
| Scroll offset apply in paint/hit-test | Clay `clip.childOffset` left at 0 so writeback keeps unshifted parent-relative rects; full Clay scroll would double-apply | Engine `scroll_x`/`scroll_y` remain source of truth |
| Wheel → `Clay_UpdateScrollContainers` via `scroll_delta_*` | Not fed from engine wheel (stale scroll-container OOB across contexts) | `scroll_view` on_event remains scroll source |
| Soft-wrap glyph placement | Clay owns wrap measure/sizing; paint still uses engine line breaker | Visual wrap placement engine-side |
| flex-wrap multi-line containers | Clay has no multi-line flex wrap | Best-effort / unsupported (logged once) |
| Reverse axes, margins, space-between/around/evenly, absolute insets beyond floating left/top | Clay model deltas | Best-effort mapping, logged once per context |
| Multi-viewport menu/dock OS hosts | Single context root only | Out of v1 scope |
| Splitter primitive | Clay has none | Fixed-size flex child + engine ratio props |
| Widget paint chrome (thumbs, carets, checkmarks, scrollbars) | Intentional engine paint on slot rects | Not a layout call-site gap |

These are **not** unmigrated call sites under APX-215; they are known Clay/engine split responsibilities already recorded on the adapter. No manager follow-up is required for leftover `ui_layout_impl` use.

---

## 5. Interactive state across frames

Stable Clay element IDs and engine interaction paths keep hover, click, and scroll offsets coherent across layout passes:

| Concern | Evidence |
| ------- | -------- |
| Hover / active style after layout | `ui_input_hover_active_style_states` (`input.c`); `ui_widget_button_hover_disabled_states` (`widgets.c`); `ui_style_state_variants_hover_active_focused_disabled` (`style.c`) |
| Click / capture | `ui_auto_action_click_button_callback_and_state`, `ui_auto_harness_query_and_click_e2e` (`automation.c`); input dispatch tests in `input.c` |
| Scroll offsets multi-frame | `ui_clay_scroll_container_stable_id_and_offset`, `ui_clay_nested_scroll_under_clip_stable` (`clay_adapter.c`); `ui_widget_scroll_view_wheel_and_clamp`; `ui_paint_scroll_offset_and_nested_clip` |
| Menu open / hover IDs across frames | `ui_clay_menu_bar_popup_stable_ids_and_open` (APX-234) |
| Dock / tab select IDs | `ui_clay_dock_space_node_splitter_stable_ids`, `ui_clay_tab_bar_select_stable_ids` (APX-235) |
| Panel / widget stable IDs | `ui_clay_panel_row_and_stable_ids`, `ui_clay_widget_button_stable_id`, `ui_clay_widget_scroll_and_slider_surfaces` |

`ui_clay_layout_impl` feeds `Clay_SetPointerState` each pass so Clay-side hover can track the pointer; engine hover/active flags and hit-test continue to use slot layout rects written back from Clay.

---

## 6. Build and test gate

Local verification for this acceptance task (`checks.yaml` stage `fast` uses **Release**):

| Step | Command | Result |
| ---- | ------- | ------ |
| Configure (Release) | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` | OK |
| Build (Release) | `cmake --build build` | OK (189/189) |
| ctest (Release) | `ctest --test-dir build --output-on-failure` | **100%** — `sk-tests` + `sk-integration-tests` |
| Binary symbols (Release) | `nm -D build/bin/plugins/sk-ui.so` | Exports `ui_clay_layout_impl`; **no** `ui_layout_impl` |
| Configure/build (Debug) | `CMAKE_BUILD_TYPE=Debug` (enables plugin `SK_TESTS`) | OK |
| sk-tests (Debug, cwd `build-debug/bin`) | `./sk-tests` | **TOTAL: ran=599 failed=0** |
| sk-ui plugin unit tests | `sk_plugin_run_tests` | **85 tests, 0 failures** (all §5 interactive / stable-id cases) |

Release strips plugin-local `SK_TEST` bodies by design (`sk_target_enable_tests`); Debug is used here only to exercise Clay hover/click/scroll multi-frame unit coverage. No new surfaces were migrated in this task.

---

## 7. Conclusion

| Criterion | Result |
| --------- | ------ |
| No UI call site uses custom layout entry points | **Pass** |
| Old solver reachable only as dead code | **N/A / stronger** — solver file and symbols fully removed |
| Unmigrated call sites + Clay limitation (APX-215) | **None to list** |
| Interactive state across frames | **Pass** (existing APX-232…236 + input/widget/automation tests) |
| Build green | **Pass** (verified this task) |
| No new surface migration in this task | **Honored** — documentation/verification only |

APX-237 acceptance gate is **satisfied**.
