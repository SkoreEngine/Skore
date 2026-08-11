# UI ↔ editor migration (APX-139)

**Goal:** Prove the editor use case for sk-ui without a full Dear ImGui replacement.
Port **one** self-contained main-branch panel to the retained ui plugin, keep a
still-ImGui (or ImGui-contract) path for the rest of the editor, run **both in
the same frame**, and record the practical integration answers this surfaces.

| Item | Choice |
| --- | --- |
| Ported panel | **Console** (`main` `Editor/Source/Skore/Window/ConsoleWindow`) |
| sk-ui implementation | `editor/console_panel.c` — retained tree, logger sink, filters, scroll |
| Still-ImGui path | `editor/imgui_shell.c` — **stand-in** for main’s ImGui Hierarchy chrome |
| Dual host | `editor/editor_ui_host.c` — arbitration + frame order |
| Run | `sk-editor --ui-migration` |
| Tests | `editor_console_panel_*`, `editor_ui_host_dual_stack_*`, `editor_imgui_shell_*` |

v2 does **not** vendor Dear ImGui. The shell models the same host-facing
contract main’s ImGui stack exposes (`begin_frame` → draw panels →
`want_capture_*`) so coexistence is real at the host boundary. Replacing the
shell with a true ImGui rehost later should not require redesigning
`editor_ui_host` — only the shell’s draw/input bodies.

---

## 1. What the Console port required

### 1.1 Mapping from ImGui ConsoleWindow

| main ImGui | sk-ui port |
| --- | --- |
| `ImGui::Button("Clear")` | `widget_button` + `on_click` → clear buffer |
| `ImGui::Checkbox` level filters | `widget_checkbox` ×6 + `checkbox_set_on_change` |
| Collapse / Auto-scroll checkboxes | same |
| `ImGuiTextFilter` | `widget_text_input` + substring filter in `sync` |
| `BeginChild` scrolling region | `widget_scroll_view` + content labels |
| `TextUnformatted` colored by level | `widget_label` + inline `SK_UI_SP_COLOR` per level |
| `ConsoleSink` message ring | `sk_logger` sink (`add_sink`) + fixed ring of 256 lines |
| Rebuild every frame | **Retained:** toolbar built once; log rows rebuilt only when `version` changes |

### 1.2 Retained state binding (vs ImGui immediate)

ImGui rebuilds the entire console UI every `Draw()` call from C++ member fields
and the sink arrays. The sk-ui port:

1. **Builds the chrome once** (`create`): panel, toolbar, filters, empty scroll.
2. **Keeps editor-side state** in `sk_editor_console_panel_t` (level toggles,
   collapse, filter string cache, line ring, version counter).
3. **Binds state → nodes** in `sk_editor_console_panel_sync`:
   - Read checkbox / text_input props (widgets own interaction).
   - When `version` bumps (new log, clear, filter change, level toggle),
     destroy previous line labels and recreate visible ones under the scroll
     content node.
4. **Does not** rebuild toolbar widgets every frame.

That is the essential retained model: stable handles for chrome; content list
is a **diffed/rebuild-on-dirty** region rather than a full immediate tree.

### 1.3 Host pipeline used

```text
console_panel_sync
  → style_resolve → layout(logical) → layout_apply_scale → paint
  → (optional) renderer_prepare / renderer_encode   // not required for CPU proof
```

Same order as player sample menu and `harness_step`. Dual host adds the ImGui
shell draw **after** sk-ui layout so console absolute rects exist for hit tests.

---

## 2. Integration answers (surfaced by the dual stack)

### 2.1 Input arbitration

**Rule implemented in `sk_editor_ui_host_pointer`:**

1. Ensure a layout pass has run (abs rects valid).
2. If the pointer is over the **console abs border box**, **or** sk-ui holds
   pointer capture / keyboard focus → dispatch only to sk-ui
   (`input_dispatch`).
3. Else → feed the ImGui-path shell (`set_pointer` + immediate hit test).

**Keyboard:** if `ui->wants_keyboard()` (filter text field focused), keys/text
go only to sk-ui; otherwise available for ImGui (shell has no text focus today).

**Combined capture** for gameplay/viewport:

```text
want_mouse     = ui->wants_mouse(ctx)     || imgui_shell_want_capture_mouse
want_keyboard  = ui->wants_keyboard(ctx)  || imgui_shell_want_capture_keyboard
```

This mirrors main’s use of `ImGuiIO::WantCapture*` while adding an explicit
**region first-refusal** for the sk-ui dock (needed because two systems do not
share one hit-test stack).

**Platform gap:** `platform_window` still has no public mouse/key/text events
(design doc G-input). Windowed `--ui-migration` runs both stacks’ CPU frame
every tick; live pointer routing is proven in unit tests via synthetic
`sk_editor_ui_host_pointer`. When platform input lands, the host maps GLFW
events → `sk_ui_input_event_t` + shell pointer the same way.

### 2.2 Render ordering and pass integration

| Layer | When | Output |
| --- | --- | --- |
| ImGui-path shell | After sk-ui layout; same host frame | CPU draw items (rect/text) |
| sk-ui Console | style → layout → paint | `sk_ui_draw_list_t` (physical px) |

**Documented order for a future GPU pass:**

1. Begin swapchain / UI render pass (clear editor chrome color).
2. Encode **ImGui-path** (or real ImGui draw data) first — underlay / docked
   windows that are not yet ported.
3. Encode **sk-ui** draw list second (`renderer_prepare` outside pass,
   `renderer_encode` inside) so the ported Console composites **on top**.
4. End pass / present.

Rationale: the ported panel is treated as the foreground migration target;
ImGui remains the bulk chrome until more panels move. If a panel is docked
*under* ImGui windows, reverse encode order for that region only — still one
pass, two encode phases.

No separate process-wide `OnRecordRenderCommands` bus exists on v2 yet (G6).
The dual host **hard-codes** phase order, same as player. A future app event
bus should call: ImGui NewFrame → editor windows → sk-ui frame → record both.

### 2.3 Editor-side state vs ImGui rebuild-every-frame

| Concern | ImGui (main) | sk-ui Console port |
| --- | --- | --- |
| Widget identity | Implicit stack IDs per frame | Stable `sk_ui_node_t` handles |
| Log lines | Loop `TextUnformatted` each Draw | Labels recreated on dirty version |
| Filter / toggles | C++ members read by widgets | Members + widget props; sync merges |
| Interaction | Inside `Draw` | Widget callbacks + `input_dispatch` |
| Persist layout | `EditorSerialize` / ini | Not yet; host rect is flex layout |

**Binding pattern for later panels:** keep domain state in editor C structs;
push into node props/text only when dirty; let sk-ui own hover/focus/scroll
chrome. Avoid “rebuild entire tree every frame” unless the panel is a true
immediate graph (then keep ImGui or a dedicated immediate layer).

---

## 3. What is still missing for the remaining panels

Named against main `Editor/Source/Skore/Window/*` and ImGui helpers.

### 3.1 Widgets / primitives not in sk-ui v1

| Need | Used by (examples) | Status |
| --- | --- | --- |
| **Docking / multi-viewport** | All docked windows, `EditorWorkspace` | Out of v1 by design |
| **Menu bar / menu items** | `Editor` menus, File/Edit/Window | Missing |
| **Tree view** (expand/collapse, multi-select, DnD) | EntityTree, ProjectBrowser, AnimatorTree | Missing |
| **Tables** (resizable columns, sort, clipper) | Packages, ProjectBrowser, Properties sections | Missing |
| **Property / reflection field widgets** | PropertiesWindow, FieldRenderers | Missing (large) |
| **Graph canvas** (nodes, wires, pan/zoom) | GraphEditor, MaterialGraph, AnimatorGraph | Missing |
| **3D viewport host** (texture in UI + pick) | SceneViewWindow | Missing (RHI host, not just UI) |
| **Modal dialogs** | Confirm/error, Save Content | Missing |
| **List box / selectable** | History, resource pickers | Partial via buttons/labels only |
| **Icons / icon font** | Window titles (Font Awesome) | Missing (bitmap font only) |
| **Text filter helper** | Console (ported as substring), browsers | Ad-hoc only |
| **Context menus** | Entity/asset right-click | Missing |
| **Drag-drop payloads** | Asset/entity reparent | Missing |
| **Multi-line text / code** | Shader/debug | Missing |
| **Color picker / curve editors** | Material/animation | Missing |
| **ImGuizmo** | Scene transforms | Missing (viewport tooling) |
| **Horizontal scrollbar child** | Console main | ScrollView is basic; no ImGui child flags |

### 3.2 Platform / engine gaps that block full editor parity

| Gap | Impact |
| --- | --- |
| No window **input events** on `platform_window` | Dual host cannot drive live mouse/key without synthetic feed |
| No frame phase bus (`OnBeginFrame` / `OnRecordRenderCommands`) | Host must hard-code UI order |
| No Dear ImGui vendor/rehost on v2 | Remaining panels stay on `imgui_shell` or wait for rehost |
| No docking layout serialization | Cannot restore main’s workspace layouts |
| Asset thumbnails dropped | Project browser previews |
| Undo/redo UI API on v2 | HistoryWindow not portable yet |

### 3.3 Panel-by-panel notes (sequencing input)

| Panel (main) | Blockers beyond Console set |
| --- | --- |
| History | Undo stack API + list selectables |
| Packages | Table + folder picker |
| EntityTree | Tree + selection events + DnD |
| ProjectBrowser | Tree/grid + thumbnails + import UX |
| Properties | Reflection field renderers (largest) |
| SceneView | Viewport texture + camera + gizmo |
| Settings | Forms + settings RID persistence |
| Graph / Material / Animator | Graph canvas + domain compilers |
| Debugger / ResourceDebugger | Tables + live inspection |

---

## 4. Recommended sequencing for the rest of the migration

1. **Platform input + capture plumbing (P0)**  
   Expose mouse/key/text/wheel on `platform_window`; dual host maps to sk-ui and
   ImGui. Without this, only tests exercise arbitration.

2. **Rehost Dear ImGui (optional P0/P1) or grow `imgui_shell`**  
   Prefer real ImGui for unported panels so main C++ windows can move
   mechanically. Keep `want_capture_*` + dual encode order.

3. **Console polish (P1)**  
   Already on sk-ui: multi-select copy, better filter (case-insensitive),
   collapse fidelity, GPU encode in `--ui-migration`.

4. **History + Packages (P1)**  
   Small surface; forces list/table primitives or careful composition of
   existing widgets.

5. **Tree primitive (P1/P2)**  
   Shared by EntityTree, ProjectBrowser, AnimatorTree — invest once.

6. **Properties / FieldRenderers (P2)**  
   After tree + basic inputs; largest editor code surface.

7. **SceneView + gizmo (P2)**  
   Mostly render-device / camera; UI is a textured rect + overlay.

8. **Graph editors last (P3)**  
   Custom canvas; least helped by flexbox widgets.

9. **Docking / layout (P3 or parallel)**  
   Either keep ImGui docking until the end, or design a sk-ui dock host.
   Do **not** block panel content ports on full docking parity.

10. **Remove ImGui** only when no window remains on the ImGui path and docking
    is replaced or abandoned.

---

## 5. Architectural decisions — no redesign required

The port **did not** hit a v1 blocker that forces a UI redesign:

| Concern | Outcome |
| --- | --- |
| Retained tree enough for Console? | **Yes** — chrome stable; lines dirty-rebuild |
| Coexistence without shared draw list? | **Yes** — two encode phases, one pass |
| Input without shared hit stack? | **Yes** — region + capture arbitration |
| Missing docking in sk-ui? | **Accepted** — ImGui path keeps chrome |
| Missing platform input? | **Escalated as platform gap**, not UI API failure |

If a later panel requires true immediate multi-pass graph editing, keep that
panel on ImGui (or a dedicated canvas) rather than warping sk-ui into ImGui.

---

## 6. File map

| Path | Role |
| --- | --- |
| `editor/console_panel.h/.c` | Retained Console port + logger sink |
| `editor/imgui_shell.h/.c` | Immediate Hierarchy stand-in + capture flags |
| `editor/editor_ui_host.h/.c` | Dual frame, arbitration, tests |
| `editor/main.c` | `--ui-migration` windowed host |
| `docs/ui-editor-migration.md` | This document |
| `docs/ui-system-design.md` | v1 UI design + main ImGui reference |
| `docs/ui-automation-api.md` | Tester/query contract used by console test ids |

---

## 7. How to verify

```bash
# Unit / dual-stack tests (cwd build/bin so sk-ui.so is under ./plugins)
ctest --test-dir build -R sk-tests --output-on-failure
# or: ./build/bin/sk-tests

# Interactive dual stack (requires display + plugins next to binary)
./build/bin/sk-editor --ui-migration
```

Expected: both stacks produce draw output each frame; pointer over Hierarchy
routes to ImGui path; pointer over Console routes to sk-ui; log lines retain
across frames when only the shell is rebuilt.
