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
| Companion docs | `docs/WIDGET_MANIFEST.md` (APX-335 widget audit), `docs/ui-editor-migration.md` (APX-139 Console port), `editor/main_windows.c` (v2 window scaffolding, same 14 windows) |

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

| Event type | Signature | Published by | Subscribed by (windows) |
| --- | --- | --- | --- |
| `OnSelectionChanged` | `void()` | `Selection` (on any selection change) | none of the 14 |
| `OnEntitySelection` | `void(u32 workspaceId, RID)` | `Selection` | PropertiesWindow |
| `OnEntityDeselection` | `void(u32 workspaceId, RID)` | `Selection` | PropertiesWindow |
| `OnEntityDebugSelection` | `void(u32 workspaceId, Entity*)` | `Selection` (runtime entity selection) | PropertiesWindow |
| `OnEntityDebugDeselection` | `void(u32 workspaceId, Entity*)` | `Selection` | PropertiesWindow |
| `OnAssetSelection` | `void(u32 workspaceId, RID)` | `EditorWorkspace` (`WorkspaceResourceState::SelectedAsset` change, set via `EditorWorkspace::OpenAsset`) | PropertiesWindow |
| `OnResourceSelection` | `void(u32 workspaceId, RID)` | `AnimatorEditor`, AnimatorTreeViewWindow | PropertiesWindow |
| `OnMaterialNodeSelection` | `void(u32 workspaceId, RID)` | MaterialGraphEditorWindow | PropertiesWindow |
| `OnDropFileCallback` | `void(StringView path)` | App (OS file drop) | ProjectBrowserWindow |
| `OnUpdate` / `OnShutdown` / `OnShutdownRequest` | `void()` / `void()` / `void(bool*)` | App | Editor core (not windows); ProjectBrowserWindow binds `OnShutdown` via its init/shutdown helpers |

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
| Incomplete | Auto-scroll only follows when already at bottom. Otherwise functional. **Already ported to v2**: `editor/console_panel.c` (see `docs/ui-editor-migration.md`). |

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
| Incomplete | Statistics tab depends on runtime `Profiler` data; GPU tab data presence-dependent. v2: mock frame statistics (or omit GPU tab until profiler exists). |

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
| Incomplete | `AddComponent` is an empty `//TODO` stub (its menu item is never registered — dead); `CheckIsOverride` always returns `false` and `RemoveOverride` is a no-op, so the "Revert Instance Overrides" menu entry is dead; `CheckEntityActions` is effectively `!removed`. Prototype-override flows (`AddBackToThisInstance`, removed-entity display) are partially implemented. v2: entity tree + active/visible/lock toggles + create/duplicate/delete/rename are the real surface; override-revert items get mock/no-op handlers. |

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
| Incomplete | Flat list of undo/redo scope names, no grouping/icons. v2: mock undo history (or wire to v2 undo stack once it exists). |

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
| Incomplete | Add/remove project package folders only; package list lives in the `.skore` project file (`packages:` sequence, `Editor::SaveProjectFile`). Functional as-is. |

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
| Incomplete | Folder-tree **drag-drop source** is commented out (folder nodes accept drops but cannot be dragged; content-grid and asset-tree-leaf drags are active); `SelectedItems`/`LastSelectedItem` resource fields dead; breadcrumb "select folder" popup; zoom slider applies only to content grid. Import flow = `Platform::OpenDialogMultiple` → `ResourceAssets::ImportAsset`. v2: tree + content grid + import + rename + delete + move + drag-drop are the real surface; thumbnails mocked. |

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
| Incomplete | Preview pane (`PreviewMode::Scene` + `PreviewGenerator` + offscreen `DefaultRenderPipeline`; `PreviewMode::Texture` mip/zoom/pan viewer) = **thumbnail generation / scene rendering → out of scope**, mock in v2. Material-graph node/instance property editors depend on `MaterialGraphResource` (**graph editor out of scope**); import-settings draft/clone + Apply/Reimport depends on `ResourceAssets::CookAsset`. In-scope surface: entity header (name/UUID/layer), component list with add/remove/reset/move-up/move-down, asset name/UUID + generic resource fields. |

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
| Incomplete | Read-only viewer (no field editing); two tabs (Types, Instance) with history navigation. Functional. |

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
| Incomplete | `Draw2DViewport` is entirely `#if 0` (2D view mode stub); "scene-options" popup is empty (PathTracer commented out); render debug toggles depend on the render graph (`RenderDebug::ForcedLod`, `selectedTextureToShow`). Everything visual (render pipeline, gizmo, entity picking, icon overlay) is **real scene rendering → out of scope**. v2: toolbar + viewport options popups + mock canvas; gizmo/camera mocked. |

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
| Incomplete | Collapsing-tree of setting groups built in `Init`; per-entry `ImGuiDrawResource` field editors; layer-collision matrix for `PhysicsSettings` (custom draw list). Functional. v2: mock the collision matrix until physics settings exist. |

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
