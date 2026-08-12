# Resource-to-ECS mapping contract

**Task:** APX-294 — agreed contract only; **no runtime behavior change**.
**Goal:** create entities from resources (`scene_resource` / `entity_resource`).
**Audience:** APX-295 (descriptor refactor), APX-296/297 (resource types),
APX-298 (component instantiate), APX-299 (`world_spawn_from_asset`), APX-300
(built-in loaders).

Related:

- ECS registration / storage: `plugins/entities/entities.h`, `entities.c`
- Repository / RIDs / SubObjectList: `core/repository.h`
- Existing named shells: `SK_ENTITY_RESOURCE_TYPE_ID`, `SK_SCENE_RESOURCE_TYPE_ID`
  in `core/resource_asset_builtins.h` (Name-only today)

This note is the shared compile-time contract. Later tasks implement it; they
must not invent a second callback signature or a second type-id mapping.

---

## 0. Current state (audit)

### 0.1 ECS registration

`sk_entities_api_t::register_component` is positional:

```c
i32 (*register_component)(sk_type_id_t type_id, u32 size, u32 align, const_chr_t name);
```

Implemented by `register_component_impl` (`entities.c`). A process-global
registry (`ecs_component_registry[SK_ECS_MAX_COMPONENT_TYPES]`, max 256)
stores `sk_component_info_t { type_id, size, align, name }`.

| Return | Meaning (keep after the refactor) |
| --- | --- |
| `0` | Success, or idempotent re-register of the same `type_id` with the same `size`/`align` |
| `-1` | Same `type_id` already registered with a different `size` or `align` |
| `-2` | Registry full (`SK_ECS_MAX_COMPONENT_TYPES`) |
| `-3` | `SK_TYPE_ID_ZERO`, `size == 0`, or `align == 0` |

Name is not compared on re-register and is not updated (first name wins).
`component_info` returns the stored layout; it does not carry hooks.

Archetypes are sorted `sk_type_id_t` signatures plus implicit column 0
(`SK_ECS_ENTITY_COMPONENT_ID`). `world_spawn` looks up each id in the
registry, creates the archetype on demand, and **zero-fills** every user
column. There is no `world_spawn_from_asset` today.

### 0.2 Repository layer

- `sk_rid_t { u64 id }` — dense page index; `SK_RID_ZERO` is invalid; never
  recycled for the life of the repository.
- `sk_repository_t` — typed instance store. Type identity is the same
  `sk_type_id_t` used by ECS (`SK_TYPE_ID("sk.…", lo, hi)`).
- Sub-object ownership is `SK_RESOURCE_FIELD_TYPE_SUB_OBJECT` / `_LIST`
  (`sk_field_subobject_list_t`). Destroy / clone / `create_from_prototype`
  walk those lists. Soft `REFERENCE` does **not** own the target.
- Scalar reads fall back through the prototype chain. Loaders must use
  `read` + field getters so inherited values are visible.

### 0.3 Existing EntityResource / SceneResource

Both are registered as **Name-only** `sk_named_resource_t` shells
(`.entity` / `.scene` handlers). They have no component list and no
roots. APX-296 / APX-297 extend those types in place, keeping `Name` at
field index 0 so existing JSON envelopes stay valid.

---

## 1. Component registration descriptor

Replace the four positional arguments with one caller-owned descriptor.
The table copies what it retains (`type_id`, `size`, `align`, `name`,
hook pointers). The descriptor may be transient (stack).

```c
typedef i32 (*sk_component_on_load_asset_fn)(sk_world_t* world,
											 sk_entity_t entity,
											 sk_repository_t* repository,
											 void_ptr_t instance,
											 sk_rid_t component_resource);

typedef struct sk_component_desc_t {
	sk_type_id_t type_id;						  /* must not be SK_TYPE_ID_ZERO */
	u32 size;									  /* byte size; must be > 0 */
	u32 align;									  /* byte align; must be > 0 */
	const_chr_t name;							  /* optional; may be NULL */
	sk_component_on_load_asset_fn on_load_asset;  /* optional; may be NULL */
	/* Reserved for future lifecycle hooks. Callers zero-init the desc
	 * (sk_component_desc_t desc = {0}). Non-NULL reserved slots are
	 * accepted and stored so a later revision can start honoring them
	 * without a second ABI break; until defined they must be treated
	 * as no-ops by the registry. */
	void (*on_unload_asset)(sk_world_t* world, sk_entity_t entity, sk_repository_t* repository, void_ptr_t instance, sk_rid_t component_resource);
	i32 (*on_save_asset)(sk_world_t* world, sk_entity_t entity, sk_repository_t* repository, void_ptr_t instance, sk_rid_t component_resource);
	void_ptr_t reserved[2];
} sk_component_desc_t;
```

New table entry (APX-295):

```c
i32 (*register_component)(const sk_component_desc_t* desc);
```

### 1.1 Registration rules (preserve today's codes)

Callers **must** zero-init then fill fields:

```c
sk_component_desc_t desc = {0};
desc.type_id = SK_TYPE_ID("sk.transform", /* halves */);
desc.size = (u32)sizeof(sk_transform_t);
desc.align = 16u; /* power-of-two align of the C type */
desc.name = "transform";
desc.on_load_asset = transform_on_load_asset; /* or NULL */
ecs->register_component(&desc);
```

| Return | Meaning |
| --- | --- |
| `0` | Success, or same `type_id` + same `size`/`align` (idempotent) |
| `-1` | Same `type_id` already registered with a different `size` or `align` |
| `-2` | Registry full |
| `-3` | `desc == NULL`, `type_id` zero, `size == 0`, or `align == 0` |

On an idempotent re-register: **do not** overwrite `name` or any hook
(first registration wins), matching today's name behavior. Hook pointer
mismatches are **not** a conflict so existing tests that re-register
layout-only stay valid.

`sk_component_info_t` stays layout-only (`type_id`, `size`, `align`,
`name`). Hooks live on the registry entry and are looked up by
`type_id` at spawn time. Archetype columns do not grow.

`entities.h` will `#include "repository.h"` so the callback can name
`sk_repository_t*` and `sk_rid_t`. That is a one-way core dependency
(entities already statically links `sk-core`).

---

## 2. `on_load_asset` callback

Exact signature (parameter order is part of the contract):

```c
i32 on_load_asset(sk_world_t* world,
				  sk_entity_t entity,
				  sk_repository_t* repository,
				  void_ptr_t instance,
				  sk_rid_t component_resource);
```

| Parameter | Contract |
| --- | --- |
| `world` | World that just spawned / added the component. Not NULL. |
| `entity` | Live handle of that entity. |
| `repository` | Repository that owns `component_resource`. Not NULL. |
| `instance` | Pointer to the **already zero-initialized** component slot (`size` bytes, `align` aligned) inside chunk storage. Valid until the entity moves archetype or is despawned. |
| `component_resource` | RID of the component **sub-object** on the `entity_resource` (not the entity payload, not the `ResourceAsset` wrapper). |

Return `0` on success, non-zero on failure.

### 2.1 Loader rules

- Read the resource with `sk_repository_api()->read(repository, component_resource)`
  and field getters so prototype inheritance applies. Do not poke the
  instance blob through `resource_instance` unless the field is known to
  be set on this object.
- Write only `instance` (and any **non-structural** world state the
  component owns). Do **not** `world_despawn`, `world_add_component`, or
  `world_remove_component` from the callback. Children are spawned by
  `world_spawn_from_asset`, not by loaders.
- `NULL` `on_load_asset` is valid: the component is part of the spawn
  signature and stays zeroed (tag components).
- A non-zero return does **not** roll back the entity. The slot stays
  zeroed / partially written. Spawn still returns the entity. Loaders
  should leave `instance` usable on failure (leave it zeroed).
- The callback is invoked on the thread that called
  `world_spawn_from_asset` (main thread, same as the registry).

Reserved hooks (`on_unload_asset`, `on_save_asset`) are **not** invoked
until a later task defines them. Do not call them from spawn.

---

## 3. How `entity_resource` component subobjects map to type ids

**Rule: the component resource's registered repository type id IS the
ECS component type id.** One `SK_TYPE_ID("sk.…")` is used for both
`sk_repository_api()->register_type` and `register_component`.

There is no extra `TypeID` field on the component sub-object. Identity
is recovered with:

```c
const sk_resource_type_t* type = repo->resource_type(repository, component_rid);
sk_type_id_t type_id = repo->type_id(type);
```

Then `ecs->component_info(type_id, &info)` (and the registry hook) must
succeed for that id to be placed on the spawn signature.

### 3.1 `entity_resource` fields (APX-296)

Keep `Name` at index 0. Append two owned lists. Type id stays
`SK_ENTITY_RESOURCE_TYPE_ID` / name `"EntityResource"`.

| Index | Name | Storage | Contents |
| --- | --- | --- | --- |
| 0 | `Name` | `STRING` | Existing display name |
| 1 | `Components` | `SUB_OBJECT_LIST` | Owned component resources |
| 2 | `Children` | `SUB_OBJECT_LIST` | Owned child `entity_resource` payloads |

```c
enum sk_entity_resource_field_t {
	SK_ENTITY_RESOURCE_FIELD_NAME = 0,
	SK_ENTITY_RESOURCE_FIELD_COMPONENTS = 1,
	SK_ENTITY_RESOURCE_FIELD_CHILDREN = 2,
};
```

`Components` items are **owned** so destroy/clone/prototype-propagate
the component payloads with the entity. They are not soft references.

A list entry is skipped when it is `SK_RID_ZERO`, the resource is not
live, or `resource_type` is NULL. An entry whose type id is **not**
registered with ECS is a spawn failure for that entity
(`SK_ENTITY_INVALID` for that subtree; siblings already spawned stay).
Two entries that resolve to the **same** type id are an authoring error
and fail that entity (`world_spawn` would only keep one column).

User component count after dedup must be `< SK_ECS_MAX_ARCHETYPE_COLUMNS`
(column 0 is the implicit entity component).

### 3.2 Instantiate order (APX-298 / APX-299)

For one `entity_resource` payload RID:

1. `read` the payload; take `Components` via `get_subobject_list`.
2. Resolve each item to a `sk_type_id_t` as above; collect the signature.
3. `world_spawn(world, ids, count)` — columns start zeroed.
4. For each component RID, in list order: `instance = world_component(world, entity, type_id)`;
   if the registry hook is non-NULL, call `on_load_asset(world, entity, repository, instance, component_rid)`.
5. Recurse `Children` (each item is an `entity_resource` payload). Spawn
   the parent entity **before** its children. Resource children are an
   authoring tree only: this contract does **not** add a Parent ECS
   component (a later Transform/Parent loader may write one).
6. Cycle: if a child RID already appears on the current ancestor chain,
   skip that child (do not recurse). Do not follow soft references as
   children.

---

## 4. How `scene_resource` roots entities

### 4.1 `scene_resource` fields (APX-297)

Keep `Name` at index 0. Type id stays `SK_SCENE_RESOURCE_TYPE_ID` /
name `"SceneResource"`.

| Index | Name | Storage | Contents |
| --- | --- | --- | --- |
| 0 | `Name` | `STRING` | Existing display name |
| 1 | `Roots` | `SUB_OBJECT_LIST` | Owned root `entity_resource` payloads |

```c
enum sk_scene_resource_field_t {
	SK_SCENE_RESOURCE_FIELD_NAME = 0,
	SK_SCENE_RESOURCE_FIELD_ROOTS = 1,
};
```

`Roots` are the scene's top-level entities: they have no resource parent
inside the scene. Each root is an `entity_resource` payload (same type
as APX-296), so it already carries `Components` + `Children`. The scene
does **not** invent an implicit ECS "scene entity". Seeding default
Lighting / PostProcessing prototypes (today's SceneHandler comment) is
authoring, not part of this mapping.

### 4.2 `world_spawn_from_asset` (APX-299)

```c
sk_entity_t (*world_spawn_from_asset)(sk_world_t* world,
									  sk_repository_t* repository,
									  sk_rid_t rid);
```

`world` and `repository` are explicit; the world does not bind a
repository. `rid` may be either:

1. An `entity_resource` / `scene_resource` **payload** RID, or
2. A `ResourceAsset` wrapper (`SK_RESOURCE_ASSET_TYPE_ID`) whose
   `OBJECT` field (`SK_RESOURCE_ASSET_FIELD_OBJECT`) is one of those
   payloads. Unwrap once.

| `rid` type after unwrap | Behavior | Return |
| --- | --- | --- |
| `SK_ENTITY_RESOURCE_TYPE_ID` | Spawn that entity, then its `Children` tree | The spawned entity, or `SK_ENTITY_INVALID` on failure |
| `SK_SCENE_RESOURCE_TYPE_ID` | Spawn every `Roots` item (and each item's children) as sibling trees | The **first successfully spawned root**, or `SK_ENTITY_INVALID` if the list is empty / every root failed |
| anything else / `SK_RID_ZERO` / dead | No spawn | `SK_ENTITY_INVALID` |

A failed root does not undo roots already spawned.

---

## 5. Migration: every `register_component` site

No production plugin registers components today (`plugin_entry_point`
only publishes the API table). Every live call is a test. APX-295
changes the function type and every call that still passes four
positional arguments.

### 5.1 API / implementation (must change)

| File | Line | Role |
| --- | --- | --- |
| `plugins/entities/entities.h` | 357 | `sk_entities_api_t::register_component` declaration |
| `plugins/entities/entities.c` | 214 | `register_component_impl` (positional → `const sk_component_desc_t*`) |
| `plugins/entities/entities.c` | 1768 | API table slot (`register_component_impl`) |

### 5.2 Direct call sites (must wrap a `sk_component_desc_t`)

| File | Line | Caller |
| --- | --- | --- |
| `plugins/entities/entities.c` | 1959 | `entities_register_component_roundtrip` |
| `plugins/entities/entities.c` | 1972, 1973, 1974 | `entities_register_component_idempotent` (3 calls) |
| `plugins/entities/entities.c` | 1980, 1981, 1982 | `entities_register_component_conflict` (3 calls) |
| `plugins/entities/entities.c` | 1991, 1994, 1995 | `entities_register_component_invalid` (3 calls) |
| `plugins/entities/entities.c` | 2843, 2844, 2845 | `ecs_world_register_components` helper (pos / vel / tag) |
| `plugins/entities/entities.c` | 2978 | `entities_world_spawn_validation` (`TEST_ECS_HUGE_ID`) |
| `plugins/entities/entities.c` | 4055 | `entities_register_component_capacity` loop |
| `plugins/entities/entities.c` | 4065 | `entities_register_component_capacity` overflow probe |
| `app/app.c` | 1605 | `app_profiler_report_end_to_end` (`pos_id`) |

`ecs_world_register_components` is the single helper used by these
tests (they do not call `register_component` themselves):
`entities_world_spawn_despawn_generation` (2849),
`entities_world_add_remove_component` (2895),
`entities_world_spawn_validation` (2948),
`entities_world_add_component_validation` (2997),
`entities_world_move_updates_swapped_slot` (3026),
`entities_world_archetype_cache` (3067),
`entities_world_archetype_cache_many_distinct` (3094),
`entities_world_add_remove_reuses_archetypes` (3122),
`entities_world_chunk_hint_reuses_freed_rows` (3146),
`entities_world_query_observes_new_archetypes` (3200),
`entities_commands_apply_order` (3238),
`entities_commands_batch_create_destroy` (3295),
`entities_commands_deferred_during_query_iteration` (3346),
`entities_commands_buffer_reuse_and_clear` (3418),
`entities_commands_target_validation` (3498),
`entities_commands_remove_component_roundtrip` (3527),
`entities_commands_unknown_placeholder_skipped` (3571),
`entities_commands_apply_reports_failures` (3592).

### 5.3 Pointer-only checks (no argument rewrite)

These only assert the table slot is non-NULL; they compile against the
new function type without further edits:

| File | Line |
| --- | --- |
| `plugins/entities/entities.c` | 1907 (`entities_api_table_is_complete`) |
| `app/app.c` | 1487 (`app_init_auto_loads_entities_plugin`) |
| `app/app.c` | 1513 (`entities_plugin_registers_api`) |

### 5.4 Comments that name the old signature

| File | Line | Note |
| --- | --- | --- |
| `plugins/entities/entities.h` | 755 | `world_spawn` doc ("see register_component") |
| `plugins/entities/entities.c` | 1901 | registry-reset comment |

No other tree (`editor/`, `player/`, other plugins) calls
`register_component` today. Samples added later follow §1.1.

---

## 6. Out of scope (this contract)

- Implementing the descriptor, resource fields, or spawn path.
- Built-in component loaders (APX-300).
- ECS parent/transform hierarchy (resource `Children` only).
- Invoking reserved save/unload hooks.
- Changing `sk_component_info_t` or chunk layout.
- Seeding default scene entities on `SceneHandler::create`.
