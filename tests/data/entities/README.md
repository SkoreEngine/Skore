# Scene / entity resource fixtures (APX-301)

On-disk `scene_resource` / `entity_resource` packages used by the ECS
integration tests in `tests/integration/entities_fixtures.c`.

## Layout

| File | Role |
| --- | --- |
| `manifest.json` | Catalog of all fixtures (id, file, root type, expected node count) |
| `entity_single.json` | Single `EntityResource` with transform + camera + light + static tag components |
| `entity_parent_children.json` | `EntityResource` with children nested **two levels deep** (parent → children → grandchildren) |
| `scene_multiple_roots.json` | `SceneResource` with **three roots**, one root carrying a child tree |
| `entity_unregistered_component.json` | Negative fixture: Components list references a `MeshResource` (a registered repository type that is **not** an ECS component) |

## Format

Each fixture is a self-contained `sk.resource_package` document (the engine's
own JSON serialization contract, `docs/repository-assets-json-serialization-contract.md`):

- Envelope: `format: "sk.resource_package"`, `format_version: 1`,
  `root_uuid`, flat `resources[]` array.
- Cross-resource edges (`Components` / `Children` / `Roots` sub-object lists)
  are UUID strings; UUID identity never changes across loads.
- Component payload values use the kinds added for APX-300 payload types:
  vec3 / quat / color encode as JSON float arrays, enums as integers.

Load with `sk_resource_deserialize_package_json_from_file` after
`sk_resource_assets_register_types` + `sk_resource_asset_builtins_register_types`
(which also registers the built-in component payload types).

## Loading

Use the harness in `tests/integration/entities_fixtures.h`:

- `sk_entities_fixture_dir` — absolute path to this directory
- `sk_entities_fixture_path` — path to one fixture JSON file
- `sk_entities_fixture_desc` — catalog entry (root type, expected node count)

Path resolution works when the test binary runs from `{build}/bin` (CTest
default) via the compile-time `SK_TEST_DATA_DIR` and relative fallbacks (same
resolution as `tests/data/resource_object/`, shared in
`resource_object_fixtures.c`).

## Fixture tree summaries

### entity_single.json (1 entity)

```
SingleHero
├─ transform   Position (1, 2, 3)  Scale (2, 3, 4)
├─ camera      Perspective, FovY 60°, Near 0.1, Far 1000
├─ light       Point, color (1, 0.8, 0.5), intensity 2.5, range 15
└─ static tag  (no authored fields)
```

### entity_parent_children.json (5 entities)

```
Parent            transform+camera
├─ ChildA         transform+light
│  ├─ GrandChildA1 transform+static tag
│  └─ GrandChildA2 transform+camera
└─ ChildB         transform+static tag
```

### scene_multiple_roots.json (6 entities)

```
MultiRootLevel (SceneResource)
├─ RootA       transform+camera
│  └─ ChildA1  transform
├─ RootB       transform+light
└─ RootC       transform
   └─ ChildC1  transform+static tag
      └─ GrandChildC1 transform
```

### entity_unregistered_component.json (negative)

```
BadEntity
├─ mesh_resource  (registered repository type, NOT an ECS component → spawn fails)
└─ transform
```
