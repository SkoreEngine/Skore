# UI vision triage — all widget snapshots (APX-255)

Consolidated vision pass over the full per-widget suite (APX-252…254), looking
for **cross-widget** rendering defects that single-family rubrics may miss:
inconsistent border radii, off-by-one padding, misaligned baselines, wrong
anti-aliasing, and color/contrast inconsistencies between related widgets.

This task **does not fix** engine paint code. Confirmed defects are filed as
findings only.

## Run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target sk-integration-tests
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export SK_TEST_FILTER='ui_widget_vision_button,ui_widget_vision_checkbox,ui_widget_vision_radio,ui_widget_vision_toggle,ui_widget_vision_text_input,ui_widget_vision_slider,ui_widget_vision_progress,ui_widget_vision_scrollbar,ui_widget_vision_window,ui_widget_vision_tab,ui_widget_vision_menu,ui_widget_vision_table,ui_widget_vision_tooltip'
(cd build/bin && ./sk-integration-tests)
```

- Suite filter: **13 tests, 0 failures, 0 ignored** (structural asserts green)
- Live per-frame vision rubrics: **39 PASS**, **1 soft FAIL then mock-retry PASS**
  (`ui_widget_vision_scrollbar_horizontal` — track not distinct enough for the
  scrollbar rubric; suite still green because SKIPPED/retry path accepts mock)
- Artifact root: `build/test-artifacts/`
- Archived copies of every scene frame: `docs/ui-vision-triage-apx255-frames/`

## Frames collected (40 scene PNGs)

| # | Frame | Size | Family / state |
| - | ----- | ---- | -------------- |
| 1 | `ui_widget_vision_button_normal.png` | 128×64 | button / normal |
| 2 | `ui_widget_vision_button_hover.png` | 128×64 | button / hover |
| 3 | `ui_widget_vision_button_active.png` | 128×64 | button / active |
| 4 | `ui_widget_vision_button_disabled.png` | 128×64 | button / disabled |
| 5 | `ui_widget_vision_checkbox_unchecked.png` | 80×80 | checkbox / unchecked |
| 6 | `ui_widget_vision_checkbox_checked.png` | 80×80 | checkbox / checked |
| 7 | `ui_widget_vision_checkbox_hover_checked.png` | 80×80 | checkbox / hover checked |
| 8 | `ui_widget_vision_checkbox_disabled_checked.png` | 80×80 | checkbox / disabled checked |
| 9 | `ui_widget_vision_radio_unchecked.png` | 80×80 | radio / unchecked |
| 10 | `ui_widget_vision_radio_checked.png` | 80×80 | radio / checked |
| 11 | `ui_widget_vision_radio_hover_checked.png` | 80×80 | radio / hover checked |
| 12 | `ui_widget_vision_radio_disabled_checked.png` | 80×80 | radio / disabled checked |
| 13 | `ui_widget_vision_toggle_off.png` | 80×64 | toggle / off |
| 14 | `ui_widget_vision_toggle_on.png` | 80×64 | toggle / on |
| 15 | `ui_widget_vision_toggle_hover_off.png` | 80×64 | toggle / hover off |
| 16 | `ui_widget_vision_toggle_disabled_on.png` | 80×64 | toggle / disabled on |
| 17 | `ui_widget_vision_text_input_empty.png` | 192×64 | text_input / empty |
| 18 | `ui_widget_vision_text_input_text_caret.png` | 192×64 | text_input / text+caret |
| 19 | `ui_widget_vision_text_input_focused_caret.png` | 192×64 | text_input / focused |
| 20 | `ui_widget_vision_text_input_selection.png` | 192×64 | text_input / selection |
| 21 | `ui_widget_vision_text_input_number.png` | 192×64 | text_input / number |
| 22 | `ui_widget_vision_text_input_disabled.png` | 192×64 | text_input / disabled |
| 23 | `ui_widget_vision_slider_mid.png` | 192×64 | slider / mid |
| 24 | `ui_widget_vision_slider_low.png` | 192×64 | slider / low |
| 25 | `ui_widget_vision_slider_hover.png` | 192×64 | slider / hover |
| 26 | `ui_widget_vision_slider_disabled.png` | 192×64 | slider / disabled |
| 27 | `ui_widget_vision_slider_range.png` | 192×64 | slider / range |
| 28 | `ui_widget_vision_progress_0.png` | 192×64 | progress / 0% |
| 29 | `ui_widget_vision_progress_partial.png` | 192×64 | progress / 50% |
| 30 | `ui_widget_vision_progress_100.png` | 192×64 | progress / 100% |
| 31 | `ui_widget_vision_scrollbar_vertical.png` | 144×128 | scrollbar / vertical mid |
| 32 | `ui_widget_vision_scrollbar_vertical_start.png` | 144×128 | scrollbar / vertical start |
| 33 | `ui_widget_vision_scrollbar_horizontal.png` | 144×128 | scrollbar / horizontal |
| 34 | `ui_widget_vision_scrollbar_both.png` | 144×128 | scrollbar / both axes |
| 35 | `ui_widget_vision_panel_border.png` | 208×140 | panel / bordered |
| 36 | `ui_widget_vision_window_title_bar.png` | 220×148 | window / title bar |
| 37 | `ui_widget_vision_tab_selected.png` | 240×64 | tab / selected mid |
| 38 | `ui_widget_vision_menu_items_sep.png` | 200×160 | menu / items+separator |
| 39 | `ui_widget_vision_table_header_stripe.png` | 232×144 | table / header+zebra |
| 40 | `ui_widget_vision_tooltip_popup.png` | 192×112 | tooltip / popup |

Fail evidence (rubric soft fail, suite still green):
`ui_widget_vision_scrollbar_horizontal_vision_fail.png`

## Prioritized defect list

Severity: **P0** = user-visible breakage / unreadable chrome; **P1** = clear
rendering bug across widgets; **P2** = consistency / quality polish.

### D1 — P0 — table cell text overflows row bounds and clips headers

| Field | Value |
| ----- | ----- |
| **Widget** | `table` |
| **Frames** | `ui_widget_vision_table_header_stripe.png` |
| **Severity** | **P0** |
| **Wrong** | Body strings wrap/overflow outside their zebra row: `"mesh"` paints as `"mes"` with a stray `"t"` on the face below the row; `"1k"` paints as `"1"` with `"k"` below. Header labels clip (`Name`→`Na`, `Type`→`Typ`). **70** light ink pixels sit at y=105..114, below the last body band (row ends ~y=104). |
| **Correct** | Glyphs fully inside the cell content box (clip or ellipsize); headers complete or ellipsized inside the header band. |
| **Why rubrics missed it** | `table.txt` only requires “header differs / striping / separators” — not clipping or overflow. |
| **Likely area** | Table row height / cell padding / text layout clip (`widgets.c` table, Clay text measure). |

### D2 — P1 — borders ignore `corner_radius` (rect strips over rounded fills)

| Field | Value |
| ----- | ----- |
| **Widget** | `toggle` (worst), also `button`, `checkbox`, `text_input`, `panel`, `window`, `menu`, `tooltip` |
| **Frames** | Primary: `ui_widget_vision_toggle_on.png`, `ui_widget_vision_toggle_off.png`, `ui_widget_vision_toggle_hover_off.png`, `ui_widget_vision_toggle_disabled_on.png`. Cross-check sharp outer silhouettes: `ui_widget_vision_button_normal.png`, `ui_widget_vision_checkbox_checked.png`, `ui_widget_vision_panel_border.png` |
| **Severity** | **P1** |
| **Wrong** | `ui_paint_add_border` draws four axis-aligned edge strips. Background uses `ui_paint_add_rounded_rect_filled` with style `corner_radius`. Result: toggle pill fill is rounded while the 1px border is a rectangle — **dark clear-color notches** at all four corners (e.g. toggle_on TL at (12,18): border `rgb(102,107,122)` on the outer edge, `rgb(31,33,38)` corner voids inside). Buttons/checkboxes author radius 4/3 but outer silhouette is a hard rectangle because the border covers the arc. |
| **Correct** | Border follows the same rounded path as the fill (or inset rounded stroke). Toggle should be a clean pill with no square corner frame. |
| **Why rubrics missed it** | Family rubrics check fill/state marks, not border-vs-fill radius agreement across widgets. |
| **Evidence in code** | `plugins/ui/paint.c` — `ui_paint_add_border` (rect strips) vs rounded fill at background paint. Styles set `corner_radius` in `widgets.c` (button 4, checkbox 3, toggle 11, panel 4). |

### D3 — P1 — hard stair-step AA on curved geometry (no coverage AA)

| Field | Value |
| ----- | ----- |
| **Widget** | `radio`, `toggle` (thumb), `slider` (thumb), shared rounded-rect corners |
| **Frames** | `ui_widget_vision_radio_checked.png`, `ui_widget_vision_radio_unchecked.png`, `ui_widget_vision_toggle_on.png`, `ui_widget_vision_slider_mid.png`, `ui_widget_vision_slider_range.png` |
| **Severity** | **P1** |
| **Wrong** | Circles/pills are pure binary coverage: radio ring radius span ~5.9px with only solid ring/fill/clear colors (3 unique colors in the neighborhood); slider thumbs use 4 flat colors with jagged edges. Looks octagonal / pixel-stepped at 1×. |
| **Correct** | Edge coverage AA (or MSAA resolve) with intermediate fringe colors along arcs. |
| **Why rubrics missed it** | Rubrics accept “filled disc / distinct thumb” without grading edge smoothness. |
| **Likely area** | `ui_paint_add_rounded_rect_filled` triangle fans + solid quads; no partial-alpha edge. |

### D4 — P1 — horizontal scrollbar track nearly invisible vs face (rubric FAIL)

| Field | Value |
| ----- | ----- |
| **Widget** | `scrollbar` (horizontal; vertical is better but still low) |
| **Frames** | `ui_widget_vision_scrollbar_horizontal.png` (+ `…_vision_fail.png`), compare `ui_widget_vision_scrollbar_vertical.png`, `ui_widget_vision_scrollbar_both.png` |
| **Severity** | **P1** |
| **Wrong** | Live vision: *“Only a short mid bottom bar (thumb) is visible; no distinct full-edge track is shown.”* Track is present (`rgb(26,28,33)` strip at y≈108–110) but contrast vs face `rgb(36,38,43)` is ~1.1–1.8 and vs outer canvas is weak; thumb is the only clearly readable chrome. Vertical track same palette but reads better as a full-height strip. |
| **Correct** | Track band clearly separable from scroll-view face on both axes (stronger track tint or inset groove). |
| **Why rubrics flaked** | Horizontal-only scene failed once then soft-retried; vertical/both still PASS. Cross-axis consistency not graded. |

### D5 — P2 — accent fill blues inconsistent across related track widgets

| Field | Value |
| ----- | ----- |
| **Widget** | `slider` vs `progress` (and vs `toggle` ON) |
| **Frames** | `ui_widget_vision_slider_mid.png`, `ui_widget_vision_progress_partial.png`, `ui_widget_vision_progress_100.png`, `ui_widget_vision_toggle_on.png` |
| **Severity** | **P2** |
| **Wrong** | Slider/toggle fill `rgb(77,140,242)` (0.30,0.55,0.95); progress fill `rgb(82,148,245)` (0.32,0.58,0.96). Same “accent” role, different tokens. |
| **Correct** | Shared accent token for filled tracks / ON toggle / progress. |
| **Why rubrics missed it** | Each family grades in isolation. |

### D6 — P2 — disabled text_input ink barely separable from field face

| Field | Value |
| ----- | ----- |
| **Widget** | `text_input` (disabled) |
| **Frames** | `ui_widget_vision_text_input_disabled.png` (compare `ui_widget_vision_button_disabled.png`, `ui_widget_vision_checkbox_disabled_checked.png`) |
| **Severity** | **P2** |
| **Wrong** | Disabled field face ≈ `rgb(89,94,107)`; glyph ink clusters around `rgb(70–80,73–84,78–90)` — contrast ~2.3 and looks almost blank at 1×. Buttons/checkboxes still show a readable mark/label when disabled. |
| **Correct** | Disabled text remains legible (muted but ≥ ~3:1 against face) consistent with other disabled chrome. |
| **Why rubrics missed it** | `text_input` / `disabled` rubrics accept “dimmed / no caret” without a contrast floor vs other families. |

### D7 — P2 — selected tab contrast too weak vs unselected siblings

| Field | Value |
| ----- | ----- |
| **Widget** | `tab` |
| **Frames** | `ui_widget_vision_tab_selected.png` |
| **Severity** | **P2** |
| **Wrong** | Selected middle tab (“Game”) is only slightly lighter than neighbors; no strong indicator (underline, accent, larger type). Passes “distinct” barely for vision but fails a design-consistency bar next to strong table header / window title separation. |
| **Correct** | Selected tab clearly stronger (accent bar or fill delta matching other selection chrome). |

## Intra-family geometry (OK)

State variants keep stable content bboxes (no layout jitter across hover/active/disabled):

| Family | Bbox stable across states? |
| ------ | -------------------------- |
| button | Yes — (16,16)–(111,47) |
| checkbox | Yes — (28,28)–(51,51) |
| radio | Yes — (24,24)–(55,55) |
| toggle | Yes — (12,18)–(67,45) |
| slider | Yes — (16,20)–(175,43) |
| progress | Yes — (16,22)–(175,41) |
| text_input | Yes — full frame |
| scrollbar | Yes — (12,12)–(131,111) |

No off-by-one padding **regressions between states** of the same widget. Slider vs progress track vertical sizing differs by design (20 vs 16 pt height).

## Rubric-only soft fail (already tracked as D4)

| Scene | Live vision | Suite outcome |
| ----- | ----------- | ------------- |
| `ui_widget_vision_scrollbar_horizontal` | FAIL: track not distinct | Soft retry → suite green |

## Outcome

**Confirmed rendering defects filed: D1–D7** (prioritized above).

| ID | Severity | Widget | Frame (primary) |
| -- | -------- | ------ | --------------- |
| D1 | P0 | table | `ui_widget_vision_table_header_stripe.png` |
| D2 | P1 | toggle (+ button/checkbox/chrome) | `ui_widget_vision_toggle_on.png` |
| D3 | P1 | radio / toggle / slider | `ui_widget_vision_radio_checked.png` |
| D4 | P1 | scrollbar | `ui_widget_vision_scrollbar_horizontal.png` |
| D5 | P2 | slider / progress / toggle | `ui_widget_vision_progress_partial.png` |
| D6 | P2 | text_input | `ui_widget_vision_text_input_disabled.png` |
| D7 | P2 | tab | `ui_widget_vision_tab_selected.png` |

No engine paint/layout fixes in this task. Recommended follow-ups for coder
workers: rounded borders (D2), coverage AA (D3), table text clip (D1),
scrollbar track contrast (D4), accent token unify (D5), disabled ink (D6),
tab selected chrome (D7).

## Method notes

- Structural pixel asserts from `ui_widget_vision.c` all green (suite 0 failures).
- Per-frame vision rubrics (APX-251) mostly PASS; consolidated pass uses scaled
  4× nearest inspection + PIL bbox/histogram metrics across all 40 frames.
- Frame archive under `docs/ui-vision-triage-apx255-frames/` is a copy of the
  live `build/test-artifacts/` scene PNGs from this run (lavapipe ICD).
