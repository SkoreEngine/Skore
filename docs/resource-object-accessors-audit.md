# ResourceObject field-type accessor audit (APX-180)

**Task:** APX-180 — inventory only; no behavior change.  
**Scope:** `sk_resource_object_t` / `sk_repository_api_t` field accessors vs the
`sk_resource_field_type_t` enum in the v2 core.

---

## 0. Where things live

| Item | Location |
| --- | --- |
| `sk_resource_field_type_t` enum | `core/repository.h:113-136` (values `NONE`…`MAX` at lines 115-135) |
| `sk_resource_object_t` view struct | `core/repository.h:266-273` (`SK_RESOURCE_OBJECT_ZERO` at 276) |
| In-blob storage structs (`sk_field_string_t`, `sk_field_blob_t`, `sk_field_rid_array_t`, `sk_field_subobject_list_t`) | `core/repository.h:146-176` |
| Accessor declarations (function pointers in `sk_repository_api_t`) | `core/repository.h:603-652` |
| Accessor implementations | `core/repository.c:2021-2474` |
| API table fill | `core/repository.c:2760-2786` |
| Registered field descriptors (resource asset types) | `core/resource_assets_types.c` |
| Buffer handle storage type | `core/resource_assets_types.h:133-135` |

---

## 1. Per-field-type inventory

Legend: ✓ = accessor exists, ✗ = missing. Signatures are the `sk_repository_api_t`
declarations (`core/repository.h`, `h:`) with the static implementation in
`core/repository.c` (`c:`).

| Field type (enum line) | Getter | Setter | Notes |
| --- | --- | --- | --- |
| `NONE` (h:115) | — (intentional) | — (intentional) | Not a real field type; used as a reserved `u64` placeholder for `ResourceAsset::Type` (`resource_assets_types.c:66`). No accessor desired. |
| `BOOL` (h:116) | ✓ `i32 (*get_bool)(sk_resource_object_t view, u32 index)` h:639 / c:2382 | ✓ `i32 (*set_bool)(sk_resource_object_t view, u32 index, i32 value)` h:603 / c:2021 | Stored as `i32` (4 bytes). |
| `INT` (h:117) | ✓ `i64 (*get_int)(sk_resource_object_t view, u32 index)` h:640 / c:2393 | ✓ `i32 (*set_int)(sk_resource_object_t view, u32 index, i64 value)` h:604 / c:2037 | |
| `UINT` (h:118) | ✓ `u64 (*get_uint)(sk_resource_object_t view, u32 index)` h:641 / c:2404 | ✓ `i32 (*set_uint)(sk_resource_object_t view, u32 index, u64 value)` h:605 / c:2053 | |
| `FLOAT` (h:119) | ✓ `f64 (*get_float)(sk_resource_object_t view, u32 index)` h:642 / c:2415 | ✓ `i32 (*set_float)(sk_resource_object_t view, u32 index, f64 value)` h:606 / c:2069 | |
| `STRING` (h:120) | ✓ `const_chr_t (*get_string)(sk_resource_object_t view, u32 index)` h:644 / c:2426 (borrowed, NULL when unset) | ✓ `i32 (*set_string)(sk_resource_object_t view, u32 index, const_chr_t value)` h:607 / c:2085 (deep copy) | In-blob: `sk_field_string_t` (h:146-149). |
| `VEC2` (h:121) | ✓ `sk_vec2_t (*get_vec2)(sk_resource_object_t view, u32 index)` (by value; zero when unset; prototype-chain fallback) | ✓ `i32 (*set_vec2)(sk_resource_object_t view, u32 index, sk_vec2_t value)` | APX-181. |
| `VEC3` (h:122) | ✓ `sk_vec3_t (*get_vec3)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_vec3)(sk_resource_object_t view, u32 index, sk_vec3_t value)` | APX-181. |
| `VEC4` (h:123) | ✓ `sk_vec4_t (*get_vec4)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_vec4)(sk_resource_object_t view, u32 index, sk_vec4_t value)` | APX-181. |
| `QUAT` (h:124) | ✓ `sk_quat_t (*get_quat)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_quat)(sk_resource_object_t view, u32 index, sk_quat_t value)` | APX-181. |
| `MAT4` (h:125) | ✓ `sk_mat44_t (*get_mat4)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_mat4)(sk_resource_object_t view, u32 index, sk_mat44_t value)` | APX-181. |
| `COLOR` (h:126) | ✓ `sk_color_t (*get_color)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_color)(sk_resource_object_t view, u32 index, sk_color_t value)` | APX-181; `sk_color_t` (4×f32 RGBA) added to math3d.h. |
| `ENUM` (h:127) | ✓ `u64 (*get_enum)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_enum)(sk_resource_object_t view, u32 index, u64 value)` | APX-181; stored as a `u64` scalar. |
| `BLOB` (h:128) | ✓ `const u8* (*get_blob)(sk_resource_object_t view, u32 index, u32* out_size)` (borrowed; NULL / size 0 when unset) | ✓ `i32 (*set_blob)(sk_resource_object_t view, u32 index, const void* data, u32 size)` (deep copy; NULL + 0 clears) | APX-181; in-blob: `sk_field_blob_t` (h:149-152). |
| `REFERENCE` (h:129) | ✓ `sk_rid_t (*get_reference)(sk_resource_object_t view, u32 index)` h:645 / c:2436 | ✓ `i32 (*set_reference)(sk_resource_object_t view, u32 index, sk_rid_t rid)` h:608 / c:2112 | |
| `REFERENCE_ARRAY` (h:130) | ✓ `const sk_rid_t* (*get_reference_array)(sk_resource_object_t view, u32 index, u32* out_count)` h:648 / c:2447 (borrowed; count via out param) | ✓ `i32 (*set_reference_array)(sk_resource_object_t view, u32 index, const sk_rid_t* items, u32 count)` h:610 / c:2128; plus ✓ `add_to_reference_array` h:612 / c:2157, ✓ `remove_from_reference_array` h:614 / c:2178 | In-blob: `sk_field_rid_array_t` (h:154-159). |
| `SUB_OBJECT` (h:131) | ✓ `sk_rid_t (*get_subobject)(sk_resource_object_t view, u32 index)` h:649 / c:2463 | ✓ `i32 (*set_subobject)(sk_resource_object_t view, u32 index, sk_rid_t rid)` h:615 / c:2193 | |
| `SUB_OBJECT_LIST` (h:132) | ✓ `const sk_rid_t* (*get_subobject_list)(sk_resource_object_t view, u32 index, u32* out_count)` h:652 / c:2474 (borrowed) | ✓ `set_subobject_list` h:617 / c:2209, `add_to_subobject_list` h:619 / c:2244, `remove_from_subobject_list` h:623 / c:2271, `remove_from_subobject_list_by_prototype` h:628 / c:2310, `has_on_subobject_list` h:630 / c:2342, `subobject_list_count` h:632 / c:2352 | In-blob: `sk_field_subobject_list_t` (h:161-176). |
| `BUFFER` (h:133) | ✓ `const u8* (*get_buffer)(sk_resource_object_t view, u32 index, u32* out_size)` (borrowed; NULL / size 0 when unset or empty; falls back through the prototype chain; `has_value_on_this_object` distinguishes empty from unset) | ✓ `i32 (*set_buffer)(sk_resource_object_t view, u32 index, const void* data, u32 size)` (deep copy with the repository allocator; NULL + size 0 sets an empty buffer) | In-blob: `sk_field_buffer_t` (h:158-175); see §2. |
| `TYPE_ID` (h:134) | ✓ `sk_type_id_t (*get_type_id)(sk_resource_object_t view, u32 index)` | ✓ `i32 (*set_type_id)(sk_resource_object_t view, u32 index, sk_type_id_t value)` | APX-181; `sk_type_id_t { u64 lo, hi; }` (`common.h:78-81`, 16 bytes); treated as POD. |
| `MAX` (h:135) | — | — | Sentinel; not a field type. |

Generic helpers that apply to every field: `has_value_on_this_object`
(h:634 / c:2362), `is_value_overridden` (h:637 / c:2374). Get accessors return
zero/empty when unset and scalar getters fall back through the prototype chain;
set accessors return 0 on success / non-zero on failure (read view, unknown
index, type mismatch, OOM) — see the block comment at `repository.h:594-600`.

**Summary — every `SK_RESOURCE_FIELD_TYPE_*` value now has a get/set accessor
pair** (APX-181 added VEC2/3/4, QUAT, MAT4, COLOR, ENUM, BLOB, TYPE_ID;
APX-182 added BUFFER). `NONE` and `MAX` intentionally have none.

---

## 2. `SK_RESOURCE_FIELD_TYPE_BUFFER` — representation, ownership, serialization

### 2.1 In-blob representation

Buffer fields carry an **owned byte payload**, not an opaque handle. The
in-blob storage is

```c
typedef struct sk_field_buffer_t {
	u8* data;
	u32 size;
} sk_field_buffer_t; /* sk_resource_asset_buffer_t is an alias */
```

(`core/repository.h:158-175`; `sk_resource_asset_buffer_t` in
`core/resource_assets_types.h:130-140` is a typedef alias so the asset-model
name stays). The repository owns the payload:

- **set_buffer deep-copies** caller bytes with the repository's allocator; the
  caller keeps ownership of its input and may free/mutate it immediately after
  the call (same contract as set_string / set_blob). Overwriting releases the
  previous payload.
- **get_buffer returns a borrowed pointer + size** (like get_reference_array);
  valid until the next write/commit on the resource or its destruction. The
  getter falls back through the prototype chain when unset on this object.
- **Empty vs unset:** set_buffer with NULL + size 0 sets an EMPTY buffer — the
  has-value bit is set, reads return NULL / size 0, and it shadows a prototype
  value. An unset buffer has no has-value bit. `has_value_on_this_object`
  distinguishes the two.
- **Instance copy / destroy / GC / undo snapshots / clone** all treat the
  payload like Blob fields: deep copy on clone and COW write, release on
  destroy; prototype instances inherit lazily (no copy).

The repository's dispatch sites:

| Site | Behavior |
| --- | --- |
| `repository.c` `sk_repo_field_destroy` | Releases the payload. |
| `repository.c` `sk_repo_copy_blob_fields` | Deep copy (payload + size). |
| `repository.c` `sk_repo_build_instance` (clone / prototype) | Deep copy for clones; inherited (not copied) for prototype instances. |
| `repository.c` child-walk sites | Nothing to walk (no RIDs inside). |

Buffer fields are registered in two descriptors, both with
`sizeof(sk_resource_asset_buffer_t)` (= `sizeof(sk_field_buffer_t)`) layout:
`ResourceImportedAsset::OriginalData` (`resource_assets_types.c:146-151`) and
`DependencyEntry::Data` (`resource_assets_types.c:206-210`).

### 2.2 Serialization / deserialization today

**Resource-object field serialization does not exist in v2 yet.** The module
header says so explicitly: *"YAML / binary serialization is intentionally absent
(owned by another goal)"* (`core/resource_assets_types.h:31-33`). Nothing in
`core/repository.c`, `core/resource_assets.c`, or `editor/` reads or writes
resource object fields to disk; handlers only receive an `sk_archive_writer_t*`
via the `export_object` hook (`resource_assets.c:209-214`), and no built-in
handler serializes resource fields today.

The generic archive layer (`core/serialization.h/.c`, added in goal
"serialization abstraction") does define blob plumbing that a future field
serializer would use:

- `write_blob(instance, name, data, size)` — `serialization.h:70`;
  `get_blob(instance)` — `serialization.h:135`; both typed with
  `sk_blob_view_t { const u8* data; u64 size; }` (`serialization.h:42-45`),
  a **borrowed, non-owning** view (`data` may be NULL when size is 0).
- Binary backend writes `[u32 name size][name][u64 value size][value]`
  (`serialization.h:12-15`); JSON backend encodes blobs as arrays of byte
  values 0..255 (`serialization.h:21-22`).

APX-182 made Buffer fields serialization-ready: the stored representation is a
size-tagged byte range (same shape as `sk_blob_view_t`), so a field serializer
can map `get_buffer` → `write_blob` / `set_buffer` ← `get_blob` directly. The
earlier draft contract that encoded the old opaque `{u64 id}` handle is
obsolete and must be revisited by the serializer task.

---

## 3. Ambiguous accessor semantics — decisions needed

1. **`BUFFER`** — **resolved by APX-182:** the setter takes caller bytes by
   value and **deep-copies** them with the repository allocator (the object
   never takes ownership of caller memory), the getter returns a borrowed
   pointer + size, empty (size 0, has-value bit set) is distinct from unset,
   overwrites release the previous payload, and clone/destroy follow the Blob
   contract. The old opaque-`{u64 id}` handle design and the handle-based
   accessor draft are obsolete; a future buffer-resource layer can layer
   handles on top of the payload.

2. **`BLOB`** — **resolved by APX-181:** getter returns a borrowed pointer +
   size (prototype-chain fallback), setter deep-copies like `set_string`; an
   empty blob is stored with its has-value bit set, distinct from unset.

3. **`ENUM`** — **resolved by APX-181:** stored as a `u64` scalar (matches the
   repository's POD byte-copy treatment); no `sub_type` validation.

4. **`VEC2/3/4`, `QUAT`, `MAT4`, `COLOR`** — **resolved by APX-181:** returned by
   value (zero when unset) with prototype-chain fallback; `sk_color_t` (4×f32
   RGBA) was added to `math3d.h` as the core storage type.

5. **`TYPE_ID`** — **resolved by APX-181:** by-value `sk_type_id_t` with the
   same prototype-chain fallback as the other scalar getters.

---

## 4. APX-185 full build and regression (v2 / feature branch)

Verification only; no accessor or buffer behavior change.

### Commands

```bash
# Clean Debug (clang-tidy ON, -Werror on first-party)
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DSK_ENABLE_CLANG_TIDY=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --verbose

# Clean Release (same flags as CI matrix)
rm -rf build-release
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DSK_ENABLE_CLANG_TIDY=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure

# Windows LLP64 tidy gate
bash scripts/check-windows-abi.sh

# Optional ASan/UBSan (no project sanitizer preset; manual flags)
rm -rf build-asan
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DSK_ENABLE_CLANG_TIDY=OFF \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan --parallel --target sk-tests sk-integration-tests
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-asan --output-on-failure
```

### Results

| Gate | Result |
| --- | --- |
| Debug clean build | Pass (122 objects). First-party: no compiler warnings. Only pre-existing thirdparty `nfd_zenity.c` format-truncation. |
| Debug `ctest` | Pass — `sk-tests` + `sk-integration-tests` (0 failures). |
| Release clean build + `ctest` | Pass (0 failures). |
| Windows ABI (`check-windows-abi.sh`) | Pass (`checked: 38  failed: 0`). |
| Accessor unit tests | `repository_extended_field_accessors` PASS; `repository_buffer_field_accessors` PASS. |
| Buffer integration tests | All five `resource_object_buffer_*` + three fixture tests PASS. |
| ASan/UBSan (`detect_leaks=0`) | Full suite PASS; no UAF/OOB/UB on buffer or extended field paths. |
| ASan + LeakSanitizer (`detect_leaks=1`) | Unity suite still 0 Failures; process exit reports leaks from plugin `dlopen` / Vulkan paths (e.g. `lib_open` → `load_plugins_from_directory`). **Pre-existing, unrelated to ResourceObject accessors/buffers — not fixed here.** |
| Valgrind | Not installed on the agent host; no project valgrind CMake preset. |

**Conclusion:** Missing `sk_resource_object_t` field-type accessors (APX-181/182) and BUFFER fixtures/tests (APX-183/184) are complete and green under clean Debug/Release regression. No accessor/buffer regressions required a code fix.
