# Repository assets: JSON serialization contract

**Task:** APX-186 — audit + contract only; **no runtime behavior change**.  
**Goal context:** implement Serialize/Deserialize on repository assets, then asset integration tests.  
**Audience:** follow-on implementers of resource-level JSON (de)serialization.

Related docs:

- `docs/repository-assets-inventory.md` — handler/importer inventory and main→v2 mapping
- `docs/repository-assets-thumbnail-drop.md` — thumbnails intentionally out of scope

---

## 1. Audit summary

### 1.1 Where the asset repository lives

| Layer | Path | Role |
| --- | --- | --- |
| Resource store | `core/repository.h`, `core/repository.c` | RID/UUID store, field accessors, hierarchy, undo scopes. **No** Serialize/Deserialize. |
| Asset type model | `core/resource_assets_types.h`, `core/resource_assets_types.c` | Package / file / asset / directory / imported + entry structs; `sk_resource_assets_register_types`. |
| Asset engine | `core/resource_assets.h`, `core/resource_assets.c` | Scan, create/move, import, handler maps. Load path for Serialize/Deserialize **deferred**. |
| Built-in handlers/payloads | `core/resource_asset_builtins.h`, `core/resource_asset_builtins.c` | Concrete handlers/importers + minimal payload types; most `load`/`save`/`export_object` are NULL. |
| Archive I/O | `core/serialization.h`, `core/serialization.c` | Binary + JSON archive backends (yyjson private). Ready for resource writers/readers. |
| Editor consumer | `editor/project.c` | Opens packages via core asset APIs only. |

### 1.2 Identity: RID vs UUID vs path

| Handle | Type | Stable across process sessions? | Notes |
| --- | --- | --- | --- |
| **RID** | `sk_rid_t { u64 id }` | **No** | Dense page index; slot 0 is invalid (`SK_RID_ZERO`). Reassigned on recreate. **Never persist RIDs in JSON.** |
| **UUID** | `sk_uuid_t { u64 lo, u64 hi }` | **Yes when caller-supplied or reloaded from disk** | Lookup via `find_by_uuid`. Create with a non-zero UUID is idempotent (returns existing RID). |
| **Auto UUID** | `sk_repo_make_uuid` | **No** (session-local) | `lo` = counter; `hi` = `(uintptr_t)repository`. Fine for in-session sub-resources; **must be replaced/persisted as explicit UUIDs** before save if identity must survive reload. |
| **Path** | repository path string (`set_path` / `get_path` / `find_by_path`) | **Yes if written to disk** | Unique among live resources. Asset browser also uses `PathId` (package-relative string field). |
| **Type id** | `sk_type_id_t { u64 lo, u64 hi }` | **Yes** | Compile-time MD5 halves of a stable name (`SK_TYPE_ID("sk.…", …)`). |

**Rule for this contract:** on-disk JSON identity is **UUID** (and optionally `PathId` / type name). RIDs exist only in memory after load.

### 1.3 How inter-asset references are stored in memory

Field categories from `sk_resource_field_type_t` that cross resources:

| Category | In-blob storage | Semantics |
| --- | --- | --- |
| `REFERENCE` | `sk_rid_t` | Soft link; target not owned. |
| `REFERENCE_ARRAY` | `sk_field_rid_array_t` | Heap array of RIDs. |
| `SUB_OBJECT` | `sk_rid_t` | Owned child; parent chain via repository hierarchy. |
| `SUB_OBJECT_LIST` | `sk_field_subobject_list_t` | Owned RID list + `prototype_removed` set. |

Clone / create_from_prototype remap RIDs inside the cloned/mirrored subtree. **JSON must encode references as UUIDs (or null), never raw RID integers.**

Imported-asset linkage already stores cross-resource identity as **strings** on entry types:

- `ResourceSubIdEntry.TargetUUID` — string field (stable sub-resource UUID)
- `ResourceExtractedEntry.SourceUUID` / `TargetUUID` — string fields

### 1.4 Existing serialization / reflection / JSON

| Facility | Status |
| --- | --- |
| C++ Reflection | **Absent** on v2. Types registered via manual `sk_resource_field_t` / `sk_resource_type_desc_t`. |
| `sk_archive_writer_t` / `sk_archive_reader_t` | **Present** (`core/serialization.h`). Multi-instance fp tables (like `sk_allocator_t`). |
| Binary backend | `sk_binary_archive_*` — main-compatible custom format, host endian. |
| JSON backend | `sk_json_archive_*` — **yyjson** linked **PRIVATE** into `sk-core` (`core/CMakeLists.txt`). Public headers never include `yyjson.h`. |
| Resource Serialize/Deserialize | **Not implemented.** `repository.h` states reflection/events/serialization are intentional gaps. `resource_assets.c` defers Serialize/Deserialize load paths. |
| Handler hooks | `load` / `save` / `export_object(…, sk_archive_writer_t*)` exist; builtins mostly NULL (exceptions: C# skeleton save, shader content load). |

**JSON library to use:** existing **yyjson** only, through `sk_json_archive_*` (or the same private yyjson usage pattern). Do not add a second JSON dependency.

### 1.5 Error model already in the codebase

C, no exceptions. Conventions to match:

| API class | Success | Failure |
| --- | --- | --- |
| Most core APIs | `0` | non-zero `i32` (`-1` OOM/parse, `-2` conflict, …) |
| Create-style | valid handle / RID | `NULL` or `SK_RID_ZERO` |
| Archive `read_*` | value | **Absent field → zero / empty view** (not an error) |
| Archive init | `0` | `-1` (alloc or parse / non-object root for JSON) |
| Field Set accessors | `0` | non-zero (type mismatch / OOM) |

**Deserialize must return `i32` (0 / non-zero), never abort or throw.** Soft defaults for optional/absent keys; hard fail only for malformed JSON, wrong root shape, unknown required `format_version` major, or unresolvable type name when creating a resource.

### 1.6 Test framework (for follow-on integration tests)

| Item | Detail |
| --- | --- |
| Framework | **Unity** (`thirdparty/unity/`), linked via `sk-test` |
| Registration | `SK_TEST(name) { … }` in `core/test.h` — constructor auto-register |
| Placement | **In-source**: same `.c` as production, under `#ifdef SK_TESTS` … `#endif` |
| Naming | `snake_case`, often `<module>_<behavior>` (e.g. `resource_assets_types_register_all`, `serialization_json_full_roundtrip`) |
| Build | `BUILD_TESTING` → `sk-test`, `sk-core-tests` (core sources + `SK_TESTS`), host `tests/main.c` → `sk-tests` executable with whole-archive of test static libs |
| Asserts | Unity `TEST_ASSERT_*` |
| Integration tree | `tests/integration/` — device/Vulkan-style hosts; asset JSON roundtrips should prefer **in-source** `SK_TEST` next to the new serialize module unless a full package filesystem scenario needs the integration harness |
| CMake | `core/CMakeLists.txt` (sk-core-tests), `tests/CMakeLists.txt` (sk-tests + `add_test`) |

Existing asset-related tests (patterns to extend):

- `resource_assets_types_*` in `resource_assets_types.c`
- `resource_asset_handler_*` / `resource_assets_engine_*` in `resource_assets.c`
- `resource_asset_builtins_*` in `resource_asset_builtins.c`
- `serialization_json_*` / `serialization_binary_*` in `serialization.c`

---

## 2. Asset types and fields (repository schema)

Field **names** below are the exact descriptor strings registered today (PascalCase, main-branch parity). JSON keys **must** match these names.

Storage mapping (from `resource_assets_types.h`):

| Field type | C storage |
| --- | --- |
| String | `sk_field_string_t` |
| Bool | `i32` (0/1) |
| UInt / Int | `u64` / `i64` |
| Reference / SubObject | `sk_rid_t` |
| SubObjectList | `sk_field_subobject_list_t` |
| TypeID | `sk_type_id_t` (16 bytes) |
| Buffer | `sk_resource_asset_buffer_t { u64 id }` (opaque; full buffer layer later) |
| Blob | `sk_field_blob_t` |
| None | reserved placeholder |

### 2.1 ResourceAssetPackage (`ResourceAssetPackage` / `SK_RESOURCE_ASSET_PACKAGE_TYPE_ID`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `Name` | String |
| 1 | `AbsolutePath` | String |
| 2 | `Files` | SubObjectList → ResourceAssetFile |
| 3 | `Root` | SubObject → ResourceAssetDirectory |

### 2.2 ResourceAssetFile (`ResourceAssetFile`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `AssetRef` | Reference → ResourceAsset |
| 1 | `AbsolutePath` | String |
| 2 | `RelativePath` | String |
| 3 | `PersistedVersion` | UInt |
| 4 | `TotalSizeInDisk` | UInt |
| 5 | `LastModifiedTime` | UInt |

### 2.3 ResourceAsset (`ResourceAsset`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `Name` | String |
| 1 | `Type` | None (reserved u64; parity with main — omit or write 0) |
| 2 | `Extension` | String (e.g. `.mesh`) |
| 3 | `Object` | SubObject → payload resource type |
| 4 | `Parent` | Reference → parent ResourceAsset |
| 5 | `PathId` | String (package-relative id) |
| 6 | `Directory` | Bool |
| 7 | `AssetFile` | Reference → ResourceAssetFile |
| 8 | `SourcePath` | String |
| 9 | `ReadOnly` | Bool |
| 10 | `ImportedAsset` | SubObject → ResourceImportedAsset |

### 2.4 ResourceAssetDirectory (`ResourceAssetDirectory`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `DirectoryAsset` | SubObject → ResourceAsset (directory node) |
| 1 | `Directories` | SubObjectList → ResourceAssetDirectory |
| 2 | `Assets` | SubObjectList → ResourceAsset |

### 2.5 ResourceImportedAsset (`ResourceImportedAsset`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `OriginalFileName` | String |
| 1 | `Extension` | String |
| 2 | `ContentHash` | String |
| 3 | `ImporterId` | TypeID |
| 4 | `CookerVersion` | UInt |
| 5 | `ImportSettings` | SubObject |
| 6 | `OriginalData` | Buffer (opaque handle today) |
| 7 | `OriginalSize` | UInt |
| 8 | `SubResources` | SubObjectList → ResourceSubIdEntry |
| 9 | `Dependencies` | SubObjectList → ResourceDependencyEntry |
| 10 | `ExtractedResources` | SubObjectList → ResourceExtractedEntry |

### 2.6 ResourceSubIdEntry (`ResourceSubIdEntry`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `SubId` | String |
| 1 | `TargetUUID` | String (canonical UUID encoding; see §4) |
| 2 | `TypeName` | String (registered type name) |

### 2.7 ResourceDependencyEntry (`ResourceDependencyEntry`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `RelPath` | String |
| 1 | `Data` | Buffer |
| 2 | `Size` | UInt |

### 2.8 ResourceExtractedEntry (`ResourceExtractedEntry`)

| Index | JSON key | Kind |
| ---: | --- | --- |
| 0 | `SourceUUID` | String |
| 1 | `TargetUUID` | String |
| 2 | `Kind` | UInt |

### 2.9 Built-in payload types (`sk_resource_asset_builtins_*`)

Minimal schemas registered for handlers (expand later as cooks grow):

| Type name | Fields |
| --- | --- |
| `AnimationClipResource`, `AnimationControllerResource`, `CSharpScriptResource`, `DCCAsset`, `EntityResource`, `FontResource`, `MaterialGraphResource`, `MeshResource`, `SceneResource`, `TextureResource`, `TextureImportSettings`, `FBXImportSettings`, `GLTFImportSettings`, `ObjImportSettings` | `Name` (String) only |
| `UIDocumentResource`, `UIStyleResource`, `ShaderResource` | `Name`, `Content` (String) |
| `AudioResource` | `Name`, `Bytes` (Blob → JSON byte array via archive) |

Handler extensions (disk file suffix) are listed in `docs/repository-assets-inventory.md` §1.3; they are not separate repository type fields.

---

## 3. JSON serialization contract

### 3.1 Document shape

Every persisted resource document is a **single JSON object** (archive root = map), suitable for `sk_json_archive_reader_init` (requires object root).

Recommended top-level envelope (resource document, one file or one logical object):

```json
{
  "format": "sk.resource",
  "format_version": 1,
  "type": "ResourceAsset",
  "uuid": "0000000000000001-00000000000000aa",
  "fields": {
    "Name": "Hero",
    "Extension": ".entity",
    "Object": "0000000000000002-00000000000000aa",
    "Parent": null,
    "PathId": "Assets/Hero.entity",
    "Directory": false,
    "AssetFile": "…",
    "SourcePath": "",
    "ReadOnly": false,
    "ImportedAsset": null
  }
}
```

| Key | Required | Description |
| --- | --- | --- |
| `format` | yes | Constant string `"sk.resource"`. Reject other values. |
| `format_version` | yes | Unsigned integer; see §3.3. |
| `type` | yes | Registered type **name** (`find_type_by_name`), e.g. `"ResourceAsset"`. |
| `uuid` | yes for durable assets | Canonical UUID string (§4). May be omitted only for pure ephemeral dumps. |
| `fields` | yes | Object whose keys are field descriptor names (§2). |

**Sub-objects** may either:

1. Be **inlined** as nested resource objects (same envelope without repeating `format` if nested under a well-known key), or  
2. Be **referenced by UUID** with a sibling/top-level table of resource objects.

**Preferred for package save (v1):** UUID references in fields + a flat `resources` array on the package document for all owned sub-objects (stable order: depth-first package tree). This matches RID remapping on load and avoids deep nesting limits.

Package document sketch:

```json
{
  "format": "sk.resource_package",
  "format_version": 1,
  "root_uuid": "…",
  "resources": [ { "format": "sk.resource", "format_version": 1, "type": "…", "uuid": "…", "fields": { } }, … ]
}
```

### 3.2 Field naming convention

- **JSON keys = exact C field descriptor `name` strings** (PascalCase): `AbsolutePath`, `PathId`, `CookerVersion`, …  
- Do **not** snake_case or camelCase-rename for JSON.  
- Envelope keys (`format`, `format_version`, `type`, `uuid`, `fields`, `resources`) are the only lower_snake exceptions and are not resource fields.  
- Type **names** in `"type"` match `sk_resource_type_desc_t.name` (`"ResourceAsset"`, `"MeshResource"`, …).

### 3.3 Format version and upgrade policy

| Constant | Value (v1) |
| --- | --- |
| `format` resource | `"sk.resource"` |
| `format` package | `"sk.resource_package"` |
| `format_version` | `1` |

**Upgrade policy:**

1. **Same major (here: integer version):** readers **must** accept older v1 documents when new optional fields appear; missing keys use type defaults / zero / empty (§3.5).  
2. **Unknown higher `format_version`:** fail deserialize with non-zero `i32` (do not partial-load).  
3. **Breaking changes:** bump `format_version` and add an explicit upgrade path in the deserializer (vN → current) before field apply. No silent reinterpretation of field meaning.  
4. Field **index** stability is for in-memory accessors; JSON uses **names**, so reordering fields in C is safe if names stay fixed. Renaming a field is a breaking change (version bump + migration).  
5. `CookerVersion` on `ResourceImportedAsset` is **importer cook cache** invalidation, orthogonal to `format_version`.

### 3.4 Value encoding by field kind

Use `sk_archive_writer_t` / `sk_archive_reader_t` so binary and JSON stay aligned where possible.

| Kind | JSON encoding |
| --- | --- |
| Bool | JSON boolean / archive bool (0/1) |
| Int | JSON number (i64) |
| UInt | JSON number (u64; values beyond 2^53 lose integer precision in pure JSON — acceptable for v1 metadata; document if counters can exceed that) |
| Float | JSON number (f64) |
| String | JSON string (empty string allowed) |
| Blob | JSON **array of byte values 0..255** (existing `sk_json_archive` blob convention) |
| Buffer | Object `{"id": <u64>}` for the opaque handle **or** null when unset; full byte payload out of band until buffer layer lands |
| TypeID | Object `{"lo": <u64>, "hi": <u64>}` **or** string type name when the id is a registered resource/importer type and name is preferred for readability. **v1 recommendation:** emit both `{"name":"…","lo":…,"hi":…}` when name is known; on load prefer `name` via `find_type_by_name`, else lo/hi |
| Reference / SubObject | UUID string, or `null` / omit when `SK_RID_ZERO` |
| ReferenceArray | JSON array of UUID strings (skip zeros) |
| SubObjectList | JSON array of UUID strings for `items` only. **`prototype_removed` is editor/runtime override state — omit from durable asset JSON in v1** unless a later version needs it (then version bump) |
| None / reserved | omit from `fields` |

Vectors/quats/mat/color (generic repository kinds, not used by asset types today): arrays of f64 in field order if needed later.

### 3.5 Optional / absent fields

Align with archive readers and prototype inheritance:

| Situation | Behavior |
| --- | --- |
| Key missing in `fields` | Treat as unset: leave instance default / zero; do **not** fail. |
| Key present with `null` for Reference/SubObject | Explicit empty (`SK_RID_ZERO`). |
| Key present with empty string | Empty string field. |
| Unknown key in `fields` | **Ignore** (forward compatible) at v1; optional debug log. |
| `has_value_on_this_object` | Serialize only fields that are set on this object when writing prototype instances; on load, Set accessors mark has-value. Full prototype chain files can omit inherited-only fields. |

### 3.6 Handles and references (load algorithm)

1. Parse all resource objects; collect `uuid` → pending field records.  
2. `create_resource(repo, type, uuid, scope)` for each (idempotent UUID).  
3. Second pass: resolve UUID strings to RIDs via `find_by_uuid`; `set_reference` / `set_subobject` / list setters.  
4. Fail with non-zero if a **required** reference target UUID is missing from the document **and** not already live in the repository (policy: package loads are self-contained; single-asset loads may allow dangling refs as `SK_RID_ZERO` + non-zero warning code only if we introduce one — **v1 package: hard fail on missing UUID**).  
5. Never write RID integers into JSON.

**UUID string form (canonical):** lowercase hex  
`%016llx-%016llx` of `(lo, hi)` — 16 hex digits, hyphen, 16 hex digits (matches two `u64` halves). Empty / missing uuid only for non-durable tests.

### 3.7 Error model for Serialize / Deserialize

Public API shape (illustrative; implement next to repository or resource_assets):

```c
/* 0 success; non-zero failure. No exceptions. */
i32 sk_resource_serialize_json(sk_repository_t* repo, sk_rid_t rid, sk_archive_writer_t* writer);
i32 sk_resource_deserialize_json(sk_repository_t* repo, sk_archive_reader_t* reader, sk_rid_t* out_rid);
```

| Condition | Result |
| --- | --- |
| Success | `0`; `out_rid` set |
| OOM | `-1` (match repository/archive) |
| JSON parse / root not object | `-1` from reader init or deserialize |
| Bad / missing `format` | non-zero |
| Unsupported `format_version` | non-zero |
| Unknown `type` name | non-zero |
| Missing UUID target (package) | non-zero |
| Field type mismatch on Set | non-zero from set_*; bubble up |

Writers: on OOM, archive backends set internal oom and produce empty emit; serialize should detect and return `-1`.

**Do not** use Unity asserts in production deserialize paths; asserts only for programmer contract violations in debug.

---

## 4. Implementation guidance (later tasks)

Suggested layering (no code in this task):

1. **Generic field walk** over `sk_resource_type_t` field descriptors → write/read via archive + repository Get/Set.  
2. **UUID table** for Reference / SubObject / lists.  
3. **Wire handler `load`/`save`/`export_object`** to the generic path for types that today pass NULL.  
4. **Package scan** may start calling load once serialization lands (`resource_assets.c` comment).  
5. **Tests:** `SK_TEST` roundtrip per asset type + one package graph test with parent/child UUID refs; place beside the new module under `#ifdef SK_TESTS`.

---

## 5. Files later tasks will touch

Primary (serialize implementation):

| Path | Likely change |
| --- | --- |
| `core/repository.h` / `core/repository.c` | Optional: expose type field iteration helpers if not already public; **or** keep generic walk in a new file that uses existing APIs only |
| `core/resource_serialize.h` / `core/resource_serialize.c` (**new**, name flexible) | `sk_resource_serialize_json` / `deserialize` / package helpers |
| `core/serialization.h` / `core/serialization.c` | Only if archive gaps appear (TypeID helpers, clearer error codes) |
| `core/resource_assets_types.h` / `.c` | Unlikely schema change; tests for roundtrip of asset types |
| `core/resource_assets.h` / `.c` | Call deserialize from scan/load; save paths |
| `core/resource_asset_builtins.c` | Fill `load`/`save`/`export_object` using generic serialize |
| `core/CMakeLists.txt` | Only if new `.c` is not covered by `GLOB_RECURSE` (currently glob picks up new core sources automatically) |

Secondary / consumers:

| Path | Likely change |
| --- | --- |
| `editor/project.c` | Save/open project assets once APIs exist |
| `docs/repository-assets-inventory.md` | Cross-link when serialize is no longer “deferred” |

Tests (registration automatic via `SK_TEST` + whole-archive):

| Path | Likely change |
| --- | --- |
| `core/resource_serialize.c` (or adjacent) | `SK_TEST(resource_serialize_*)` roundtrips |
| `core/resource_assets.c` | Engine-level load/save integration `SK_TEST`s |
| `core/resource_asset_builtins.c` | Handler load/save integration samples |
| `core/serialization.c` | Only if archive API grows |
| `tests/main.c` / `tests/CMakeLists.txt` | Usually **no** change (in-source registration) |
| `tests/integration/` | Optional filesystem package roundtrip later |

Do **not** touch for serialize work unless needed:

- `docs/repository-assets-thumbnail-drop.md` / thumbnail code (dropped)
- Plugin RHI / render_graph
- yyjson public exposure

---

## 6. Audit checklist (APX-186 deliverable map)

| Required investigation item | Section |
| --- | --- |
| (1) Every asset type and fields | §2 |
| (2) Handles/IDs and session stability | §1.2 |
| (3) Inter-asset references | §1.3, §3.4–3.6 |
| (4) Serialization / reflection / JSON lib | §1.4 |
| (5) Test framework / layout / naming / build | §1.5–1.6 |
| Contract: schema, naming, version, handles, optional fields, errors | §3 |
| Later file paths | §5 |

End of contract. Documentation only; no runtime behavior changed.
