# C++ editor windows → v2 migration manifest

Task: **APX-364**. Goal: editor window (ui) migration.

This is the **authoritative inventory of every editor window** in the C++
editor on `main`, written so the v2 C editor (`editor/` on this branch) can
recreate each window's external contract (what other code calls, what events
it participates in, what it loads from `Content/`, what state must survive a
session, and what is known-incomplete and therefore gets mocked data in v2).
It **specifies behaviour only** — no implementation code.

| Item | Value |
| --- | --- |
| Source branch | `origin/main` (C++ editor, Dear ImGui) |
| Source commit | `62b1d00e62a97747a95b16f11a9f9c8bdc1c29a0` |
| Target branch | v2-based C rewrite (`feature/editor-window-ui-migration`), `editor/` C sources |
| Audited scope | `Editor/Source/Skore/Window/**` (14 window classes) + the shared editor infrastructure they depend on |
| Method | `git archive origin/main`, then read every window `.hpp`/`.cpp`; grep for external callers, `Event::Bind`/`EventHandler<...>::Invoke`, `StaticContent::Get*("Content/Images/...")`, `EditorSerialize`, `EditorWindowProperties` |
| Cross-check | Window directory listing vs manifest: **28 files (14 `.cpp` + 14 `.hpp`) = 14 window classes; all 14 are listed in §3, none missing, none extra** (see §5) |
| Out of scope | Graph node editors, thumbnail generation, real scene rendering — listed separately in §4, **not** part of the v2 window work queue |
| Companion docs | `docs/WIDGET_MANIFEST.md` (APX-335 widget audit), `docs/ui-editor-migration.md` (APX-139 Console port), `editor/main_windows.c` (v2 window scaffolding, same 14 windows), `docs/editor/window-table-pattern.md` (APX-365 ops tables + add_impl observers), `docs/editor/migration-audit.md` (APX-378 independent close-out) |

## 1. The window base contract (shared by all 14)

Every window is a `class X : public EditorWindow` (reflection `SK_CLASS`), created
by reflection and managed by `EditorWorkspace` (`EditorWorkspace::OpenWindowInternal`).
The v2 scaffolding (`editor/main_windows.c` + `editor/editor_window.h`) already
mirrors this: a static `sk_editor_window_t` per window with title, dock id,
dock position and workspace mask; windows are instantiated/closed via the
editor API. Per-window notes below only list what is **additional** to this base.

Common surface every window inherits (v2 equivalent in parentheses):

- `Init(VoidPtr userData)` — called once after construction with the open
  user-data pointer (`OpenWindow` passes it through). Default no-op.
- `GetTitle()` — ImGui window title, includes the Font Awesome glyph prefix.
- `Draw(bool& open)` — immediate-mode body; sets `open=false` to close the
  window (the workspace then destroys the instance and records the close).
- `Render(GPUCommandBuffer*)` — optional; only `SceneViewWindow` and
  `PropertiesWindow` implement it (both are **out of scope**, §4).
- `EditorWindowProperties` attribute: `dockPosition` (default dock node),
  `order` (window-menu / default-open ordering), `workspaceTypes` (which
  workspace types auto-open the window at dockspace init; `All` = all four).
- `EditorSerialize` field attribute: fields tagged with it are serialized to
  JSON by `EditorWorkspace::CaptureWindowStates` → `EditorLayout::SaveWindowState`
  and restored on the next session ("persisted window state" below).
- Window types are enumerated at startup from `Reflection::GetDerivedTypes(EditorWindow)`
  into `GetEditorWindowStorages()`; each window's `RegisterType` also registers
  menu items and resource types.

### Persisted layout / workspace state (shared mechanism)

| Store | Location | Contents |
| --- | --- | --- |
| `EditorLayout.json` | `FileSystem::AppFolder()/Skore/EditorLayout.json` | ImGui ini blob (`imguiSettings`, dockspace geometry), open-window list per workspace type (**only the Scene workspace is currently written** — `EditorLayout.cpp::WriteToDisk` skips non-Scene), and per-window `state` blob = serialized `EditorSerialize` fields. Written on shutdown/flush (`EditorLayout::Flush`). |
| `EditorSettings.cfg` | `AppFolder()/Skore/EditorSettings.cfg` | `EditorSettings` resource; `GeneralEditorSettings::LoadPreviousProjectOnStartup`. |
| `ProjectSettings.cfg` | `<project>/ProjectSettings.cfg` | `ProjectSettings` resource incl. `SceneSettings::DefaultEditorScene` (what the Scene workspace opens at startup). |
| `types.json` | `<project>/types.json` | Written on save; **not read back** (deserialization is commented out). |
| Resource-backed per-instance state | Resources (RID) | Per-window resource types (see per-window notes: `ProjectBrowserWindowData`, `AnimatorTreeWindowState`, `WorkspaceResourceState::SelectedAsset`, `SelectionState`, `EditorState::ActiveWorkspaceIndex`). Survives within a session via undo/redo; not part of `EditorLayout.json`. |
| Thumbnail cache | `<project>/Local/Cache/Thumbnails` | 128×128 thumbnails per asset UUID — **out of scope**, §4. |

### Icons (shared mechanism)

- **Bitmap icons from `Content/Images`** (loaded via `StaticContent::GetTexture`,
  `StaticContent::GetImage`, or embedded in the Win32 resource): the full list
  and the per-window consumers are in §2/§3 under "Content/Images".
- **Font glyph icons**: the editor does **not** load per-window bitmap icons.
  All toolbar/menu/tree glyphs come from the Font Awesome solid font
  (`Content/Fonts/fa-solid-900.otf`, merged into the ImGui atlas in
  `ImGui.cpp`) plus a tiny custom font (`Content/Fonts/custom-font.ttf`,
  glyphs `ICON_CUSTOM_2D`/`ICON_CUSTOM_3D` used by SceneViewWindow). Base UI
  font is `Content/Fonts/DejaVuSans.ttf`. Per-asset-type icons come from each
  `ResourceAssetHandler::GetIcon()` (FA glyphs, e.g. `ICON_FA_CUBES` for scenes).
- **Asset thumbnails** in the Project Browser come from `ResourceAssets::GetThumbnail`
  (GPU texture + disk cache) — **out of scope**, §4; v2 shows mock thumbnails.

### Global event catalogue the windows touch (defined in `EditorCommon.hpp` / `Skore/Events.hpp`)

C++ `Event` is **not** ported. Each former event is an `add_impl` observer
struct in `editor/notify.h` (APX-365 / APX-375) unless explicitly dropped.

| Event type | Signature | Published by | Subscribed by (windows) | v2 (APX-375) |
| --- | --- | --- | --- | --- |
| `OnSelectionChanged` | `void()` | `Selection` (on any selection change) | none of the 14 | **Implemented** — `SK_EDITOR_NOTIFY_SELECTION_CHANGED`. Entity Tree + Project Browser publish. No window subscriber (matches C++). |
| `OnEntitySelection` | `void(u32 workspaceId, RID)` | `Selection` | PropertiesWindow | **Implemented** — `SK_EDITOR_NOTIFY_ENTITY_SELECTION`. Entity Tree publishes; Properties subscribes (workspace-filtered). |
| `OnEntityDeselection` | `void(u32 workspaceId, RID)` | `Selection` | PropertiesWindow | **Implemented** — `SK_EDITOR_NOTIFY_ENTITY_DESELECTION`. Same pair. |
| `OnEntityDebugSelection` | `void(u32 workspaceId, Entity*)` | `Selection` (runtime entity selection) | PropertiesWindow | **Implemented** — `SK_EDITOR_NOTIFY_ENTITY_DEBUG_SELECTION`. Properties subscribes. No in-scope publisher (no live runtime Entity*); kept for the C++ payload. |
| `OnEntityDebugDeselection` | `void(u32 workspaceId, Entity*)` | `Selection` | PropertiesWindow | **Implemented** — `SK_EDITOR_NOTIFY_ENTITY_DEBUG_DESELECTION`. Same. |
| `OnAssetSelection` | `void(u32 workspaceId, RID)` | `EditorWorkspace` (`WorkspaceResourceState::SelectedAsset` change, set via `EditorWorkspace::OpenAsset`) | PropertiesWindow | **Implemented** — `SK_EDITOR_NOTIFY_ASSET_SELECTION`. Project Browser publishes; Properties subscribes. |
| `OnResourceSelection` | `void(u32 workspaceId, RID)` | `AnimatorEditor`, AnimatorTreeViewWindow | PropertiesWindow | **Implemented** (observer) — `SK_EDITOR_NOTIFY_RESOURCE_SELECTION`. Properties subscribes. Publishers are graph windows → **out of scope** (§2.1 / §4); no in-scope publisher. |
| `OnMaterialNodeSelection` | `void(u32 workspaceId, RID)` | MaterialGraphEditorWindow | PropertiesWindow | **Implemented** (observer) — `SK_EDITOR_NOTIFY_MATERIAL_NODE_SELECTION`. Properties subscribes. Publisher is the material graph window → **out of scope** (§2.1 / §4). |
| `OnDropFileCallback` | `void(StringView path)` | App (OS file drop) | ProjectBrowserWindow | **Implemented** — `SK_EDITOR_NOTIFY_DROP_FILE`. Project Browser subscribes (`import_asset` into the open directory). Host publishes via `sk_editor_notify_drop_file` (v2 platform window has no drop callback yet). |
| `OnUpdate` / `OnShutdown` / `OnShutdownRequest` | `void()` / `void()` / `void(bool*)` | App | Editor core (not windows); ProjectBrowserWindow binds `OnShutdown` via its init/shutdown helpers | **Dropped as out of scope** — editor-core / host lifecycle, not a window Event. Project Browser teardown is `destroy` (`remove_impl` of the drop observer). |

Additional v2 observer kinds (not C++ Events; used so windows stay on the struct tables):

| Kind | Role |
| --- | --- |
| `SK_EDITOR_NOTIFY_ASSET_OPENED` / `ASSET_ACTIVATED` | Project Browser double-click / Enter (`OpenAsset`). Properties also consumes `ASSET_ACTIVATED`. |
| `SK_EDITOR_NOTIFY_ENTITY_CREATED` / `RENAMED` / `DELETED` / `REPARENTED` | Entity Tree structure mutations. Properties refreshes / clears on rename / delete / reparent. |
| `SK_EDITOR_NOTIFY_VIEWPORT_STATE` | Scene View toolbar / options snapshot. |
| `SK_EDITOR_NOTIFY_DIRTY` / `SAVE` | Content dirty (entity / asset / inspector mutations) and File/Save All (also captures + writes the layout store). |

Resource-change events (`Resources::RegisterEvent(ResourceEventType::Changed, ...)`),
used by the workspace/selection/animator state objects, are listed per window.

---

## 2. Out-of-scope cross-cutting features (do NOT port; mock in v2)

These are explicitly **out of scope** for the window migration and are listed
separately from the per-window work queue. v2 windows that need them render
placeholder/mock content instead.

### 2.1 Graph node editors (animation + materials)

Graph canvas editing is a single feature spanning four windows and the
`ImGui/GraphEditor` widget:

- `GraphEditorWindow` — generic blueprint/state-machine graph **demo** (hard-coded
  test nodes in `Init`); no asset binding, nothing persisted. Pure mock in v2.
- `MaterialGraphEditorWindow` — node graph for `MaterialGraphResource`
  (`MaterialNodeRegistry`, `MaterialGraphCompiler`); node thumbnails via
  `ResolveThumbnail`/`RenderResourceCache`.
- `AnimatorGraphWindow` — state graph for `AnimationLayerResource`; drives
  `AnimatorEditor` (`AddState/RemoveState/AddTransition/...`).
- `AnimatorTreeViewWindow` — layers/parameters/avatars tree for
  `AnimationControllerResource`; drives the same `AnimatorEditor`.
- Backing widget `ImGui/GraphEditor` (nodes, pins, links, pan/zoom canvas) and
  the `GraphEditorResult` plumbing (created/moved/deleted nodes+links,
  selection change, link-from-node gestures).

v2: the four window scaffolds exist (`main_windows.c`) with titles/dock only;
their `Draw` bodies are **mocked data** (static nodes/links, no
`AnimatorEditor`/`MaterialEditor` backends). The graph canvas widget itself is
a separate work item, tracked by `docs/WIDGET_MANIFEST.md` §21 ("do not start
graph or gizmo work from this list").

### 2.2 Thumbnail generation

- `ResourceAssets::GetThumbnail(RID)` / disk cache `<project>/Local/Cache/Thumbnails`
  (128×128 `thumbnailSize`), thumbnail read/write threads.
- `Utils/PreviewGenerator` (per-asset-type preview scene builders,
  `GenerateThumbnail`, `SetupDefaultEnvironment`).
- `PropertiesWindow` scene preview (`PreviewMode::Scene`, `PreviewGenerator`,
  offscreen `DefaultRenderPipeline` context) and its asset-preview path
  (`ResourceAssetHandler::GetPreviewGenerator()`).
- `MaterialGraphEditorWindow::ResolveThumbnail` + `m_thumbnailCaches`.

v2: Project Browser content grid shows a **generic icon** for assets and skips
thumbnail textures; Properties window omits the scene preview pane.

### 2.3 Real scene rendering

- `SceneViewWindow::Render` + `SceneViewRenderPipeline` +
  `RenderPipelineContext` (offscreen render graph for the viewport), camera
  update, output texture display, `GetDisplayTexture`, `GetViewportCamera`.
- `PropertiesWindow::Render` (preview render, `DefaultRenderPipeline`).
- GPU-backed viewport interactions: `EntityPicker` (ray-picked entities) and
  ImGuizmo (translate/rotate/scale gizmo, third-party).
- Render-debug toggles (`drawDebugPhysics`, `drawNavMesh`, `drawMeshAABB`,
  `selectedTextureToShow`, `RenderDebug::ForcedLod`) — read GPU/render state.

v2: SceneView shows a **placeholder canvas** (grid + camera fly-through or a
static mock image); gizmos/entity picking are mocked (e.g. move on click).

---

## 3. Window-by-window manifest

Legend: **Dock** = `EditorWindowProperties{dockPosition, order?, workspaceTypes}`;
**Persisted** = `EditorSerialize` fields (+ per-instance resource state);
**Public API** = members other translation units call (all `static` unless noted).

### 3.1 ConsoleWindow

| Field | Value |
| --- | --- |
| Class | `Skore::ConsoleWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/ConsoleWindow.hpp`, `.cpp` |
| Title | `ICON_FA_TERMINAL " Console"` |
| Dock | `BottomRight`, order 10, workspace `All` (auto-opened in every workspace) |
| Public API | `RegisterType`; `OpenHistoryWindow(MenuItemEventData)` (named after HistoryWindow but opens **Console**; not wired to any menu item — only `RegisterType` matters). No external callers. |
| Events | Subscribes: none. Reads the global `GetConsoleSink()` (`SK_API`, defined in `Editor.cpp`; registered as a logger sink from `Main.cpp`). No publishes. |
| Content/Images | none (glyph title only) |
| Persisted | none (`showTrace/Debug/Info/Warn/Error/Critical`, `collapse`, `shouldScrollToBottom`, `filter` are session-only) |
| Incomplete | Auto-scroll only follows when already at bottom. Otherwise functional. **Migrated to v2 (APX-373)**: `editor/windows/console_window.h/.c` — the v2 shell window. Message ring bound to the v2 logger (`add_sink` on the app logger context), severity checkboxes + colouring, Collapse / Auto-scroll, Filter search and Clear; public `AddMessage`/`Clear`/level-visibility/filter entry points go through the `sk_editor_console_ops_t` table registered with `add_impl` (window-table-pattern.md §7). The APX-139 `editor/console_panel.c` dual-stack demo stays for the `--ui-migration` host. |

### 3.2 DebuggerWindow

| Field | Value |
| --- | --- |
| Class | `Skore::DebuggerWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/DebuggerWindow.hpp`, `.cpp` |
| Title | `ICON_FA_BUG " Debugger"` |
| Dock | `BottomRight`, order 20, workspace `All` (auto-opened) |
| Public API | `RegisterType` only; no menu item |
| Events | none — polls the global `Profiler` singleton (`GetCpuTasks/GetGpuTasks/GetCpuFrameStats/GetGpuFrameStats/IsActive/SetActive/ResetStats`) and `Platform`/`Graphics` stats |
| Content/Images | none (glyph title; toolbar glyphs `STOP`/`CIRCLE`/`PLAY`/`PAUSE`/`TRASH`) |
| Persisted | none (per-tab `ProfilerView` state is session-only) |
| Incomplete | Statistics tab depends on runtime `Profiler` data; GPU tab data presence-dependent. **Migrated to v2 (APX-374)**: `editor/windows/debugger_window.h/.c` — the v2 shell window (BottomRight/All). Statistics / CPU Profiler / GPU Profiler tabs with the C++ toolbar (Record/Stop, Pause/Resume, Clear, scale combo 2ms..66ms). The v2 engine has no `Profiler` singleton, so the window runs a **session mock profiler** (deterministic LCG frame timings + a fixed task tree with per-frame variance) that the toolbar controls and the Statistics tab summarizes (FPS, frame time, CPU %, memory, VRAM — mock values); the ops table exposes `feed_frame`/`set_tasks` so real data can replace the mock without UI changes. Public entry points go through the `sk_editor_debugger_ops_t` table registered with `add_impl` (window-table-pattern.md §7). MOCK: all statistics/profiler data (no runtime Profiler), per-tab ProfilerView state shared. |

### 3.3 EntityTreeWindow

| Field | Value |
| --- | --- |
| Class | `Skore::EntityTreeWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/EntityTreeWindow.hpp`, `.cpp` |
| Title | `ICON_FA_LIST " Entity Tree"` |
| Dock | `RightTop`, workspace `Scene` |
| Public API | `RegisterType`; `OpenEntityTree` (menu `Window/Entity Tree`); context-menu surface registered through `AddMenuItem(MenuItemCreation)` (extension point, no current external callers) and the static callbacks `AddSceneEntity`, `AddSceneEntityFromAsset`, `AddComponent` (**stub**), `RenameSceneEntity`, `DuplicateSceneEntity`, `DeleteSceneEntity`, `CheckEntityActions`, `CheckSelectedEntity`, `CheckReadOnly`, `ShowSceneEntity`, `IsShowSceneEntitySelected`, `CheckIsOverride`, `RemoveOverride`, `CheckIsRemoved`, `AddBackToThisInstance`, `ShowResourceInspector` (calls `ResourceDebuggerWindow::InspectResource`). All used via `MenuItemContext` with the window as `eventData.drawData`. |
| Events | Subscribes/publishes: none directly. Selection is driven through `workspace->GetSceneEditor()` (RID and runtime `Entity*` paths). |
| Content/Images | none (glyphs only: title `LIST`; rows `CUBE`/`CUBES`; `EYE`/`EYE_SLASH`, `LOCK`/`LOCK_OPEN`, `PLUS`, `MAGNIFYING_GLASS`) |
| Persisted | none (`EditorSerialize`); open/closed tree state lives in ImGui ini (`imguiSettings`). Dock `RightTop`, Scene workspace. |
| Incomplete | `AddComponent` is an empty `//TODO` stub (its menu item is never registered — dead); `CheckIsOverride` always returns `false` and `RemoveOverride` is a no-op, so the "Revert Instance Overrides" menu entry is dead; `CheckEntityActions` is effectively `!removed`. Prototype-override flows (`AddBackToThisInstance`, removed-entity display) are partially implemented. v2: entity tree + active/visible/lock toggles + create/duplicate/delete/rename are the real surface; override-revert items get mock/no-op handlers. **Migrated to v2 (APX-370)**: `editor/windows/entity_tree_window.h/.c` — the v2 shell window. Hierarchical entity tree over the real entity layer when a scene is attached (`set_scene(repository, scene_rid)` → `sk.scene_resource` Roots → `sk.entity_resource` Children/Name): `widget_tree` expand/collapse + selection (ctrl toggles, empty-click clears), F2 in-row rename overlay, the Create / Rename / Duplicate / Delete context menu (+ dead override rows), per-row visible/lock toggles (session-only MOCK — v2 payloads have no Deactivated/Locked fields), drag sources on rows with drop-target reparent (cycle-guarded, publishes ENTITY_REPARENTED), and the search filter. Create/rename/delete/duplicate/reparent mutate the repository and publish the notify.h observers (ENTITY_CREATED/RENAMED/DELETED, selection → SELECTION_CHANGED + ENTITY_SELECTION/DESELECTION). Public entry points go through the `sk_editor_entity_tree_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Window/Entity Tree` menu routes through it. Without an attached scene the window shows a clearly-marked MOCK scene (Demo Scene / Main Camera / Directional Light / Player / Character Mesh) that supports the full ops surface. MOCK: Create Entity From Asset popup (records a flag), Show Resource Inspector (records the inspected RID), Show Scene Entity debug toggle (no live Scene/Entity runtime tree), SceneView framing (double-click) not ported. |

### 3.4 HistoryWindow

| Field | Value |
| --- | --- |
| Class | `Skore::HistoryWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/HistoryWindow.hpp`, `.cpp` |
| Title | `ICON_FA_CLOCK_ROTATE_LEFT " History"` |
| Dock | `RightTop`, order 10, workspace `Scene` |
| Public API | `RegisterType`; `OpenHistoryWindow` (menu `Window/History`) |
| Events | none. Renders the editor's undo/redo stacks through the free function `ImGuiDrawUndoRedoActions()` (defined in `Editor.cpp`, reads the module-private `undoActions`/`redoActions` arrays). |
| Content/Images | none |
| Persisted | none (`m_shouldAutoScroll` unused) |
| Incomplete | Flat list of undo/redo scope names, no grouping/icons. **Migrated to v2 (APX-374)**: `editor/windows/history_window.h/.c` — the v2 shell window (RightTop/Scene). Undo (most recent first) + Redo sections rendering the stacks like `ImGuiDrawUndoRedoActions`. The v2 editor has no undo stack yet, so the stacks are **MOCK session data** (seeded scopes) the ops table pushes/pops/clears — the same surface will carry the real undo stack when it lands. Public entry points go through the `sk_editor_history_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Window/History` menu routes through it. MOCK: all undo/redo data; `m_shouldAutoScroll` (unused in C++) dropped. |

### 3.5 PackagesWindow

| Field | Value |
| --- | --- |
| Class | `Skore::PackagesWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/PackagesWindow.hpp`, `.cpp` |
| Title | `ICON_FA_BOXES_STACKED " Packages"` |
| Dock | `None` (floating, `ImGuiCenterWindow`; opened on demand, never by dockspace init) |
| Public API | `RegisterType`; `Open` (menu `Edit/Packages`, priority 1015) |
| Events | none — reads `Editor::GetProjectPackages()`, calls `Editor::AddProjectPackage(path)` / `Editor::RemoveProjectPackage(path)` |
| Content/Images | none (glyphs `FOLDER_OPEN`, `TRASH`) |
| Persisted | none |
| Incomplete | Add/remove project package folders only; package list lives in the `.skore` project file (`packages:` sequence, `Editor::SaveProjectFile`). **Migrated to v2 (APX-374)**: `editor/windows/packages_window.h/.c` — the v2 shell window (floating on-demand, dock None / mask 0). Add Package button + hint, and a Name / Path / remove table of the tracked folders. The v2 project model has no `packages:` list, so the list is **MOCK session data** seeded at open and edited via the ops table; `Platform::PickFolder` does not exist in v2, so the Add button records a pending flag instead of opening a folder dialog. Because the window is never docked its chrome is a floating `widget_window` on the shell context root (absolute, centered — the C++ `ImGuiCenterWindow` equivalent). Public entry points go through the `sk_editor_packages_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Edit/Packages` menu routes through it. MOCK: package list + Add folder dialog. |

### 3.6 ProjectBrowserWindow

| Field | Value |
| --- | --- |
| Class | `Skore::ProjectBrowserWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/ProjectBrowserWindow.hpp`, `.cpp` |
| Title | `ICON_FA_FOLDER " Project Browser"` |
| Dock | `BottomLeft`, workspace `All` |
| Public API (called by other code) | Static: `AddMenuItem(MenuItemCreation)` and `AssetNew(MenuItemEventData)` + `CanCreateAsset(MenuItemEventData)` — **called by 7 resource handlers** (AnimationControllerHandler, SceneHandler, RmlUiHandler, EntityHandler, CSharpScriptHandler, DCCAssetHandler, MaterialGraphHandler) to register per-asset-type "Create" entries in the browser context menu; `HideExtension(StringView)` — public, **no current callers**; `RegisterType`. Instance (used by its own menu actions via `eventData.drawData`): `ClearSelection(UndoRedoScope*)`, `SelectItem(RID, UndoRedoScope*)`, `SetRenameItem(RID, UndoRedoScope*)`, `GetOpenDirectory()`, `GetLastSelectedItem()`. |
| Events | Subscribes: `OnDropFileCallback` (`Event::Bind` in `RegisterType` → `OnDropFile` imports the dropped path into `GetOpenDirectory()`), `OnShutdown` (via `ProjectBrowserWindowInit/Shutdown`, destroys `directoryTexture`). Publishes: none directly — selection goes through the global `Selection` system (`Selection::Select(SelectionType::Asset, ...)`, which feeds `OnAssetSelection`/`OnSelectionChanged`). |
| Content/Images | `Content/Images/FolderIcon.png` → `directoryTexture` (folder tiles in the content grid). File tiles use per-handler FA glyphs (`ResourceAssets::GetIcon`) + thumbnails (`ResourceAssets::GetThumbnail`, **out of scope** → mock). |
| Persisted | `EditorSerialize`: `treeOnlyView` (one vs two column), `contentBrowserZoom`. Per-instance resource `ProjectBrowserWindowData` (`OpenDirectory`, `RenamingItem`; `SelectedItems`/`LastSelectedItem` registered but **unused**). |
| Incomplete | Folder-tree **drag-drop source** is commented out (folder nodes accept drops but cannot be dragged; content-grid and asset-tree-leaf drags are active); `SelectedItems`/`LastSelectedItem` resource fields dead; breadcrumb "select folder" popup; zoom slider applies only to content grid. Import flow = `Platform::OpenDialogMultiple` → `ResourceAssets::ImportAsset`. **Migrated to v2 (APX-369)**: `editor/windows/project_browser_window.h/.c` — folder tree (`widget_tree` on the real `ResourceAssetDirectory` graph), content item grid (`widget_content_grid`, folder/file tiles from the Content/Images atlas), breadcrumb navigation, single + multi selection (ctrl toggles), the Create/Delete/Rename/Show-in-Explorer/Copy-Path-Id/Show-Resource-Inspector context menu (Create entries registered by handlers via `add_menu_item`), search filter (content grid + tree leaves), and drag sources on content-grid items and tree file leaves with folder drop targets that move assets through `move_asset`. Public entry points `ClearSelection`/`SelectItem`/`SetSelection`/`SetRenameItem`/`RevealPath`/`Refresh`/`GetOpenDirectory`/`GetLastSelectedItem`/`activate_item`/`set_project`/listing queries go through the `sk_editor_project_browser_ops_t` table registered with `add_impl` (window-table-pattern.md §7); selection changes announce through the notify.h observers. Data comes from the v2 asset layer when a project is attached (`set_project`); without one a clearly-marked MOCK tree/list is shown. Thumbnails stay mocked (generic file icon). MOCK: Import button (no OS dialog yet; drop-file import is real), Show in Explorer / Show Resource Inspector / Copy Path Id record the target path id instead of calling the OS (the ResourceDebuggerWindow port is `editor/windows/resource_debugger_window.h/.c`, §3.8; the browser's Show Resource Inspector entries still record the RID until they call `inspect_resource`). |

### 3.7 PropertiesWindow

| Field | Value |
| --- | --- |
| Class | `Skore::PropertiesWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/PropertiesWindow.hpp`, `.cpp` |
| Title | `ICON_FA_CIRCLE_INFO " Properties"` |
| Dock | `RightBottom`, workspace `All` |
| Public API | `RegisterType`; `OpenProperties` (menu `Window/Properties`). Also `Init`/`Render(GPUCommandBuffer*)` (preview render — **out of scope**). |
| Events | Subscribes (bound in ctor, unbound in dtor): `OnEntitySelection`, `OnEntityDeselection`, `OnEntityDebugSelection`, `OnEntityDebugDeselection`, `OnAssetSelection`, `OnResourceSelection`, `OnMaterialNodeSelection` — all filtered by `workspace->GetId()`. Publishes: none. |
| Content/Images | none (glyph title; texture-viewer toolbar glyphs `MAGNIFYING_GLASS_PLUS/MINUS`, `CIRCLE_DOT`, `XMARK`). Texture preview uses `RenderResourceCache` GPU textures. |
| Persisted | none (`EditorSerialize`); `m_previewHeight` and material-node edit state are session-only. |
| Incomplete | Preview pane (`PreviewMode::Scene` + `PreviewGenerator` + offscreen `DefaultRenderPipeline`; `PreviewMode::Texture` mip/zoom/pan viewer) = **thumbnail generation / scene rendering → out of scope**, mock in v2. Material-graph node/instance property editors depend on `MaterialGraphResource` (**graph editor out of scope**); import-settings draft/clone + Apply/Reimport depends on `ResourceAssets::CookAsset`. **Migrated to v2 (APX-371)**: `editor/windows/properties_window.h/.c` — the v2 shell window (RightBottom/All) implementing the in-scope surface: entity header (name/UUID + layer MOCK), per-component collapsing headers with property rows drawn from the payload fields using the v2 widget set (numeric, text, bool, enum, vector, color, asset reference — all read-only MOCK until v2 can edit arbitrary payload fields), the Add Component popup (search filter + registered component types) and the '...' component settings popup (Reset / Remove / Move Up / Move Down), asset name + UUID (+ MOCK import-settings Apply/Reimport), and generic resource fields. The manifest lists no multi-select for the inspector (single selectedEntity/Asset/Resource/MaterialNode like C++). Selection flows through the notify.h observers (ENTITY_SELECTION/DESELECTION, ENTITY_DEBUG_SELECTION/DESELECTION, ASSET_SELECTION, RESOURCE_SELECTION, MATERIAL_NODE_SELECTION — filtered by workspace id like the C++ ctor bindings). Entity/component data reads the attached repository + scene (`set_scene`, same contract as the Entity Tree): the scene Roots/Children own `sk.entity_resource` payloads whose Components sub-object list carries the builtin component payload types; Add/Remove/Move mutate the repository (create_resource / sub-object list ops) and Reset records a MOCK flag. Public entry points go through the `sk_editor_properties_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Window/Properties` menu routes through it. MOCK: scene/texture preview pane, material-graph node editing (RID + label), layer data (v2 payload has no Layer field), import settings. |

### 3.8 ResourceDebuggerWindow

| Field | Value |
| --- | --- |
| Class | `Skore::ResourceDebuggerWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/ResourceDebuggerWindow.hpp`, `.cpp` |
| Title | `"Resource Debugger"` (plain) |
| Dock | `Center`, order 50, **no workspace mask** (on-demand only; never auto-opened) |
| Public API | `RegisterType`; `Open` (menu `Window/Resource Debugger`); **`InspectResource(RID)`** — called by `ProjectBrowserWindow::ShowResourceInspector` and `EntityTreeWindow::ShowResourceInspector` (opens the window with `userData=&rid`); `NavigateToInstance(RID)` (public, used internally + externally available). |
| Events | none — reads `Resources` type/instance tables directly. |
| Content/Images | none (glyph `ARROW_LEFT` back button) |
| Persisted | none |
| Incomplete | Read-only viewer (no field editing); two tabs (Types, Instance) with history navigation. **Migrated to v2 (APX-374)**: `editor/windows/resource_debugger_window.h/.c` — the v2 shell window (on-demand Center, mask 0 — never auto-opened). Types tab (search + type list + type info/fields/instances tables) and Instance tab (Back button, RID/Type/UUID/Path/Version/Parent/Prototype metadata, generic field values with navigable RID links) over the attached repository (`set_repository`; the Project Browser / Entity Tree "Show Resource Inspector" callers pass their scene/project repo once wired). The v2 repository has no public type enumeration, so the Types tab lists the builtin payload/component type ids `find_type` resolves plus the distinct types of live resources found by walking the dense RID range 1..resource_count; instance lists come from the same walk. Public entry points (Open / InspectResource / NavigateToInstance / back / type+instance queries / generic field reads) go through the `sk_editor_resource_debugger_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Window/Resource Debugger` menu routes through it. MOCK: full registered-type enumeration (no repo walk API; types without live instances appear only when builtin-registered), field editing (read-only like C++). |

### 3.9 SceneViewWindow — (scene rendering out of scope, §4)

| Field | Value |
| --- | --- |
| Class | `Skore::SceneViewWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/SceneViewWindow.hpp`, `.cpp` |
| Title | `ICON_FA_BORDER_ALL " Scene Viewport"` |
| Dock | `Center`, workspace `Scene` |
| Public API | `RegisterType`; `OpenSceneView` (menu `Window/Scene Viewport`); **`ViewEntity(Entity*)` / `ViewEntity(RID)`** — called by `EntityTreeWindow` (double-click frames the camera on the entity); `GetSceneEditor()` (used by `SceneViewRenderPipeline`); `IsSceneInteractionDisabled()`; `AddMenuItem(MenuItemCreation)` (context: Duplicate/Delete, hotkeys Ctrl+D / Delete). |
| Events | none — selection via `workspace->GetSceneEditor()`; simulation via `SceneEditor::StartSimulation/StopSimulation`; `RenderPipelineContext::SetMainContext`. |
| Content/Images | none (glyph title; toolbar glyphs incl. `ICON_CUSTOM_2D`/`ICON_CUSTOM_3D` from `custom-font.ttf`) |
| Persisted | `EditorSerialize` (14 fields): `guizmoOperation`, `guizmoMode`, `guizmoSnapEnabled`, `viewType`, `drawIcons`, `cameraFov`, `drawGrid`, `drawSelectionOutline`, `drawDebugPhysics`, `showAllPhysicsShapes`, `lockCameraFrustum`, `drawMeshAABB`, `drawNavMesh`, `drawComponentGizmos`. Free-camera state (`FreeViewCamera`) is session-only. |
| Incomplete | `Draw2DViewport` is entirely `#if 0` (2D view mode stub); "scene-options" popup is empty (PathTracer commented out); render debug toggles depend on the render graph (`RenderDebug::ForcedLod`, `selectedTextureToShow`). Everything visual (render pipeline, gizmo, entity picking, icon overlay) is **real scene rendering → out of scope**. v2: toolbar + viewport options popups + mock canvas; gizmo/camera mocked. **Migrated to v2 (APX-372)**: `editor/windows/scene_view_window.h/.c` — the v2 shell window. Dockable Center/Scene window whose toolbar mirrors the C++ top bar (select/translate/rotate/scale, world/local, snap + right-click snap-size popup, grid, scene-options "Scn" (APX-383: no ellipsis placeholder), Play/Stop, 2D/3D, sound MOCK, camera + viewport-options popups with FOV/speed/smooth sliders+checkbox, the eight render-debug checkboxes and a MOCK Forced LOD slider). The viewport area draws a **placeholder texture** (CPU-generated 512x288 RGBA8 checkerboard/gradient/crosshair mock) letterboxed to the content area — real scene rendering stays out of scope (§4), so the placeholder is the accepted result. The texture follows the APX-367 host-binding pattern: `sk_editor_scene_view_texture_create` (staging → copy → view) registers on the app context and the host binds its view at `SK_EDITOR_SCENE_VIEW_TEX_SLOT` in the UI renderer images array; without one the chrome still works (bordered fallback). Public entry points `OpenSceneView` / `ViewEntity(RID)` (records the request; no camera) / `IsSceneInteractionDisabled` / `AddMenuItem` / `GetSceneEditor` (NULL mock) / `StartSimulation` / `StopSimulation` / viewport-state getters+setters go through the `sk_editor_scene_view_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Window/Scene Viewport` menu routes through it. Every viewport state change announces through the notify.h VIEWPORT_STATE observer (`sk_editor_on_viewport_state_t`). MOCK: gizmos/entity picking/camera fly-through (session-only state), 2D view mode (same placeholder), simulation (session flag), AudioEngine sound toggle, ReadOnly standing in for the C++ UI-document interaction lock; persisted `EditorSerialize` fields stay session-only until layout state lands. |

### 3.10 SettingsWindow

| Field | Value |
| --- | --- |
| Class | `Skore::SettingsWindow : EditorWindow` |
| Files | `Editor/Source/Skore/Window/SettingsWindow.hpp`, `.cpp` |
| Title | `FormatName(type simple name)` of the settings group (e.g. "Editor Settings", "Project Settings") |
| Dock | `None` (floating, centered; on-demand) |
| Public API | `RegisterType`; `Open(TypeID group)` — currently only invoked by `OpenAction` (menu `Edit/Editor Settings` → `EditorSettings`, `Edit/Project Settings` → `ProjectSettings`, both with `.userData = TypeID`); `OpenAction` is the registered menu handler. |
| Events | none — reads `Resources::FindTypesByAttribute<EditableSettings>()` and `Settings::Get` |
| Content/Images | none |
| Persisted | none |
| Incomplete | Collapsing-tree of setting groups built in `Init`; per-entry `ImGuiDrawResource` field editors; layer-collision matrix for `PhysicsSettings` (custom draw list). **Migrated to v2 (APX-374)**: `editor/windows/settings_window.h/.c` — the v2 shell window (floating on-demand, dock None / mask 0) titled by the opened group ("Editor Settings" / "Project Settings"). Two-pane body: search + setting-group tree (left) and the selected group's entries (right) with per-entry bool/int/float/string editors and — for the Physics group — the **MOCK layer-collision matrix** (8 named layers, session state) until physics settings exist. The v2 engine has no `Settings::Get`/`EditableSettings` registry, so the tree is **MOCK session data** (seeded groups + entry values) edited via the ops table; nothing is persisted (C++ window has no EditorSerialize fields either). Like Packages, the never-docked chrome is a floating `widget_window` on the shell context root (absolute, centered). Public entry points (Open(TypeID group) / entry read-write / selection / search / collision matrix) go through the `sk_editor_settings_ops_t` table registered with `add_impl` (window-table-pattern.md §7); the shell's `Edit/Editor Settings` and `Edit/Project Settings` menus route through it. MOCK: all setting groups/entries + collision matrix (no EditableSettings registry). |

### 3.11–3.14 Graph node editors (details for cross-check; work is out of scope §4)

| Window | Files | Title / Dock | Public API (static, menu-surface) | Events | Content/Images | Persisted |
| --- | --- | --- | --- | --- | --- | --- |
| **GraphEditorWindow** | `GraphEditorWindow.hpp/.cpp` | `ICON_FA_DIAGRAM_PROJECT " Graph Editor"` / `Center`, order 20, Graph | `RegisterType`; `OpenGraphEditorWindow` (**not wired** to any menu item) | none | none | none — `m_nodes`/`m_links` are **hard-coded demo data** in `Init` |
| **MaterialGraphEditorWindow** | `MaterialGraphEditorWindow.hpp/.cpp` | `ICON_FA_DIAGRAM_PROJECT " Material Graph"` / `Center`, Material | `RegisterType`; `AddNodeAction`, `DeleteNodeAction`, `HasContextNode` (context menu, `s_menu`) | **Publishes** `OnMaterialNodeSelection` (single-node selection mirror) | none (node thumbnails via `RenderResourceCache`, out of scope) | none; `m_selectedNode`/`m_centeredGraph` session-only |
| **AnimatorGraphWindow** | `AnimatorGraphWindow.hpp/.cpp` | `ICON_FA_DIAGRAM_PROJECT " Animator Graph"` / `Center`, Animator | `RegisterType`; `AddMenuItem`; `NewEmptyState`, `NewTransition`, `SetAsDefaultState`, `DuplicateState`, `DeleteState`, `HasContextState`, `HasNoContextState` | none directly (uses `AnimatorEditor` which publishes `OnResourceSelection`) | none (glyphs) | none (state positions live in `AnimationStateResource::Position`, not window state) |
| **AnimatorTreeViewWindow** | `AnimatorTreeViewWindow.hpp/.cpp` | `ICON_FA_LIST " Animator Tree"` / `Left`, Animator | `RegisterType`; `AddMenuItem`; `NewLayer`, `NewParameter`, `NewAvatar`, `RenameItem`, `DeleteItem`, `HasContextItem`, `IsLayerContext`, `IsParamContext`, `IsAvatarContext`, `IsNoContext` | Subscribes: `ResourceEventType::Changed` on its per-instance `AnimatorTreeWindowState` (`OnTreeSelectionChange`); **publishes** `OnResourceSelection` (via the same handler → PropertiesWindow) | none (glyphs `DIAGRAM_PROJECT`, `LAYER_GROUP`, `SLIDERS`, `PERSON`, `PLUS`) | Per-instance resource `AnimatorTreeWindowState::SelectedItem` (not `EditorSerialize`) |

---

## 4. Out-of-scope summary (explicit)

| Area | Windows / code | What v2 does instead |
| --- | --- | --- |
| Graph node editors (animation, materials) | `GraphEditorWindow`, `MaterialGraphEditorWindow`, `AnimatorGraphWindow`, `AnimatorTreeViewWindow`; backing `ImGui/GraphEditor`, `MaterialGraph/*`, `AnimatorEditor` | Scaffolds with **mock static node graphs**; no asset/controller binding, no compile/HLSL panel, no animator backend |
| Thumbnail generation | `ResourceAssets::GetThumbnail` + `<project>/Local/Cache/Thumbnails`, `Utils/PreviewGenerator`, `PropertiesWindow` scene preview, `MaterialGraphEditorWindow::ResolveThumbnail` | **Generic asset icons**; no thumbnail textures in Project Browser / Properties |
| Real scene rendering | `SceneViewWindow::Render`/`SceneViewRenderPipeline`/`RenderPipelineContext`, `PropertiesWindow::Render`/`DefaultRenderPipeline`, `EntityPicker`, ImGuizmo, render-debug toggles | **Placeholder viewport canvas** (grid + mock camera); gizmo/picking/entity-icons mocked |

Everything in §3.1–§3.10 **is** in scope for the window migration (minus the
out-of-scope pieces flagged inside each row).

## 5. Cross-check: window list vs file listing

Source of truth: `git ls-tree origin/main --name-only Editor/Source/Skore/Window/`
= 28 files = 14 `.cpp`/`.hpp` pairs. Every pair defines exactly one
`EditorWindow` subclass; all 14 are inventoried above:

| # | Directory file | Manifest § |
| --- | --- | --- |
| 1 | `AnimatorGraphWindow.cpp/.hpp` | §3.12 (out of scope) |
| 2 | `AnimatorTreeViewWindow.cpp/.hpp` | §3.14 (out of scope) |
| 3 | `ConsoleWindow.cpp/.hpp` | §3.1 |
| 4 | `DebuggerWindow.cpp/.hpp` | §3.2 |
| 5 | `EntityTreeWindow.cpp/.hpp` | §3.3 |
| 6 | `GraphEditorWindow.cpp/.hpp` | §3.11 (out of scope) |
| 7 | `HistoryWindow.cpp/.hpp` | §3.4 |
| 8 | `MaterialGraphEditorWindow.cpp/.hpp` | §3.13 (out of scope) |
| 9 | `PackagesWindow.cpp/.hpp` | §3.5 |
| 10 | `ProjectBrowserWindow.cpp/.hpp` | §3.6 |
| 11 | `PropertiesWindow.cpp/.hpp` | §3.7 |
| 12 | `ResourceDebuggerWindow.cpp/.hpp` | §3.8 |
| 13 | `SceneViewWindow.cpp/.hpp` | §3.9 |
| 14 | `SettingsWindow.cpp/.hpp` | §3.10 |

`Editor/Source/Main/Main.cpp` + `Main/Skore.rc` are the editor **app entry**
(create context, register logger sink, window icon `Content/Images/skore.ico`),
not a window; `Editor/Source/Skore/Editor.cpp` is the editor core (menu bar,
workspace tabs, undo/redo stacks, dialogs, save/export, `EditorTypeRegister`
which reflection-registers all 14 window types) — both are covered as
infrastructure in §1. The v2 scaffold registers the **same 14 windows**
(`editor/main_windows.c`), which matches this listing 1:1.

## 6. Migration notes for v2 (per-window acceptance hints)

1. Window registry must expose the same **default dock position + workspace
   mask** (already in `main_windows.c`) and the same **on-demand** set
   (ResourceDebugger, Packages, Settings are never auto-opened).
2. Menu surface to recreate: `Window/Project Browser`, `Window/Entity Tree`,
   `Window/Scene Viewport`, `Window/Properties`, `Window/History`,
   `Window/Resource Debugger`, `Edit/Packages`, `Edit/Editor Settings`,
   `Edit/Project Settings` (SettingsWindow is the only window whose title
   varies by opened group).
3. Cross-window call graph to preserve: `EntityTreeWindow → SceneViewWindow::ViewEntity`
   and `→ ResourceDebuggerWindow::InspectResource`;
   `ProjectBrowserWindow → ResourceDebuggerWindow::InspectResource`;
   `ResourceAssetHandler → ProjectBrowserWindow::{AddMenuItem, AssetNew, CanCreateAsset}`
   (the handler-based "Create" menu is the only place a window's public statics
   are consumed by non-window code).
4. Event wiring to preserve: selection events (PropertiesWindow is the only
   window subscriber); `OnDropFileCallback` (ProjectBrowser import);
   `OnAssetSelection` (workspace open-asset flow). v2's `sk_event`/logger
   equivalents should carry the same payloads (workspace id + RID).
5. State to persist across sessions: `EditorSerialize` fields are only on
   ProjectBrowserWindow (`treeOnlyView`, `contentBrowserZoom`) and
   SceneViewWindow (14 viewport/gizmo toggles) — everything else is session
   or resource-backed. v2 should mirror `EditorLayout.json` (dockspace + open
   windows + per-window state blob) once window state lands.
6. Out-of-scope features must be **mockable per window** so each window's in-scope
   UI can be exercised without graph/thumbnail/render subsystems (§2).

## 7. v2 struct-table + notify pattern (APX-365)

Public window functions go through a **per-window ops table** registered with
`app_api->add_impl` — never a direct symbol (`ProjectBrowserWindow.ClearSelection`
→ `sk_editor_project_browser_ops(ctx, api)->clear_selection(...)`).

C++ `Event` is **not** ported. Each former event is an observer struct of
function pointers, also published with `add_impl`. Publishers call
`sk_editor_notify_*` which copies, sorts by `order`, and invokes that type
only. No event bus.

Registration, lookup, ordering, and lifetime rules — and the Project Browser
reference window later migrations copy — are in
`docs/editor/window-table-pattern.md`.

## 8. Editor shell (APX-366)

The v2 shell that hosts every migrated window lives in
`editor/editor_shell.h` / `.c` (plus the `--shell` host mode in
`editor/main.c`):

- **Application frame**: one sk-ui context with menu bar, toolbar, and the
  docking host (active workspace's dockspace) — the workspace dock models are
  bound to the shared context via `sk_editor_workspace_set_dock_context` and
  only the active workspace's model is live at a time
  (`sk_editor_workspace_clear_dockspace` on switch).
- **Menu bar**: File / Edit / Build / Tools / Window / Help mirroring the C++
  `MenuItemContext` surface. Window-toggling entries route through the
  registered tables (`sk_editor_window_open` / the Project Browser ops
  table), never direct calls. Not-yet-implemented features stay inert or
  mocked (Save All, Export, Undo/Redo, Build C#, Tools…); graph node editor
  menus are not ported (§4).
- **Toolbar**: Save All / Undo / Redo / Play / Pause / Stop / Reset Layout.
- **Workspace switcher**: tabs for open workspaces + "+" popup of the
  registered workspace types.
- Render check: `sandbox/editor_shell_sandbox.c` (`sk-sandbox-shell`) writes
  the Scene frame plus Graph / Scene-restored siblings (APX-377). `--out-dir`
  emits `01_scene_workspace.png` / `02_graph_workspace.png` /
  `03_scene_restored.png`. Default `--out` is still `./editor_shell.png`.
- Known plugin limitation: an open menu popup is painted in sk-ui tree order
  (painter's algorithm), so it renders under the later toolbar/dock siblings;
  input still routes through Clay's floating z-index (menu clicks work and are
  covered by tests). Fix lives in the ui plugin (paint floating nodes last).

## 9. Editor icons from Content/Images (APX-367)

The C++ editor's bitmap icon set is now available to v2 windows:

- **Content**: the five `Content/Images` bitmaps live in
  `editor/content/images/` (copied verbatim from `origin/main` 62b1d00e) plus
  RGBA PNG conversions of `LogoSmall.jpeg` and `skore.ico`
  (`logo_small.png`, `skore.png`) — the vendored stb_image is `STBI_ONLY_PNG`,
  so PNG is the only decodable format. The raw bytes are embedded in
  `editor/content/skore_editor_icons_embed.h` (regenerated by
  `scripts/gen-editor-icons-embed.py`), so tests/sandbox need no cwd.
- **Registry**: `editor/editor_icons.h/.c` decodes all five icons, packs them
  into one RGBA8 strip atlas, and uploads it through the existing v2 UI
  texture path (staging buffer → barrier → copy → texture view). The host
  passes `sk_editor_icons_views()` to the UI renderer every frame
  (`sk_ui_renderer_images_t`); slot 0 stays zero because the paint pass
  treats texture id 0 as "no texture".
- **Lookup**: windows resolve the registry on the app context
  (`sk_editor_icons_register` / `sk_editor_icons_resolve`) and call
  `sk_editor_icons_get(id)` → `sk_editor_icon_t{view_index, uv rect, size}`.
  The Project Browser mock content grid draws folder tiles with
  `FolderIcon.png` and file tiles with `FileIcon.png` (manifest §3.6); the
  other three icons cover ResourceAssets' generic tile fallback, the project
  manager logo, and the window icon.
- **Not implemented**: thumbnail generation stays out of scope (§2.2) — every
  asset shows the generic file icon, never a thumbnail texture.
- Render check: `sk-sandbox-shell` pins an icon sample strip (all five ids)
  plus the Project Browser content grid under the frame and captures them
  with the atlas bound. APX-377 vision frames live in
  `docs/editor/apx-377-frames/` (see `docs/editor/apx-377-vision-validation.md`).
