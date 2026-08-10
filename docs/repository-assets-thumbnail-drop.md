# Repository assets: editor thumbnail drop report (APX-146)

**Status:** Thumbnails / PreviewGenerator were **not** ported into v2 core or the
v2 editor. The editor is only a consumer of `sk_resource_assets_api_t` and
`sk_resource_asset_builtins_register_impls`. There is no second
ResourceAssetHandler / ResourceAssetImporter implementation on the editor side.

The C++ main-branch editor never landed on this branch as source; the behavior
below is what is **lost** relative to main when using the core-backed editor.

## Editor features that lost thumbnail behavior

| Feature (main) | What users saw | v2 status |
| --- | --- | --- |
| **Project browser asset grid** (`ProjectBrowserWindow`) | Per-asset GPU thumbnail via `ResourceAssets::GetThumbnail` | Icons / thumbnails not available; content table can only use default file icons when UI lands |
| **ImGui content table / content item zoom** (`ImGui.cpp` `GetThumbnail` / `GetDefaultThumbnail`) | Zoomed parent-asset previews in browser panels | No asset-cache thumbnails; layout may still use scale fields later but textures will be absent |
| **Default thumbnail fallback** (`GetDefaultThumbnail` → assert/file icon) | Placeholder when generation pending | No cache pipeline; no default GPU thumbnail texture from the asset system |
| **On-disk thumbnail cache** (`…/Cache/Thumbnails`, ZSTD RGBA) | Persistent previews across sessions | Not created; no HasThumbnail / UpdateThumbnail |
| **Async thumbnail generate on first view** | Background `PreviewGenerator::GenerateThumbnail` task | Not scheduled |
| **DCC asset browser previews** (`DCCAssetPreviewGenerator`) | 3D root-entity snapshot for `.dcc_asset` | Lost |
| **Entity browser previews** (`EntityPreviewGenerator`) | Entity hierarchy snapshot for `.entity` | Lost |
| **Material graph browser previews** (`MaterialGraphPreviewGenerator`) | Sphere + material snapshot for `.matgraph` | Lost |
| **Mesh browser previews** (`MeshPreviewGenerator`) | Mesh snapshot for `.mesh` | Lost |
| **Texture browser previews** (`TexturePreviewGenerator::GenerateThumbnail`) | GPU resize of texture into thumbnail | Lost |
| **Handler `GetPreviewGenerator` hook** | Handler-selected preview class | Omitted from `sk_resource_asset_handler_t` (no field / dispatch) |

## Intentionally out of this drop (UI-only, not asset-cache)

These main paths used live GPU textures / node previews, **not**
`ResourceAssets` thumbnail files. They are unrelated to the asset pipeline port
and are neither present nor “dropped” as part of APX-146:

- Graph editor `NodeThumbnail` / per-node `ImTextureID`
- Material graph editor `ResolveThumbnail` via `RenderResourceCache`

## Editor surface after APX-146

| Concern | Location |
| --- | --- |
| Handler / importer tables | `core/resource_asset_builtins.c` → `add_impl` |
| Scan / import engine | `core/resource_assets.c` (`sk_resource_assets_api_t`) |
| Editor project open / import | `editor/project.c` (calls core only) |
| Host binary | `sk-editor` (`editor/main.c`) |

Registration entry point used by the editor:

```c
sk_resource_asset_builtins_register_impls(app_context, app_api);
/* discovers tables with sk_resource_assets_api()->create(...); reload_handlers inside create */
```

No `ResourceAssets.hpp` / `PreviewGenerator` / editor `Handlers/*` / `Importers/*`
remain on this branch as a parallel implementation.
