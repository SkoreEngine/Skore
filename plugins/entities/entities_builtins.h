#pragma once

/**
 * @file entities_builtins.h
 * @brief Built-in ECS components shipped with the sk-entities plugin.
 *
 * Each built-in component pairs a POD component struct with a repository
 * payload type whose registered type id IS the component's ECS type id
 * (resource-to-ECS mapping contract §3). The payload types are registered by
 * core (sk_resource_asset_builtins_register_types) so EntityResource
 * Components lists can author them; this header/implementation provides the
 * ECS half — component structs, on_load_asset loaders that copy authored
 * fields from the component resource into the spawned instance, and the
 * registration entry point the plugin calls at load.
 *
 * Component -> resource type id:
 *   sk_transform_t       SK_TRANSFORM_COMPONENT_TYPE_ID      (sk.transform_resource)
 *   sk_camera_t          SK_CAMERA_COMPONENT_TYPE_ID         (sk.camera_resource)
 *   sk_light_t           SK_LIGHT_COMPONENT_TYPE_ID          (sk.light_resource)
 *   sk_mesh_renderer_t   SK_MESH_RENDERER_COMPONENT_TYPE_ID  (sk.mesh_renderer_resource)
 *   sk_static_tag_t      SK_STATIC_TAG_COMPONENT_TYPE_ID     (sk.static_tag_resource)
 */

#include "common.h"
#include "entities.h"
#include "repository.h"
#include "resource_component_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- component structs (POD; layout is part of the public API) ---- */

/**
 * World transform: position + rotation (unit quaternion) + non-uniform scale.
 * Rotation convention follows sk_quat_t (rotate with q * v * conjugate(q)).
 */
typedef struct sk_transform_t {
	sk_vec3_t position;
	sk_quat_t rotation;
	sk_vec3_t scale;
} sk_transform_t;

/** Camera projection modes (sk.camera_resource Projection field). */
typedef enum sk_camera_projection_t {
	SK_CAMERA_PROJECTION_PERSPECTIVE = 0,
	SK_CAMERA_PROJECTION_ORTHOGRAPHIC = 1,
} sk_camera_projection_t;

/** Camera view/projection settings (sk.camera_resource). */
typedef struct sk_camera_t {
	sk_camera_projection_t projection; /* SK_CAMERA_FIELD_PROJECTION */
	f32 fov_y;						   /* vertical field of view, radians */
	f32 near_z;						   /* near clip distance */
	f32 far_z;						   /* far clip distance */
} sk_camera_t;

/** Light kinds (sk.light_resource Type field). */
typedef enum sk_light_type_t {
	SK_LIGHT_TYPE_DIRECTIONAL = 0,
	SK_LIGHT_TYPE_POINT = 1,
	SK_LIGHT_TYPE_SPOT = 2,
} sk_light_type_t;

/** Light settings (sk.light_resource). Color is linear-space RGBA. */
typedef struct sk_light_t {
	sk_light_type_t type; /* SK_LIGHT_FIELD_TYPE */
	sk_color_t color;	  /* SK_LIGHT_FIELD_COLOR */
	f32 intensity;		  /* SK_LIGHT_FIELD_INTENSITY */
	f32 range;			  /* SK_LIGHT_FIELD_RANGE (point/spot attenuation) */
} sk_light_t;

/**
 * Renders a mesh with a material. The mesh / material fields are soft
 * references (sk_rid_t) into the repository that owns the component resource;
 * they are valid for as long as that repository holds the targets live.
 */
typedef struct sk_mesh_renderer_t {
	sk_rid_t mesh;	   /* SK_MESH_RENDERER_FIELD_MESH (sk.mesh_resource) */
	sk_rid_t material; /* SK_MESH_RENDERER_FIELD_MATERIAL (sk.material_graph_resource) */
} sk_mesh_renderer_t;

/**
 * Runtime-only static marker. It has NO meaningful asset representation: the
 * payload type carries zero authored fields (StaticTagResource is an empty
 * 1-byte instance) and the component is a pure query tag, so its
 * on_load_asset hook is deliberately NULL — spawned instances stay zeroed and
 * systems simply query for the presence of the component.
 */
typedef struct sk_static_tag_t {
	u8 marker;
} sk_static_tag_t;

/**
 * Register every built-in component with @p ecs (idempotent; the registry
 * keeps the first registration's name/hooks). Called automatically from the
 * plugin entry point; hosts/tests may call it again safely.
 * @param ecs ECS API table (must not be NULL).
 * @return 0 on success, the first register_component error otherwise.
 */
i32 sk_entities_builtins_register(const sk_entities_api_t* ecs);

/**
 * INTERNAL (plugin-internal, not part of the public host surface): returns
 * the sk-entities module API table — the same static table the plugin
 * registers on the app context. Used by sk_entities_builtins_register at
 * load time and by the plugin's in-source tests. Hosts must obtain the API
 * with app_api->get_api(ctx, SK_ENTITIES_API_TYPE_ID) instead.
 */
const sk_entities_api_t* sk_entities_module_api(void);

#ifdef __cplusplus
}
#endif
