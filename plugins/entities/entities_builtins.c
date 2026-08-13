/**
 * @file entities_builtins.c
 * @brief Built-in ECS components (structs + on_load_asset loaders) shipped
 *        with the sk-entities plugin.
 *
 * Implements the loaders that copy authored values from a component resource
 * into a spawned component instance. Each loader follows the mapping contract
 * §2.1: read the component resource through sk_repository_api()->read and the
 * field getters (so prototype inheritance is honored), write only the
 * instance slot, and leave the instance zeroed on failure. The loaders are
 * registered with register_component (sk_component_desc_t.on_load_asset) by
 * sk_entities_builtins_register, which the plugin entry point calls at load —
 * these are "the component types the engine registers today".
 *
 * NULL on_load_asset is intentional for sk_static_tag_t: the static tag is a
 * runtime-only query marker whose resource carries no authored fields (see
 * entities_builtins.h), so there is nothing to load.
 */

#include "entities_builtins.h"

#include "allocator.h"
#include "resource_asset_builtins.h" /* sk_resource_entity_component_type_id (header-only use) */
#include "resource_component_types.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  on_load_asset loaders                                             */
/* ------------------------------------------------------------------ */

static i32 transform_on_load_asset(sk_world_t* world, sk_entity_t entity, sk_repository_t* repository, void_ptr_t instance, sk_rid_t component_resource) {
	(void)world;
	(void)entity;
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(repository, component_resource);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return -1; /* dead RID: leave the instance zeroed */
	}
	sk_transform_t* transform = (sk_transform_t*)instance;
	transform->position = repo->get_vec3(view, SK_TRANSFORM_FIELD_POSITION);
	transform->rotation = repo->get_quat(view, SK_TRANSFORM_FIELD_ROTATION);
	transform->scale = repo->get_vec3(view, SK_TRANSFORM_FIELD_SCALE);
	return 0;
}

static i32 camera_on_load_asset(sk_world_t* world, sk_entity_t entity, sk_repository_t* repository, void_ptr_t instance, sk_rid_t component_resource) {
	(void)world;
	(void)entity;
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(repository, component_resource);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return -1; /* dead RID: leave the instance zeroed */
	}
	sk_camera_t* camera = (sk_camera_t*)instance;
	camera->projection = (sk_camera_projection_t)repo->get_enum(view, SK_CAMERA_FIELD_PROJECTION);
	camera->fov_y = (f32)repo->get_float(view, SK_CAMERA_FIELD_FOV_Y);
	camera->near_z = (f32)repo->get_float(view, SK_CAMERA_FIELD_NEAR);
	camera->far_z = (f32)repo->get_float(view, SK_CAMERA_FIELD_FAR);
	return 0;
}

static i32 light_on_load_asset(sk_world_t* world, sk_entity_t entity, sk_repository_t* repository, void_ptr_t instance, sk_rid_t component_resource) {
	(void)world;
	(void)entity;
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(repository, component_resource);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return -1; /* dead RID: leave the instance zeroed */
	}
	sk_light_t* light = (sk_light_t*)instance;
	light->type = (sk_light_type_t)repo->get_enum(view, SK_LIGHT_FIELD_TYPE);
	light->color = repo->get_color(view, SK_LIGHT_FIELD_COLOR);
	light->intensity = (f32)repo->get_float(view, SK_LIGHT_FIELD_INTENSITY);
	light->range = (f32)repo->get_float(view, SK_LIGHT_FIELD_RANGE);
	return 0;
}

static i32 mesh_renderer_on_load_asset(sk_world_t* world, sk_entity_t entity, sk_repository_t* repository, void_ptr_t instance, sk_rid_t component_resource) {
	(void)world;
	(void)entity;
	const sk_repository_api_t* repo = sk_repository_api();
	sk_resource_object_t view = repo->read(repository, component_resource);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return -1; /* dead RID: leave the instance zeroed */
	}
	sk_mesh_renderer_t* renderer = (sk_mesh_renderer_t*)instance;
	renderer->mesh = repo->get_reference(view, SK_MESH_RENDERER_FIELD_MESH);
	renderer->material = repo->get_reference(view, SK_MESH_RENDERER_FIELD_MATERIAL);
	return 0;
}

/* sk_static_tag_t deliberately has NO on_load_asset: its resource carries no
 * authored fields, so the loader would be a no-op. The component exists as a
 * query tag and stays zeroed (see entities_builtins.h). */

/* ------------------------------------------------------------------ */
/*  Registration                                                      */
/* ------------------------------------------------------------------ */

/* Alignment of a POD component type (C11 _Alignof; the probe-struct/offsetof
 * trick used for the entity component trips clang-tidy's C23-extension check). */
#define SK_BUILTIN_ALIGN_OF(_type) ((u32) _Alignof(_type))

i32 sk_entities_builtins_register(const sk_entities_api_t* ecs) {
	i32 rc = 0;
	sk_component_desc_t desc = {0};

	desc.type_id = SK_TRANSFORM_COMPONENT_TYPE_ID;
	desc.size = (u32)sizeof(sk_transform_t);
	desc.align = SK_BUILTIN_ALIGN_OF(sk_transform_t);
	desc.name = "transform";
	desc.on_load_asset = transform_on_load_asset;
	rc = ecs->register_component(&desc);
	if (rc != 0) {
		return rc;
	}

	desc.type_id = SK_CAMERA_COMPONENT_TYPE_ID;
	desc.size = (u32)sizeof(sk_camera_t);
	desc.align = SK_BUILTIN_ALIGN_OF(sk_camera_t);
	desc.name = "camera";
	desc.on_load_asset = camera_on_load_asset;
	rc = ecs->register_component(&desc);
	if (rc != 0) {
		return rc;
	}

	desc.type_id = SK_LIGHT_COMPONENT_TYPE_ID;
	desc.size = (u32)sizeof(sk_light_t);
	desc.align = SK_BUILTIN_ALIGN_OF(sk_light_t);
	desc.name = "light";
	desc.on_load_asset = light_on_load_asset;
	rc = ecs->register_component(&desc);
	if (rc != 0) {
		return rc;
	}

	desc.type_id = SK_MESH_RENDERER_COMPONENT_TYPE_ID;
	desc.size = (u32)sizeof(sk_mesh_renderer_t);
	desc.align = SK_BUILTIN_ALIGN_OF(sk_mesh_renderer_t);
	desc.name = "mesh_renderer";
	desc.on_load_asset = mesh_renderer_on_load_asset;
	rc = ecs->register_component(&desc);
	if (rc != 0) {
		return rc;
	}

	/* Static tag: no meaningful asset representation -> on_load_asset stays
	 * NULL (registered hook is never invoked; spawned instances stay zeroed). */
	desc.type_id = SK_STATIC_TAG_COMPONENT_TYPE_ID;
	desc.size = (u32)sizeof(sk_static_tag_t);
	desc.align = SK_BUILTIN_ALIGN_OF(sk_static_tag_t);
	desc.name = "static_tag";
	desc.on_load_asset = NULL;
	rc = ecs->register_component(&desc);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "test.h"
#include "unity.h"

/* Fixture shared by the per-component tests: registers the built-in
 * components (idempotent, so tests pass regardless of other tests resetting
 * the module component registry), a fresh repository carrying every payload
 * type, and a fresh world. */
typedef struct builtins_test_env_t {
	sk_repository_t* repository;
	sk_world_t* world;
} builtins_test_env_t;

static const sk_entities_api_t* builtins_ecs(void) {
	/* Internal accessor defined in entities.c (same table the plugin
	 * registers on the app context). */
	return sk_entities_module_api();
}

static void builtins_env_setup(builtins_test_env_t* env) {
	const sk_repository_api_t* repo = sk_repository_api();
	memset(env, 0, sizeof(*env));
	TEST_ASSERT_EQUAL_INT(0, sk_entities_builtins_register(builtins_ecs()));
	env->repository = repo->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(env->repository);
	/* Component payload types only: this module is app-dependency-free, so
	 * the plugin can link and register it (asset handler payload types live
	 * in resource_asset_builtins, which references sk-app and cannot be
	 * linked into plugins). */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_component_types_register(env->repository));
	env->world = builtins_ecs()->world_create();
	TEST_ASSERT_NOT_NULL(env->world);
}

static void builtins_env_teardown(builtins_test_env_t* env) {
	builtins_ecs()->world_destroy(env->world);
	sk_repository_api()->destroy(env->repository);
}

/* Create one resource of @p type_id in the fixture repository. */
static sk_rid_t builtins_create_resource(builtins_test_env_t* env, sk_type_id_t type_id, sk_uuid_t uuid) {
	const sk_repository_api_t* repo = sk_repository_api();
	const sk_resource_type_t* type = repo->find_type(env->repository, type_id);
	TEST_ASSERT_NOT_NULL(type);
	return repo->create_resource(env->repository, type, uuid, NULL);
}

/* Spawn an entity holding @p type_id and invoke the registered on_load_asset
 * hook for @p component_rid the same way world_add_component_from_asset /
 * world_spawn_from_asset dispatch: lookup the stored descriptor
 * (component_desc), then call its hook with the live entity, repository,
 * instance slot, and component resource RID. */
static void builtins_spawn_and_load(builtins_test_env_t* env, sk_type_id_t type_id, sk_rid_t component_rid, sk_entity_t* out_entity, void_ptr_t* out_instance) {
	const sk_entities_api_t* ecs = builtins_ecs();
	sk_entity_t entity = ecs->world_spawn(env->world, &type_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));
	void_ptr_t instance = ecs->world_component(env->world, entity, type_id);
	TEST_ASSERT_NOT_NULL(instance);
	sk_component_desc_t stored = {0};
	TEST_ASSERT_EQUAL_INT(0, ecs->component_desc(type_id, &stored));
	TEST_ASSERT_NOT_NULL(stored.on_load_asset);
	TEST_ASSERT_EQUAL_INT(0, stored.on_load_asset(env->world, entity, env->repository, instance, component_rid));
	*out_entity = entity;
	*out_instance = instance;
}

SK_TEST(entities_builtins_transform_load_asset) {
	builtins_test_env_t env;
	builtins_env_setup(&env);
	const sk_repository_api_t* repo = sk_repository_api();

	/* Author a transform resource: position (1,2,3), identity rotation,
	 * scale (2,2,2). */
	sk_rid_t rid = builtins_create_resource(&env, SK_TRANSFORM_COMPONENT_TYPE_ID, (sk_uuid_t){0x3001u, 0x3001u});
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t w = repo->write(env.repository, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, repo->set_vec3(w, SK_TRANSFORM_FIELD_POSITION, (sk_vec3_t){1.0f, 2.0f, 3.0f}));
		TEST_ASSERT_EQUAL_INT(0, repo->set_quat(w, SK_TRANSFORM_FIELD_ROTATION, (sk_quat_t){0.0f, 0.0f, 0.0f, 1.0f}));
		TEST_ASSERT_EQUAL_INT(0, repo->set_vec3(w, SK_TRANSFORM_FIELD_SCALE, (sk_vec3_t){2.0f, 3.0f, 4.0f}));
		repo->commit(w, NULL);
	}

	/* The component resource's repository type id IS the ECS type id. */
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, rid), SK_TRANSFORM_COMPONENT_TYPE_ID));

	sk_entity_t entity = SK_ENTITY_INVALID;
	void_ptr_t instance = NULL;
	builtins_spawn_and_load(&env, SK_TRANSFORM_COMPONENT_TYPE_ID, rid, &entity, &instance);

	const sk_transform_t* transform = (const sk_transform_t*)instance;
	TEST_ASSERT_EQUAL_FLOAT(1.0f, transform->position.x);
	TEST_ASSERT_EQUAL_FLOAT(2.0f, transform->position.y);
	TEST_ASSERT_EQUAL_FLOAT(3.0f, transform->position.z);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, transform->rotation.x);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, transform->rotation.y);
	TEST_ASSERT_EQUAL_FLOAT(0.0f, transform->rotation.z);
	TEST_ASSERT_EQUAL_FLOAT(1.0f, transform->rotation.w);
	TEST_ASSERT_EQUAL_FLOAT(2.0f, transform->scale.x);
	TEST_ASSERT_EQUAL_FLOAT(3.0f, transform->scale.y);
	TEST_ASSERT_EQUAL_FLOAT(4.0f, transform->scale.z);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));

	builtins_env_teardown(&env);
}

SK_TEST(entities_builtins_camera_load_asset) {
	builtins_test_env_t env;
	builtins_env_setup(&env);
	const sk_repository_api_t* repo = sk_repository_api();

	sk_rid_t rid = builtins_create_resource(&env, SK_CAMERA_COMPONENT_TYPE_ID, (sk_uuid_t){0x3002u, 0x3002u});
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t w = repo->write(env.repository, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, repo->set_enum(w, SK_CAMERA_FIELD_PROJECTION, (u64)SK_CAMERA_PROJECTION_ORTHOGRAPHIC));
		TEST_ASSERT_EQUAL_INT(0, repo->set_float(w, SK_CAMERA_FIELD_FOV_Y, 1.0471975511965976)); /* 60 deg */
		TEST_ASSERT_EQUAL_INT(0, repo->set_float(w, SK_CAMERA_FIELD_NEAR, 0.10000000000000001));
		TEST_ASSERT_EQUAL_INT(0, repo->set_float(w, SK_CAMERA_FIELD_FAR, 1000.0));
		repo->commit(w, NULL);
	}

	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, rid), SK_CAMERA_COMPONENT_TYPE_ID));

	sk_entity_t entity = SK_ENTITY_INVALID;
	void_ptr_t instance = NULL;
	builtins_spawn_and_load(&env, SK_CAMERA_COMPONENT_TYPE_ID, rid, &entity, &instance);

	const sk_camera_t* camera = (const sk_camera_t*)instance;
	TEST_ASSERT_EQUAL_INT((int)SK_CAMERA_PROJECTION_ORTHOGRAPHIC, (int)camera->projection);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0471976f, camera->fov_y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.1f, camera->near_z);
	TEST_ASSERT_EQUAL_FLOAT(1000.0f, camera->far_z);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));

	builtins_env_teardown(&env);
}

SK_TEST(entities_builtins_light_load_asset) {
	builtins_test_env_t env;
	builtins_env_setup(&env);
	const sk_repository_api_t* repo = sk_repository_api();

	sk_rid_t rid = builtins_create_resource(&env, SK_LIGHT_COMPONENT_TYPE_ID, (sk_uuid_t){0x3003u, 0x3003u});
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t w = repo->write(env.repository, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, repo->set_enum(w, SK_LIGHT_FIELD_TYPE, (u64)SK_LIGHT_TYPE_SPOT));
		TEST_ASSERT_EQUAL_INT(0, repo->set_color(w, SK_LIGHT_FIELD_COLOR, (sk_color_t){0.8f, 0.1f, 0.2f, 1.0f}));
		TEST_ASSERT_EQUAL_INT(0, repo->set_float(w, SK_LIGHT_FIELD_INTENSITY, 3.5));
		TEST_ASSERT_EQUAL_INT(0, repo->set_float(w, SK_LIGHT_FIELD_RANGE, 12.25));
		repo->commit(w, NULL);
	}

	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, rid), SK_LIGHT_COMPONENT_TYPE_ID));

	sk_entity_t entity = SK_ENTITY_INVALID;
	void_ptr_t instance = NULL;
	builtins_spawn_and_load(&env, SK_LIGHT_COMPONENT_TYPE_ID, rid, &entity, &instance);

	const sk_light_t* light = (const sk_light_t*)instance;
	TEST_ASSERT_EQUAL_INT((int)SK_LIGHT_TYPE_SPOT, (int)light->type);
	TEST_ASSERT_EQUAL_FLOAT(0.8f, light->color.r);
	TEST_ASSERT_EQUAL_FLOAT(0.1f, light->color.g);
	TEST_ASSERT_EQUAL_FLOAT(0.2f, light->color.b);
	TEST_ASSERT_EQUAL_FLOAT(1.0f, light->color.a);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 3.5f, light->intensity);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 12.25f, light->range);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));

	builtins_env_teardown(&env);
}

SK_TEST(entities_builtins_mesh_renderer_load_asset) {
	builtins_test_env_t env;
	builtins_env_setup(&env);
	const sk_repository_api_t* repo = sk_repository_api();

	/* Author the referenced mesh / material targets as plain resources (the
	 * REFERENCE fields store RIDs; the plugin cannot link the asset payload
	 * types, and the storage layer does not validate the target type). */
	sk_rid_t mesh = builtins_create_resource(&env, SK_TRANSFORM_COMPONENT_TYPE_ID, (sk_uuid_t){0x3004u, 0x3004u});
	sk_rid_t material = builtins_create_resource(&env, SK_TRANSFORM_COMPONENT_TYPE_ID, (sk_uuid_t){0x3005u, 0x3005u});
	TEST_ASSERT_TRUE(mesh.id != 0u && material.id != 0u);

	sk_rid_t rid = builtins_create_resource(&env, SK_MESH_RENDERER_COMPONENT_TYPE_ID, (sk_uuid_t){0x3006u, 0x3006u});
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t w = repo->write(env.repository, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, repo->set_reference(w, SK_MESH_RENDERER_FIELD_MESH, mesh));
		TEST_ASSERT_EQUAL_INT(0, repo->set_reference(w, SK_MESH_RENDERER_FIELD_MATERIAL, material));
		repo->commit(w, NULL);
	}

	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, rid), SK_MESH_RENDERER_COMPONENT_TYPE_ID));

	sk_entity_t entity = SK_ENTITY_INVALID;
	void_ptr_t instance = NULL;
	builtins_spawn_and_load(&env, SK_MESH_RENDERER_COMPONENT_TYPE_ID, rid, &entity, &instance);

	const sk_mesh_renderer_t* renderer = (const sk_mesh_renderer_t*)instance;
	TEST_ASSERT_TRUE(SK_RID_EQ(renderer->mesh, mesh));
	TEST_ASSERT_TRUE(SK_RID_EQ(renderer->material, material));
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));

	builtins_env_teardown(&env);
}

SK_TEST(entities_builtins_static_tag_no_asset_loader) {
	builtins_test_env_t env;
	builtins_env_setup(&env);
	const sk_entities_api_t* ecs = builtins_ecs();
	const sk_repository_api_t* repo = sk_repository_api();

	/* Static tag has no authored payload: registered hook must be NULL and
	 * the repository type must carry zero fields (empty asset shell). */
	sk_component_desc_t stored = {0};
	TEST_ASSERT_EQUAL_INT(0, ecs->component_desc(SK_STATIC_TAG_COMPONENT_TYPE_ID, &stored));
	TEST_ASSERT_NULL(stored.on_load_asset);
	TEST_ASSERT_EQUAL_UINT32((u32)sizeof(sk_static_tag_t), stored.size);

	const sk_resource_type_t* type = repo->find_type(env.repository, SK_STATIC_TAG_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(type);
	TEST_ASSERT_EQUAL_UINT32(0u, repo->type_field_count(type));

	/* Spawning still works: the component is part of the signature and stays
	 * zeroed (nothing to load). */
	sk_rid_t rid = builtins_create_resource(&env, SK_STATIC_TAG_COMPONENT_TYPE_ID, (sk_uuid_t){0x3007u, 0x3007u});
	TEST_ASSERT_TRUE(rid.id != 0u);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, rid), SK_STATIC_TAG_COMPONENT_TYPE_ID));

	const sk_type_id_t tag_id = SK_STATIC_TAG_COMPONENT_TYPE_ID;
	sk_entity_t entity = ecs->world_spawn(env.world, &tag_id, 1u);
	TEST_ASSERT_TRUE(sk_entity_is_valid(entity));
	void_ptr_t instance = ecs->world_component(env.world, entity, SK_STATIC_TAG_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(instance);
	TEST_ASSERT_EQUAL_UINT8(0u, ((const sk_static_tag_t*)instance)->marker);

	builtins_env_teardown(&env);
}

SK_TEST(entities_builtins_spawn_from_asset_loads_transform) {
	builtins_test_env_t env;
	builtins_env_setup(&env);
	const sk_repository_api_t* repo = sk_repository_api();
	const sk_entities_api_t* ecs = builtins_ecs();

	typedef struct spawn_entity_resource_t {
		sk_field_string_t name;
		sk_field_subobject_list_t components;
		sk_field_subobject_list_t children;
	} spawn_entity_resource_t;
	static const sk_resource_field_t fields[] = {
		{"Name", SK_ENTITY_RESOURCE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(spawn_entity_resource_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
		{"Components",
		 SK_ENTITY_RESOURCE_FIELD_COMPONENTS,
		 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
		 (u32)offsetof(spawn_entity_resource_t, components),
		 (u32)sizeof(sk_field_subobject_list_t),
		 {0ull, 0ull}},
		{"Children",
		 SK_ENTITY_RESOURCE_FIELD_CHILDREN,
		 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
		 (u32)offsetof(spawn_entity_resource_t, children),
		 (u32)sizeof(sk_field_subobject_list_t),
		 {0ull, 0ull}},
	};
	sk_resource_type_desc_t entity_desc = {0};
	entity_desc.type_id = SK_ENTITY_RESOURCE_TYPE_ID;
	entity_desc.name = "EntityResource";
	entity_desc.instance_size = (u32)sizeof(spawn_entity_resource_t);
	entity_desc.fields = fields;
	entity_desc.field_count = (u32)(sizeof(fields) / sizeof(fields[0]));
	TEST_ASSERT_EQUAL_INT(0, repo->register_type(env.repository, &entity_desc));

	sk_rid_t transform = builtins_create_resource(&env, SK_TRANSFORM_COMPONENT_TYPE_ID, (sk_uuid_t){0x3101u, 0x3101u});
	{
		sk_resource_object_t w = repo->write(env.repository, transform);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, repo->set_vec3(w, SK_TRANSFORM_FIELD_POSITION, (sk_vec3_t){4.0f, 5.0f, 6.0f}));
		TEST_ASSERT_EQUAL_INT(0, repo->set_quat(w, SK_TRANSFORM_FIELD_ROTATION, (sk_quat_t){0.0f, 0.0f, 0.0f, 1.0f}));
		TEST_ASSERT_EQUAL_INT(0, repo->set_vec3(w, SK_TRANSFORM_FIELD_SCALE, (sk_vec3_t){1.0f, 1.0f, 1.0f}));
		repo->commit(w, NULL);
	}

	const sk_resource_type_t* entity_type = repo->find_type(env.repository, SK_ENTITY_RESOURCE_TYPE_ID);
	TEST_ASSERT_NOT_NULL(entity_type);
	sk_rid_t entity_rid = repo->create_resource(env.repository, entity_type, (sk_uuid_t){0x3102u, 0x3102u}, NULL);
	TEST_ASSERT_TRUE(entity_rid.id != 0u);
	{
		sk_resource_object_t w = repo->write(env.repository, entity_rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
		TEST_ASSERT_EQUAL_INT(0, repo->set_subobject_list(w, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, &transform, 1u));
		repo->commit(w, NULL);
	}

	sk_entity_t spawned = ecs->world_spawn_from_asset(env.world, env.repository, entity_rid);
	TEST_ASSERT_TRUE(sk_entity_is_valid(spawned));
	const sk_transform_t* live = (const sk_transform_t*)ecs->world_component(env.world, spawned, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(live);
	TEST_ASSERT_EQUAL_FLOAT(4.0f, live->position.x);
	TEST_ASSERT_EQUAL_FLOAT(5.0f, live->position.y);
	TEST_ASSERT_EQUAL_FLOAT(6.0f, live->position.z);
	TEST_ASSERT_EQUAL_FLOAT(1.0f, live->rotation.w);
	TEST_ASSERT_EQUAL_FLOAT(1.0f, live->scale.x);

	builtins_env_teardown(&env);
}

#endif /* SK_TESTS */
