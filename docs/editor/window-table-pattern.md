# Window ops tables and add_impl observers (APX-365)

Companion to `docs/editor/migration-manifest.md`. This is the pattern every
later window migration copies. There is **no event bus**.

## Public window functions: per-window ops table

Callers never take a direct symbol such as `sk_editor_project_browser_clear_selection`.
Each window class publishes one process-lifetime struct of function pointers.

```c
app_api->add_impl(ctx, SK_EDITOR_PROJECT_BROWSER_OPS_TYPE_ID, &pb_ops);

const sk_editor_project_browser_ops_t* pb = sk_editor_project_browser_ops(ctx, api);
pb->clear_selection(ctx, api, window, NULL);
```

| Rule | Contract |
| --- | --- |
| Registration | Once per app context at boot. Table is `static` / process-lifetime. Register functions are idempotent. |
| Lookup | `sk_editor_window_ops_lookup` → first `add_impl` for that ops type id. Missing → NULL. |
| Lifetime | Table outlives every open instance. Pointers die at `remove_impl` / `sk_app_shutdown`. |
| Implementations | `static` in the window `.c`. The register function is the only published symbol. |

Reference: `editor/windows/project_browser_window.h` / `.c`.

## Notifications: observer structs, not an event bus

C++ `Event::Bind` / `EventHandler<T>::Invoke` is replaced by one observer
struct + one type id per kind (selection changed, asset opened/activated,
entity created/renamed/deleted, drop file, …). See `editor/notify.h`.

```c
sk_editor_on_asset_selection_t obs = {.order = 10, .user = self, .on_asset_selection = on_asset};
app_api->add_impl(ctx, SK_EDITOR_NOTIFY_ASSET_SELECTION, &obs);

/* publisher (window or selection code) */
sk_editor_notify_asset_selection(ctx, api, workspace_id, rid);

app_api->remove_impl(ctx, SK_EDITOR_NOTIFY_ASSET_SELECTION, &obs);
```

| Rule | Contract |
| --- | --- |
| Registration | `add_impl(ctx, SK_EDITOR_NOTIFY_*, &observer)`. Storage is subscriber-owned (usually a field on the window instance). |
| Lookup | `get_all_impls` / `impl_count` on that type only. No wildcard, no queue, no deferred invoke. |
| Ordering | First field is `i32 order` (lower first). Ties keep insertion order. Dispatch **copies** the list before calling. |
| Lifetime | `destroy` must `remove_impl` every observer the window added. Do not register a stack observer without a matching remove. |

`sk_editor_notify_*` helpers only walk one type's impls. Do not add a central
`Event` type, a subscribe API, or a queued bus.

## Adding the next window

1. New TU under `editor/windows/<name>_window.h` / `.c` (do not edit a v2-owned
   file to insert the window — PR CI compiles the merge into v2).
2. Ops struct = the C++ public methods from the manifest (§3).
3. `sk_editor_<name>_register` adds the window impl + the ops table.
4. Subscribe/publish with `notify.h` structs. Mock out-of-scope data
   (graphs, thumbnails, scene render) as the manifest says.
5. When the real impl should replace the APX-330 stub, stop registering that
   row from `main_windows.c` (or register the real impl first — `window_open`
   copies the first matching type id).
