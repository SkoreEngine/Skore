# Repository asset system inventory (main vs v2 core)

**Task:** APX-141 — inventory only; no code ported.  
**Sources:** `main` branch editor resource system vs current `v2` core (`sk_app_api_t::add_impl`, `sk_repository_*`).  
**main tip surveyed:** `62b1d00` (`Add README notice that development continues on v2`).  
**v2 tip surveyed:** workspace default branch (current checkout).

---

## 0. Architecture snapshot (main)

Primary surface: `Editor/Source/Skore/Resource/ResourceAssets.hpp` + `ResourceAssets.cpp`.

| Concern | Location | Notes |
| --- | --- | --- |
| Asset / package / file / directory schemas | `ResourceAssets.hpp` (`ResourceAsset`, `ResourceAssetPackage`, `ResourceAssetFile`, `ResourceAssetDirectory`, `ResourceImportedAsset`, …) | Field enums, not free-standing types |
| Handler base | `ResourceAssetHandler` | Virtual extension → load/save/open/create |
| Importer base | `ResourceAssetImporter` | Extension claims → ingest/cook (or legacy `ImportAsset`) |
| Registry | file-static maps in `ResourceAssets.cpp` | By extension, by resource TypeID, by handler/importer TypeID |
| Registration | `Reflection::Type<T>()` + `ReloadAssetHandlers()` | Discovery via `Reflection::GetDerivedTypes` |
| Import queue | `pendingImports` + `ResourceAssetsUpdate` | Debounced async tasks |
| File watch | `efsw::FileWatcher` + `AssetFileListener` | 200 ms debounce; calls `handler->Reloaded` |
| Thumbnails | cache dir + `PreviewGenerator` | Listed in §3; **not ported** |
| Runtime resource store | `Runtime/.../Resource/*` | Separate from editor asset browser layer |
| Helper (non-handler) importers | `MaterialImporter`, `MeshImporter` | Free functions used by FBX/GLTF/OBJ cooks; **not** `ResourceAssetImporter` subclasses |

### 0.1 Registry maps (`ResourceAssets.cpp`)

```text
handlersByExtension      : String  → ResourceAssetHandler*
handlersByTypeID         : TypeID  → ResourceAssetHandler*   // resource type (MeshResource, …)
handlersByHandlerType    : TypeID  → ResourceAssetHandler*   // handler class TypeID
importersByExtension     : String  → ResourceAssetImporter*
importersByImporterType  : TypeID  → ResourceAssetImporter*
assetsByType             : TypeID  → HashSet<RID>            // live assets of type
thumbnails               : RID     → ThumbnailData
loadedPackages           : String  → String                  // package name → root abs path
```

`ReloadAssetHandlers()` (bound to `OnReflectionUpdated`, also called from `ResourceAssetsInit`):

1. For each derived `ResourceAssetHandler`: construct instance; index by `Extension()` and `GetResourceTypeId()`.
2. For each derived `ResourceAssetImporter`: construct instance; index every `ImportedExtensions()` entry; if `OutputExtension()` non-empty, also map that extension to the **ImportedAssetHandler** instance (wrapper type).

### 0.2 Import pipeline (main)

1. `ResourceAssets::ImportAsset(parent, path)` — enqueue path (or recurse directories).
2. Per-frame `ResourceAssetsUpdate`: lower-case extension → `importersByExtension`.
3. If `importer->OutputExtension()` non-empty → **ingest/cook path** (`IngestImportedAsset`):
   - create `ResourceImportedAsset` wrapper + parent `ResourceAsset`
   - compress original source into `OriginalData` buffer
   - set `ImporterId`, `CookerVersion`, optional settings sub-object
   - `importer->Ingest(IngestContext)` → `ApplyIngest` (sub-resources / dependencies)
   - `EnsureCooked` → `importer->Cook(CookContext)` when needed
   - attach cooked primary under the asset tree / library
4. Else → legacy `importer->ImportAsset(...)` (default base returns `false`; no concrete subclass currently relies on this alone for production import).

### 0.3 File watching (main)

- `efswWatcher` created in `ResourceAssetsInit`; recursive watch added when scanning each package `Assets/` root.
- Events coalesced in `AssetFileListener::pendingEvents` with `fileWatchDebounceTime = 200ms`.
- On ready path: map abs path → package-relative `pathId` → `Resources::FindByPath` → asset parent → `handler->Reloaded(asset, absPath)`; if version changed, refresh `ResourceAssetFile` metadata.

### 0.4 Base virtual method sets

#### `ResourceAssetHandler` (`ResourceAssets.hpp` ~232–258)

| Method | Pure? | Default (cpp) | Role |
| --- | --- | --- | --- |
| `Extension()` | pure | — | Disk extension key (e.g. `.mesh`) |
| `OpenAsset(RID)` | pure | — | Editor open action |
| `GetResourceTypeId()` | pure | — | Runtime resource TypeID |
| `GetDesc()` | pure | — | Human description |
| `Load(RID, path)` | virtual | YAML deserialize + optional `.buffer` | Load object from disk |
| `Save(RID, path)` | virtual | YAML serialize + buffer files | Persist object |
| `Create(UUID, scope)` | virtual | `Resources::Create(GetResourceTypeId(), …)` | New asset object |
| `Reloaded(RID, path)` | virtual | no-op | Hot-reload from watcher |
| `AfterMove(RID, old, new)` | virtual | no-op | Path rename side effects |
| `Export(RID, writer)` | virtual | `Resources::Serialize` | Package export |
| `GetPreviewGenerator()` | virtual | `{}` | PreviewGenerator TypeID or none |
| `GetIcon() const` | virtual | `ICON_FA_FILE` | FontAwesome / icon font |
| `GetLoadOrder() const` | virtual | `INT32_MAX` | Package scan / export sort |
| `GetAssetName(RID, String&)` | virtual | `false` | Optional custom display name |

No concrete handler currently overrides `GetLoadOrder` or `GetAssetName`.

#### `ResourceAssetImporter` (`ResourceAssets.hpp` ~260–272)

| Method | Pure? | Default | Role |
| --- | --- | --- | --- |
| `ImportedExtensions()` | pure | — | Source extensions claimed |
| `OutputExtension()` | virtual | `{}` | Cooked/wrapper extension (routes to ingest path) |
| `CookerVersion()` | virtual | `1` | Invalidates cooked cache |
| `GetSettingsType()` | virtual | `{}` | Import settings resource TypeID |
| `Ingest(IngestContext&)` | virtual | no-op | Declare sub-resources + external deps |
| `Cook(CookContext&)` | virtual | no-op | Produce cooked sub-resources |
| `ImportAsset(...)` | virtual | `false` | Legacy direct import |

#### Supporting contexts (`ResourceAssets.hpp`)

- `IngestContext` — `DeclareSubResource`, `AddDependency`, `HasDependency`
- `CookContext` — `SubResource`, `Dependency`, `CreateBuffer`, `Allocator()`
- `SubResourceAllocator` — stable UUID sub-resource creation + temp buffers
- `ApplyIngest(IngestContext&)` — persists ingest decls onto the wrapper

---

## 1. Every handler / importer subclass

Paths below are under `Editor/Source/Skore/Resource/` on **main**.

### 1.1 Handlers (`Handlers/` + inline)

Legend for override columns: listed methods are **overridden** beyond the four pure virtuals unless noted. Pure virtuals are always implemented.

| Class | File | Extension | Resource type | Desc | Overrides / notes |
| --- | --- | --- | --- | --- | --- |
| `AnimationClipHandler` | `Handlers/AnimationClipHandler.cpp` | `.animation` | `AnimationClipResource` | `"Animation Clip"` | `OpenAsset` (TODO), `GetIcon` (`ICON_FA_PERSON_RUNNING`) |
| `AnimationControllerHandler` | `Handlers/AnimationControllerHandler.cpp` | `.animcontroller` | `AnimationControllerResource` | `"Animation Controller"` | `OpenAsset` (animator workspace), `Create` |
| `AudioHandler` | `Handlers/AudioHandler.cpp` | `.audio` | `AudioResource` | `"Audio Clip"` | `OpenAsset` (TODO), `GetIcon` (`ICON_FA_FILE_AUDIO`) |
| `CSharpScriptHandler` | `Handlers/CSharpScriptHandler.cpp` | `.cs` | `CSharpScriptResource` (local type) | `"C# Component"` | `OpenAsset`, `Load`, `Save`, `Reloaded` (dotnet rebuild), `AfterMove` (rename class), `GetIcon` |
| `DCCAssetHandler` | `Handlers/DCCAssetHandler.cpp` | `.dcc_asset` | `DCCAsset` | `"DCC Asset"` | `OpenAsset`, `GetIcon`, `GetPreviewGenerator` → `DCCAssetPreviewGenerator` |
| `EntityHandler` | `Handlers/EntityHandler.cpp` | `.entity` | `EntityResource` | `"Entity"` | `OpenAsset`, `Create`, `GetPreviewGenerator` → `EntityPreviewGenerator` |
| `FontHandler` | `Handlers/FontHandler.cpp` | `.font` | `FontResource` | `"Font"` | `OpenAsset` empty, `GetIcon` |
| `MaterialGraphHandler` | `Handlers/MaterialGraphHandler.cpp` | `.matgraph` | `MaterialGraphResource` | `"Material Graph"` | `OpenAsset` (graph vs instance), `Create`, `GetIcon`, `GetPreviewGenerator` → `MaterialGraphPreviewGenerator` |
| `MeshHandler` | `Handlers/MeshHandler.cpp` | `.mesh` | `MeshResource` | `"Mesh"` | `OpenAsset` (TODO), `Create`, `GetPreviewGenerator` → `MeshPreviewGenerator` |
| `RmlUiHandler` | `Handlers/RmlUiHandler.cpp` | *(abstract mid-base)* | — | — | Shared: `OpenAsset`, `Load`, `Save`, `Reloaded`, `AfterMove`, `Create`; pure `DefaultContent()` |
| `RmlUiDocumentHandler` | same | `.rml` | `UIDocumentResource` | `"UI Document"` | `DefaultContent` (rml skeleton), `GetIcon` |
| `RmlUiStyleHandler` | same | `.rcss` | `UIStyleResource` | `"UI Style"` | `DefaultContent` (css skeleton), `GetIcon` |
| `SceneHandler` | `Handlers/SceneHandler.cpp` | `.scene` | `SceneResource` | `"Scene"` | `OpenAsset`, `Create` (seeds Lighting + PostProcessing prototypes), `GetIcon` |
| `ShaderHandler` | `Handlers/ShaderHandler.cpp` | *(abstract mid-base)* | `ShaderResource` | `"Shader"` | `OpenAsset` (open URL), custom `Load` (compile); pure `GetShaderAssetType()` |
| `RasterShaderHandler` | same | `.raster` | via base | — | `GetShaderAssetType` = Graphics |
| `ComputeShaderHandler` | same | `.comp` | via base | — | Compute |
| `RaytraceShaderHandler` | same | `.rt` | via base | — | Raytrace |
| `ConfigShaderHandler` | same | `.shader` | via base | — | None (config-driven) |
| `TextureHandler` | `Handlers/TextureHandler.cpp` | `.texture` | `TextureResource` | `"Texture"` | `OpenAsset`, `GetPreviewGenerator` → `TexturePreviewGenerator` (custom `GenerateThumbnail`) |
| `ImportedAssetHandler` | `ResourceAssets.cpp` ~1945 | `{}` empty | `ResourceImportedAsset` | `"Imported Asset"` | `OpenAsset` empty; **`Save`** writes wrapper + cooked library folder |

**Registration entry points** (all call `Reflection::Type<…>()`; some add project-browser menu items):  
`RegisterAnimationHandler`, `RegisterAnimationControllerHandler`, `RegisterAudioHandler`, `RegisterCSharpScriptHandler`, `RegisterDCCAssetHandler`, `RegisterEntityHandler`, `RegisterFontHandler`, `RegisterMaterialGraphHandler`, `RegisterMeshHandler`, `RegisterRmlUiHandler`, `RegisterSceneHandler`, `RegisterShaderHandler`, `RegisterTextureHandler`, `RegisterImportedAssetHandler` — invoked from the bottom of `ResourceAssets.cpp` / editor init chain, then re-collected by `ReloadAssetHandlers`.

**Preview generators** (not handlers; subclasses of `PreviewGenerator`):

| Class | Host handler | Overrides |
| --- | --- | --- |
| `DCCAssetPreviewGenerator` | DCC | `SetupScene` (root entity) |
| `EntityPreviewGenerator` | Entity | `SetupScene` |
| `MaterialGraphPreviewGenerator` | MaterialGraph | `SetupScene` (sphere + material), `PercentageInScreen` → 0.9 |
| `MeshPreviewGenerator` | Mesh | `SetupScene` |
| `TexturePreviewGenerator` | Texture | empty `SetupScene`; **`GenerateThumbnail` override** (GPU resize, no scene) |

### 1.2 Importers (`Importers/`)

| Class | File | Imported extensions | Output extension | Settings type | CookerVersion | Methods overridden |
| --- | --- | --- | --- | --- | --- | --- |
| `AudioImporter` | `AudioImporter.cpp` | `.wav` `.mp3` `.ogg` `.flac` | `.audio` | none | default 1 | `Ingest`, `Cook` |
| `FontImporter` | `FontImpoter.cpp` *(filename typo)* | `.ttf` `.otf` | `.font` | none | default 1 | `Ingest`, `Cook` |
| `TextureImporter` | `TextureImporter.cpp` | `.png` `.jpg` `.jpeg` `.tga` `.bmp` `.hdr` | `.texture` | `TextureImportSettings` | 1 | `Ingest`, `Cook` (+ free helpers `ImportTexture` / `ImportTextureFromMemory`) |
| `FBXImporter` | `FBXImporter.cpp` | `.fbx` | `.dcc_asset` | `FBXImportSettings` | 1 | `Ingest` (texture deps), `Cook` → `ImportFBX` |
| `GLTFImporter` | `GLTFImporter.cpp` | `.gltf` `.glb` | `.dcc_asset` | `GLTFImportSettings` | 1 | `Ingest`, `Cook` |
| `ObjImporter` | `ObjImporter.cpp` | `.obj` | `.dcc_asset` | `ObjImportSettings` | 1 | `Ingest` (mtl deps), `Cook` |

**Not `ResourceAssetImporter` subclasses** (cook helpers only):

| Module | Role |
| --- | --- |
| `MaterialImporter.cpp` / `.hpp` | `ImportMaterial(...)` — material instance from `MaterialImportData` |
| `MeshImporter.cpp` / `.hpp` | `ImportMesh` / `ReimportMesh` + `MeshImportSettings`; `RegisterMeshImportSettings()` for reflection only |

### 1.3 Extension → handler / importer claim map (main)

| Extension | Handler | Importer |
| --- | --- | --- |
| `.animation` | AnimationClipHandler | — |
| `.animcontroller` | AnimationControllerHandler | — |
| `.audio` | AudioHandler | *(output of AudioImporter)* |
| `.cs` | CSharpScriptHandler | — |
| `.dcc_asset` | DCCAssetHandler | *(output of FBX/GLTF/OBJ)* |
| `.entity` | EntityHandler | — |
| `.font` | FontHandler | *(output of FontImporter)* |
| `.matgraph` | MaterialGraphHandler | — |
| `.mesh` | MeshHandler | — |
| `.rml` | RmlUiDocumentHandler | — |
| `.rcss` | RmlUiStyleHandler | — |
| `.scene` | SceneHandler | — |
| `.raster` / `.comp` / `.rt` / `.shader` | ShaderHandler variants | — |
| `.texture` | TextureHandler | *(output of TextureImporter)* |
| `.wav` `.mp3` `.ogg` `.flac` | — | AudioImporter |
| `.ttf` `.otf` | — | FontImporter |
| `.png` `.jpg` `.jpeg` `.tga` `.bmp` `.hdr` | — | TextureImporter |
| `.fbx` | — | FBXImporter |
| `.gltf` `.glb` | — | GLTFImporter |
| `.obj` | — | ObjImporter |

Output extensions for cooked imports are also registered onto `ImportedAssetHandler` so wrapper files open/save through the imported-asset path.

### 1.4 v2 core gap (relative to this inventory)

| main concept | v2 today |
| --- | --- |
| Editor `ResourceAssets` / handlers / importers | **Absent** (C rewrite; no editor asset browser yet) |
| Runtime `Resources` store | Partially ported as **`core/repository.h`** (`sk_repository_t`, RID/UUID, hierarchy, undo scopes) — storage only, no handlers |
| Handler registration via reflection | No C++ reflection; intended multi-impl surface is **`sk_app_api_t::add_impl`** (see §2) |
| Import pipeline / efsw watch | **Absent** |
| Thumbnails / PreviewGenerator | **Absent** (call sites listed only in §3) |

---

## 2. `sk_app_api_t::add_impl` convention (v2 core)

This is the multi-implementation registry intended for plugin-extensible tables (future asset handlers/importers map cleanly here: one `type_id` for the interface, many impl pointers).

### 2.1 Signature

Declared on the process-wide function table `sk_app_api_t` in `core/app.h`:

```c
void (*add_impl)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer);
void (*remove_impl)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t pointer);
u32  (*impl_count)(sk_app_context_t* context, sk_type_id_t type_id);
u32  (*get_all_impls)(sk_app_context_t* context, sk_type_id_t type_id, const_ptr_t* out, u32 out_cap);
```

- **Lines:** `core/app.h:70` (`add_impl`), `core/app.h:80` (`remove_impl`), `core/app.h:88` (`impl_count`), `core/app.h:103` (`get_all_impls`).
- Semantics (header comments `core/app.h:59–65`, `91–96`):
  - Unlike `set_api` (single pointer per `type_id`), **`add_impl` appends** to a list.
  - Duplicate pointer addresses are allowed (two entries).
  - `remove_impl` drops the **first** exact pointer match (swap-remove; may reorder).
  - `get_all_impls` copies up to `out_cap` entries; returns **total** count (may exceed capacity); `out == NULL` / `out_cap == 0` is count-only.
  - Context owns list storage; pointers are opaque addresses (never dereferenced by the registry).

### 2.2 How type IDs are declared

`sk_type_id_t` is a 128-bit value (`u64 lo`, `u64 hi`) in `core/common.h:78–81`.

```c
#define SK_TYPE_ID(_name, _lo, _hi) ((sk_type_id_t){(u64)(_lo), (u64)(_hi)})
#define SK_TYPE_ID_ZERO ((sk_type_id_t){0ull, 0ull})
#define SK_TYPE_ID_EQ(a, b) (((a).lo == (b).lo) && ((a).hi == (b).hi))
```

- **Lines:** `core/common.h:69–90`.
- Authors write `SK_TYPE_ID("sk.some_name", …)`; CMake (`cmake/cmake_functions.cmake`) injects MD5 halves of the name string into the `lo`/`hi` args.
- Public APIs publish a named macro, e.g.:
  - `SK_PLATFORM_API_TYPE_ID` — `core/platform.h:27`
  - `SK_LOGGER_API_TYPE_ID` — `core/logger.h:106`
  - `SK_ENTITIES_API_TYPE_ID` — `plugins/entities/entities.h:68`
- Tests invent local IDs the same way: `SK_TYPE_ID("test.multi_impl", 0x1111…, 0x3333…)`.

**Note:** production plugins today register **single** module tables with `set_api` (same `type_id` namespace, separate map). `add_impl` shares the `type_id` type but a **different** map (`impls` vs `apis`); the same id can hold both an API table and an impl list independently (`app_impl_independent_of_set_get_api` test).

### 2.3 How impls are stored and enumerated

Implementation backend in `app/app.c` (not core — core declares, app implements):

```c
typedef SK_ARRAY(void_ptr_t) sk_app_impl_list_t;
typedef SK_HASH_MAP(sk_type_id_t, sk_app_impl_list_t) sk_app_impl_map_t;

struct sk_app_context_t {
    sk_app_api_map_t  apis;
    sk_app_impl_map_t impls;
    /* … runtime fields … */
};
```

- **Storage types:** `app/app.c:26–28`, member `impls` at `app/app.c:37`.
- **`add_impl`:** `app/app.c:121–133` — if no list for `type_id`, create empty `sk_array` and put in map; `sk_array_push` the pointer.
- **`remove_impl`:** `app/app.c:135–146` — linear scan, `sk_array_swap_remove`.
- **`impl_count`:** `app/app.c:148–151`.
- **`get_all_impls`:** `app/app.c:153–165` — copy min(count, out_cap).
- Wired into static table: `app/app.c:167–168`.

### 2.4 Existing call-site examples (≥2)

There are **no production plugin call sites** of `add_impl` yet; the live convention is proven by unit tests and the `set_api` registration pattern for type-id macros.

**Example A — multi-impl roundtrip** (`app/app.c:1034–1050`):

```c
SK_TEST(app_impl_add_count_roundtrip) {
    static char a = 'a', b = 'b', c = 'c';
    sk_type_id_t id = SK_TYPE_ID("test.multi_impl", 0x1111111111111111ULL, 0x3333333333333333ULL);
    sk_app_context_t* ctx = sk_app_create();
    const sk_app_api_t* api = sk_app_api();
    api->add_impl(ctx, id, &a);
    api->add_impl(ctx, id, &b);
    api->add_impl(ctx, id, &c);
    /* impl_count == 3; get_all_impls preserves insertion order */
}
```

**Example B — independent of `set_api`** (`app/app.c:1165–1180`):

```c
SK_TEST(app_impl_independent_of_set_get_api) {
    static char impl = 'i';
    static int api_val = 7;
    sk_type_id_t id = SK_TYPE_ID("test.multi_indep", 0x1111111111111111ULL, 0x8888888888888888ULL);
    /* … */
    api->add_impl(ctx, id, &impl);
    api->set_api(ctx, id, &api_val);
    /* impl list still has &impl; get_api returns &api_val */
}
```

**Parallel production pattern for type-id registration** (single-API map, same ID discipline):

| File:line | Code |
| --- | --- |
| `app/platform_unix.c:118` | `app_api->set_api(context, SK_PLATFORM_API_TYPE_ID, sk_platform_api());` |
| `plugins/entities/entities.c:1841` | `app_api->set_api(context, SK_ENTITIES_API_TYPE_ID, &entities_api);` |
| `app/app.c:74` | `api->set_api(context, SK_LOGGER_API_TYPE_ID, logger_api);` |

**Porting implication for handlers/importers:** declare interface type IDs with `SK_TYPE_ID("sk.resource_asset_handler", …)` (and importer counterpart); each plugin/module calls `app_api->add_impl(ctx, ID, &my_handler_vtable)`; hosts enumerate with `impl_count` / `get_all_impls` instead of C++ `Reflection::GetDerivedTypes`.

---

## 3. Thumbnail generation call sites (listed, **not ported**)

All paths on **main**. Graph-editor / material-node `NodeThumbnail` / `ResolveThumbnail` that display *live GPU texture caches* (not the asset-browser thumbnail pipeline) are marked **UI-only**.

### 3.1 Core pipeline

| File | Line(s) | What |
| --- | --- | --- |
| `Editor/Source/Skore/EditorCommon.hpp` | 46–48 | Comment: mesh/entity thumbnails before texture import finishes; `ImportChildAssetsAsync` |
| `Editor/Source/Skore/EditorCommon.hpp` | 133 | `constexpr Extent thumbnailSize = {128, 128}` |
| `Editor/Source/Skore/Resource/ResourceAssets.hpp` | 248 | `ResourceAssetHandler::GetPreviewGenerator` |
| `Editor/Source/Skore/Resource/ResourceAssets.hpp` | 318–321 | `GetThumbnail`, `GetDefaultThumbnail`, `UpdateThumbnail` API |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 44–50 | `ThumbnailData` struct |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 102–104 | `thumbnailDirectory`, `thumbnails` map, mutex |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 145–149 | `HasThumbnail` (disk probe under cache) |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 151–196 | `UpdateThumbnailData` (decompress ZSTD file → GPU texture) |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 316–318 | default `GetPreviewGenerator` → empty |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 2622–2708 | **`GetThumbnail`** — version check, schedule load or `PreviewGenerator::GenerateThumbnail` task |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 2711–2713 | `GetDefaultThumbnail` (`assertTexture` / file icon) |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 2725–2738 | **`UpdateThumbnail`** — ZSTD compress RGBA → cache file → `UpdateThumbnailData` |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 2915+ | shutdown: destroy thumbnail GPU textures |
| `Editor/Source/Skore/Resource/ResourceAssets.cpp` | 3072–3075 | init: create `…/Cache/Thumbnails` |
| `Editor/Source/Skore/Utils/PreviewGenerator.hpp` | 10–30 | base class; `SetupScene`, `GenerateThumbnail`, `asset` RID |
| `Editor/Source/Skore/Utils/PreviewGenerator.cpp` | 27–29 | `ThumbnailBufferName`; standalone RenderGraph path comment |
| `Editor/Source/Skore/Utils/PreviewGenerator.cpp` | 106+ | warm-up render pass notes |
| `Editor/Source/Skore/Utils/PreviewGenerator.cpp` | 163–180 | copy-to-thumbnail-buffer pass |
| `Editor/Source/Skore/Utils/PreviewGenerator.cpp` | 359–384 | `SetupDefaultEnvironment`, `PopulateScene` |
| `Editor/Source/Skore/Utils/PreviewGenerator.cpp` | 386–462 | **`GenerateThumbnail`** default scene render → `ResourceAssets::UpdateThumbnail` |

### 3.2 Handler-side generators

| File | Line(s) | What |
| --- | --- | --- |
| `…/Handlers/DCCAssetHandler.cpp` | 11, 16–38, 76–78, 163 | `DCCAssetPreviewGenerator`; `GetPreviewGenerator` |
| `…/Handlers/EntityHandler.cpp` | 8, 12–28, 56–58, 83 | `EntityPreviewGenerator` |
| `…/Handlers/MaterialGraphHandler.cpp` | 13, 18–45, 121–123, 177 | `MaterialGraphPreviewGenerator` |
| `…/Handlers/MeshHandler.cpp` | 10, 18–36, 75–77, 83 | `MeshPreviewGenerator` |
| `…/Handlers/TextureHandler.cpp` | 11, 15–117, 150–152, 158 | **`TexturePreviewGenerator::GenerateThumbnail`** override + `UpdateThumbnail` |
| `…/Handlers/SceneHandler.cpp` | 8 | includes `PreviewGenerator.hpp` but **does not** override `GetPreviewGenerator` |

### 3.3 Consumers (request display)

| File | Line(s) | What |
| --- | --- | --- |
| `Editor/Source/Skore/Window/ProjectBrowserWindow.cpp` | 667 | `desc.texture = … ResourceAssets::GetThumbnail(asset)` |
| `Editor/Source/Skore/ImGui/ImGui.cpp` | 1176–1345 | content table layout using `thumbnailScale` / cell size |
| `Editor/Source/Skore/ImGui/ImGui.cpp` | 2289, 2308–2309 | content item zoom; `GetThumbnail` / `GetDefaultThumbnail` for parent assets |
| `Editor/Source/Skore/ImGui/ImGui.hpp` | 73, 241 | `thumbnailScale` field; `ImGuiBeginContentTable` |
| `Editor/Source/Skore/Project/ProjectManager.cpp` | 474, 550, 566, 582 | sets `contentItem.thumbnailScale = 1.0` (layout only) |

### 3.4 UI-only thumbnail (not asset-cache pipeline)

| File | Line(s) | What |
| --- | --- | --- |
| `Editor/Source/Skore/ImGui/GraphEditor.hpp` | 110, 175, 271 | `NodeThumbnail`, per-node `ImTextureID`, size constant |
| `Editor/Source/Skore/ImGui/GraphEditor.cpp` | 59–62, 671–673, 796–807 | store/draw node preview image |
| `Editor/Source/Skore/Window/MaterialGraphEditorWindow.cpp` | 236–267, 450–452, 482–493 | `ResolveThumbnail` via `RenderResourceCache` (live texture, not `ResourceAssets` thumbnails) |
| `Editor/Source/Skore/Window/MaterialGraphEditorWindow.hpp` | (thumbnail cache members) | `m_thumbnailCaches` |

### 3.5 Porting note

Do **not** carry thumbnail generation into the initial repository-asset port. Keep this list as a follow-up surface after handlers/importers and the package scan/import pipeline exist on v2.

---

## 4. Suggested v2 mapping (inventory only)

| main | v2 direction |
| --- | --- |
| `ResourceAssetHandler` vtable | C struct of function pointers; register with `add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &impl)` |
| `ResourceAssetImporter` vtable | same pattern, separate type id |
| `handlersByExtension` / `importersByExtension` | host builds maps by enumerating `get_all_impls` after plugins load |
| `ReloadAssetHandlers` / reflection | plugin load + optional re-scan of impl lists |
| `Resources::*` object store | `sk_repository_*` already in `core/repository.h` |
| efsw watch / thumbnails | later tasks; out of scope here |

---

## 5. File index (main, editor resource system)

```
Editor/Source/Skore/Resource/ResourceAssets.hpp
Editor/Source/Skore/Resource/ResourceAssets.cpp
Editor/Source/Skore/Resource/Handlers/*.cpp          (13 translation units; multiple classes in Shader/Rml)
Editor/Source/Skore/Resource/Importers/*.cpp|.hpp
Editor/Source/Skore/Utils/PreviewGenerator.hpp|.cpp
Runtime/Source/Skore/Resource/*                     (runtime store; not editor browser)
Tests/Source/Editor/AssetTests.cpp
Tests/Source/Editor/ImportedAssetTests.cpp
```

End of inventory. No runtime behavior changed; documentation only.
