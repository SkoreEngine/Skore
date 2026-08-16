# APX-377 — Visual acceptance of the migrated editor

**Task:** APX-377  
**Date:** 2026-08-16  
**Host:** `sk-sandbox-shell` (`sandbox/editor_shell_sandbox.c`)  
**Recipe:** lavapipe offscreen capture (`capture_create` → `paint` → `capture_frame` → `cpu_image_write_png`)  
**Viewport:** 1280×720, scale 1.0  
**Frames:** [`docs/editor/apx-377-frames/`](apx-377-frames/)

This is the goal's final visual gate: look at the v2 editor against the C++
InitDockSpace layout and `docs/editor/migration-manifest.md`. No window
bodies were rewritten in this task. Findings below are filed by location
instead of being fixed broadly.

```bash
cmake --build build --target sk-sandbox-shell
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
(cd build/bin && ./sk-sandbox-shell --out-dir ../../docs/editor/apx-377-frames)
```

| Frame | Workspace | What it proves |
| --- | --- | --- |
| [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png) | Scene (default) | Shell, docked Scene windows, icons, placeholder viewport |
| [`02_graph_workspace.png`](apx-377-frames/02_graph_workspace.png) | Graph (after switch) | Scene-only windows leave; Graph Editor takes Center |
| [`03_scene_restored.png`](apx-377-frames/03_scene_restored.png) | Scene (after switch back) | Captured Scene layout restored |

C++ InitDockSpace zones used as the layout oracle (manifest §1 / `editor_window.h`):

| Zone | Scene (auto-open) | Graph (auto-open) |
| --- | --- | --- |
| Center | Scene Viewport | Graph Editor (scaffold, empty Draw — out of scope §4) |
| RightTop | Entity Tree, History | (none) |
| RightBottom | Properties | Properties |
| BottomLeft | Project Browser | Project Browser |
| BottomRight | Console, Debugger | Console, Debugger |
| On-demand (not auto-opened) | Packages, Settings, Resource Debugger | same |

---

## Check summary

| # | Criterion | Result |
| - | --------- | ------ |
| 1 | Shell / menu bar present | **MET** |
| 2 | All migrated windows docked and drawing | **MET** (with findings) |
| 3 | Icons from Content/Images visible | **MET** |
| 4 | Scene view showing the placeholder texture | **MET** |
| 5 | Mocked panels clearly populated rather than blank | **NOT** |
| 6 | Additional PNG after workspace switch proves restore | **MET** |

**Overall:** the migrated shell, dock map, icon atlas, and Scene placeholder
read as the C++ editor. Default tab choice, Scene Viewport toolbar crowding,
and several mock bodies that do not show in the default 1280×720 frame keep
criterion 5 from passing.

---

## 1. Shell / menu bar — MET

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)

![Scene workspace](apx-377-frames/01_scene_workspace.png)

**Rendered (matches `editor/editor_shell.c` menu priorities and toolbar):**

- Menu bar: File, Edit, Tools, Build, Window, Help (closed; open popups are a
  known painter's-algorithm limitation, manifest §8).
- Toolbar: Save All, Undo, Redo, Play, Pause, Stop, Reset Layout.
- Workspace switcher: Scene tab + "+" on the first frame.

---

## 2. All migrated windows docked and drawing — MET (with findings)

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png),
[`02_graph_workspace.png`](apx-377-frames/02_graph_workspace.png)

Scene auto-open set is in the C++ zones:

| Window | Zone | Visible chrome |
| --- | --- | --- |
| Scene Viewport | Center | Tab + tool row + placeholder canvas |
| Entity Tree | RightTop | Tab (body behind History) |
| History | RightTop | Selected tab; mock undo/redo list |
| Properties | RightBottom | Tab + empty-selection copy |
| Project Browser | BottomLeft | Tab + Import / Assets / zoom / Settings |
| Console | BottomRight | Tab (body behind Debugger) |
| Debugger | BottomRight | Selected tab + Statistics / CPU / GPU bar |

On-demand windows (Packages, Settings, Resource Debugger, mask 0) are
**not** in the default dock. That matches the manifest.

Graph after switch: Scene Viewport / Entity Tree / History leave; Graph
Editor occupies Center; Properties / Project Browser / Console / Debugger
stay. Graph Editor body is empty — expected (out of scope §2.1 / §4,
scaffold `Draw`).

### Findings (not fixed)

1. **RightTop default tab is History, not Entity Tree.**  
   Entity Tree is `order 0`, History is `order 10`
   (`editor/windows/entity_tree_window.c`, `editor/windows/history_window.c`).
   C++ would typically show the first/lowest-order window. The Entity Tree
   mock hierarchy (Demo Scene / Main Camera / …) is therefore not on screen
   at rest. Location: dockspace tab activation in
   `editor/editor_window.c` `dockspace_dock_model_build`.

2. **BottomRight default tab is Debugger, not Console.**  
   Console `order 10`, Debugger `order 20`. Same last-tab-wins behaviour.
   Console log lines are not visible at rest.

3. **Scene Viewport tool labels collide at 1280×720.**  
   The row is `Sel Mov Rot Scl Glo Sna Gric … Play Stop 2D 3D Vol Cam Opts`
   with overlapping glyphs. Buttons are created in
   `editor/windows/scene_view_window.c` (~815–859, `"Sel"` / `"Move"` /
   `"Rot"` / `"Snap"` / `"Grid"` / …) without enough row width.

---

## 3. Icons from Content/Images — MET

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)
(bottom strip)

The sandbox icon strip (APX-367) samples the atlas through
`sk_editor_icons_get`. All five ids render as the C++ bitmaps:

| Id | File | Visible |
| --- | --- | --- |
| folder | `FolderIcon.png` | yellow folder |
| file | `FileIcon.png` | white document |
| logo_small | `LogoSmall.jpeg` → `logo_small.png` | red phoenix |
| logo_minimal | `minimalist-logo.png` | list + square in a circle |
| skore | `skore.ico` → `skore.png` | red phoenix |

### Finding (sandbox host, not an editor window)

The strip is absolutely positioned at `y = 640` (`SANDBOX_H - 80`) in
`sandbox/editor_shell_sandbox.c` `sandbox_build_icon_strip` and covers the
bottom of the BottomLeft / BottomRight leaves. Project Browser folder/file
tiles (the in-window consumers of those icons) cannot be judged on this
frame. Strip labels `logo_small` and `logo_minimal` also run together.

---

## 4. Scene view placeholder texture — MET

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)
center pane

Center is a letterboxed dark canvas with the CPU placeholder from
`sv_tex_generate` in `editor/windows/scene_view_window.c`: vertical
gradient, subtle cell grid, lighter center crosshair, grey border. Not a
solid empty panel. Real scene rendering stays out of scope (manifest §2.3).

---

## 5. Mocked panels populated rather than blank — NOT

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)

| Panel | Expected mock | On the frame |
| --- | --- | --- |
| History | seeded undo/redo scopes | **Populated** — Undo: Create Entity, Move Entity, Delete Entity, Rename Asset; Redo: "No actions." |
| Properties | empty-selection until an entity/asset is selected | **OK** — "Select something..." (not a blank dock) |
| Debugger Statistics | FPS, frame time, CPU, working set, VRAM (`dgb_build_statistics`) | **Blank** — only the Statistics / CPU Profiler / GPU Profiler bar; no numbers in the visible body |
| Console | logger ring | **Not visible** (Debugger selected) |
| Entity Tree | Demo Scene / Main Camera / Directional Light / Player / Character Mesh | **Not visible** (History selected) |
| Project Browser | folder tree + FolderIcon/FileIcon grid | **Chrome only** — Import / Assets / slider / Settings / clipped "Assets" tree; no tiles |

### Findings (not fixed)

4. **Debugger Statistics body is empty on the default frame.**  
   `editor/windows/debugger_window.c` `dgb_build_statistics` should emit FPS
   / frame time / CPU / memory rows under the Statistics tab. None of those
   labels appear in the gap between the inner tab bar and the icon strip.

5. **Project Browser content grid is not on screen.**  
   `editor/windows/project_browser_window.c` is supposed to draw folder/file
   tiles from the icon atlas (manifest §3.6). At 1280×720 the BottomLeft
   leaf only fits the toolbar and a clipped "Assets" tree row. Combined with
   finding 3's overlay, the grid cannot be confirmed.

---

## 6. Workspace restore — MET

**Frames:** [`02_graph_workspace.png`](apx-377-frames/02_graph_workspace.png),
[`03_scene_restored.png`](apx-377-frames/03_scene_restored.png)

![Graph workspace](apx-377-frames/02_graph_workspace.png)

![Scene restored](apx-377-frames/03_scene_restored.png)

Same process: Scene → `sk_editor_shell_switch_workspace(GRAPH)` (outgoing
Scene captured) → Graph frame → switch back to Scene (saved layout
restored).

- Graph: switcher shows Scene + **Graph** (Graph selected); Center is
  **Graph Editor**; RightTop Entity Tree / History are gone; Properties /
  Project Browser / Console / Debugger remain.
- Restored Scene: Center is **Scene Viewport** with the same placeholder;
  RightTop Entity Tree / History return; Properties / bottom strip return.
  The Graph workspace tab stays in the switcher (workspace still open).

That is visual proof the layout store captured Scene, swapped the live dock
model, and restored Scene.

---

## What was not opened (intentional)

Packages, Settings, and Resource Debugger stay on-demand (manifest §3.5,
§3.8, §3.10). Graph / Animator / Material graph windows stay scaffolds
(§4). This task did not click those menus.
