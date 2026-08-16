# Editor window migration audit (APX-378)

**Task:** APX-378 — independently verify the C++ → v2 window migration
surface after APX-374. Do **not** migrate anything new or fix unrelated
code; report unmigrated windows, source windows missing from the
manifest, and rule violations as a list with file paths.

**Goal:** editor window (ui) migration
**Branch:** `feature/editor-window-ui-migration`
**Manifest:** `docs/editor/migration-manifest.md` (APX-364)
**Source editor:** `origin/main` @ `62b1d00e62a97747a95b16f11a9f9c8bdc1c29a0`
**Method:** read the manifest; `git ls-tree` / `git grep` on
`origin/main Editor/Source/Skore/Window/`; inspect every v2
`editor/windows/*.h/.c`, `editor/main_windows.c`, boot registration in
`editor/main.c` and `editor/editor_shell.c`; grep for Event-bus usage
and graph-node work; build `sk-editor-lib` + `sk-editor-tests` +
`sk-tests` (Release, Ninja) and run `./sk-tests --filter=editor_*`.

---

## Verdict

The in-scope window migration surface is **closed**.

- Every in-scope manifest entry (§3.1–§3.10) is marked **Migrated to v2**.
- Each of those ten windows exists under `editor/windows/` and follows
  the goal rules (ops table + `add_impl`, notify.h observers, Content/Images
  icons, mocked data where the v2 backend is missing, no graph-node work).
- The four graph-node windows (§3.11–§3.14) are **not** marked migrated.
  That is intentional: they are out of scope (§2.1 / §4 / goal rule 3).
- No `EditorWindow` subclass on `origin/main` is missing from the
  manifest.
- No goal-rule violations that need a follow-up migration task.
- Editor test suite: **48 Tests 0 Failures 0 Ignored**.

---

## 1. Manifest status (every §3 entry)

Legend: **Migrated** = the Incomplete cell contains `Migrated to v2
(APX-xxx)` and a v2 `editor/windows/<name>_window.h/.c` pair exists.

| § | C++ class | Manifest mark | v2 files | Status |
| --- | --- | --- | --- | --- |
| 3.1 | `ConsoleWindow` | Migrated to v2 (APX-373) | `editor/windows/console_window.h/.c` | migrated |
| 3.2 | `DebuggerWindow` | Migrated to v2 (APX-374) | `editor/windows/debugger_window.h/.c` | migrated |
| 3.3 | `EntityTreeWindow` | Migrated to v2 (APX-370) | `editor/windows/entity_tree_window.h/.c` | migrated |
| 3.4 | `HistoryWindow` | Migrated to v2 (APX-374) | `editor/windows/history_window.h/.c` | migrated |
| 3.5 | `PackagesWindow` | Migrated to v2 (APX-374) | `editor/windows/packages_window.h/.c` | migrated |
| 3.6 | `ProjectBrowserWindow` | Migrated to v2 (APX-369) | `editor/windows/project_browser_window.h/.c` | migrated |
| 3.7 | `PropertiesWindow` | Migrated to v2 (APX-371) | `editor/windows/properties_window.h/.c` | migrated |
| 3.8 | `ResourceDebuggerWindow` | Migrated to v2 (APX-374) | `editor/windows/resource_debugger_window.h/.c` | migrated |
| 3.9 | `SceneViewWindow` | Migrated to v2 (APX-372) | `editor/windows/scene_view_window.h/.c` | migrated |
| 3.10 | `SettingsWindow` | Migrated to v2 (APX-374) | `editor/windows/settings_window.h/.c` | migrated |
| 3.11 | `GraphEditorWindow` | **not marked migrated** (out of scope §4) | no dedicated TU; scaffold in `editor/main_windows.c` | see §3 |
| 3.12 | `AnimatorGraphWindow` | **not marked migrated** (out of scope §4) | no dedicated TU; scaffold in `editor/main_windows.c` | see §3 |
| 3.13 | `MaterialGraphEditorWindow` | **not marked migrated** (out of scope §4) | no dedicated TU; scaffold in `editor/main_windows.c` | see §3 |
| 3.14 | `AnimatorTreeViewWindow` | **not marked migrated** (out of scope §4) | no dedicated TU; scaffold in `editor/main_windows.c` | see §3 |

Entries **not** marked migrated: the four graph-node windows above. They
are listed so a later task can own them if graph work is ever opened;
this audit does not start that work.

---

## 2. Rule check for every window marked migrated

Goal rules checked on each of §3.1–§3.10:

1. Public functions only through a per-window ops struct registered with
   `app_api->add_impl` (looked up via `sk_editor_window_ops_lookup`).
2. Cross-window notifications via `notify.h` observer structs (no Event
   bus).
3. Icons sourced from `Content/Images` (`editor/content/images/` +
   `editor/editor_icons.h/.c`).
4. Mocked data where v2 lacks the backing feature.
5. No graph-node (animation / material) work.

Boot order (real impl first so `window_open` wins over the APX-330
scaffold) is `editor/main.c` (`run_shell_mode`) and the shell test
fixture in `editor/editor_shell.c`.

| Window | ops table + add_impl | observers | icons | mock where needed | no graph work |
| --- | --- | --- | --- | --- | --- |
| Console | `sk_editor_console_ops_t` / `SK_EDITOR_CONSOLE_OPS_TYPE_ID` | none (logger sink, matching C++) | none (glyph title only) | n/a (bound to v2 logger) | yes |
| Debugger | `sk_editor_debugger_ops_t` | none (C++ polls Profiler) | none | session mock profiler (no v2 Profiler) | yes |
| Entity Tree | `sk_editor_entity_tree_ops_t` | publishes SELECTION / ENTITY_* | none (C++ FA glyphs; v2 text V/L) | visible/lock, create-from-asset, show-scene-entity | yes |
| History | `sk_editor_history_ops_t` | none | none | mock undo/redo stacks (no v2 undo) | yes |
| Packages | `sk_editor_packages_ops_t` | none | none | mock package list + Add pending (no `packages:` / PickFolder) | yes |
| Project Browser | `sk_editor_project_browser_ops_t` | drop-file subscribe; selection / asset opened / activated publish | `FolderIcon.png` / `FileIcon.png` via icon atlas | thumbnails generic file icon; Import / Explorer / Copy Path MOCK | yes |
| Properties | `sk_editor_properties_ops_t` | subscribes ENTITY / DEBUG / ASSET / RESOURCE / MATERIAL_NODE | none | preview pane, layer, import-settings, material-node label | yes (material node is a MOCK label, no graph) |
| Resource Debugger | `sk_editor_resource_debugger_ops_t` | none | none | type enum approximated by builtin ids + RID walk | yes |
| Scene View | `sk_editor_scene_view_ops_t` | publishes VIEWPORT_STATE | none (placeholder texture, not a Content/Images icon) | gizmos / camera / sim / 2D / render-debug session state | yes |
| Settings | `sk_editor_settings_ops_t` | none | none | mock groups/entries + collision matrix | yes |

Public symbols on each window header are `sk_editor_<name>_register` /
`sk_editor_<name>_shutdown` plus the inline ops lookup. Implementations
in the `.c` are `static`. Scene View additionally publishes the
host-binding texture helpers (`sk_editor_scene_view_texture_*`) — the
same pattern as `editor/editor_icons.h` (APX-367), not window ops such
as `ClearSelection`. Not a rule-1 violation.

`Event::` / `sk_event` / event-bus usage in `editor/` is comments only
(`notify.h`, `properties_window.c`). Dispatch is `sk_editor_notify_*`.

---

## 3. Lists for follow-up planning (the deliverable)

### 3.1 Unmigrated windows

These C++ `EditorWindow` subclasses are in the manifest but **not**
marked migrated. Graph-node work is out of scope (goal rule 3, manifest
§2.1 / §4). v2 has title/dock scaffolds only.

| C++ files (`origin/main`) | Manifest | v2 today |
| --- | --- | --- |
| `Editor/Source/Skore/Window/GraphEditorWindow.hpp/.cpp` | §3.11 | `editor/main_windows.c` (empty `Draw`, Center / Graph, order 20) |
| `Editor/Source/Skore/Window/MaterialGraphEditorWindow.hpp/.cpp` | §3.13 | `editor/main_windows.c` (empty `Draw`, Center / Material) |
| `Editor/Source/Skore/Window/AnimatorGraphWindow.hpp/.cpp` | §3.12 | `editor/main_windows.c` (empty `Draw`, Center / Animator) |
| `Editor/Source/Skore/Window/AnimatorTreeViewWindow.hpp/.cpp` | §3.14 | `editor/main_windows.c` (empty `Draw`, Left / Animator) |

Type ids: `SK_EDITOR_WINDOW_GRAPH_EDITOR`,
`SK_EDITOR_WINDOW_MATERIAL_GRAPH_EDITOR`,
`SK_EDITOR_WINDOW_ANIMATOR_GRAPH`,
`SK_EDITOR_WINDOW_ANIMATOR_TREE_VIEW` in `editor/main_windows.h`.

Manifest §2.1 says the scaffolds' `Draw` bodies would be mocked static
nodes/links. Current `Draw` is empty. That matches the goal ("ignore
anything related to graph nodes") and is **not** a defect of the
in-scope sweep. Do not start graph or gizmo work from this list.

### 3.2 Source-editor windows absent from the manifest

**None.**

`git grep 'public EditorWindow' origin/main -- Editor/` returns exactly
the 14 classes in `Editor/Source/Skore/Window/`. `git ls-tree
origin/main --name-only Editor/Source/Skore/Window/` is 28 files (14
`.cpp` + 14 `.hpp`). All 14 are in manifest §5. v2
`editor/windows/` has the 10 in-scope pairs and no extra window class.

`editor/console_panel.c` is the APX-139 `--ui-migration` dual-stack
demo, not a C++ `EditorWindow`.

### 3.3 Goal-rule violations

**None** that require a new window-migration task.

Documented leftover MOCK after both ends of the C++ call graph now
exist (manifest §6.3). These are **not** unmigrated windows and **not**
rule-1/2/3/5 violations; they are optional follow-up wiring:

| Item | Paths | Notes |
| --- | --- | --- |
| Project Browser "Show Resource Inspector" still records a path id instead of calling `inspect_resource` | `editor/windows/project_browser_window.c` (`pb_action_show_resource_inspector`); stale comment in `editor/windows/project_browser_window.h` | Manifest §3.6 already flags this MOCK. Resource Debugger is migrated (APX-374). |
| Entity Tree "Show Resource Inspector" still records the selected RID | `editor/windows/entity_tree_window.c` (`et_action_show_resource_inspector`); stale comments in `editor/windows/entity_tree_window.h` and the `.c` file header | Same C++ call (`InspectResource`). |
| Entity Tree double-click → `SceneViewWindow::ViewEntity` not wired | `editor/windows/entity_tree_window.c` (file header: no double-click handler) | Scene View ops expose `view_entity` (APX-372). Manifest §3.3 lists framing as not ported. |

Do **not** treat these as APX-378 work. Plan them separately if the
cross-window call graph in manifest §6.3 should be closed.

---

## 4. Build + editor test suite (verbatim)

Configure and build (Release, Ninja) from the repo root:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sk-tests sk-editor-lib sk-editor-tests
```

`sk-editor-lib` compiled every migrated window TU (`console_window.c`,
`debugger_window.c`, `entity_tree_window.c`, `history_window.c`,
`packages_window.c`, `project_browser_window.c`, `properties_window.c`,
`resource_debugger_window.c`, `scene_view_window.c`,
`settings_window.c`) plus `main_windows.c`. `sk-editor-tests` rebuilt
the same TUs with `SK_TESTS`. Exit code **0**.

Run (cwd `build/bin`):

```
./sk-tests --filter=editor_*
```

Unity host result (verbatim):

```
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:387:editor_console_panel_retained_logs_and_filter:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:388:editor_api_resolves_via_app_registry:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:389:editor_icons_decode_atlas_and_lookup:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:390:editor_icons_upload_path_and_views:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:391:editor_icons_draw_command_from_lookup:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:392:editor_workspace_presets_match_manifest:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:393:editor_layout_switch_does_not_leak_or_double_register:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:394:editor_layout_roundtrip_save_switch_restore_compare:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:395:editor_layout_corrupt_and_unknown_window_do_not_crash:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:396:editor_layout_disk_roundtrip_restores_on_init:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:397:editor_shell_frame_menu_toolbar_dock:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:398:editor_shell_window_menu_uses_registry:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:399:editor_shell_window_close_and_toolbar:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:400:editor_shell_workspace_switch_rebuilds_dock:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:401:editor_ui_host_dual_stack_same_frame:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:402:editor_imgui_shell_immediate_selection:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:403:editor_workspace_mask_contains_bits:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:404:editor_workspace_impls_register_and_count:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:405:editor_window_impls_register_open_close:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:406:editor_scene_default_dock_layout:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:407:editor_window_close_removes_instance:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:408:editor_window_undock_redock_changes_parent:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:409:editor_main_windows_impl_count_covers_all_types:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:410:editor_notify_empty_emit_is_noop:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:411:editor_notify_dispatches_sorted_by_order:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:412:editor_notify_remove_and_copy_before_walk:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:413:editor_notify_payloads_match_cpp_events:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:414:editor_project_open_scan_and_import_via_core:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:415:editor_project_rejects_missing_assets_dir:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:416:editor_window_ops_lookup_first_impl:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:417:editor_console_window_ops_and_logger_sink:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:418:editor_console_window_ui_dock_chrome_and_filter:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:419:editor_debugger_window_ops_mock_profiler:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:420:editor_entity_tree_ops_mock_scene:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:421:editor_entity_tree_ops_real_scene:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:422:editor_entity_tree_ui_dock_tree:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:423:editor_history_window_ops_mock_stacks:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:424:editor_packages_window_ops_mock_list:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:425:editor_project_browser_ops_real_assets_listing:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:426:editor_project_browser_save_load_roundtrip:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:427:editor_project_browser_ui_dock_tree_grid_context:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:428:editor_properties_window_ops_selection_and_components:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:429:editor_properties_window_ui_components_and_menus:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:430:editor_resource_debugger_window_ops_repo_introspection:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:431:editor_scene_view_placeholder_texture_upload:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:432:editor_scene_view_ops_mock:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:433:editor_scene_view_chrome_and_aspect:PASS
/home/apex/.apex-runner/workdirs/027924ca-d8ea-4621-a7fc-933ded826a9f/tasks/82ccbb31-a375-499f-8387-a74029649e72/skore/foundation/test.c:434:editor_settings_window_ops_mock_tree:PASS

-----------------------
48 Tests 0 Failures 0 Ignored
OK
SK_TEST_FILTER: ran=48 skipped=387
host: ran=48 failed=0

======== TOTAL: ran=48 failed=0 ========
```

Failures: **none**.

Release plugins omit `sk_plugin_run_tests`; the host still scanned
`build/bin/plugins` and skipped those DLLs. That is expected for this
Release unit run and is not an editor-window failure.

---

## 5. What this task did not do

No new window was migrated. No inspect/view-entity wiring was added.
No graph-node editor was started. No unrelated editor/UI/plugin code
was changed.
