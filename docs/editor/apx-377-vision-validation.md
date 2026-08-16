# APX-377 / APX-396 — Visual acceptance of the migrated editor

**Task:** APX-396 (second pass of the goal's final acceptance gate; same
protocol as APX-385, which this repeats rather than replaces)  
**Date:** 2026-08-16  
**Host:** `sk-sandbox-shell` (`sandbox/editor_shell_sandbox.c`)  
**Recipe:** lavapipe offscreen capture (`capture_create` → `paint` → `capture_frame` → `cpu_image_write_png`)  
**Viewport:** 1280×720, scale 1.0  
**Frames:** [`docs/editor/apx-377-frames/`](apx-377-frames/)  
**Rebuild:** clean Release `sk-sandbox-shell` (Ninja, 230 targets), then
`--out-dir` overwrote all four PNGs. SHA-256 of the new files matches the
previous tip (deterministic lavapipe capture). Verdicts below are from
reading those pixels this pass — previous MET rows were not copied.

This is the goal's final visual gate after the four chrome fixes from the
APX-385 sweep (Packages, Project Browser, Debugger, Console) landed. Look at
the v2 editor against the C++ InitDockSpace layout and
`docs/editor/migration-manifest.md`. Shared widget code
(`plugins/ui/content_item.c`) and dock-leaf sizing can regress chrome that
already passed, so every criterion is re-judged from the new frames.

APX-379 activates the lowest-order tab in each leaf; APX-380 sizes Scene
Viewport toolbar buttons from their labels; APX-382 sizes Console severity
labels from real glyph advance; APX-383 labels the scene-options tool `Scn`;
APX-384 sizes the Entity Tree search input to the toolbar height with 3px
vertical padding; Packages hint/table sizing lives in
`editor/windows/packages_window.c`; tile captions are measured in
`plugins/ui/content_item.c`; Debugger Statistics is a scroll host in
`editor/windows/debugger_window.c`; Console log lines wrap in
`editor/windows/console_window.c`.

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

**Overall:** all six criteria are accepted on the APX-396 frames. The
migrated shell, dock map, icon atlas, Scene placeholder, and mock window
bodies read as the C++ editor.

The six defects APX-385 refused to accept are gone (see
[APX-385 defect confirmation](#apx-385-defect-confirmation)). APX-382 /
APX-383 / APX-384 still hold. A sweep of the four 1280×720 frames found no
other clipped, mid-word-wrapped, or overlapping chrome in migrated windows.
The remaining-defects table is **empty**. Out of scope stays out of scope:
graph-node windows, real scene rendering, thumbnails.

---

## 1. Shell / menu bar — MET

**Frame:** [`01_scene_workspace.png`](apx-377-frames/01_scene_workspace.png)

![Scene workspace](apx-377-frames/01_scene_workspace.png)

**Rendered (matches `editor/editor_shell.c` menu priorities and toolbar):**

- Menu bar: File, Edit, Tools, Build, Window, Help (closed; open popups are a
  known painter's-algorithm limitation, manifest §8).
- Toolbar: Save All, Undo, Redo, Play, Pause, Stop, Reset Layout.
- Workspace switcher: Scene tab + "+" on the first frame.

No menu or toolbar label is clipped, wrapped mid-word, or overlapped.

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

Scene Viewport tool labels do not collide at 1280×720
(`scene_view_window.c:828`–`:841`): the row reads
`Sel Move Rot Scl Glo Snap Grid Scn Play Stop 2D 3D Vol Cam Opts`.
No tool button is an ellipsis.

The Console toolbar (`console_window.c:562` / `:259`) keeps every severity
checkbox label intact on one line (`Trace Debug Info Warn Error Fatal`) plus
Clear / Collapse / Auto-scroll on the same row. No severity label wraps
mid-word.

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
| Entity Tree | Demo Scene / Main Camera / Directional Light / Player / Character Mesh | **Populated** on 01 — hierarchy is the selected RightTop tab (APX-379). Player is collapsed (disclosure triangle), so Character Mesh is not required on screen. Names and `V`/`L` toggles are intact. |
| Console | logger ring | **Populated** on 01 — selected BottomRight tab; seeded Info lines (`Scene workspace ready`, mock Assets/Project/Renderer) are on screen; the WARN row wraps at a word break and the rest of the message is on the next line inside the panel (`console_window.c:384` / `:413`) |
| Project Browser | folder tree + FolderIcon/FileIcon grid | **Populated** on 01 — Assets tree + four tiles (Scenes, Textures, Main.scene, Hero.png) using Content/Images icons; `Main.scene` is the full caption (`project_browser_window.c:269`; `content_item.c:263`) |
| Debugger Statistics | FPS, frame time, CPU, working set, VRAM | **Populated** on 04 — the Statistics tab shows FPS / Frame time / Process / GPU memory / Dedicated (VRAM) fully (`debugger_window.c:389`). The body is a scroll host (`debugger.content.host`); the capture scrolls only far enough for `debugger.stat.vram` so Frame/Process stay visible. Console is the default tab (order 10); the host activates Debugger (order 20) on the Window-menu frame so the body is not judged while hidden behind its sibling |
| History | seeded undo/redo scopes | Sibling of Entity Tree (order 10); not required on the default tab |
| Properties | empty-selection until an entity/asset is selected | **OK** — "Select something..." (`properties_window.c:1265`; the ellipsis is the string, not a clip) |

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

- **Packages** — floating window, Add Package + hint on separate rows,
  Name/Path table of the two seeded mock folders (see
  [APX-385 defect confirmation](#apx-385-defect-confirmation)).
- **Editor Settings** — floating window, left tree (General / Rendering /
  Audio / Physics / Editor), right pane (Project Name, Company Name, Auto Save).
  Search hint (`settings_window.c:472`) is the whole word `Search`.
- **Resource Debugger** — Center tab next to Scene Viewport (Types list
  visible behind the floaters; occlusion is stacking, not a clip).

---

## APX-385 defect confirmation

Judged on the recaptured 1280×720 frames. Each of the six defects APX-385
refused to accept is gone. A partial fix would be a failure; none of these
rows is a caveat.

| # | Defect APX-385 refused | Frame | Verdict |
| - | ---------------------- | ----- | ------- |
| 1 | Packages hint clipped mid-quote (`"Binaries" folder.` hidden) | 04 | **Fixed.** Add Package is on its own row; the hint is below it (`packages_window.c:434`). Frame 04 shows the whole sentence `A package is a folder containing an "Assets" and/or "Binaries" folder.` including the closing quote and period. |
| 2 | Packages Name cells clip `SkoreGame`→`Skore`, `EnginePlugins`→`Engin` | 04 | **Fixed.** Name labels are wrap-off + `flex_shrink` 0 (`packages_window.c:147`–`:157`); Name has no tree indent (`:303`). Frame 04 shows full `SkoreGame` and `EnginePlugins` with a gap before Path. |
| 3 | Packages Path cells clip + overlap the remove `x` | 04 | **Fixed.** Path stretch takes leftover after measured Name + reserved 36px remove (`packages_window.c:185`–`:232`); path cells clip children (`:331`). Frame 04 shows full `D:/Projects/SkoreGame` and `D:/Projects/EnginePlugins` with a clear gap before each `x`. |
| 4 | Project Browser tile caption `Main.scene` clips to `Main.scen` | 01, 02, 03, 04 | **Fixed.** Caption is measured from Clay layout-font glyph advance (`content_item.c:263` / `:476`) and sized with `flex_shrink` 0 (`:494`). Recaptured frames show full `Main.scene` next to intact `Scenes` / `Textures` / `Hero.png` (seed `project_browser_window.c:269`). |
| 5 | Debugger Statistics `Dedicated (VRAM)` row clipped off the leaf bottom | 04 | **Fixed.** Statistics body is a scroll host (`debugger.content.host`); inner panel is auto-height / `flex_shrink` 0 (`debugger_window.c:260` / `:389`). Frame 04 shows `Dedicated (VRAM)  7.45 GB / 7.45 GB` fully on screen under the GPU memory header. Frame/Process rows stay visible. |
| 6 | Console WARN line cut at the panel edge (`…mapped via Clay floati`) | 01, 02, 03 | **Fixed.** Log rows wrap at the panel width (`label_set_wrap 1` at `console_window.c:384` / `:413`). The WARN is two lines; the tail `constraints may differ (kept best-effort mapping)` is fully inside the panel (pixel evidence below). |

### Pixel evidence (this recapture)

Rebuilt Release `sk-sandbox-shell` (Ninja, 230 targets) and re-ran the
lavapipe recipe (`capture_create → paint → capture_frame →
cpu_image_write_png`, 1280×720, scale 1.0, `--out-dir`). All four PNGs are
1280×720. Console WARN ink on frames 01/02/03 (same extents):

- Line 1 (y 675–685) spans x 663–1225 and wraps at a word break 40px before
  the content edge (border at x 1265).
- Line 2 (y 686–699) spans x 662–1009 — the
  `constraints may differ (kept best-effort mapping)` tail is fully inside
  the panel.
- Last WARN ink is at y 699 against the body bottom at y 719, so Auto-scroll
  is still pinned to the newest row.

The full long message is readable on all three Console frames.

---

## APX-382 / 383 / 384 still hold

Re-checked on the new pixels (not carried forward from the previous write-up).

| Prior fix | Frame | Verdict |
| --- | --- | --- |
| Console severity label wraps mid-word (`Debu g`, `War n`) | 01, 02, 03 | **Holds.** `Trace Debug Info Warn Error Fatal` are whole words on one toolbar row with Clear / Collapse / Auto-scroll (`console_window.c:562`–`:605`). |
| Scene Viewport tool button renders as `...` | 01, 03 | **Holds.** The Grid–Play slot is `Scn` (`scene_view_window.c:841`). Full row: `Sel Move Rot Scl Glo Snap Grid Scn Play Stop 2D 3D Vol Cam Opts`. No ellipsis glyph. |
| Entity Tree search hint is clipped | 01, 03 | **Holds.** Hint `"Search entities"` (`entity_tree_window.c:1810`, 3px vertical pad at `:1820`–`:1822`) is fully inside the input, including the `g`/`y` descenders. |

---

## Remaining clipped / wrapped / overlapping chrome

Sweep of all four 1280×720 frames — menu bar, shell toolbar, workspace
switcher, Scene Viewport tools, Entity Tree search / `V` `L` toggles,
Properties `Select something...`, Project Browser tiles and toolbar,
Console toolbar + wrapped WARN body, Debugger Statistics, Packages hint +
Name/Path cells, Settings tree + entries (`settings_window.c:472`,
`:146` / `:150`), tab titles, Resource Debugger tab. No clipped,
mid-word-wrapped, or overlapping chrome in migrated windows.

| Frame | What is wrong | Source |
| --- | --- | --- |

The remaining-defects table is empty. The vision gate is closed.
