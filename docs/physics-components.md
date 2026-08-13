# Physics ECS components (APX-306)

The physics integration composes a rigid body from small, focused ECS
components instead of one fat component, so simulation systems can iterate and
write only the data they touch:

| Component | Type id (registered repository type) | Data | Access |
| --- | --- | --- | --- |
| `sk_rigid_body_config_t` | `sk.rigid_body_config_resource` | cold / authored: motion type, mass, friction, restitution, linear + angular damping, gravity factor, object layer, behavior flags, opaque Jolt body handle | read by the physics integration; written at spawn |
| `sk_rigid_body_state_t` | `sk.rigid_body_state_resource` | hot / per-frame: linear + angular velocity (24 bytes, one cache line) | iterated and written every tick by simulation systems |
| `sk_box_collider_t` | `sk.box_collider_resource` | shape: half extents | composed per body |
| `sk_sphere_collider_t` | `sk.sphere_collider_resource` | shape: radius | composed per body |
| `sk_capsule_collider_t` | `sk.capsule_collider_resource` | shape: half height + radius | composed per body |

## Layout

- **Ownership**: ECS structs and the registration entry points live in
  `plugins/jolt/jolt_components.{h,c}`; the repository payload type
  descriptors (reflection) live in `core/resource_jolt_component_types.{h,c}`
  so hosts/tests can register and serialize them without linking the plugin.
- **Type identity**: each component's registered repository type id **is** the
  ECS component type id (resource-to-ECS mapping contract §3), so an authored
  component value serializes under the same identity the ECS registers.
- **Defaults** mirror the vendored Jolt `BodyCreationSettings` (friction 0.2,
  restitution 0.0, damping 0.05 each, gravity factor 1.0, sleeping allowed)
  plus authored mass 1.0 kg, motion type Dynamic, object layer MOVING; the
  value conventions match `plugins/jolt/jolt.h`.
- **Body handle**: `sk_rigid_body_config_t.body` is an opaque `sk_jolt_body_t*`
  filled in by the integration when the body is created. It is deliberately
  **not** a repository field — pointers cannot round-trip — so it exists only
  on the ECS struct.

## Registration

- `sk_jolt_components_register(ecs)` — registers the five components with the
  entities API (idempotent). Called from the plugin entry point once the
  entities table is available (retried on first `init` for out-of-order plugin
  loads).
- `sk_jolt_component_types_register(repository)` — registers the five payload
  types into a repository via manual field descriptors (reflection; no C++
  Reflection — see `core/repository.h`), making them appear in the repository
  type listing and serialize/deserialize through `core/resource_serialize.h`.
  The editor project host calls this next to the built-in asset type
  registration so the types show up in the same listing.

## Serialization

Vector / quaternion / color fields use the JSON wire form defined by the
serialization contract ("arrays of f64 in field order"); enum fields serialize
as plain unsigned integers. The serializer support for these generic field
kinds lives in `core/resource_serialize.c` (previously skipped: "not used by
asset types today").

## Tests

- Core: `resource_serialize_vec_enum_roundtrip` — vec2/3/4, quat, color, mat4,
  enum field kinds round-trip through JSON.
- Plugin: repository type registration, defaults, and descriptor-driven
  storage (write/read/clone) for every physics payload; ECS registration +
  entity spawn round-trip.
- Host (`app.c`): `jolt_components_serialize_roundtrip` — the actual physics
  payload types serialize → destroy → deserialize → re-serialize to identical
  JSON; `app_init_auto_loads_jolt_plugin` — components appear in the ECS
  component registry after the real (sorted) plugin auto-load.
