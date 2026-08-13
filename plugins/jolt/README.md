# sk-jolt plugin (physics)

The `sk-jolt` plugin backs the engine physics with the vendored
[Jolt](https://github.com/jankrassnigg/jolt) rigid-body engine. The public
surface is **plain C only** — `plugins/jolt/jolt.h` is the entire host-facing
API (opaque handles, POD structs, enums/constants, one `sk_jolt_api_t` table);
the implementation lives in C++ translation units (`jolt.cpp`, `plugin_entry_point.cpp`)
that link the vendored Jolt static library behind that boundary. A C compiler
can consume the header; no C++ types, templates, or mangled names cross it.

Hosts obtain the table from the app registry after the plugin auto-loads
(`sk_app_init` scans `{app_folder}/plugins` and calls `sk_plugin_entry_point`):

```c
const sk_jolt_api_t* jolt = (const sk_jolt_api_t*)app_api->get_api(ctx, SK_JOLT_API_TYPE_ID);
```

The engine integration itself (app-owned ECS scene world + per-frame
`jolt->step_world(world, delta_time)`) lives in `app/app.c`; a runnable scene
that exercises the whole stack end to end is `tests/physics_scene.c`
(`sk-physics-scene`, registered with CTest). See also
[`docs/physics-components.md`](../../docs/physics-components.md) for the
serialization / repository half of the components and
[`plugins/jolt/jolt_components.h`](jolt_components.h) for the ECS structs.

## Component set

Rigid bodies and characters are composed from small, focused ECS components
(registered by `sk_jolt_components_register(ecs)`, declared in
`jolt_components.h`, type ids in `core/resource_jolt_component_types.h`):

| Component | Struct | Role |
| --- | --- | --- |
| `rigid_body_config` | `sk_rigid_body_config_t` | Cold / authored: motion type (static/kinematic/dynamic), mass, friction, restitution, linear + angular damping, gravity factor, object layer, behavior flags, opaque Jolt body handle (filled by the integration; not serialized) |
| `rigid_body_state` | `sk_rigid_body_state_t` | Hot / per-frame: linear + angular velocity |
| `transform` | `sk_transform_t` | World pose (position + rotation); the integration reads it for static/kinematic bodies and writes the simulated pose back for dynamic ones |
| `box_collider` | `sk_box_collider_t` | Box shape: half extents |
| `sphere_collider` | `sk_sphere_collider_t` | Sphere shape: radius |
| `capsule_collider` | `sk_capsule_collider_t` | Capsule shape: half height + radius |
| `character_config` | `sk_character_config_t` | Cold / authored: capsule radius + standing height, max slope angle, step height, mass, object layer, opaque CharacterVirtual handle (not serialized) |
| `character_state` | `sk_character_state_t` | Hot / per-frame: linear velocity (the walk input) + ground state |

A body is an entity with `rigid_body_config` + `transform` + at least one
collider (`rigid_body_state` is what the integration writes back into). A
character is an entity with `character_config` + `character_state` +
`transform`.

## Hot / cold split

- **Cold data** (`rigid_body_config`, `character_config`, collider shapes) is
  authored once and read by the physics integration. It sits in its own
  components so per-frame simulation never touches it.
- **Hot data** (`rigid_body_state`, `character_state`, `transform`) is the
  per-frame channel: velocities are applied every step and the simulated pose /
  velocities are written back every step.
- The config structs stay within one cache line and the hot state components
  hold only the per-frame fields (velocities), so simulation systems can
  iterate and write them independently of cold data.

## C API entry points (`sk_jolt_api_t`, resolved via `SK_JOLT_API_TYPE_ID`)

The table below is the shipped surface of `plugins/jolt/jolt.h` (48 entries).
Every handle-returning or handle-taking entry validates its handles and returns
an error (never crashes) on NULL / destroyed / stale handles. All entries are
main-thread only.

**Lifecycle**

| Entry | Purpose |
| --- | --- |
| `init(settings)` | Create the Jolt world: factory + registered types, 10 MiB temp allocator, job-system threads, layer filters, `PhysicsSystem`. Re-entrant: replaces a live world. |
| `shutdown()` | Tear the world down in reverse construction order and release every Jolt resource. Idempotent; safe without a matching `init`. |
| `settings_defaults(out)` | Default world settings (gravity `{0,-9.81,0}`, fixed timestep `1/60`, 1 substep, 65536 bodies / body pairs, 10240 constraints). |
| `set_gravity / get_gravity` | World gravity (m/s²). |
| `set_fixed_timestep / get_fixed_timestep` | Seconds per physics step (> 0); draining the accumulator on change. |
| `set_substeps / get_substeps` | Collision/integration substeps per step (>= 1). |
| `step(delta, callback, user_data)` | Advance the fixed-step accumulator by one host frame delta (clamped to 0.25 s); runs `PhysicsSystem::Update` and the per-step callback once per completed step. The callback is a `sk_jolt_step_callback_fn` (user_data, physics_time, step_index). |

**Rigid bodies (direct runtime access, no ECS involved)**

| Entry | Purpose |
| --- | --- |
| `body_create(shape, motion_type, object_layer)` / `body_destroy(body)` | Create a body from a box/sphere/capsule POD shape description; destroy it. |
| `body_get/set_position`, `body_get/set_rotation` | World-space pose (m / unit quaternion); setters wake the body. |
| `body_get/set_linear_velocity`, `body_get/set_angular_velocity` | Velocities (m/s, rad/s); setters wake the body (ignored for static). |
| `body_get/set_motion_type` | Static ↔ kinematic ↔ dynamic at runtime; switching to static freezes the body. |
| `body_add_force`, `body_add_force_at_position`, `body_add_impulse`, `body_add_impulse_at_position`, `body_add_torque`, `body_add_angular_impulse` | Forces (N, accumulated per step), impulses (N·s, instantaneous), torques (N·m) and angular impulses (N·m·s). |
| `body_teleport(body, position, rotation)` | Atomic `SetPositionAndRotation` re-placement. |
| `body_activate / body_deactivate / body_is_active` | Control / query the body's simulated (awake) state. |

**Characters (Jolt `CharacterVirtual`)**

| Entry | Purpose |
| --- | --- |
| `character_settings_defaults(out)` | Defaults: radius 0.3, height 1.8, max slope 50°, step height 0.4, mass 70 kg, MOVING layer. |
| `character_create(shape, object_layer)` / `character_create_configured(settings)` | Create a capsule controller at the origin (feet at y = 0). |
| `character_destroy(character)` | Destroy the controller. |
| `character_get/set_position` | Controller position (feet); the setter refreshes ground contacts. |
| `character_get/set_velocity` | Full 3D velocity (jumps / knockback). |
| `character_move(desired_velocity)` | Persistent walk input (m/s): slope limiting + stair walking per step (the Y component is ignored — use `character_set_velocity` to jump). |
| `character_get_ground_state(character, out)` | Grounded / on steep slope / in air. |

**Scene queries**

| Entry | Purpose |
| --- | --- |
| `ray_cast(origin, direction, max_distance, object_layer, out_hit)` | Closest ray hit (body handle, point, normal, fraction), honoring the layer collision matrix. Returns 0 hit / 1 no hit / negative invalid. |
| `sphere_cast(radius, origin, direction, max_distance, object_layer, out_hit)` | Closest sphere-sweep hit, same contract. |

**ECS ↔ Jolt sync**

| Entry | Purpose |
| --- | --- |
| `sync_world(world)` | Reconcile the ECS world with Jolt: create bodies for new rigid-body entities, destroy them on despawn / component removal, push static/kinematic poses and hot velocities, rebuild shapes on collider change, honor the require-update dirty set. |
| `write_back(world)` | Write the simulated pose + velocities onto `transform` / `rigid_body_state` (and the character pose / velocity / ground state). |
| `step_world(world, delta_time)` | One engine frame: `sync_world` → fixed `step` with `write_back` after every completed step. The app frame loop calls exactly this. |
| `entity_require_update(world, entity)` | See below. |

## `entity_require_update` contract

ECS components are plain POD data — nothing inside them can notify the physics
integration when gameplay mutates a field. The rule:

- **Must call** `entity_require_update(world, entity)` after mutating **cold /
  authored** component data of an entity that already has a live Jolt body:
  any `rigid_body_config` field (motion type, mass, friction, restitution,
  damping, gravity factor, object layer, flags) or any collider shape field
  (box half extents, sphere radius, capsule half height / radius). The next
  `sync_world` / `step_world` then rebuilds or reconfigures the body from the
  new values. **Without the call the mutation is silently ignored.**
- **Not required** (the sync detects these automatically):
  - spawning / despawning the entity or adding / removing components
    (structural changes),
  - mutating the `transform` of a static / kinematic body (followed every step)
    or the hot `rigid_body_state` velocities (applied every step),
  - any `body_*` direct runtime operation (velocities, forces / impulses /
    torques, teleport, activate / deactivate, motion type) — these act on the
    live Jolt body immediately and never go through components.

Marking the same entity repeatedly is harmless (dirty is a set, consumed by the
next sync); marking a dead entity or one without a body is a no-op.

## Value conventions

Coordinates are Jolt world space (Y-up, meters); positions are the body's
shape origin. Motion types mirror `JPH::EMotionType` (Static=0, Kinematic=1,
Dynamic=2). Object layers are 16-bit: `NON_MOVING` (0, static geometry),
`MOVING` (1, dynamic/kinematic bodies and characters), `SENSOR` (2, ghosts that
collide with nothing); the collision matrix is encoded in the
`SK_JOLT_COLLISION_MASK_*` constants, which drive both the object-layer pair
filter and the object-vs-broad-phase filter. Collision groups
(`SK_JOLT_COLLISION_GROUP_DEFAULT` / `_CHARACTER`) are the per-body
group-filter mechanism used to separate character probes from dynamic bodies.

## Tests / samples

- Plugin-local unit tests: `plugins/jolt/jolt_tests.c` (world lifecycle,
  bodies, layers, queries, characters, ECS sync, require-update contract,
  determinism).
- End-to-end scene: `tests/physics_scene.c` → `sk-physics-scene` (ground plane,
  falling/settling box + sphere stacks, kinematic platform with a rider,
  character walking over terrain and up stairs; reports stable resting, no
  jitter, no tunneling at the default timestep, and repeat-run determinism).
