/**
 * @file resource_component_types.c
 * @brief Built-in ECS component payload resource types (APX-300).
 *
 * Repository payload type descriptors for the built-in components. Each type
 * id doubles as the ECS component type id (resource-to-ECS mapping contract
 * §3); the ECS side — component structs and on_load_asset loaders — is
 * registered by the sk-entities plugin (plugins/entities/entities_builtins.c)
 * from the same constants.
 *
 * This module is kept free of app-only symbols (no filesystem.h / app API
 * calls) so the sk-entities plugin can link it: plugins statically link
 * sk-core only, and anything touching sk_filesystem_api lives in sk-app.
 */

#include "resource_component_types.h"

#include "allocator.h"
#include "resource_asset_builtins.h" /* mesh / material type ids for REFERENCE sub_type */

#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Transform (sk.transform_resource)                                  */
/* ------------------------------------------------------------------ */

typedef struct sk_transform_resource_t {
	sk_vec3_t position; /* SK_TRANSFORM_FIELD_POSITION */
	sk_quat_t rotation; /* SK_TRANSFORM_FIELD_ROTATION */
	sk_vec3_t scale;	/* SK_TRANSFORM_FIELD_SCALE */
} sk_transform_resource_t;

static const sk_resource_field_t transform_resource_fields[] = {
	{"Position", SK_TRANSFORM_FIELD_POSITION, SK_RESOURCE_FIELD_TYPE_VEC3, (u32)offsetof(sk_transform_resource_t, position), (u32)sizeof(sk_vec3_t), {0ull, 0ull}},
	{"Rotation", SK_TRANSFORM_FIELD_ROTATION, SK_RESOURCE_FIELD_TYPE_QUAT, (u32)offsetof(sk_transform_resource_t, rotation), (u32)sizeof(sk_quat_t), {0ull, 0ull}},
	{"Scale", SK_TRANSFORM_FIELD_SCALE, SK_RESOURCE_FIELD_TYPE_VEC3, (u32)offsetof(sk_transform_resource_t, scale), (u32)sizeof(sk_vec3_t), {0ull, 0ull}},
};

/* Newly created transforms default to identity (origin, no rotation, unit
 * scale) so un-authored spawns are well-defined. */
static const sk_transform_resource_t transform_resource_defaults = {
	{0.0f, 0.0f, 0.0f},
	{0.0f, 0.0f, 0.0f, 1.0f},
	{1.0f, 1.0f, 1.0f},
};

static const sk_resource_type_desc_t transform_resource_type_desc = {
	{SK_TRANSFORM_COMPONENT_TYPE_ID_LO, SK_TRANSFORM_COMPONENT_TYPE_ID_HI},			 "TransformResource",		   (u32)sizeof(sk_transform_resource_t), transform_resource_fields,
	(u32)(sizeof(transform_resource_fields) / sizeof(transform_resource_fields[0])), &transform_resource_defaults,
};

/* ------------------------------------------------------------------ */
/*  Camera (sk.camera_resource)                                        */
/* ------------------------------------------------------------------ */

typedef struct sk_camera_resource_t {
	u64 projection; /* SK_CAMERA_FIELD_PROJECTION (ENUM) */
	f64 fov_y;		/* SK_CAMERA_FIELD_FOV_Y (FLOAT) */
	f64 near_z;		/* SK_CAMERA_FIELD_NEAR (FLOAT) */
	f64 far_z;		/* SK_CAMERA_FIELD_FAR (FLOAT) */
} sk_camera_resource_t;

static const sk_resource_field_t camera_resource_fields[] = {
	{"Projection", SK_CAMERA_FIELD_PROJECTION, SK_RESOURCE_FIELD_TYPE_ENUM, (u32)offsetof(sk_camera_resource_t, projection), (u32)sizeof(u64), {0ull, 0ull}},
	{"FovY", SK_CAMERA_FIELD_FOV_Y, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_camera_resource_t, fov_y), (u32)sizeof(f64), {0ull, 0ull}},
	{"Near", SK_CAMERA_FIELD_NEAR, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_camera_resource_t, near_z), (u32)sizeof(f64), {0ull, 0ull}},
	{"Far", SK_CAMERA_FIELD_FAR, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_camera_resource_t, far_z), (u32)sizeof(f64), {0ull, 0ull}},
};

static const sk_resource_type_desc_t camera_resource_type_desc = {
	{SK_CAMERA_COMPONENT_TYPE_ID_LO, SK_CAMERA_COMPONENT_TYPE_ID_HI},
	"CameraResource",
	(u32)sizeof(sk_camera_resource_t),
	camera_resource_fields,
	(u32)(sizeof(camera_resource_fields) / sizeof(camera_resource_fields[0])),
	NULL,
};

/* ------------------------------------------------------------------ */
/*  Light (sk.light_resource)                                          */
/* ------------------------------------------------------------------ */

typedef struct sk_light_resource_t {
	u64 type;		  /* SK_LIGHT_FIELD_TYPE (ENUM) */
	sk_color_t color; /* SK_LIGHT_FIELD_COLOR (COLOR) */
	f64 intensity;	  /* SK_LIGHT_FIELD_INTENSITY (FLOAT) */
	f64 range;		  /* SK_LIGHT_FIELD_RANGE (FLOAT) */
} sk_light_resource_t;

static const sk_resource_field_t light_resource_fields[] = {
	{"Type", SK_LIGHT_FIELD_TYPE, SK_RESOURCE_FIELD_TYPE_ENUM, (u32)offsetof(sk_light_resource_t, type), (u32)sizeof(u64), {0ull, 0ull}},
	{"Color", SK_LIGHT_FIELD_COLOR, SK_RESOURCE_FIELD_TYPE_COLOR, (u32)offsetof(sk_light_resource_t, color), (u32)sizeof(sk_color_t), {0ull, 0ull}},
	{"Intensity", SK_LIGHT_FIELD_INTENSITY, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_light_resource_t, intensity), (u32)sizeof(f64), {0ull, 0ull}},
	{"Range", SK_LIGHT_FIELD_RANGE, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_light_resource_t, range), (u32)sizeof(f64), {0ull, 0ull}},
};

static const sk_resource_type_desc_t light_resource_type_desc = {
	{SK_LIGHT_COMPONENT_TYPE_ID_LO, SK_LIGHT_COMPONENT_TYPE_ID_HI},
	"LightResource",
	(u32)sizeof(sk_light_resource_t),
	light_resource_fields,
	(u32)(sizeof(light_resource_fields) / sizeof(light_resource_fields[0])),
	NULL,
};

/* ------------------------------------------------------------------ */
/*  Mesh renderer (sk.mesh_renderer_resource)                          */
/* ------------------------------------------------------------------ */

typedef struct sk_mesh_renderer_resource_t {
	sk_rid_t mesh;	   /* SK_MESH_RENDERER_FIELD_MESH (REFERENCE) */
	sk_rid_t material; /* SK_MESH_RENDERER_FIELD_MATERIAL (REFERENCE) */
} sk_mesh_renderer_resource_t;

static const sk_resource_field_t mesh_renderer_resource_fields[] = {
	{"Mesh",
	 SK_MESH_RENDERER_FIELD_MESH,
	 SK_RESOURCE_FIELD_TYPE_REFERENCE,
	 (u32)offsetof(sk_mesh_renderer_resource_t, mesh),
	 (u32)sizeof(sk_rid_t),
	 {SK_MESH_RESOURCE_TYPE_ID_LO, SK_MESH_RESOURCE_TYPE_ID_HI}},
	{"Material",
	 SK_MESH_RENDERER_FIELD_MATERIAL,
	 SK_RESOURCE_FIELD_TYPE_REFERENCE,
	 (u32)offsetof(sk_mesh_renderer_resource_t, material),
	 (u32)sizeof(sk_rid_t),
	 {SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_LO, SK_MATERIAL_GRAPH_RESOURCE_TYPE_ID_HI}},
};

static const sk_resource_type_desc_t mesh_renderer_resource_type_desc = {
	{SK_MESH_RENDERER_COMPONENT_TYPE_ID_LO, SK_MESH_RENDERER_COMPONENT_TYPE_ID_HI},
	"MeshRendererResource",
	(u32)sizeof(sk_mesh_renderer_resource_t),
	mesh_renderer_resource_fields,
	(u32)(sizeof(mesh_renderer_resource_fields) / sizeof(mesh_renderer_resource_fields[0])),
	NULL,
};

/* ------------------------------------------------------------------ */
/*  Static tag (sk.static_tag_resource)                                */
/* ------------------------------------------------------------------ */

/* The static tag has no authored payload; the repository still needs a
 * non-zero instance size, so the payload is a single unused byte. */
typedef struct sk_static_tag_resource_t {
	u8 marker;
} sk_static_tag_resource_t;

static const sk_resource_type_desc_t static_tag_resource_type_desc = {
	{SK_STATIC_TAG_COMPONENT_TYPE_ID_LO, SK_STATIC_TAG_COMPONENT_TYPE_ID_HI}, "StaticTagResource", (u32)sizeof(sk_static_tag_resource_t), NULL, 0u, NULL,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                      */
/* ------------------------------------------------------------------ */

static const sk_resource_type_desc_t* const component_type_descs[] = {
	&transform_resource_type_desc, &camera_resource_type_desc, &light_resource_type_desc, &mesh_renderer_resource_type_desc, &static_tag_resource_type_desc,
};

i32 sk_resource_component_types_register(sk_repository_t* repository, const sk_repository_api_t* repo_api) {
	const sk_repository_api_t* api = repo_api;
	u32 count = (u32)(sizeof(component_type_descs) / sizeof(component_type_descs[0]));
	for (u32 i = 0u; i < count; ++i) {
		i32 result = api->register_type(repository, component_type_descs[i]);
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

sk_type_id_t sk_resource_entity_component_type_id(const sk_repository_t* repository, const sk_repository_api_t* repo_api, sk_rid_t component_rid) {
	const sk_repository_api_t* api = repo_api;
	const sk_resource_type_t* type = api->resource_type(repository, component_rid);
	if (type == NULL) {
		return SK_TYPE_ID_ZERO;
	}
	return api->type_id(type);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "app.h"
#include "test.h"
#include "unity.h"

static const sk_repository_api_t* ct_repo_api(void) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	return api;
}

/* APX-300: the built-in component payload types must register cleanly with
 * their field descriptors and defaults, and a component resource must resolve
 * back to its component type id (mapping contract §3 — the registered
 * repository type id IS the ECS component type id). */
SK_TEST(resource_component_types_register_and_resolve) {
	const sk_repository_api_t* api = ct_repo_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	TEST_ASSERT_EQUAL_INT(0, sk_resource_component_types_register(repo, api));

	/* Every component type is a registered repository payload type. */
#define TEST_COMPONENT_TYPE_REGISTERED(_id)                         \
	do {                                                            \
		const sk_resource_type_t* _t = api->find_type(repo, (_id)); \
		TEST_ASSERT_NOT_NULL(_t);                                   \
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->type_id(_t), (_id)));   \
	} while (0)
	TEST_COMPONENT_TYPE_REGISTERED(SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_COMPONENT_TYPE_REGISTERED(SK_CAMERA_COMPONENT_TYPE_ID);
	TEST_COMPONENT_TYPE_REGISTERED(SK_LIGHT_COMPONENT_TYPE_ID);
	TEST_COMPONENT_TYPE_REGISTERED(SK_MESH_RENDERER_COMPONENT_TYPE_ID);
	TEST_COMPONENT_TYPE_REGISTERED(SK_STATIC_TAG_COMPONENT_TYPE_ID);
#undef TEST_COMPONENT_TYPE_REGISTERED

	/* Field descriptors match the mapping contract (§7 of
	 * docs/resource-to-ecs-mapping-contract.md). */
	const sk_resource_type_t* transform = api->find_type(repo, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_EQUAL_UINT32(3u, api->type_field_count(transform));
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_VEC3, api->type_field_at(transform, 0u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_QUAT, api->type_field_at(transform, 1u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_VEC3, api->type_field_at(transform, 2u)->type);

	const sk_resource_type_t* camera = api->find_type(repo, SK_CAMERA_COMPONENT_TYPE_ID);
	TEST_ASSERT_EQUAL_UINT32(4u, api->type_field_count(camera));
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_ENUM, api->type_field_at(camera, 0u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_FLOAT, api->type_field_at(camera, 1u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_FLOAT, api->type_field_at(camera, 2u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_FLOAT, api->type_field_at(camera, 3u)->type);

	const sk_resource_type_t* light = api->find_type(repo, SK_LIGHT_COMPONENT_TYPE_ID);
	TEST_ASSERT_EQUAL_UINT32(4u, api->type_field_count(light));
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_ENUM, api->type_field_at(light, 0u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_COLOR, api->type_field_at(light, 1u)->type);

	const sk_resource_type_t* renderer = api->find_type(repo, SK_MESH_RENDERER_COMPONENT_TYPE_ID);
	TEST_ASSERT_EQUAL_UINT32(2u, api->type_field_count(renderer));
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_REFERENCE, api->type_field_at(renderer, 0u)->type);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_REFERENCE, api->type_field_at(renderer, 1u)->type);

	/* Static tag: empty asset shell (no authored fields). */
	const sk_resource_type_t* tag = api->find_type(repo, SK_STATIC_TAG_COMPONENT_TYPE_ID);
	TEST_ASSERT_EQUAL_UINT32(0u, api->type_field_count(tag));

	/* New transform resources default to identity. */
	sk_rid_t transform_rid = api->create_resource(repo, transform, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(transform_rid.id != 0u);
	{
		sk_resource_object_t view = api->read(repo, transform_rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		sk_vec3_t position = api->get_vec3(view, SK_TRANSFORM_FIELD_POSITION);
		sk_quat_t rotation = api->get_quat(view, SK_TRANSFORM_FIELD_ROTATION);
		sk_vec3_t scale = api->get_vec3(view, SK_TRANSFORM_FIELD_SCALE);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, position.x);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, position.y);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, position.z);
		TEST_ASSERT_EQUAL_FLOAT(1.0f, rotation.w);
		TEST_ASSERT_EQUAL_FLOAT(1.0f, scale.x);
	}

	/* The component resource's repository type id IS the component type id. */
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, transform_rid), SK_TRANSFORM_COMPONENT_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(repo, api, SK_RID_ZERO), SK_TYPE_ID_ZERO));

	api->destroy(repo);
}

#endif /* SK_TESTS */
