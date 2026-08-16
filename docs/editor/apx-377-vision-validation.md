# APX-377 / APX-385 — Visual acceptance of the migrated editor

**Task:** APX-385 (re-render + re-judge after APX-382 / APX-383 / APX-384 chrome fixes)  
**Date:** 2026-08-16  
**Host:** `sk-sandbox-shell` (`sandbox/editor_shell_sandbox.c`)  
**Recipe:** lavapipe offscreen capture (`capture_create` → `paint` → `capture_frame` → `cpu_image_write_png`)  
**Viewport:** 1280×720, scale 1.0  
**Frames:** [`docs/editor/apx-377-frames/`](apx-377-frames/)  
**Rebuild:** Release `sk-sandbox-shell` (Ninja), then recapture overwrote all four PNGs.

This is the goal's final visual gate: look at the v2 editor against the C++
InitDockSpace layout and `docs/editor/migration-manifest.md`. APX-379 activates
the lowest-order tab in each leaf; APX-380 sizes Scene Viewport toolbar buttons
from their labels; APX-382 sizes Console severity labels from real glyph
advance so they no longer wrap mid-word; APX-383 labels the scene-options tool
`Scn` instead of `…`; APX-384 sizes the Entity Tree search input to the toolbar
height with 3px vertical padding so the hint is not clipped. APX-385 rebuilds
the host, recaptures the four frames, and re-judges.

```bash
cmake --build build --target sk-sandbox-shell
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
(cd build/bin && ./sk-sandbox-shell --out-dir ../../docs/editor/apx-377-frames)
```

| Frame | Workspace | What it proves |
| --- | --- | --- |
| [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png) | Scene (default) | Shell, docked Scene windows, Entity Tree hierarchy, Console log lines, Project Browser folder/file tiles, Scene placeholder, Content/Images icon strip |
| [`02_graph_workspace.png`](apx-377-frames/02_graph_workspace.png) | Graph (after switch) | Scene-only windows leave; Graph Editor takes Center |
| [`03_scene_restored.png`](apx-377-frames/03_scene_restored.png) | Scene (after switch back) | Captured Scene layout restored |
| [`04_on_demand_windows.png`](apx-377-frames/04_on_demand_windows.png) | Scene + Window menu | Packages, Settings, Resource Debugger opened; Debugger Statistics body populated |

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
| 2 | All migrated windows docked and drawing | **MET** |
| 3 | Icons from Content/Images visible | **MET** |
| 4 | Scene view showing the placeholder texture | **MET** |
| 5 | Mocked panels clearly populated rather than blank | **MET** |
| 6 | Additional PNG after workspace switch proves restore | **MET** |

**Overall:** all six criteria are accepted. The migrated shell, dock map, icon
atlas, Scene placeholder, and mock window bodies read as the C++ editor.

The three chrome defects this wave targeted are gone on the new frames (see
[Chrome-fix confirmation](#chrome-fix-confirmation-apx-382--383--384--387)). Other
migrated chrome still clips or overlaps; those are listed with `file:line`
below and are **not** accepted.

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

## 2. All migrated windows docked and drawing — MET

**Frames:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png),
[`02_graph_workspace.png`](apx-377-frames/02_graph_workspace.png),
[`04_on_demand_windows.png`](apx-377-frames/04_on_demand_windows.png)

Scene auto-open set is in the C++ zones. APX-379 activates the lowest-order
window of each leaf (Entity Tree, not History; Console, not Debugger).

| Window | Zone | Visible chrome |
| --- | --- | --- |
| Scene Viewport | Center | Tab + tool row + placeholder canvas |
| Entity Tree | RightTop | Selected tab; Demo Scene hierarchy |
| History | RightTop | Sibling tab |
| Properties | RightBottom | Tab + empty-selection copy |
| Project Browser | BottomLeft | Tab + Import / Assets / zoom / Settings + tiles |
| Console | BottomRight | Selected tab + toolbar + seeded log lines |
| Debugger | BottomRight | Sibling tab; Statistics body on frame 04 |

On-demand windows on frame 04 (opened via `sk_editor_shell_open_window`, the
Window-menu path):

| Window | How it appears |
| --- | --- |
| Packages | Floating `widget_window`; Add Package + Name/Path table |
| Settings | Floating `widget_window` titled Editor Settings; group tree + entries |
| Resource Debugger | Docked to Center as a tab next to Scene Viewport |

Graph after switch: Scene Viewport / Entity Tree / History leave; Graph
Editor occupies Center; Properties / Project Browser / Console / Debugger
stay. Graph Editor body is empty — expected (out of scope §2.1 / §4,
scaffold `Draw`).

Scene Viewport tool labels do not collide at 1280×720 (APX-380 / APX-383): the
row reads `Sel Move Rot Scl Glo Snap Grid Scn Play Stop 2D 3D Vol Cam Opts`.
No tool button is an ellipsis.

The Console toolbar (APX-382) keeps every severity checkbox label intact on one
line (`Trace Debug Info Warn Error Fatal`) plus Clear / Collapse / Auto-scroll
on the same row. No severity label wraps mid-word.

---

## 3. Icons from Content/Images — MET

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)

Two consumers are on the frame:

1. **Project Browser content grid** (the in-window consumer, manifest §3.6)
   draws folder/file tiles through `sk_editor_icons_get` + atlas UVs:
   yellow `FolderIcon` tiles for Scenes / Textures, white `FileIcon` tiles
   for Main.scene / Hero.png.
2. **Sandbox icon strip** (APX-367) sits in the Scene Viewport letterbox so
   it does not cover BottomLeft / BottomRight. All five ids render:

| Id | File | Visible |
| --- | --- | --- |
| folder | `FolderIcon.png` | yellow folder |
| file | `FileIcon.png` | white document |
| logo_small | `LogoSmall.jpeg` → `logo_small.png` | red phoenix |
| logo_minimal | `minimalist-logo.png` | list + square in a circle |
| skore | `skore.ico` → `skore.png` | red phoenix |

---

## 4. Scene view placeholder texture — MET

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)
center pane

Center is a letterboxed dark canvas with the CPU placeholder from
`sv_tex_generate` in `editor/windows/scene_view_window.c`: vertical
gradient, subtle cell grid, lighter center crosshair, grey border. Not a
solid empty panel. Real scene rendering stays out of scope (manifest §2.3).

---

## 5. Mocked panels populated rather than blank — MET

**Frames:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)
(default rest), [`04_on_demand_windows.png`](apx-377-frames/04_on_demand_windows.png)
(Debugger tab + on-demand windows)

| Panel | Expected mock | On the frames |
| --- | --- | --- |
| Entity Tree | Demo Scene / Main Camera / Directional Light / Player / Character Mesh | **Populated** on 01 — hierarchy is the selected RightTop tab (APX-379), not hidden behind History |
| Console | logger ring | **Populated** on 01 — selected BottomRight tab; seeded Info lines (`Scene workspace ready`, mock Assets/Project/Renderer) are on screen |
| Project Browser | folder tree + FolderIcon/FileIcon grid | **Populated** on 01 — Assets tree + four tiles (Scenes, Textures, Main.scene, Hero.png) using Content/Images icons |
| Debugger Statistics | FPS, frame time, CPU, working set, VRAM | **Populated** on 04 — Statistics tab shows Frame / FPS 59.9 / Frame time 16.70 ms / Process CPU 12.5% / Working set / System memory / GPU memory. Console is the default tab (order 10); the host activates Debugger (order 20) on the Window-menu frame so the body is not judged while hidden behind its sibling |
| History | seeded undo/redo scopes | Sibling of Entity Tree (order 10); not required on the default tab |
| Properties | empty-selection until an entity/asset is selected | **OK** — "Select something..." (not a blank dock) |

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
  Project Browser / Console / Debugger remain; tiles and Console lines stay.
- Restored Scene: Center is **Scene Viewport** with the same placeholder;
  RightTop Entity Tree / History return; Properties / bottom strip return.
  The Graph workspace tab stays in the switcher (workspace still open).

That is visual proof the layout store captured Scene, swapped the live dock
model, and restored Scene.

---

## On-demand Window-menu windows — confirmed

**Frame:** [`04_on_demand_windows.png`](apx-377-frames/04_on_demand_windows.png)

![On-demand windows](apx-377-frames/04_on_demand_windows.png)

Opened through `sk_editor_shell_open_window` (the same path as the Window
menu items):

- **Packages** — floating window, Add Package + hint on separate rows, \
  Name/Path table of the two seeded mock folders (see \
  [Chrome-fix confirmation](#chrome-fix-confirmation-apx-382--383--384--387)).
- **Editor Settings** — floating window, left tree (General / Rendering /
  Audio / Physics / Editor), right pane (Project Name, Company Name, Auto Save).
- **Resource Debugger** — Center tab next to Scene Viewport (Types list
  visible behind the floaters).

---

## Chrome-fix confirmation (APX-382 / 383 / 384 / 387)

Judged on the recaptured 1280×720 frames (01 / 03 for Scene chrome; Console
labels also on 02).

| Defect this wave fixed | Frame | Verdict |
| --- | --- | --- |
| Packages window hint clipped mid-quote (`"Binaries" folder.` hidden) | 04 | **Fixed.** The toolbar is now a column: Add Package on its own row and the hint sentence below it at 13px, full row, wrapping — the whole sentence renders on one line inside the 500px window. |
| Packages Name cells clip `SkoreGame`→`Skore`, `EnginePlugins`→`Engin` | 04 | **Fixed.** The table now sizes its stretch columns from the real ~480px window width instead of the 3×80px default (240px), so the 0.30/0.70 Name/Path split gives ~133px/311px and every basename/path fits its cell. |
| Packages Path cells clip + overlap the remove `x` (leftover `s`) | 04 | **Fixed.** Path (311px) holds both mock paths clear of the 36px remove column; no glyph paints over the button. |
| Console severity label wraps mid-word (`Debu g`, `War n`) | 01, 02, 03 | **Fixed.** `Trace Debug Info Warn Error Fatal` are whole words on one toolbar row with Clear / Collapse / Auto-scroll. |
| Scene Viewport tool button renders as `...` | 01, 03 | **Fixed.** The Grid–Play slot is `Scn`. The full row is `Sel Move Rot Scl Glo Snap Grid Scn Play Stop 2D 3D Vol Cam Opts`. No ellipsis glyph. |
| Entity Tree search hint is clipped | 01, 03 | **Fixed.** Hint `"Search entities"` (`entity_tree_window.c:1810`) is fully inside the input, including the `g`/`y` descenders. |

---

## Remaining clipped / wrapped / overlapping chrome

These are **not** accepted. Each is a pixel finding on the recaptured frames,
cited at the chrome that emits the string or sizes the box.

| Frame | What is wrong | Source |
| --- | --- | --- |
| 01, 02, 03 | Project Browser tile caption `Main.scene` clips to `Main.scen`. `Scenes` / `Textures` / `Hero.png` are intact. | Seed name `editor/windows/project_browser_window.c:269`; grid host `:1696`. Caption is no-wrap + clip-children at `plugins/ui/content_item.c:410`–`:417` (`max_width` = thumb − pad). |
| 04 | Debugger Statistics `Dedicated (VRAM)` value row is clipped off the bottom of the BottomRight leaf. Visible last line is the `GPU memory` section header. | Header + row `editor/windows/debugger_window.c:360`–`:363`; body is a non-scroll `content_host` at 100% height (`:654`–`:664`) filled by `dgb_build_statistics` (`:624`) with `flex_shrink 0` stat rows (`:264`–`:275`). |
| 01, 02, 03 | Console WARN line is clipped at the panel edge (`…mapped via Clay floati`). Toolbar chrome is intact; this is the log body. | `editor/windows/console_window.c:369` (`widget_label` of the full line), `:374` (`label_set_wrap(..., 0)`), `:321` (width 100%). |

No other migrated window chrome on these frames (menu bar, shell toolbar,
workspace switcher, Scene Viewport tools, Entity Tree search / `V` `L`
toggles, Properties `Select something...`, Settings tree + entries, tab
titles) is clipped, wrapped mid-word, or overlapping.
