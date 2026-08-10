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
| `VEC2` (h:121) | ✗ | ✗ | No accessor of any kind. |
| `VEC3` (h:122) | ✗ | ✗ | No accessor of any kind. |
| `VEC4` (h:123) | ✗ | ✗ | No accessor of any kind. |
| `QUAT` (h:124) | ✗ | ✗ | No accessor of any kind. |
| `MAT4` (h:125) | ✗ | ✗ | No accessor of any kind. |
| `COLOR` (h:126) | ✗ | ✗ | No accessor of any kind; no core storage type for a color either. |
| `ENUM` (h:127) | ✗ | ✗ | Never used by any registered descriptor; `sub_type` is documented as the enum backing type (h:197-198). |
| `BLOB` (h:128) | ✗ | ✗ | In-blob storage `sk_field_blob_t { u8* data; u32 size; }` (h:149-152) is fully wired (deep copy c:299-310, destroy c:209-216) but no accessor exposes it. |
| `REFERENCE` (h:129) | ✓ `sk_rid_t (*get_reference)(sk_resource_object_t view, u32 index)` h:645 / c:2436 | ✓ `i32 (*set_reference)(sk_resource_object_t view, u32 index, sk_rid_t rid)` h:608 / c:2112 | |
| `REFERENCE_ARRAY` (h:130) | ✓ `const sk_rid_t* (*get_reference_array)(sk_resource_object_t view, u32 index, u32* out_count)` h:648 / c:2447 (borrowed; count via out param) | ✓ `i32 (*set_reference_array)(sk_resource_object_t view, u32 index, const sk_rid_t* items, u32 count)` h:610 / c:2128; plus ✓ `add_to_reference_array` h:612 / c:2157, ✓ `remove_from_reference_array` h:614 / c:2178 | In-blob: `sk_field_rid_array_t` (h:154-159). |
| `SUB_OBJECT` (h:131) | ✓ `sk_rid_t (*get_subobject)(sk_resource_object_t view, u32 index)` h:649 / c:2463 | ✓ `i32 (*set_subobject)(sk_resource_object_t view, u32 index, sk_rid_t rid)` h:615 / c:2193 | |
| `SUB_OBJECT_LIST` (h:132) | ✓ `const sk_rid_t* (*get_subobject_list)(sk_resource_object_t view, u32 index, u32* out_count)` h:652 / c:2474 (borrowed) | ✓ `set_subobject_list` h:617 / c:2209, `add_to_subobject_list` h:619 / c:2244, `remove_from_subobject_list` h:623 / c:2271, `remove_from_subobject_list_by_prototype` h:628 / c:2310, `has_on_subobject_list` h:630 / c:2342, `subobject_list_count` h:632 / c:2352 | In-blob: `sk_field_subobject_list_t` (h:161-176). |
| `BUFFER` (h:133) | ✗ | ✗ | See §2. |
| `TYPE_ID` (h:134) | ✗ | ✗ | `sk_type_id_t { u64 lo, hi; }` (`common.h:78-81`, 16 bytes); treated as POD. |
| `MAX` (h:135) | — | — | Sentinel; not a field type. |

Generic helpers that apply to every field: `has_value_on_this_object`
(h:634 / c:2362), `is_value_overridden` (h:637 / c:2374). Get accessors return
zero/empty when unset and scalar getters fall back through the prototype chain;
set accessors return 0 on success / non-zero on failure (read view, unknown
index, type mismatch, OOM) — see the block comment at `repository.h:594-600`.

**Summary — field types with NO accessors at all (10):**
`VEC2`, `VEC3`, `VEC4`, `QUAT`, `MAT4`, `COLOR`, `ENUM`, `BLOB`, `BUFFER`,
`TYPE_ID`.

---

## 2. `SK_RESOURCE_FIELD_TYPE_BUFFER` — current representation & serialization

### 2.1 In-blob representation

Buffer fields are **opaque handles, not data**. The in-blob storage is

```c
typedef struct sk_resource_asset_buffer_t {
	u64 id;
} sk_resource_asset_buffer_t;
```

(`core/resource_assets_types.h:133-135`). The header comment (lines 27-29, 132-136)
states the intent explicitly: *"This revision stores an opaque handle only; the
buffer / serialization layer that interprets it lands later. The repository
treats Buffer fields as POD (copied by bytes)."*

Concretely:

- **No pointer + size.** There is no byte range anywhere in the instance blob —
  only the 8-byte `id` handle.
- **No owned vs borrowed distinction.** Nothing owns payload bytes; the handle
  is a plain `u64` with no lifetime semantics.
- **No allocator involved.** The repository never allocates/frees anything for
  a Buffer field; the handle is copied by `memcpy` like any POD scalar.

The repository's dispatch sites all classify `BUFFER` as POD:

| Site | Behavior |
| --- | --- |
| `repository.c:258` (`sk_repo_field_destroy`) | No heap payload to release. |
| `repository.c:363-365` (`sk_repo_copy_blob_fields`) | Copied by `field->size` bytes. |
| `repository.c:877` (`destroy_resource` child walk) | Nothing to walk. |
| `repository.c:1314` (sub-object detach walk) | Nothing to walk. |
| `repository.c:1812` (clone / prototype copy; block 1806-1819) | `memcpy` for clones; inherited (not copied) for prototype instances. |

Buffer fields are registered today in two descriptors, both with
`sizeof(sk_resource_asset_buffer_t)` layout: `ResourceImportedAsset::OriginalData`
(`resource_assets_types.c:146-151`) and `DependencyEntry::Data`
(`resource_assets_types.c:206-210`).

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

So today a Buffer field's `u64 id` would serialize as plain POD bytes if a
field serializer existed; the referenced payload has no on-disk representation
at all.

---

## 3. Ambiguous accessor semantics — decisions needed

1. **`BUFFER`** — the handle has no interpretation layer yet, so `get_buffer` /
   `set_buffer` semantics are undefined: does the setter take the handle by
   value, or (later) a `sk_blob_view_t` / owned payload? Decision needed:
   *add trivial handle-based accessors now, or defer the pair until the buffer
   layer (pointer+size, ownership, allocator) lands.* Current design intent
   (`resource_assets_types.h:132-136`) suggests deferring the payload but the
   handle accessors could still ship.

2. **`BLOB`** — storage (`sk_field_blob_t`, owned by the repository, allocated
   with the repository allocator) and copy/destroy wiring already exist; only
   the accessors are missing. Ambiguity: getter return type (borrowed
   `sk_blob_view_t` vs `const sk_field_blob_t*`) and setter ownership (deep copy
   like `set_string`, vs borrow). Recommended: mirror `get_string`/`set_string`
   with a borrowed view + deep-copy setter. Also: does the getter fall back
   through the prototype chain (scalars do today)?

3. **`ENUM`** — no v2 storage struct and no registered user. `sub_type` is
   documented as the enum backing type (`repository.h:197-198`), and `NONE` is
   already used as a `u64`-backed reserved field (`resource_assets_types.c:66`).
   Decision needed: value type (`u64` index vs string name) and whether
   `sub_type` validation is enforced.

4. **`VEC2/3/4`, `QUAT`, `MAT4`, `COLOR`** — math3d POD types are `f32`-based
   (`core/math3d.h:39-63`; `sk_mat44_t` at 61-63 is `f32 m[16]`). Ambiguity: return by
   value vs out-pointer, and whether these participate in prototype-chain
   fallback like other scalar getters. `COLOR` additionally has **no core
   storage type at all** (only `plugins/render_device/render_device.h` defines
   an `sk_color` today) — representation (4×f32 RGBA vs packed u32) must be
   chosen first.

5. **`TYPE_ID`** — POD `{u64 lo, hi}` (16 bytes) already copied/inherited like a
   scalar (c:1812-1815); accessors are trivial (by-value). Only open question:
   same prototype-chain fallback semantics as other scalars (consistent with
   today's behavior, so likely yes — confirm when implementing).
