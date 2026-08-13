# Resource-to-ECS mapping

**Status:** implemented (APX-295 descriptor refactor, APX-296/297 resource
types, APX-298 component instantiate, APX-299 `world_spawn_from_asset`,
APX-300 built-in loaders, APX-301 spawn fixtures, APX-302 migration sweep +
docs). This document is the authoritative reference for creating entities
from resources (`scene_resource` / `entity_resource`).
**Goal:** create entities from resources (`scene_resource` / `entity_resource`).
**Audience:** engine hosts, plugin authors, tooling.

Related:

- ECS registration / storage: `plugins/entities/entities.h`, `entities.c`
- Repository / RIDs / SubObjectList: `core/repository.h`
- Entity/Scene payload types: `core/resource_asset_builtins.h`
  (`SK_ENTITY_RESOURCE_TYPE_ID`, `SK_SCENE_RESOURCE_TYPE_ID`)
- ResourceAsset wrapper + envelope payload types:
  `core/resource_assets_types.h` (`SK_RESOURCE_ASSET_TYPE_ID`,
  `SK_RESOURCE_ASSET_FIELD_OBJECT`)
- Built-in component payloads: `core/resource_component_types.h`
- Built-in ECS components: `plugins/entities/entities_builtins.{h,c}`
- Spawn integration fixtures: `tests/data/entities/`,
  `tests/integration/entities_fixtures.c`

---

## 0. How the pieces fit together

### 0.1 ECS registration

Component types are identified by `sk_type_id_t` (see `common.h`). Each
component's compile-time type id maps to its layout (`size` / `align`), an
optional display name, and an optional asset-load hook. Registration uses one
caller-owned descriptor:

```c
i32 (*register_component)(const sk_component_desc_t* desc);
```

Implemented by `register_component_impl` (`entities.c`). A process-global
registry (`ecs_component_registry[SK_ECS_MAX_COMPONENT_TYPES]`, max 256)
stores the full `sk_component_desc_t` (layout + name + hooks).

| Return | Meaning |
| --- | --- |
| `0` | Success, or idempotent re-register of the same `type_id` with the same `size`/`align` |
| `-1` | Same `type_id` already registered with a different `size` or `align` |
| `-2` | Registry full (`SK_ECS_MAX_COMPONENT_TYPES`) |
| `-3` | `desc == NULL`, `type_id` zero, `size == 0`, or `align == 0` |

Name is not compared on re-register and is not updated (first name wins).
`component_info` returns the stored layout (`sk_component_info_t`); it does
not carry hooks. `component_desc` returns the stored descriptor including
hooks.

Archetypes are sorted `sk_type_id_t` signatures plus implicit column 0
(`SK_ECS_ENTITY_COMPONENT_ID`). `world_spawn` looks up each id in the
registry, creates the archetype on demand, and **zero-fills** every user
column. `world_spawn_from_asset` spawns entity/scene resources (§4.2).

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

### 0.3 EntityResource / SceneResource payloads

`EntityResource` and `SceneResource` are repository payload types registered
by `sk_resource_asset_builtins_register_types` (which also registers the
built-in component payload types, §7). `Name` stays at field index 0 so
existing JSON envelopes stay valid; the component/roots lists are owned
`SUB_OBJECT_LIST` fields (§3.1 / §4.1).

---

## 1. Component registration descriptor

Registration passes one caller-owned descriptor. The registry stores the
descriptor by value (`type_id`, `size`, `align`, `name`, hook pointers); the
descriptor may be transient (stack).

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

`entities.h` `#include`s `repository.h` so the callback can name
`sk_repository_t*` and `sk_rid_t`; that is a one-way core dependency
(entities already statically links `sk-core`).

### 1.1 Registration rules

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
(first registration wins). Hook pointer mismatches are **not** a conflict.

`sk_component_info_t` stays layout-only (`type_id`, `size`, `align`,
`name`). Hooks live on the registry entry and are looked up by
`type_id` at spawn time. Archetype columns do not grow.

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
- A non-zero return fails the instantiate of that component:
  `world_add_component_from_asset` removes the just-added component, and
  `world_spawn_from_asset` despawns the failed entity, so a half-initialized
  slot is never left behind. Siblings / scene roots already spawned are not
  rolled back. Loaders should leave `instance` usable on failure (leave it
  zeroed).
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

or with the built-in resolver `sk_resource_entity_component_type_id`
(`core/resource_component_types.h`). Then `ecs->component_info(type_id, &info)`
(and the registry hook) must succeed for that id to be placed on the spawn
signature.

### 3.1 `entity_resource` fields

`Name` is at index 0. The type id stays `SK_ENTITY_RESOURCE_TYPE_ID` / name
`"EntityResource"`.

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

A list entry is skipped when it is `SK_RID_ZERO`, the resource is not live,
or `resource_type` is NULL. An entry whose type id is **not** registered
with ECS is a spawn failure for that entity (`SK_ENTITY_INVALID` for that
subtree; siblings already spawned stay). Two entries that resolve to the
**same** type id are an authoring error and fail that entity (`world_spawn`
would only keep one column).

User component count after dedup must be `< SK_ECS_MAX_ARCHETYPE_COLUMNS`
(column 0 is the implicit entity component).

### 3.2 Instantiate order

For one `entity_resource` payload RID:

1. `read` the payload; take `Components` via `get_subobject_list`.
2. Resolve each item to a `sk_type_id_t` as above; collect the signature.
3. `world_spawn(world, ids, count)` — columns start zeroed.
4. For each component RID, in list order: `instance = world_component(world, entity, type_id)`;
   if the registry hook is non-NULL, call `on_load_asset(world, entity, repository, instance, component_rid)`.
   On hook failure the entity is despawned (see §2.1).
5. Recurse `Children` (each item is an `entity_resource` payload). Spawn
   the parent entity **before** its children. Resource children are an
   authoring tree only: this contract does **not** add a Parent ECS
   component (a later Transform/Parent loader may write one).
6. Cycle: if a child RID already appears on the current ancestor chain,
   skip that child (do not recurse). Do not follow soft references as
   children. Depth is capped at `SK_ECS_MAX_ASSET_SPAWN_DEPTH` (64).

---

## 4. How `scene_resource` roots entities

### 4.1 `scene_resource` fields

`Name` is at index 0. Type id stays `SK_SCENE_RESOURCE_TYPE_ID` /
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
inside the scene. Each root is an `entity_resource` payload (same
type as §3.1), so it already carries `Components` + `Children`. The scene
does **not** invent an implicit ECS "scene entity". Seeding default
Lighting / PostProcessing prototypes (today's SceneHandler comment) is
authoring, not part of this mapping.

### 4.2 `world_spawn_from_asset`

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

A failed root does not undo roots already spawned. The scene variant is also
exposed directly as `world_spawn_scene_from_asset` (same unwrap + same
return contract); `world_spawn_from_asset` dispatches to it for scene RIDs.

---

## 5. Migration status

The old positional `register_component` signature
(`i32 (*)(sk_type_id_t, u32 size, u32 align, const_chr_t name)`) has been
**removed**; there is no code path that still passes four positional
arguments. All call sites now build a zero-init `sk_component_desc_t`:

- `plugins/entities/entities.c` — `register_component_impl` (descriptor),
  the API table slot, every in-source test (`test_register_component` wraps
  the descriptor), and the `ecs_world_register_components` test helper.
- `plugins/entities/entities_builtins.c` — `sk_entities_builtins_register`
  registers the five built-in components through descriptors (one shared
  zero-init desc, fields re-filled per component).
- `app/app.c` — `app_profiler_report_end_to_end` registers a test
  "position" component through a descriptor; the pointer-only checks
  (`app_init_auto_loads_entities_plugin`, `entities_plugin_registers_api`)
  only assert `register_component` / `component_desc` /
  `world_spawn_from_asset` / `world_spawn_scene_from_asset` are non-NULL.

`world_spawn_from_asset` (`sk_entity_t (*)(sk_world_t*, sk_repository_t*,
sk_rid_t)`) is the only spawn-from-asset entry point; there is no legacy
variant anywhere in the tree. The skore-ecs-benchmark sample (external repo)
still targets the pre-descriptor `v2` API and is updated separately when v2
adopts this contract.

No other tree (`editor/`, `player/`, other plugins, `tests/benchmarks/`)
calls `register_component` or the spawn-from-asset entry points.

---

## 6. Out of scope (not implemented)

- Invoking reserved save/unload hooks (`on_save_asset`, `on_unload_asset`).
- ECS parent/transform hierarchy (resource `Children` only; a later
  Transform/Parent loader may write a Parent component).
- Seeding default scene entities on `SceneHandler::create`.
- Changing `sk_component_info_t` or chunk layout.

---

## 7. Built-in components

The sk-entities plugin ships a set of built-in components, each pairing a POD
component struct with a repository payload type whose registered type id IS
the ECS component type id (§3). The repository half lives in
`core/resource_component_types.{h,c}` (type id constants, field indices,
payload descriptors, `sk_resource_component_types_register`, and the
`sk_resource_entity_component_type_id` resolver); the ECS half — component
structs, `on_load_asset` loaders, `sk_entities_builtins_register` — lives in
`plugins/entities/entities_builtins.{h,c}`. The plugin registers the ECS
components from its entry point (`sk_entities_init` → `sk_entities_builtins_register`),
so the built-ins are "the component types the engine registers today".
Repositories get the payload types through
`sk_resource_asset_builtins_register_types` (which delegates to
`sk_resource_component_types_register`).

Component payload types are deliberately app-dependency-free (no filesystem /
app API) so the plugin — which statically links sk-core only — can register
them into a repository for its load tests without pulling sk-app symbols.

### 7.1 Component → payload mapping

| Component struct | Type id (`sk.*_resource`) | Payload fields | Loader |
| --- | --- | --- | --- |
| `sk_transform_t` | `sk.transform_resource` | `Position` VEC3, `Rotation` QUAT, `Scale` VEC3 | reads all three |
| `sk_camera_t` | `sk.camera_resource` | `Projection` ENUM, `FovY` FLOAT, `Near` FLOAT, `Far` FLOAT | reads all four |
| `sk_light_t` | `sk.light_resource` | `Type` ENUM, `Color` COLOR, `Intensity` FLOAT, `Range` FLOAT | reads all four |
| `sk_mesh_renderer_t` | `sk.mesh_renderer_resource` | `Mesh` REFERENCE → mesh_resource, `Material` REFERENCE → material_graph_resource | reads both RIDs |
| `sk_static_tag_t` | `sk.static_tag_resource` | *(none)* | **NULL** |

Field 0 is **not** a Name string on component payloads: components are
authored inside an EntityResource `Components` list, so the payload starts
directly with component values (Name-at-0 is an envelope-only convention,
§3.1 / §4.1).

### 7.2 No meaningful asset representation

`sk_static_tag_t` is a runtime-only query marker: its payload type carries
zero authored fields (a 1-byte empty instance so the repository accepts the
type), so its `on_load_asset` is deliberately **NULL** and spawned instances
stay zeroed. It is still a registered component (query tags need no loader)
and can appear in a `Components` list; there is simply nothing to load. This
is the only built-in without a loader.

### 7.3 Loader contract

Every loader follows §2.1: `sk_repository_api()->read(repository, rid)` then
the typed field getters (so prototype-chain inheritance is honored), writes
only `instance`, and returns non-zero (leaving the instance zeroed) when the
component resource is not a live read view. Per-component tests
(`entities_builtins_*_load_asset`) author a component resource, spawn an
entity holding the component, and invoke the stored hook via
`component_desc(..., &desc).on_load_asset` — the exact dispatch
`world_spawn_from_asset` (§4.2 / step 3.2-4) performs — then assert the
authored values landed in the spawned instance.

---

## 8. Worked example: author a scene asset and spawn it

This is the minimal end-to-end flow: author a `scene_resource` package with
one root entity carrying a transform, load it into a repository, and spawn it
into an ECS world.

### 8.1 The scene asset (JSON)

Each fixture is a self-contained `sk.resource_package` document (the engine's
JSON serialization contract, `docs/repository-assets-json-serialization-contract.md`):
an envelope with a flat `resources[]` array; cross-resource edges
(`Components` / `Children` / `Roots` sub-object lists) are UUID strings.
Vec3 / quat / color encode as JSON float arrays; enums as integers.

```json
{
  "format": "sk.resource_package",
  "format_version": 1,
  "root_uuid": "0000000000007001-0000000000007001",
  "resources": [
    {
      "format": "sk.resource",
      "format_version": 1,
      "type": "SceneResource",
      "uuid": "0000000000007001-0000000000007001",
      "fields": {
        "Name": "HelloScene",
        "Roots": ["0000000000007002-0000000000007002"]
      }
    },
    {
      "format": "sk.resource",
      "format_version": 1,
      "type": "EntityResource",
      "uuid": "0000000000007002-0000000000007002",
      "fields": {
        "Name": "Hero",
        "Components": ["0000000000007003-0000000000007003"],
        "Children": []
      }
    },
    {
      "format": "sk.resource",
      "format_version": 1,
      "type": "TransformResource",
      "uuid": "0000000000007003-0000000000007003",
      "fields": {
        "Position": [1.0, 2.0, 3.0],
        "Rotation": [0.0, 0.0, 0.0, 1.0],
        "Scale": [1.0, 1.0, 1.0]
      }
    }
  ]
}
```

The tree is: `HelloScene` (SceneResource) → `Hero` (EntityResource) →
`TransformResource { Position (1,2,3) }`. Real packages can nest arbitrarily
deeper via `Children`.

### 8.2 Loading and spawning (C)

```c
#include "app.h"
#include "entities.h"
#include "entities_builtins.h"
#include "resource_asset_builtins.h"
#include "resource_serialize.h"

/* 1. Repository: register the asset + built-in component payload types
 *    (EntityResource / SceneResource / TransformResource / …), then load
 *    the package. sk_app_init auto-loads the sk-entities plugin, which
 *    registers the ECS API table and the built-in components. */
sk_repository_t* repository = sk_repository_api()->create(sk_allocator_default());
sk_resource_assets_register_types(repository);            /* envelopes */
sk_resource_asset_builtins_register_types(repository);    /* + component payloads */

sk_rid_t scene_rid = SK_RID_ZERO;
sk_resource_deserialize_package_json_from_file(repository, "hello_scene.json", &scene_rid);

const sk_entities_api_t* ecs =
    (const sk_entities_api_t*)sk_app_api()->get_api(app, SK_ENTITIES_API_TYPE_ID);
sk_world_t* world = ecs->world_create();

/* 2. Spawn: one ECS entity per entity_resource node, components populated
 *    from the authored payloads through their on_load_asset hooks. */
sk_entity_t hero = ecs->world_spawn_from_asset(world, repository, scene_rid);
/* sk_entity_is_valid(hero) == 1; world_count(world) == 1 */

/* 3. The authored values landed in the spawned component slot. */
const sk_transform_t* t = (const sk_transform_t*)ecs->world_component(world, hero, SK_TRANSFORM_COMPONENT_TYPE_ID);
/* t->position == {1.0f, 2.0f, 3.0f} */
```

Step 1 is exactly what `sk_entities_fixture_load` does for the on-disk
fixtures in `tests/data/entities/` (see `tests/integration/entities_fixtures.c`);
`scene_multiple_roots.json` is a three-root variant of the same shape and
`entity_parent_children.json` shows nested `Children`.
