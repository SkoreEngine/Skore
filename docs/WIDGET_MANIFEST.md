# Editor widget manifest (main-branch ImGui audit)

Task: **APX-335**. Goal: review necessary widgets for the skore editor.

This file is the **authoritative inventory** of Dear ImGui widgets the C++
editor on `main` actually calls, the **work queue** for later sk-ui waves, and
the **tracker** for the goal acceptance criterion (unit test / headless UI
automation / lavapipe PNG reviewed). It does **not** implement widgets.

| Item | Value |
| --- | --- |
| Audited branch | `origin/main` |
| Audited commit | `62b1d00e62a97747a95b16f11a9f9c8bdc1c29a0` |
| Scope | `Editor/Source/Skore/**` (editor + ImGui helpers). Third-party Dear ImGui / ImGuizmo sources are not counted as call sites. |
| Method | `git fetch origin main` then enumerate live (non-comment) `ImGui::` call sites and editor wrappers in `ImGui.hpp`. Group by family. Spec only behaviour the editor relies on. |
| Line numbers | Paths and lines are **on `main`**, not on the v2 / goal-branch checkout. |

**How to read an entry**

- **ImGui functions / overloads** — the exact signatures the editor invokes
  (Dear ImGui API plus the thin `ImGui*` wrappers in
  `Editor/Source/Skore/ImGui/ImGui.hpp`). Unused overloads are omitted.
- **Distinct call sites** — count of live source lines that invoke the family.
  Wrapper *definitions* are not double-counted as editor usage; wrapper
  *callers* are listed separately when the editor never calls `ImGui::`
  directly.
- **Holds state** — whether the widget keeps interaction or value state
  across frames (ImGui storage, open/close, caret, drag, payload).
- **Retained data-pointer binding** — whether a later sk-ui port must accept
  a pointer to a **caller-owned, mutable item array** instead of rebuilding
  the widget tree every frame. Hierarchical (entity / asset / animator trees)
  is the known required case; see §21.
- **Checks** — later-wave acceptance. All start unchecked. Existing sk-ui
  factories (button, checkbox, slider, …) are noted as coverage, not as a pass.

Do not treat vision goldens for a *different* widget shape as a pass for the
editor overload listed here.

---

## 1. Priority ordering

Order is **editor dependence** first, then **widgets that unblock others**.
Implement / harden in this sequence. Do not start graph or gizmo work from
this list (see §21).

| Pri | Family | Why first |
| --- | --- | --- |
| P0 | Child / Window / Layout (§13) | Every panel is `ImGuiBegin` + `BeginChild` + stack layout (`SameLine` / `BeginHorizontal` / `Spring`). Unblocks hosting every other widget. |
| P0 | Text (§3) | Status bar, empty states, property labels, console lines, debugger. |
| P0 | Separator / Spacing / SameLine (§19) | Toolbar rows, property rows, menu separators. Used more than any interactive widget. |
| P0 | Button (§2) | Clear, Save, Import, play/stop, icon chrome, add/remove array rows. |
| P0 | MenuBar / Menu / MenuItem (§11) | Entire editor command surface (`MenuItem.cpp`) plus workspace `+` popup. |
| P0 | TabBar (§10) | Workspace switcher and Debugger / ResourceDebugger pages. |
| P0 | Popup / Modal (§12) | Save Content, error dialogs, context menus, resource pickers, scene option popups. |
| P1 | Checkbox (§4) | Console filters (already ported), property bools, viewport/settings toggles. |
| P1 | InputText family (§5) | Search, rename (entity / asset / animator), property strings, path/read-only IDs. Unblocks trees and browsers. |
| P1 | TreeNode / CollapsingHeader (§8) | **Unblocks Entity Tree, Project Browser, Animator Tree, Settings, Properties sections.** Requires hierarchical item-array binding (§21). |
| P1 | Table (§9) | Entity-tree columns, Packages, pending-save, property label/value grids, debugger. `Columns` is unused. |
| P1 | Selectable (§18) | History rows, combo items, type lists, project-launcher nav. |
| P1 | Combo / ListBox (§7) | Enum / layer / material dropdowns; History list; entity picker list. |
| P1 | DragDrop (§17) | Entity reparent and asset move/assign. Needs tree + content items to be useful. |
| P2 | Slider / Drag (§6) | Property scalars, camera FOV/speed, browser zoom, material params. |
| P2 | ColorEdit / ColorPicker (§15) | `Color` fields (button + popup picker) and material RGB. |
| P2 | Image / content item (§16) | Viewport texture, asset thumbnails, project launcher tiles. Needs a caller-owned item list. |
| P2 | Tooltip (§20) | Asset hover card; profiler segment hover. |
| — | ProgressBar (§14) | **Not used** (commented status-bar only). Do not schedule a wave for it. |
| — | Radio / ImageButton / Columns | **Not called.** Do not schedule from this audit. |

**Already on sk-ui (still must pass the three checks against *editor* behaviour):**
button, label, checkbox, slider (float only), text_input (single-line,
multiline, hint/search, read-only, InputScalar / InputFloat / InputFloat3),
scroll_view, image, menu_bar / menu / menu_item / menu_popup / dropdown /
context_menu / submenu, dock_space / dock_node / splitter / tab_bar / tab /
editor_window. Missing factories that this audit makes load-bearing: **tree**,
**table**, **drag-drop payload**, **color picker**, **combo that binds an
int + zero-separated items**, **tooltip**.

---

## 2. Button family

**ImGui functions:** `Button`, `SmallButton`, `InvisibleButton`.
Editor wrappers: `ImGuiSelectionButton`, `ImGuiBorderedButton`.

**Overloads the editor calls**

```cpp
bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
bool SmallButton(const char* label);
bool InvisibleButton(const char* str_id, const ImVec2& size, ImGuiButtonFlags flags = 0);

bool ImGuiSelectionButton(const char* label, bool selected, const ImVec2& sizeArg = ImVec2(0, 0));
bool ImGuiBorderedButton(const char* label, const ImVec2& size = ImVec2(0, 0));
```

Live size variants: default auto size; `ImVec2(120, 0)` modal buttons;
square icon `ImVec2(h, h)`; column-width `ImVec2(GetColumnWidth(), 0)`;
toolbar `buttonSize`. Labels are often Font Awesome icon codepoints
(`ICON_FA_*`) with optional `###id` suffix.

`InvisibleButton` flags used: `ImGuiButtonFlags_MouseButtonLeft |
ImGuiButtonFlags_MouseButtonMiddle` (texture canvas).

**Distinct call sites:** 65 `ImGui::` (Button / SmallButton / InvisibleButton).
Wrapper callers: `ImGuiSelectionButton` ×8 (`SceneViewWindow.cpp` toolbar);
`ImGuiBorderedButton` ×4 (`PropertiesWindow.cpp`).

**Representative sites (main)**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/ConsoleWindow.cpp:39` | `Button("Clear")` |
| `Editor/Source/Skore/Editor.cpp:976` | `Button("OK", ImVec2(120, 0))` |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:235` | icon visibility toggle, column width |
| `Editor/Source/Skore/Window/PackagesWindow.cpp:67` | `SmallButton(ICON_FA_TRASH)` |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:320` | `ImGuiBorderedButton("Add Component", …)` |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:711` | `ImGuiSelectionButton` tool mode |

**Holds state:** no value state. Click is edge-triggered (`true` on release).
`ImGuiSelectionButton` paints selected from a caller `bool`; it does not own
the mode.

**Retained data-pointer binding:** no.

**Minimum behaviour**

- Click returns true once; disabled via `BeginDisabled` (eye/lock when read-only).
- Optional explicit size; zero on an axis means auto.
- Icon-only and icon+text labels.
- Invisible hit target for splitters / texture pan-zoom.
- Selected-look toolbar toggle (background when `selected`).

**sk-ui today:** `widget_button`, `widget_small_button`, `widget_invisible_button`,
`widget_selection_button`, `widget_bordered_button`, `widget_arrow_button`.
Click is edge-triggered (`button_clicked` on release over the button). Zero on
a size axis is auto. `###id` labels strip the visible prefix. InvisibleButton
accepts `SK_UI_BUTTON_FLAG_MOUSE_LEFT | MIDDLE` (texture canvas). Selection
look is a caller `bool` (`button_set_selected`). ArrowButton is implemented
for completeness; the editor never calls it (§22.3). Repeat/held is not used.

- [x] Unit test — `plugins/ui/widgets.c`
  (`ui_widget_button_family_labels_ids_size`,
  `ui_widget_button_family_click_press_release`)
- [x] Headless UI automation — `plugins/ui/button_family_tests.c`
  (`ui_author_button_family_press_release_drag_off`, SK_UI_TEST harness)
- [x] lavapipe PNG reviewed — `sandbox/widget_review.c` (`--widget button`,
  `small_button`, `invisible_button`, `selection_button`, `bordered_button`,
  `arrow_button`; states default/hovered/pressed/disabled/focused)

---

## 3. Text family

**ImGui functions:** `Text`, `TextUnformatted`, `TextDisabled`, `TextColored`,
`TextWrapped`, `SeparatorText`. Wrappers: `ImGuiTextWithLabel`,
`ImGuiCentralizedText`.

**Overloads the editor calls**

```cpp
void Text(const char* fmt, ...);                 // often Text("%s", …) or Text("%.2f ms …")
void TextUnformatted(const char* text, const char* text_end = NULL);
void TextDisabled(const char* fmt, ...);
void TextColored(const ImVec4& col, const char* fmt, ...);
void TextWrapped(const char* fmt, ...);
void SeparatorText(const char* label);

void ImGuiTextWithLabel(StringView label, StringView text);
void ImGuiCentralizedText(const char* text);
```

**Distinct call sites:** 174 `ImGui::` text-family lines (including
`SeparatorText`). Wrapper callers: `ImGuiCentralizedText` ×3
(EntityTree / Properties empty states).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/ConsoleWindow.cpp:114` | `TextUnformatted(begin, end)` per log line, colour via `PushStyleColor` |
| `Editor/Source/Skore/Editor.cpp:1173` | `Text("%.2f ms (%.2f FPS)", …)` status bar |
| `Editor/Source/Skore/Window/PackagesWindow.cpp:39` | `TextDisabled` hint |
| `Editor/Source/Skore/Editor.cpp:874` | `TextColored` Created/Deleted in save table |
| `Editor/Source/Skore/Window/ResourceDebuggerWindow.cpp:388` | `SeparatorText("Resource Info")` |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:466` | `ImGuiCentralizedText("Open a scene…")` |

**Holds state:** no.

**Retained data-pointer binding:** no for labels. Console *lines* are a
changing list: the existing v2 Console port dirty-rebuilds labels when the
sink version changes — do not rebuild chrome every frame. Not a hierarchical
tree.

**Minimum behaviour**

- Unformatted UTF-8, including a non-null-terminated range (`text_end`).
- Printf-style numeric / `%s` formatting.
- Disabled (dim) and explicit RGBA colour.
- Wrapped warning text (animator validation).
- Centred empty-state string.
- `SeparatorText` as a labelled section rule.

**sk-ui today:** `widget_text` (no soft wrap), `widget_text_wrapped`,
`widget_text_disabled` (dim + disabled state), `widget_text_colored` (per-node
RGBA), `widget_separator_text` (rule + label gap), `widget_bullet_text` and
`widget_label_text` (completeness; the editor never calls them, §22.3),
`widget_text_with_label` / `widget_text_centered` wrappers. `label_set_text` /
`text_set_text_range` update content live and relayout on the next frame.
`text_set_disabled` maps to the class disabled variant (ImGui `TextDisabled`
colour); `text_set_color` / `text_get_color` cover `TextColored`.

- [x] Unit test — `plugins/ui/widgets.c`
  (`ui_widget_text_family_measure_wrap_variants`,
  `ui_widget_text_family_utf8_newlines_range`,
  `ui_widget_text_family_special_nodes`)
- [x] Headless UI automation — `plugins/ui/text_family_tests.c`
  (`ui_author_text_family_layout_extent_and_content_updates`, SK_UI_TEST harness)
- [x] lavapipe PNG reviewed — `sandbox/widget_review.c` (`--widget text`,
  `text_colored`, `text_disabled`, `text_wrapped`, `bullet_text`,
  `separator_text`)

---

## 4. Checkbox / Radio

**ImGui functions:** `Checkbox` only. `RadioButton` is **not called**.

**Overload**

```cpp
bool Checkbox(const char* label, bool* v);
```

Labels: visible (`"Trace"`) or hidden id (`"##v"`, `"###"`, `"##cm"`).
Always binds a caller `bool*` (or a stack `bool` written back).

**Distinct call sites:** 21.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/ConsoleWindow.cpp:50-68` | Trace/Debug/Info/Warn/Error/Critical + Collapse + Auto-scroll |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:286` | reflected `bool` property |
| `Editor/Source/Skore/Window/SettingsWindow.cpp:334` | layer collision matrix cell |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:950` | Smooth Camera |
| `Editor/Source/Skore/Editor.cpp:860` | pending-save row include checkbox |

**Holds state:** yes — the `bool*` is the value. ImGui owns hover/press chrome
only.

**Retained data-pointer binding:** no (scalar). Settings matrix is a 2D bool
grid owned by the settings object; bind cells, do not invent a radio group.

**Minimum behaviour**

- Toggle `*v` on click; return true on change.
- Works with empty/`##` label (box only) and with a text label on the same item.
- Participates in `BeginDisabled`.
- Collision-matrix layout: many boxes on one row via `SameLine` (not Radio).

**Not used:** `RadioButton(label, bool)` and `RadioButton(label, int*, int)`.
sk-ui `widget_radio` / `widget_toggle` are **not** editor-ImGui requirements.

**sk-ui today:** `widget_checkbox` (Console port) plus bind / flags / radio.

- Toggle writes a caller `i32*` (`checkbox_bind`); `checkbox_changed` is
  consume-on-read and true once per value-changing click (ImGui return).
- `checkbox_bind_flags` implements CheckboxFlags: set / clear the mask,
  mixed/indeterminate when some but not all bits are set.
- `widget_radio` siblings under the same parent are exclusive. `radio_bind`
  is the `RadioButton(label, int*, int)` form. Editor does not call Radio
  or CheckboxFlags today; both are covered because the three checks require
  them.
- Empty / `##` label is box-only; a visible label sits on the same item.

- [x] Unit test — `plugins/ui/widgets.c`
  (`ui_widget_checkbox_toggle_and_callback`,
  `ui_widget_checkbox_bind_flags_mixed_disabled`,
  `ui_widget_radio_group_exclusivity_and_bind`)
- [x] Headless UI automation — `plugins/ui/checkbox_radio_tests.c`
  (`ui_author_checkbox_radio_toggle_bind_group_and_external`, SK_UI_TEST harness)
- [x] lavapipe PNG reviewed — `sandbox/widget_review.c` (`--widget checkbox`,
  `radio`, `radio_group`; states default/hovered/disabled/checked/mixed)

---

## 5. InputText family

**ImGui functions:** `InputText`, `InputTextMultiline`, `InputFloat`,
`InputFloat3`, `InputScalar`. Editor wrappers (the editor-facing API):

```cpp
bool ImGuiInputText(u32 idx, String& string, ImGuiInputTextFlags flags = 0,
                    ImGuiInputTextExtraFlags extraFlags = ImGuiInputTextExtraFlags_None);
bool ImGuiInputTextMultiline(u32 idx, String& string, const ImVec2& size,
                             ImGuiInputTextFlags flags = 0,
                             ImGuiInputTextExtraFlags extraFlags = ImGuiInputTextExtraFlags_None);
void ImGuiInputTextReadOnly(u32 idx, StringView str, ImGuiInputTextFlags flags = 0);
bool ImGuiSearchInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags = 0);
bool ImGuiPathInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags = 0); // stub: returns false
```

**Dear ImGui overloads actually reached**

```cpp
bool InputText(const char* label, char* buf, size_t buf_size,
               ImGuiInputTextFlags flags = 0,
               ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
bool InputTextMultiline(const char* label, char* buf, size_t buf_size,
                        const ImVec2& size = ImVec2(0, 0),
                        ImGuiInputTextFlags flags = 0,
                        ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
bool InputFloat(const char* label, float* v, …);
bool InputFloat3(const char* label, float v[3], …);
bool InputScalar(const char* label, ImGuiDataType data_type, void* p_data,
                 const void* p_step = NULL, const void* p_step_fast = NULL,
                 const char* format = NULL, ImGuiInputTextFlags flags = 0);
```

Flags used: `ImGuiInputTextFlags_CallbackResize` (always, wrappers),
`ImGuiInputTextFlags_ReadOnly`, `ImGuiInputTextFlags_CharsDecimal`,
`ImGuiInputTextFlags_EnterReturnsTrue`. Extra: `ImGuiInputTextExtraFlags_ShowError`
(red focus rect on invalid project name).

`InputScalar` data types used: `ImGuiDataType_Float`, `ImGuiDataType_Double`,
`ImGuiDataType_S32`, `ImGuiDataType_U32`, `ImGuiDataType_U64`.

`ImGuiTextFilter` (Console, EntityTree, ProjectBrowser, ResourceDebugger,
resource-selection popup) is a helper that owns a filter string and
`PassFilter`. Console calls `filter.Draw("Filter", 180)`.

**Distinct call sites:** 4 direct `ImGui::InputText*` (all inside
`ImGui.cpp` wrappers). Editor-facing wrapper callers: `ImGuiInputText` ~10
(rename + string fields + project name), `ImGuiInputTextMultiline` ×3
(string property + generated HLSL / build log), `ImGuiInputTextReadOnly` ~10
(UUID / path / entity id), `ImGuiSearchInputText` ×7 (every browser/search
bar). `InputFloat` ×1, `InputFloat3` ×2, `InputScalar` ×2 in FieldRenderers.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/ImGui/ImGui.cpp:897` | `InputText` + resize callback into `String&` |
| `Editor/Source/Skore/ImGui/ImGui.cpp:927` | multiline + resize callback |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:141` | inline rename |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:495` | `ImGuiSearchInputText` |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:387` | `InputScalar` float/double |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:962` | `InputFloat3("###cameraPos", &position.x)` |
| `Editor/Source/Skore/Window/ConsoleWindow.cpp:73` | `filter.Draw("Filter", 180)` |

**Holds state:** yes — buffer contents (caller `String` / `char*`), caret,
selection, IME, and “active id”. Wrappers report
`IsItemDeactivatedAfterEdit` as commit (`updatedFinished`) for undo.

**Retained data-pointer binding:** no. Bind a caller-owned mutable string /
scalar. Search filter is a single string, not a list.

**Minimum behaviour**

- Dynamic string grow via callback (no fixed 256-char cap).
- Single-line, multi-line, read-only (still focusable, blue focus rect).
- Search field: magnifier icon + “Search” placeholder when empty.
- Optional error chrome (red rect) without blocking input.
- Numeric text edit for f32/f64/i32/u32/u64 and `float[3]`.
- Commit vs live-edit distinction (`DeactivatedAfterEdit`) for undo scopes.
- Substring filter helper (`ImGuiTextFilter` / `PassFilter`).

**Not used:** `InputTextWithHint`, `InputInt*`, `InputFloat2/4`, `InputDouble`
as distinct APIs (covered by `InputScalar`). `ImGuiPathInputText` is a stub.

**sk-ui today:** `widget_text_input` plus multiline / hint / search / read-only
factories, InputScalar / InputFloat / InputFloat3 / InputInt, flags
(ReadOnly, Password, EnterReturnsTrue, AutoSelectAll, CharsDecimal,
ShowError), capacity grow/truncate, commit vs live-edit, escape-to-revert,
and `text_filter_pass` (ImGuiTextFilter).

- [x] Unit test — `plugins/ui/widgets.c`
  (`ui_widget_text_input_edit_ops`,
  `ui_widget_text_input_family_buffer_edit_selection`,
  `ui_widget_text_input_family_flags_commit_revert`,
  `ui_widget_text_input_family_numeric_parse_clamp`)
- [x] Headless UI automation — `plugins/ui/input_text_family_tests.c`
  (`ui_author_input_text_family_keystrokes_focus_commit_revert`, SK_UI_TEST harness)
- [x] lavapipe PNG reviewed — `sandbox/widget_review.c` (`--widget text_input`,
  `text_input_hint`, `text_input_selection`, `text_input_multiline`,
  `text_input_readonly`; states default/hovered/pressed/disabled/focused)

---

## 6. Slider / Drag family

**ImGui functions**

```cpp
bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                 const char* format = "%.3f", ImGuiSliderFlags flags = 0);
bool SliderInt(const char* label, int* v, int v_min, int v_max,
               const char* format = "%d", ImGuiSliderFlags flags = 0);
bool SliderScalar(const char* label, ImGuiDataType data_type, void* p_data,
                  const void* p_min, const void* p_max,
                  const char* format = NULL, ImGuiSliderFlags flags = 0);

bool DragFloat(const char* label, float* v, float v_speed = 1.0f,
               float v_min = 0.0f, float v_max = 0.0f,
               const char* format = "%.3f", ImGuiSliderFlags flags = 0);
bool DragFloat2(const char* label, float v[2], float v_speed = 1.0f, …);
bool DragFloat3(const char* label, float v[3], float v_speed = 1.0f, …);
bool DragFloat4(const char* label, float v[4], float v_speed = 1.0f, …);
bool DragInt(const char* label, int* v, float v_speed = 1.0f, …);
```

`ImGuiBasicSlider` is declared and implemented (`ImGui.cpp:1394`) but has
**no editor callers** — do not port it unless a later panel needs a custom
labelled slider.

Flags used: `ImGuiSliderFlags_AlwaysClamp` on property sliders.
`SliderInt` format override `"auto"` vs `"LOD %d"` (SceneView forced LOD).
`DragFloat` speed `0.01f`; unbounded when min==max==0 (material scalars).

**Distinct call sites:** Slider ×7, Drag ×13 (2 of the Drag sites are
GraphEditor pin widgets; still the same family).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:59` | `SliderFloat` from `UISliderProperty` |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:379` | `SliderScalar` f32/f64 |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:933` | FOV `SliderFloat("##fov", &cameraFov, 4, 120, "%.0f")` |
| `Editor/Source/Skore/Window/ProjectBrowserWindow.cpp:464` | zoom `SliderFloat("###zoom", &contentBrowserZoom, 0.4f, 5.0f, "")` |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:1528` | `DragFloat("##v", &scalar, 0.01f)` |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:1555-1561` | `DragFloat2/3/4` |

**Holds state:** yes — `*v` plus active-drag mouse capture. Commit via
`IsItemDeactivatedAfterEdit` on property fields.

**Retained data-pointer binding:** no (scalar / small vector).

**Minimum behaviour**

- Horizontal slider with min/max, optional format, AlwaysClamp.
- Integer slider with custom format string (including a sentinel label).
- Drag scalar / 2 / 3 / 4 and DragInt at a given speed; optional hard range
  (mask cutoff 0..1).
- Hidden `##` labels; width from `SetNextItemWidth(-1)` in property columns.

**Not used:** `SliderFloat2/3/4`, `VSlider*`, `SliderAngle`, `DragScalarN`
outside GraphEditor pins.

**sk-ui today:** `widget_slider` (SliderFloat) plus SliderInt, DragFloat /
DragInt, and N-component rows (DragFloat2/3/4). Format, step, speed,
AlwaysClamp, inverted and zero-width ranges, ctrl-click text-entry, and a
consume-on-read changed flag. Vector children edit independently.

- [x] Unit test — `plugins/ui/widgets.c`
  (`ui_widget_slider_clamp_and_drag`,
  `ui_widget_slider_family_mapping_clamp_step_format`)
- [x] Headless UI automation — `plugins/ui/slider_drag_family_tests.c`
  (`ui_author_slider_drag_family_drag_clamp_changed_text_entry`, SK_UI_TEST harness)
- [x] lavapipe PNG reviewed — `sandbox/widget_review.c` (`--widget slider`,
  `slider_int`, `slider_float3`, `drag_float`, `drag_int`, `drag_float3`;
  states min/mid/max/dragging/text_entry/disabled)

---

## 7. Combo / ListBox

**ImGui functions**

```cpp
bool BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0);
void EndCombo(); // only if BeginCombo returned true
bool Combo(const char* label, int* current_item,
           const char* items_separated_by_zeros,
           int popup_max_height_in_items = -1);

bool BeginListBox(const char* label, const ImVec2& size = ImVec2(0, 0));
void EndListBox();
```

`BeginCombo` is always used with default flags and a preview string; items
are `Selectable`s. `Combo` uses the `\0`-separated items overload only
(material channel / shading / alpha / face / depth).

`ListBox()` convenience overloads are **not** used.

**Distinct call sites:** Combo family 16; ListBox family 4
(`BeginListBox`/`EndListBox` pairs in History + entity picker).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:254` | enum `BeginCombo` + `Selectable` per value |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:254` | layer combo |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:1635` | `Combo("##shadingmodel", &shadingIndex, "Default Lit\0Unlit\0")` |
| `Editor/Source/Skore/Window/DebuggerWindow.cpp:327` | profiler scale `BeginCombo` |
| `Editor/Source/Skore/Window/HistoryWindow.cpp:22` | `BeginListBox("###", GetWindowSize())` |
| `Editor/Source/Skore/ImGui/ImGui.cpp:2330` | entity picker `BeginListBox("Entities", ImVec2(-FLT_MIN, -FLT_MIN))` |

**Holds state:** yes — open/close of the combo popup; `*current_item` for
`Combo`; list-box scroll.

**Retained data-pointer binding:** no for enum combos (small static lists).
History and the entity picker are **caller-owned item lists** that change;
bind `sk_ui_item_array_t*` with `SK_UI_ITEM_BIND_LIST` / `COMBO` (§21)
rather than creating one retained node per undo entry / entity every frame.

**Minimum behaviour**

- Closed combo shows preview; open list of selectables; click sets value.
- Zero-separated `Combo` with `int*` index.
- Full-window list box of selectables (history).
- Nested entity names inside a list box (picker); double-click accepts.

**sk-ui today:** `widget_dropdown` (menu surface). No `int*` + `\0` items
combo, no list box.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 8. TreeNode / CollapsingHeader

**ImGui functions**

```cpp
bool TreeNode(const char* str_id, const char* fmt, ...);
bool TreeNodeEx(const void* ptr_id, ImGuiTreeNodeFlags flags, const char* fmt, ...);
void TreePop();
bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0);
void SetNextItemOpen(bool is_open, ImGuiCond cond = 0);
bool TreeNodeUpdateNextOpen(ImGuiID id, ImGuiTreeNodeFlags flags); // imgui_internal.h
```

Editor wrappers:

```cpp
void ImGuiBeginTreeNodeStyle();
void ImGuiEndTreeNodeStyle();
bool ImGuiTreeNode(ConstPtr id, const char* label, ImGuiTreeNodeFlags flags = 0);
bool ImGuiTreeLeaf(ConstPtr id, const char* label, ImGuiTreeNodeFlags flags = 0);
bool ImGuiCollapsingHeaderProps(i32 id, const char* label, bool* buttonClicked);
```

`ImGuiTreeNode` always ORs
`OpenOnArrow | SpanAvailWidth | SpanFullWidth | FramePadding` and calls
`TreeNodeEx(id, flags, "%s", label)`.
`ImGuiTreeLeaf` additionally ORs `Leaf | NoTreePushOnOpen`.

Other flags used at call sites: `OpenOnDoubleClick`, `Selected`,
`DefaultOpen`, `AllowOverlap`, `SpanFullWidth`.

`SetNextItemOpen(true, ImGuiCond_Once)` opens ancestors of the selection
(EntityTree) and the settings root.

`TreeNodeUpdateNextOpen` is used **only** in `EntityTreeWindow` to tell
expand/collapse apart from selection (so clicking the arrow does not change
the selected entity).

**Distinct call sites:** 47 `ImGui::` tree-family lines. Wrapper callers:
`ImGuiTreeNode` ×11, `ImGuiTreeLeaf` ×9, `ImGuiCollapsingHeaderProps` ×1,
style push/pop ×3 windows.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:169-174` | `TreeNodeUpdateNextOpen` + `ImGuiTreeNode` / `ImGuiTreeLeaf` per RID |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:589-600` | scene root node / leaf |
| `Editor/Source/Skore/Window/ProjectBrowserWindow.cpp:199` | folder `ImGuiTreeNode((void*)asset.id, …)` |
| `Editor/Source/Skore/Window/AnimatorTreeViewWindow.cpp:174` | controller / Layers / Parameters / Avatars |
| `Editor/Source/Skore/Window/SettingsWindow.cpp:187` | settings category tree |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:558` | `CollapsingHeader` per component |
| `Editor/Source/Skore/Window/DebuggerWindow.cpp:455` | `TreeNodeEx((void*)i, …)` profiler zones |

**Holds state:** yes — open/close in ImGui window storage keyed by pointer /
string id; `Selected` is a flag, real selection lives in `SceneEditor` /
browser.

**Retained data-pointer binding:** **required** (see §21). Entity trees,
project-browser folders, animator trees, and settings categories are
rebuilt every `Draw()` from live engine data. Bind
`sk_ui_item_array_t*` via `widget_tree` / `item_bind` (APX-338). Diff by
`id`. Do not emit one retained node per entity by walking the scene
every frame.

**Minimum behaviour**

- Stable id from caller pointer / RID (not from the label string).
- Branch vs leaf; leaf does not push indent / TreePop.
- Open on arrow (and EntityTree also OpenOnDoubleClick).
- Full-row hit / highlight (`SpanFullWidth` / `SpanAvailWidth`).
- Selected styling; default-open and `SetNextItemOpen(..., Once)`.
- Distinguish arrow toggle from row activation (`TreeNodeUpdateNextOpen`).
- Inline rename replaces the label with `ImGuiInputText` on the same row.
- Works *inside a table row* (EntityTree is a 3-column table: name / vis / lock).
- `CollapsingHeader` for property sections without indent/TreePop.
- Header with an extra `…` button (`ImGuiCollapsingHeaderProps`).

**sk-ui today:** `widget_tree` + `item_bind_*` (APX-338) is the retained
array contract. Full TreeNode chrome (table host, inline rename, header
`…` button) is a later wave on this binding.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 9. Table / Columns

**ImGui functions** (no `Columns` API)

```cpp
bool BeginTable(const char* str_id, int columns, ImGuiTableFlags flags = 0,
                const ImVec2& outer_size = ImVec2(0.0f, 0.0f), float inner_width = 0.0f);
void EndTable();
void TableNextRow(ImGuiTableRowFlags row_flags = 0, float min_row_height = 0.0f);
bool TableNextColumn();
bool TableSetColumnIndex(int column_n);
void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0,
                      float init_width_or_weight = 0.0f, ImGuiID user_id = 0);
void TableHeadersRow();
void TableSetupScrollFreeze(int cols, int rows);
void TableSetBgColor(ImGuiTableBgTarget target, ImU32 color, int column_n = -1);
int  TableGetColumnCount();
```

**Table flags the editor sets**

`SizingFixedFit`, `SizingFixedSame`, `SizingStretchProp`, `Resizable`,
`RowBg`, `Borders`, `BordersOuter`, `BordersInnerH`, `BordersInnerV`,
`ScrollY`, `ScrollX`, `NoBordersInBody`.

**Column flags:** `WidthStretch`, `WidthFixed`, `None`, `NoHide`, `NoResize`,
`IndentDisable`, `IndentEnable`.

Outer size used when the table fills leftover height (`PackagesWindow`,
profiler). `TableSetupScrollFreeze(1, 1)` once (profiler).
`TableSetBgColor(CellBg)` fills a whole EntityTree row.

`ImGuiListClipper` is used for the project-browser thumbnail grid and the
resource-selection grid — clipper is table-adjacent, not a separate widget.

**Distinct call sites:** 331 (largest family by line count; many are
`TableNextColumn` in 2-column property layouts).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:514-516` | 3-col tree (`Resizable \| NoBordersInBody`) |
| `Editor/Source/Skore/Window/PackagesWindow.cpp:43-45` | 3-col packages + headers + ScrollY |
| `Editor/Source/Skore/Editor.cpp:840-847` | pending-save 4-col + `TableHeadersRow` |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:95-118` | vec2/3/4 component strips (2–4 cols, default flags) |
| `Editor/Source/Skore/Window/ResourceDebuggerWindow.cpp:250` | 5-col fields, borders + row bg + resizable |
| `Editor/Source/Skore/Window/SettingsWindow.cpp:131` | 2-col resizable master/detail |
| `Editor/Source/Skore/ImGui/ImGui.cpp:1183` | content-table N cols `SizingFixedSame` |

**Holds state:** yes — column widths, sort-not-used, scroll (when ScrollX/Y).

**Retained data-pointer binding:** not hierarchical. Row *contents* for
packages / pending-save / debugger instances should bind
`sk_ui_item_array_t*` with `SK_UI_ITEM_BIND_TABLE` (§21). Property
2-column “label | widget” tables are layout only.

**Minimum behaviour**

- 1–5 columns; stretch and fixed widths; user-resizable.
- Optional header row.
- Optional inner/outer borders and alternating row background.
- ScrollY (and ScrollX on the profiler) with frozen header.
- Per-cell advance (`TableNextColumn`) and per-row colour.
- Host a tree in column 0 (EntityTree).
- Auto column count from available width (thumbnail grid).

**Not used:** `Columns` / `NextColumn` legacy API; `TableGetSortSpecs`
(no click-to-sort in the editor).

**sk-ui today:** no table factory (widgets.c: “no tables”).

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 10. TabBar

**ImGui functions**

```cpp
bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags = 0);
void EndTabBar();
bool BeginTabItem(const char* label, bool* p_open = NULL, ImGuiTabItemFlags flags = 0);
void EndTabItem();
bool TabItemButton(const char* label, ImGuiTabItemFlags flags = 0);
```

Flags used: `ImGuiTabItemFlags_None`, `ImGuiTabItemFlags_SetSelected`.
No tab-bar flags. Close button only when `p_open != NULL` (workspace tabs
when more than one workspace exists).

**Distinct call sites:** 19.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Editor.cpp:732-764` | workspace tabs + `TabItemButton("+")` |
| `Editor/Source/Skore/Window/DebuggerWindow.cpp:59-85` | Statistics / CPU / GPU |
| `Editor/Source/Skore/Window/ResourceDebuggerWindow.cpp:122-144` | Types / Instance (`SetSelected`) |

**Holds state:** yes — selected tab (overridden with `SetSelected` when the
editor drives workspace index).

**Retained data-pointer binding:** no. Workspace list is small and
editor-owned; bind selected index, do not rebuild the bar every frame.

**Minimum behaviour**

- Named tabs; selected tab’s body is submitted between Begin/EndTabItem.
- Optional per-tab close (`bool*`).
- Programmatic select.
- Trailing button tab (`+`) that is not a selectable page.

**sk-ui today:** `widget_tab_bar` / `widget_tab`.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 11. MenuBar / Menu / MenuItem

**ImGui functions**

```cpp
bool BeginMenuBar();
void EndMenuBar();
bool BeginMenu(const char* label, bool enabled = true);
void EndMenu();
bool MenuItem(const char* label, const char* shortcut = NULL,
              bool selected = false, bool enabled = true);
```

`MenuItem(label, shortcut, bool* p_selected, …)` is **not** used.
`BeginMainMenuBar` is **not** used. The host window is opened with
`ImGuiWindowFlags_MenuBar` via `ImGuiCreateDockSpace`.

Shortcut strings are built by the editor (`Ctrl+` / `Alt+` / `Shift+` +
`GetKeyName`); ImGui only *displays* them.

Separators inside menus come from priority gaps (`MenuItem.cpp:141`).

**Distinct call sites:** 19 `ImGui::` menu-family lines. Almost all items
flow through `MenuItemContext::DrawMenuItemChildren`.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Editor.cpp:728` | `BeginMenuBar` around menus + workspace tabs |
| `Editor/Source/Skore/MenuItem.cpp:118` | `MenuItem(label, shortcut, selected, enabled)` |
| `Editor/Source/Skore/MenuItem.cpp:136` | `BeginMenu(label, enabled)` + nested children |
| `Editor/Source/Skore/Editor.cpp:773` | `MenuItem` inside workspace-type popup |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:453` | `BeginMenu` in Add Component popup |

**Holds state:** yes — which submenu is open. Item selected/enabled is
caller-computed each frame.

**Retained data-pointer binding:** no. Menu tree is registered once
(`AddMenuItem("Window/History")` paths) and walked each frame. That
registration tree is already caller-owned; keep it.

**Minimum behaviour**

- Top menu bar on the dock host.
- Nested menus; disabled menus/items.
- Item with optional shortcut text and checked mark.
- Separator between priority groups.
- Same `MenuItem` inside popups (not only the bar).

**sk-ui today:** `widget_menu_bar` / `widget_menu` / `widget_menu_item` /
submenu.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 12. Popup / Modal

**ImGui functions**

```cpp
void OpenPopup(const char* str_id, ImGuiPopupFlags popup_flags = 0);
bool BeginPopup(const char* str_id, ImGuiWindowFlags flags = 0);
bool BeginPopupModal(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0);
void EndPopup();
void CloseCurrentPopup();
```

Editor wrapper:

```cpp
bool ImGuiBeginPopupMenu(const char* str, ImGuiWindowFlags popupFlags = 0, bool setSize = true);
void ImGuiEndPopupMenu(bool closePopup = true);
void ImGuiResourceSelectionPopup(u64 id, TypeID typeId, RID contextRid, bool open,
                                 FnResourceSelectionCallback callback, VoidPtr userData = nullptr);
```

Modal flags used: `ImGuiWindowFlags_NoScrollbar` (Save Content),
`ImGuiWindowFlags_AlwaysAutoResize` (message box).
`ImGuiBeginPopupMenu` pushes padding/colours and optionally
`SetNextWindowSize(ImVec2{300, 0}, Once)`.

**Distinct call sites:** 56 `ImGui::` popup-family lines.
`ImGuiBeginPopupMenu` callers ×14 (context menus + scene option popovers).
`ImGuiResourceSelectionPopup` callers ×5.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Editor.cpp:827` | `BeginPopupModal("Save Content", &open, NoScrollbar)` |
| `Editor/Source/Skore/Editor.cpp:969` | `BeginPopupModal(title, nullptr, AlwaysAutoResize)` OK/Close |
| `Editor/Source/Skore/Editor.cpp:769` | `BeginPopup("workspace-type-popup")` |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:684` | `ImGuiBeginPopupMenu("scene-tree-popup")` |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:208` | colour-picker `BeginPopup` |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:695` | `ImGuiResourceSelectionPopup` |

**Holds state:** yes — ImGui owns open/close; `OpenPopup` is edge-triggered.
Modals dim the background and block input behind.

**Retained data-pointer binding:** no for the popup chrome. The resource
picker *contents* are a filtered asset list (see Image / content item).

**Minimum behaviour**

- Context popup: open on right-click / explicit `OpenPopup`; click-outside
  or `CloseCurrentPopup` dismisses.
- Modal: title, optional `p_open`, blocks the editor, auto-resize or fixed
  child+table body (Save Content).
- Default focus on the primary button (`SetItemDefaultFocus`).
- Styled 300px context menu used everywhere (`ImGuiBeginPopupMenu`).
- Resource picker: search + thumbnail grid or entity list + callback.

**sk-ui today:** `widget_menu_popup` / `widget_context_menu`. No modal
dialog factory.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 13. Child / Window / Layout

**ImGui functions (host + layout)**

```cpp
bool Begin(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0);
void End();
bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0, 0),
                ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0);
bool BeginChild(ImGuiID id, const ImVec2& size = …); // numeric ids used widely
void EndChild();

void SetNextWindowPos / SetNextWindowSize / SetNextWindowViewport;
ImGuiID DockSpace(ImGuiID id, const ImVec2& size, ImGuiDockNodeFlags flags);
void DockBuilderRemoveNode / AddNode / SetNodeSize / SplitNode / DockWindow;

void BeginDisabled(bool disabled = true);
void EndDisabled();
void PushID / PopID;                 // string, int, pointer
void SetNextItemWidth(float);
void PushItemWidth / PopItemWidth;
void Indent / Unindent;
void BeginGroup / EndGroup;
void AlignTextToFramePadding();

// imgui_stacklayout (vendored with Dear ImGui on main)
void BeginHorizontal / EndHorizontal;
void BeginVertical / EndVertical;
void Spring(float weight = 1.0f);
```

Editor wrappers: `ImGuiBegin`, `ImGuiBeginFullscreen`, `ImGuiCreateDockSpace`,
`ImGuiCenterWindow`, `ImGuiDockBuilderReset`, `ImGuiDockBuilderDockWindow`.

Window flags used: `MenuBar`, `NoDocking`, `NoTitleBar`, `NoCollapse`,
`NoResize`, `NoMove`, `NoBringToFrontOnFocus`, `NoNavFocus`,
`NoScrollbar`, `NoScrollWithMouse`, `AlwaysUseWindowPadding`,
`HorizontalScrollbar`, `AlwaysAutoResize`, `NoFocusOnAppearing`.

Child flags used: `false`/`true`/`0` (legacy border bool),
`ImGuiChildFlags_Border(s)`, `ImGuiChildFlags_ResizeX`.

**Distinct call sites:** `BeginChild`/`EndChild` 70; `Begin`/`End` 31
(plus `ImGuiBegin` on every `EditorWindow`); `BeginDisabled`/`EndDisabled` 38;
`Spring` 29; `BeginHorizontal` 15.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/ImGui/ImGui.cpp:833` | `ImGuiBegin` → `Begin("%s###%u", title, id)` |
| `Editor/Source/Skore/ImGui/ImGui.cpp:812` | `DockSpace` under the menu/status chrome |
| `Editor/Source/Skore/Window/ConsoleWindow.cpp:78` | `BeginChild("ScrollingRegion", (0,0), false, HorizontalScrollbar)` |
| `Editor/Source/Skore/Window/ResourceDebuggerWindow.cpp:167` | left pane `ChildFlags_Borders \| ResizeX` |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:228` | `BeginDisabled` around visibility button |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:43` | `Spring()` in a property row |

**Holds state:** yes — window pos/size/dock, child scroll, disabled stack,
ID stack. Dock layout is serialized through
`SaveIniSettingsToMemory` / `LoadIniSettingsFromMemory` (`EditorLayout.cpp`).

**Retained data-pointer binding:** no.

**Minimum behaviour**

- Named dockable window with close flag (`bool* p_open`).
- Fullscreen host (`ImGuiBeginFullscreen`) for the project launcher.
- Child regions: remaining-size `(0,0)`, fixed height toolbars, optional
  border, optional horizontal scrollbar, optional user-resize.
- Scroll query + `SetScrollHereY(1)` (console auto-scroll).
- Dock space + programmatic `DockBuilder*` for default layouts.
- Disable a subtree (grey + no input).
- Horizontal/vertical stack with springs (property rows, thumbnail label).
- `SetNextItemWidth(-1)` fill remaining column.

Dock *interaction* (split, tab, float) is already a sk-ui concern
(`widget_dock_*`). This family is the **panel host + layout primitives**
the editor still needs inside a pane.

**sk-ui today:** `widget_window` (named dockable host + `p_open` close flag),
`widget_fullscreen` (ImGuiBeginFullscreen), `widget_child` (remaining-size
`(0,0)`, fixed-height toolbars, `SK_UI_CHILD_FLAG_BORDER` /
`RESIZE_X` / `HORIZONTAL_SCROLLBAR`), `scroll_view_scroll_to_bottom`
(`SetScrollHereY(1)`), `begin_disabled`/`end_disabled`/`set_disabled`
(scoped stack; greys + blocks input on the whole subtree), `push_id` /
`push_id_int` / `push_id_ptr` / `pop_id`, `set_next_item_width(-1)` fill,
`indent`/`unindent`, `widget_group` + `group_get_extents`,
`widget_horizontal` / `widget_vertical` / `widget_spring`.
**Spring mapping:** `widget_spring(weight)` is an empty flex item with
`flex_grow = weight` (default 1). Clay GROW is unweighted, so every live
spring shares leftover space equally. Dock interaction stays on
`widget_dock_*`.

- [x] Unit test — `plugins/ui/widgets.c`
  (`ui_widget_child_sizing_modes`,
  `ui_widget_disabled_stack_propagation`,
  `ui_widget_id_scope_uniqueness`,
  `ui_widget_item_width_fill`,
  `ui_widget_group_extents`,
  `ui_widget_spring_flex_distribution`)
- [x] Headless UI automation — `plugins/ui/child_window_layout_family_tests.c`
  (`ui_author_child_window_layout_family_scroll_disabled_resizex`, SK_UI_TEST harness)
- [x] lavapipe PNG reviewed — `sandbox/widget_review.c` (`--widget window`,
  `fullscreen`, `child`, `child_resize`, `layout`, `disabled`;
  states default/disabled)

---

## 14. ProgressBar

**ImGui function**

```cpp
void ProgressBar(float fraction, const ImVec2& size_arg = ImVec2(-FLT_MIN, 0),
                 const char* overlay = NULL);
```

**Distinct call sites:** **0 live**. The only hit is a comment in
`Editor.cpp:1192` (status-bar placeholder).

**Holds state / binding:** n/a.

**Minimum behaviour:** none for the editor. Do not implement from this
audit. sk-ui already has `widget_progress` from an earlier widget wave.

- [ ] Unit test *(not required for editor acceptance)*
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 15. ColorEdit / ColorPicker

**ImGui functions**

```cpp
bool ColorButton(const char* desc_id, const ImVec4& col,
                 ImGuiColorEditFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags = 0,
                  const float* ref_col = NULL);
bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
```

Property `Color` fields: `ColorButton` (full item width) opens a `BeginPopup`
containing `ColorPicker4` with
`DisplayMask_ | NoLabel | AlphaPreviewHalf | AlphaBar`.

`ColorEdit3` is used for material RGB (`PropertiesWindow`) and GraphEditor
pins (`NoInputs` — out of scope for the pin, in scope if Properties needs
the compact edit).

ResourceDebugger uses `ColorButton(..., NoTooltip | NoPicker, ImVec2(14,14))`
as a swatch only.

**Distinct call sites:** 6.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:203-220` | button + popup `ColorPicker4` |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:1552` | `ColorEdit3("##v", &v.x)` |
| `Editor/Source/Skore/Window/ResourceDebuggerWindow.cpp:600` | 14×14 swatch |

**Holds state:** yes — picker popup and the `float[3]/[4]` being edited.
Commit via `IsItemDeactivatedAfterEdit`.

**Retained data-pointer binding:** no (`Color*` / `float*`).

**Minimum behaviour**

- Opaque/alpha colour button sized to the property column.
- Popup HSV/RGB picker with alpha bar and half-alpha preview.
- Compact `ColorEdit3` on a `float[3]`.
- Read-only swatch (no picker).

**Not used:** `ColorEdit4`, `ColorPicker3`, `SetColorEditOptions`.

**sk-ui today:** no color factory.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 16. Image / ImageButton / content item

**ImGui functions**

```cpp
void Image(ImTextureID texture_id, const ImVec2& image_size,
           const ImVec2& uv0, const ImVec2& uv1,
           const ImVec4& tint_col, const ImVec4& border_col);
```

(`Image` on this Dear ImGui revision also has an `ImTextureRef` form; the
editor wrapper still passes `GetImGuiTextureId` + size + UVs + tint + border.)

`ImageButton` is **not called**.

Editor wrappers:

```cpp
void ImGuiTextureItem(GPUTexture* texture, const ImVec2& image_size, …);
void ImGuiDrawTexture(GPUTexture* texture, const Rect& rect, …);
void ImGuiDrawTextureView(GPUTextureView* textureView, const Rect& rect, …);
bool ImGuiBeginContentTable(const char* id, f32 thumbnailScale);
ImGuiContentItemState ImGuiContentItem(const ImGuiContentItemDesc& desc);
void ImGuiEndContentTable();
```

`ImGuiContentItemDesc`: `id`, `label`, optional `texture` or `icon`,
`selected`, `thumbnailScale`, `renameItem`, `showError`.
Returned state: hover / click / release / enter (double-click or Enter) /
rename finish + `newName` / screen rect.

**Distinct call sites:** 1 `ImGui::Image` (inside `ImGuiTextureItem`).
`ImGuiDrawTextureView` ×1 (SceneView). `ImGuiBeginContentTable` ×4.
`ImGuiContentItem` ×6 (ProjectBrowser + ProjectManager tiles).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:188` | viewport `ImGuiDrawTextureView` |
| `Editor/Source/Skore/Window/PropertiesWindow.cpp:970` | texture preview `ImGuiTextureItem` |
| `Editor/Source/Skore/Window/ProjectBrowserWindow.cpp:591-673` | zoomable thumbnail grid |
| `Editor/Source/Skore/Project/ProjectManager.cpp:465-477` | recent-project tiles |

**Holds state:** image itself is stateless. Content items hold hover/select/
rename interaction; selection set is caller-owned.

**Retained data-pointer binding:** **yes, flat item array** (`sk_ui_item_t`
+ `SK_UI_ITEM_BIND_TABLE` / `LIST`, §21). The browser rebuilds the grid
every frame from the current folder + filter + zoom. Pass a pointer to a
caller-owned `sk_ui_item_array_t` (`id`, label, icon, selected, error).
Do not create one retained thumbnail node per asset per frame. (Not
hierarchical — folders use the Tree family.)

**Minimum behaviour**

- Textured quad with UV rect, tint, nearest/clamp sampler (viewport).
- Thumbnail cell: image or icon, label (clipped), selected rect, error mark,
  click / double-click / right-click, inline rename.
- Grid column count from available width and zoom.

**sk-ui today:** `widget_image`. No content-item / thumbnail-grid factory.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 17. DragDrop

**ImGui functions**

```cpp
bool BeginDragDropSource(ImGuiDragDropFlags flags = 0);
bool SetDragDropPayload(const char* type, const void* data, size_t sz, ImGuiCond cond = 0);
void EndDragDropSource();
bool BeginDragDropTarget();
bool BeginDragDropTargetCustom(const ImRect& bb, ImGuiID id);
const ImGuiPayload* AcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags = 0);
void EndDragDropTarget();
const ImGuiPayload* GetDragDropPayload();
```

Payload type strings (`EditorCommon.hpp`):

- `SK_ASSET_PAYLOAD` `"sk-asset-payload"` — `AssetPayload` blob
- `SK_ENTITY_PAYLOAD` `"sk-entity-payload"` — empty payload; selection is
  implicit (`SetDragDropPayload(..., nullptr, 0)`)

Flags used: `SourceNoHoldToOpenOthers`, `SourceNoDisableHover`,
`AcceptNoDrawDefaultRect`, `AcceptNoPreviewTooltip`.

**Distinct call sites:** 56.

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:206-216` | drag entity; drop reparents selection |
| `Editor/Source/Skore/Window/EntityTreeWindow.cpp:419` | `BeginDragDropTargetCustom` between-row drop |
| `Editor/Source/Skore/Window/ProjectBrowserWindow.cpp:311-318` | drag asset |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:480-491` | drop asset onto a resource field |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:538-605` | drop asset into the viewport |

**Holds state:** yes — active payload until mouse release.

**Retained data-pointer binding:** no (the payload is transient). Sources
and targets hang off tree rows / content items / property fields.

**Minimum behaviour**

- Begin source on an item; typed payload; preview text.
- Begin target on an item or a custom rect (between-row, full viewport).
- Accept by type; optional no default highlight / no tooltip.
- Peek `GetDragDropPayload` for hover highlighting before accept.

**sk-ui today:** no drag-drop payload API.

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 18. Selectable

**ImGui functions**

```cpp
bool Selectable(const char* label, bool selected = false,
                ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));
```

Flags used: `Disabled`, `SpanAllColumns`, `SpanAvailWidth`,
`AllowDoubleClick`. Optional explicit `buttonSize` (project launcher).

**Distinct call sites:** 15 live (plus one commented).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Editor.cpp:1459-1466` | undo/redo history rows |
| `Editor/Source/Skore/ImGui/FieldRenderers.cpp:258` | combo enum value |
| `Editor/Source/Skore/Project/ProjectManager.cpp:404` | launcher nav |
| `Editor/Source/Skore/Window/ResourceDebuggerWindow.cpp:194` | type list |
| `Editor/Source/Skore/ImGui/ImGui.cpp:2355` | entity picker + `AllowDoubleClick` |

**Holds state:** selected is a caller `bool` passed in; ImGui owns hover.

**Retained data-pointer binding:** no per control. Lists of selectables
(history, types) should bind `sk_ui_item_array_t*` with
`SK_UI_ITEM_BIND_LIST` (§21).

**Minimum behaviour**

- Highlight on hover; selected appearance; return true on activate.
- Disabled (visible, not clickable).
- Span table columns / available width.
- Double-click detection when `AllowDoubleClick` is set.
- Optional fixed size.

**sk-ui today:** no selectable factory (lists are faked with buttons/labels).

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 19. Separator / Spacing / SameLine

**ImGui functions**

```cpp
void Separator();
void SeparatorText(const char* label); // also listed under Text
void Spacing();
void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
void Dummy(const ImVec2& size);
```

`SameLine` variants: default (next item); `SameLine(0, 0)` / `SameLine(0, gap)`
tight icon buttons; `SameLine(labelWidth)` for the collision-matrix grid.

**Distinct call sites:** included in the 214-line layout family
(`Separator` 18, `SameLine` 58, `Dummy` 12, `Spacing` 3, plus stack-layout
springs).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/ConsoleWindow.cpp:44-70` | toolbar `SameLine` + `Separator` between groups |
| `Editor/Source/Skore/MenuItem.cpp:141` | `Separator` between menu priority groups |
| `Editor/Source/Skore/Window/SettingsWindow.cpp:326` | `SameLine(labelWidth)` matrix |
| `Editor/Source/Skore/Window/SceneViewWindow.cpp:870` | `Dummy(ImVec2(0, 2))` |

**Holds state:** no.

**Retained data-pointer binding:** no.

**Minimum behaviour**

- Horizontal rule; in a menu bar / horizontal layout it becomes vertical.
- Horizontal packing with optional x-offset and gap override.
- Vertical spacer (`Spacing` / `Dummy`).

**sk-ui today:** flex gap / dummy boxes. No dedicated separator widget
(menu already draws separators).

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 20. Tooltip

**ImGui functions**

```cpp
bool BeginTooltip();
void EndTooltip();
```

`SetTooltip` / `BeginItemTooltip` are **not** used. One site gates on
`IsItemHovered(ImGuiHoveredFlags_DelayNormal)`.

**Distinct call sites:** 2 begin/end pairs (4 lines).

**Representative sites**

| Site | Use |
| --- | --- |
| `Editor/Source/Skore/Window/ProjectBrowserWindow.cpp:730` | delayed hover card (table of asset info) |
| `Editor/Source/Skore/Window/DebuggerWindow.cpp:592` | profiler segment: coloured duration text |

**Holds state:** transient follow-mouse window.

**Retained data-pointer binding:** no.

**Minimum behaviour**

- Hover + optional delay opens a floating window that does not steal focus.
- May contain a small table / coloured text (not text-only).

**sk-ui today:** no tooltip factory (a vision PNG exists from an earlier
composed popup; that is not this widget).

- [ ] Unit test
- [ ] Headless UI automation
- [ ] lavapipe PNG reviewed

---

## 21. Per-frame data and retained pointer binding

ImGui rebuilds the *description* of the UI every `Draw()`. Most editor
widgets bind a **scalar** (`bool*`, `float*`, `String&`). Three surfaces
feed **collections that change every frame** and must not be turned into
“create N retained nodes every tick”.

**This section is the verbatim contract.** Hierarchical-widget tasks
(TreeNode, and any later list / combo / table that binds a mutating
collection) must use the types and rules below unchanged. Public
definitions live in `plugins/ui/ui.h` (`sk_ui_item_t`,
`sk_ui_item_array_t`, `widget_tree` / `widget_list` / `widget_item_view`,
`item_bind_*`).

### 21.1 Item / array types (use these, do not invent another)

```c
#define SK_UI_ITEM_NONE     0xFFFFFFFFu
#define SK_UI_ITEM_ID_NONE  0ull

typedef enum sk_ui_item_flag_t {
    SK_UI_ITEM_FLAG_NONE     = 0,
    SK_UI_ITEM_FLAG_LEAF     = 1u << 0,
    SK_UI_ITEM_FLAG_SELECTED = 1u << 1,
    SK_UI_ITEM_FLAG_OPEN     = 1u << 2,
    SK_UI_ITEM_FLAG_DISABLED = 1u << 3,
    SK_UI_ITEM_FLAG_ERROR    = 1u << 4,
} sk_ui_item_flag_t;

typedef enum sk_ui_item_bind_kind_t {
    SK_UI_ITEM_BIND_TREE  = 0,
    SK_UI_ITEM_BIND_LIST  = 1,
    SK_UI_ITEM_BIND_COMBO = 2,
    SK_UI_ITEM_BIND_TABLE = 3,
} sk_ui_item_bind_kind_t;

typedef struct sk_ui_item_t {
    u64 id;            /* Stable identity. Unique among live items; not 0. */
    u64 parent_id;     /* 0 = root. Must be another item's id or 0. */
    u32 first_child;   /* Optional packed children; SK_UI_ITEM_NONE if unused. */
    u32 child_count;   /* 0 = derive children by scanning parent_id. */
    u32 flags;         /* sk_ui_item_flag_t bits. */
    u32 icon;          /* Optional host icon / texture id; 0 = none. */
    const_chr_t label; /* Caller-owned UTF-8; NULL treated as "". */
} sk_ui_item_t;

typedef struct sk_ui_item_array_t {
    sk_ui_item_t* items; /* Caller-owned storage; NULL iff count == 0. */
    u32 count;
    u32 revision;        /* Optional; bump on mutation (not required). */
} sk_ui_item_array_t;
```

`widget_tree(ctx, parent, &array, id)` is `widget_item_view` with
`SK_UI_ITEM_BIND_TREE`. Lists, combo popups, and table row sets use the
**same** `sk_ui_item_array_t*` with `SK_UI_ITEM_BIND_LIST` /
`COMBO` / `TABLE` (or `widget_list`). Do not add a second item struct.

### 21.2 Ownership

- The **caller** allocates and frees `sk_ui_item_array_t` and `items[]`.
- The **caller** owns every `label` string. A label pointer must stay
  valid until the next `item_bind_sync` after that item is removed or
  the pointer is replaced.
- The widget stores the **pointer** to the `sk_ui_item_array_t` (not a
  copy of the items). It never frees the array, the items, or the labels.
- Destroying the host widget frees only widget-owned row nodes and the
  id → state maps. The caller array is untouched.
- The `sk_ui_item_array_t` object itself must outlive the bind (do not
  pass a temporary). Replacing `array->items` / `array->count` in place
  is the supported grow/shrink path (realloc of the item buffer).

### 21.3 What invalidates

- Freeing or moving the `sk_ui_item_array_t` while a widget still holds
  the pointer is invalid. Call `item_bind_set_array(ctx, host, NULL)`
  (or destroy the host) first.
- `items == NULL` with `count > 0` is treated as empty.
- `id == 0` (`SK_UI_ITEM_ID_NONE`) is skipped.
- Duplicate ids: the first occurrence in `items[]` wins; later copies
  are ignored for hierarchy and row identity.

### 21.4 What happens when the pointer's contents change between frames

The host widget is **not** recreated. On each `item_bind_sync` (also
invoked automatically from `style_resolve` / `harness_step`):

- New ids get a row node.
- Removed ids have their row node destroyed.
- Surviving ids keep the same row handle (generation-stable).
- Labels, flags, parent, and sibling order are applied in place.
- TREE: rows whose ancestors are collapsed are not materialized.

Callers mutate the array (add, remove, reorder, relabel) and step a
frame. Do not destroy / recreate the tree widget.

### 21.5 Stable identity (selection / expansion survive mutation)

`id` is the identity. Open and selected state live in widget-owned maps
keyed by `id`, seeded once from `SK_UI_ITEM_FLAG_OPEN` /
`SK_UI_ITEM_FLAG_SELECTED`. Insert, remove, reorder, and relabel do not
drop that state. Removing an item and adding it back with the same `id`
restores open/selected (e.g. a filter). `item_bind_clear_state` wipes
maps. After interaction the widget writes OPEN/SELECTED back onto the
live item flags so the caller can read them.

`item_bind_set_selected` adds or removes without clearing others
(multi-select). A row click is exclusive select.

### 21.6 Hierarchy

`parent_id == 0` is a root. If the parent item has `child_count > 0`
and `first_child + child_count` is in range, children are that slice
(in slice order). Otherwise children are every item whose `parent_id`
matches, in array order. Cycles stop at depth 64.

### 21.7 Expand-arrow vs row-activate

Click the arrow node (`{host}/a{id}`) to toggle open without changing
selection (`item_bind_last_was_arrow` is non-zero). Click the row
(`{host}/i{id}`) to select (exclusive) and activate (`last_was_arrow`
is 0). This is the EntityTree `TreeNodeUpdateNextOpen` distinction.

### 21.8 Known hierarchical producers

| Panel | Item identity | Children come from |
| --- | --- | --- |
| Entity Tree | RID / `Entity*` | `EntityResource` child list + prototype-removed |
| Project Browser folders | asset id | folder contents |
| Animator Tree | layer / parameter / avatar id | controller resource |
| Settings categories | settings object id | nested settings |
| Bone tree (FieldRenderers) | bone id | skeleton |

### 21.9 Flat item array (same binding, `SK_UI_ITEM_BIND_LIST` / `COMBO` / `TABLE`)

| Surface | Widget kind | Item `id` |
| --- | --- | --- |
| Project Browser / launcher tiles | TABLE or LIST (§16) | asset / project id |
| History | LIST | undo/redo index |
| Packages | TABLE | package path / id |
| Console lines | Text (already ported) | dirty-rebuild on sink version; not this binding |
| Resource picker | LIST or TABLE | RID |
| Combo enum popup | COMBO | enum value / index |

Flat kinds ignore expand: every live item is a visible row, in array
order. `parent_id` may still be stored (entity picker nesting) but does
not hide children.

### 21.10 Not this problem

Buttons, checkboxes, sliders, single text fields, menus, tabs, popups,
colour pickers, drag-drop payloads, tooltips — bind scalars or transient
interaction state only.

---

## 22. Not in scope

Do **not** create work items, factories, or acceptance checks from this
audit for the following.

### 22.1 Gizmos (explicit exclusion)

| Piece | Where on main | Notes |
| --- | --- | --- |
| ImGuizmo | `ThirdParty/ImGuizmo/*`; `ImGui.cpp` `ImGuizmo::BeginFrame` / style; `SceneViewWindow` transform tools | 3D manipulate / snap. Scene toolbar **buttons** that *select* the gizmo mode are ordinary Button/SelectionButton (§2) and stay in scope. |
| `DrawGizmos` entity event | `SceneViewRenderPipeline.cpp:455` | Viewport overlay, not an ImGui widget. |

### 22.2 Node / node-graph widgets (explicit exclusion)

| Piece | Where on main |
| --- | --- |
| `GraphEditor` canvas (nodes, pins, links, grid, pan/zoom, lasso) | `Editor/Source/Skore/ImGui/GraphEditor.cpp` |
| Material graph window | `Window/MaterialGraphEditorWindow.cpp` |
| Animator graph window | `Window/AnimatorGraphWindow.cpp` |
| Generic graph window | `Window/GraphEditorWindow.cpp` |

Pin *value* controls inside GraphEditor call the same Combo / Drag /
Checkbox / InputText / ColorEdit3 families already listed. Those families
remain in scope for Properties / Debugger; the **canvas** does not.

### 22.3 ImGui widgets the editor never calls

`RadioButton`, `ImageButton`, `Columns` / `NextColumn`, `ProgressBar`
(commented only), `PlotLines` / `PlotHistogram`, `ArrowButton`,
`InputTextWithHint`, `ColorEdit4`, `ColorPicker3`, `BeginMainMenuBar`,
`SetTooltip`, `ListBox()` convenience overloads, `SliderAngle` /
`VSlider*`, `Bullet` / `BulletText`, `LabelText`.

sk-ui `widget_radio`, `widget_toggle`, and `widget_progress` may stay for
other hosts; they are **not** editor-ImGui requirements.

### 22.4 Host / backend (not widgets)

`CreateContext` / `DestroyContext` / `NewFrame` / `EndFrame` / `Render` /
`GetDrawData`, Vulkan/SDL backends, `SetAllocatorFunctions`,
`ShowDemoWindow`, ini `SaveIniSettingsToMemory` /
`LoadIniSettingsFromMemory`, font atlas / `SetFontRasterizerDensity`,
`GetIO` / `GetStyle` / `GetWindowDrawList` immediate geometry used to
decorate existing widgets (focus rects, thumbnail selection, dock status
line). Draw-list decoration is not a widget family.

### 22.5 Platform / engine (not widgets)

Native file/folder dialogs (`Platform::PickFolder`), 3D scene view
picking, GPU viewport production, undo stack internals, asset import
pipelines, Font Awesome icon *font* loading (labels already contain the
codepoints).

---

## 23. Editor surfaces → families

| main panel | Families it depends on |
| --- | --- |
| Dock host / menus / workspaces (`Editor.cpp`) | Window/Dock, Menu, TabBar, Popup/Modal, Table (save), Button, Text, Layout |
| Console | Button, Checkbox, InputText (filter), Child (scroll), Text |
| Entity Tree | Tree **+ pointer binding**, Table, Button, InputText (search/rename), Popup, DragDrop |
| Project Browser | Tree, Table, Content item **+ pointer binding**, Slider (zoom), InputText (search), Button, Popup, DragDrop, Tooltip |
| Properties | Table, CollapsingHeader, InputText/Scalar, Checkbox, Combo, Slider/Drag, Color, Button, Popup, DragDrop, Image |
| Scene View | Image (viewport), Button / SelectionButton, Popup, Slider, InputFloat3, Checkbox, DragDrop. **Gizmo out of scope.** |
| History | Window, ListBox, Selectable |
| Packages | Button, Table, Text, SmallButton |
| Settings | Tree, Table, Child, CollapsingHeader, Checkbox matrix, Input (via `ImGuiDrawResource`) |
| Debugger / ResourceDebugger | TabBar, Table, Tree, Child, Combo, Selectable, Text, Tooltip |
| Animator Tree | Tree, InputText (rename), Popup |
| Project launcher (`ProjectManager`) | Fullscreen window, Child, Selectable, Content item, InputText, Button, Popup |
| Graph / Material / Animator graph | **Out of scope** (canvas). Toolbar Button / Checkbox / InputTextMultiline stay in those families. |

---

## 24. Acceptance tracking

A family is **done** for the goal when all three boxes on its section are
checked:

1. **Unit test** — model + interaction in `sk-tests` (no GPU).
2. **Headless UI automation** — `query_*` / `action_*` / test-engine scenario
   (`docs/ui-automation-api.md`, `docs/ui-test-engine-design.md`).
3. **lavapipe PNG reviewed** — offscreen capture compared (or explicitly
   vision-reviewed) against a rubric for the *editor* overload, not a
   generic sample.

Check #3 uses the committed `sk-sandbox --widget` host
(`docs/widget-lavapipe-png-review.md`): build `sk-sandbox`, force lavapipe
with `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` → `lvp_icd.json`, run
`sk-sandbox --widget <family> --out <dir>`, and **open the PNGs**. Do not
add CTest cases and do not shell out to a vision / LLM CLI.

This manifest is the queue. Later waves tick the boxes; they do not
re-audit `main` unless a new editor panel appears there.
