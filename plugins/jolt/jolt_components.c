/**
 * @file jolt_components.c
 * @brief Physics ECS component registration (plugin half).
 *
 * Implements the ECS half of the physics components declared in
 * jolt_components.h: POD component structs registered with the sk-entities
 * API under the type ids the repository payload types (owned by core,
 * resource_jolt_component_types.c) register under, so the reflection listing
 * and serialization stay in sync. The plugin entry glue
 * (sk_jolt_components_register_all) resolves the entities table from the app
 * registry at load and registers the components once entities is available
 * (retried on first init when plugin load order is not deterministic).
 *
 * No Jolt interaction here: the only Jolt type referenced is the opaque body
 * handle field on sk_rigid_body_config_t, which is deliberately NOT a
 * repository field (pointers cannot round-trip).
 *
 * Plugin-local tests stay free of resource_serialize (its *_to_file helpers
 * pull sk_filesystem_api, which only exists in sk-app and would break the
 * plugin shared library at dlopen/link): the repository descriptors are
 * exercised directly here, and the JSON round-trip of the physics payload
 * types is covered by a host-side integration test in app.c (linked against
 * sk-app where sk_filesystem_api resolves).
 */

#include "jolt_components.h"

#include "app.h" /* sk_app_context_t / sk_app_api_t for register_all glue */
#include "allocator.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Registration                                                      */
/* ------------------------------------------------------------------ */

/* Alignment of a POD component type (C11 _Alignof). */
#define SK_JOLT_COMPONENT_ALIGN(_type) ((u32) _Alignof(_type))

i32 sk_jolt_components_register(const sk_entities_api_t* ecs) {
	i32 rc = 0;

	rc = ecs->register_component(SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, (u32)sizeof(sk_rigid_body_config_t), SK_JOLT_COMPONENT_ALIGN(sk_rigid_body_config_t), "rigid_body_config");
	if (rc != 0) {
		return rc;
	}
	rc = ecs->register_component(SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, (u32)sizeof(sk_rigid_body_state_t), SK_JOLT_COMPONENT_ALIGN(sk_rigid_body_state_t), "rigid_body_state");
	if (rc != 0) {
		return rc;
	}
	rc = ecs->register_component(SK_TRANSFORM_COMPONENT_TYPE_ID, (u32)sizeof(sk_transform_t), SK_JOLT_COMPONENT_ALIGN(sk_transform_t), "transform");
	if (rc != 0) {
		return rc;
	}
	rc = ecs->register_component(SK_BOX_COLLIDER_COMPONENT_TYPE_ID, (u32)sizeof(sk_box_collider_t), SK_JOLT_COMPONENT_ALIGN(sk_box_collider_t), "box_collider");
	if (rc != 0) {
		return rc;
	}
	rc = ecs->register_component(SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID, (u32)sizeof(sk_sphere_collider_t), SK_JOLT_COMPONENT_ALIGN(sk_sphere_collider_t), "sphere_collider");
	if (rc != 0) {
		return rc;
	}
	rc = ecs->register_component(SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID, (u32)sizeof(sk_capsule_collider_t), SK_JOLT_COMPONENT_ALIGN(sk_capsule_collider_t), "capsule_collider");
	if (rc != 0) {
		return rc;
	}
	return 0;
}

/* Plugin glue: resolve the entities API from the app registry and register
 * the physics components. Safe to call repeatedly (register_component is
 * idempotent for a matching layout); a no-op until entities is registered, so
 * out-of-order plugin loads (test host scans are unsorted) are covered by the
 * caller retrying on first use. Not part of the public host surface. */
void sk_jolt_components_register_all(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_jolt_components_register_all(sk_app_context_t* context, const sk_app_api_t* app_api) {
	if (context == NULL || app_api == NULL) {
		return;
	}
	const sk_entities_api_t* ecs = (const sk_entities_api_t*)app_api->get_api(context, SK_ENTITIES_API_TYPE_ID);
	if (ecs == NULL || ecs->register_component == NULL) {
		return; /* entities not loaded yet; caller retries */
	}
	(void)sk_jolt_components_register(ecs);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS
#include "test.h"

static sk_repository_t* jolt_components_test_repo(void) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_jolt_component_types_register(repo));
	return repo;
}

/* Read a config resource and assert every field matches the snapshot below. */
static void jolt_components_assert_config_fields(sk_repository_t* repo, sk_rid_t rid) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_resource_object_t r = api->read(repo, rid);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(r));
	TEST_ASSERT_EQUAL_UINT64(SK_JOLT_MOTION_TYPE_DYNAMIC, api->get_enum(r, SK_RIGID_BODY_CONFIG_FIELD_MOTION_TYPE));
	TEST_ASSERT_EQUAL_DOUBLE(2.5, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_MASS));
	TEST_ASSERT_EQUAL_DOUBLE(0.8, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_FRICTION));
	TEST_ASSERT_EQUAL_DOUBLE(0.3, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_RESTITUTION));
	TEST_ASSERT_EQUAL_DOUBLE(0.1, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_LINEAR_DAMPING));
	TEST_ASSERT_EQUAL_DOUBLE(0.2, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_ANGULAR_DAMPING));
	TEST_ASSERT_EQUAL_DOUBLE(0.0, api->get_float(r, SK_RIGID_BODY_CONFIG_FIELD_GRAVITY_FACTOR));
	TEST_ASSERT_EQUAL_UINT64(SK_JOLT_OBJECT_LAYER_MOVING, api->get_uint(r, SK_RIGID_BODY_CONFIG_FIELD_OBJECT_LAYER));
	TEST_ASSERT_EQUAL_UINT64(SK_RIGID_BODY_FLAG_SENSOR | SK_RIGID_BODY_FLAG_CONTINUOUS_COLLISION, api->get_uint(r, SK_RIGID_BODY_CONFIG_FIELD_FLAGS));
}

SK_TEST(jolt_components_repository_types_registered) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = jolt_components_test_repo();

	/* Every physics component payload type is registered with the type id the
	 * ECS component registers under (reflection listing parity). */
	const sk_resource_type_t* t = api->find_type_by_name(repo, "RigidBodyConfigResource");
	TEST_ASSERT_NOT_NULL(t);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_RIGID_BODY_CONFIG_COMPONENT_TYPE_ID, api->type_id(t)));
	TEST_ASSERT_EQUAL_UINT32(9u, api->type_field_count(t));

	TEST_ASSERT_NOT_NULL(api->find_type_by_name(repo, "RigidBodyStateResource"));
	TEST_ASSERT_NOT_NULL(api->find_type_by_name(repo, "BoxColliderResource"));
	TEST_ASSERT_NOT_NULL(api->find_type_by_name(repo, "SphereColliderResource"));
	TEST_ASSERT_NOT_NULL(api->find_type_by_name(repo, "CapsuleColliderResource"));

	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_RIGID_BODY_STATE_COMPONENT_TYPE_ID, api->type_id(api->find_type_by_name(repo, "RigidBodyStateResource"))));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_BOX_COLLIDER_COMPONENT_TYPE_ID, api->type_id(api->find_type_by_name(repo, "BoxColliderResource"))));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_SPHERE_COLLIDER_COMPONENT_TYPE_ID, api->type_id(api->find_type_by_name(repo, "SphereColliderResource"))));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_CAPSULE_COLLIDER_COMPONENT_TYPE_ID, api->type_id(api->find_type_by_name(repo, "CapsuleColliderResource"))));

	/* Field descriptor names drive the JSON wire keys (round-trip contract). */
	t = api->find_type_by_name(repo, "RigidBodyConfigResource");
	const sk_resource_field_t* f = api->type_field_at(t, 0u);
	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_STRING("MotionType", f->name);
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_FIELD_TYPE_ENUM, f->type);
	TEST_ASSERT_EQUAL_STRING("ObjectLayer", api->type_field_at(t, 7u)->name);
	TEST_ASSERT_EQUAL_STRING("Flags", api->type_field_at(t, 8u)->name);

	/* Defaults: an un-authored config maps onto Jolt body defaults, and a
	 * default state is at rest. */
	const sk_resource_type_t* state_type = api->find_type_by_name(repo, "RigidBodyStateResource");
	sk_rid_t state_rid = api->create_resource(repo, state_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(state_rid.id != 0u);
	sk_resource_object_t r = api->read(repo, state_rid);
	sk_vec3_t zero = sk_vec3_zero();
	sk_vec3_t lv = api->get_vec3(r, SK_RIGID_BODY_STATE_FIELD_LINEAR_VELOCITY);
	TEST_ASSERT_EQUAL_FLOAT(zero.x, lv.x);
	TEST_ASSERT_EQUAL_FLOAT(zero.y, lv.y);
	TEST_ASSERT_EQUAL_FLOAT(zero.z, lv.z);

	const sk_resource_type_t* box_type = api->find_type_by_name(repo, "BoxColliderResource");
	sk_rid_t box_rid = api->create_resource(repo, box_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(box_rid.id != 0u);
	r = api->read(repo, box_rid);
	sk_vec3_t he = api->get_vec3(r, SK_BOX_COLLIDER_FIELD_HALF_EXTENT);
	TEST_ASSERT_EQUAL_FLOAT(0.5f, he.x);
	TEST_ASSERT_EQUAL_FLOAT(0.5f, he.y);
	TEST_ASSERT_EQUAL_FLOAT(0.5f, he.z);

	api->destroy(repo);
}

SK_TEST(jolt_components_rigid_body_config_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = jolt_components_test_repo();

	const sk_resource_type_t* type = api->find_type_by_name(repo, "RigidBodyConfigResource");
	TEST_ASSERT_NOT_NULL(type);
	sk_uuid_t uuid;
	uuid.lo = 0x306u;
	uuid.hi = 0xbeefu;
	sk_rid_t rid = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);

	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_enum(w, SK_RIGID_BODY_CONFIG_FIELD_MOTION_TYPE, SK_JOLT_MOTION_TYPE_DYNAMIC));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_MASS, 2.5));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_FRICTION, 0.8));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_RESTITUTION, 0.3));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_LINEAR_DAMPING, 0.1));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_ANGULAR_DAMPING, 0.2));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_RIGID_BODY_CONFIG_FIELD_GRAVITY_FACTOR, 0.0));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RIGID_BODY_CONFIG_FIELD_OBJECT_LAYER, SK_JOLT_OBJECT_LAYER_MOVING));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RIGID_BODY_CONFIG_FIELD_FLAGS, SK_RIGID_BODY_FLAG_SENSOR | SK_RIGID_BODY_FLAG_CONTINUOUS_COLLISION));
	api->commit(w, NULL);

	/* Values land in the descriptor-laid-out blob. */
	jolt_components_assert_config_fields(repo, rid);

	/* Descriptor-driven deep copy (clone) preserves every field: the offsets /
	 * sizes the reflection declares match the in-memory storage. */
	sk_rid_t copy = api->clone(repo, rid, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(copy.id != 0u);
	jolt_components_assert_config_fields(repo, copy);

	api->destroy(repo);
}

SK_TEST(jolt_components_state_and_colliders_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = jolt_components_test_repo();

	/* Rigid body state: hot per-frame velocities stored via vec3 accessors. */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "RigidBodyStateResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_RIGID_BODY_STATE_FIELD_LINEAR_VELOCITY, sk_vec3(1.5f, -2.0f, 0.25f)));
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_RIGID_BODY_STATE_FIELD_ANGULAR_VELOCITY, sk_vec3(0.0f, 3.0f, 0.0f)));
		api->commit(w, NULL);
		sk_resource_object_t r = api->read(repo, rid);
		sk_vec3_t lv = api->get_vec3(r, SK_RIGID_BODY_STATE_FIELD_LINEAR_VELOCITY);
		TEST_ASSERT_EQUAL_FLOAT(1.5f, lv.x);
		TEST_ASSERT_EQUAL_FLOAT(-2.0f, lv.y);
		sk_vec3_t av = api->get_vec3(r, SK_RIGID_BODY_STATE_FIELD_ANGULAR_VELOCITY);
		TEST_ASSERT_EQUAL_FLOAT(3.0f, av.y);
	}

	/* Box collider half extent. */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "BoxColliderResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(w, SK_BOX_COLLIDER_FIELD_HALF_EXTENT, sk_vec3(0.5f, 1.0f, 2.0f)));
		api->commit(w, NULL);
		sk_resource_object_t r = api->read(repo, rid);
		sk_vec3_t he = api->get_vec3(r, SK_BOX_COLLIDER_FIELD_HALF_EXTENT);
		TEST_ASSERT_EQUAL_FLOAT(1.0f, he.y);
		TEST_ASSERT_EQUAL_FLOAT(2.0f, he.z);
	}

	/* Sphere collider radius. */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "SphereColliderResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SPHERE_COLLIDER_FIELD_RADIUS, 0.75));
		api->commit(w, NULL);
		sk_resource_object_t r = api->read(repo, rid);
		TEST_ASSERT_EQUAL_DOUBLE(0.75, api->get_float(r, SK_SPHERE_COLLIDER_FIELD_RADIUS));
	}

	/* Capsule collider half height + radius. */
	{
		const sk_resource_type_t* type = api->find_type_by_name(repo, "CapsuleColliderResource");
		sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CAPSULE_COLLIDER_FIELD_HALF_HEIGHT, 0.75));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_CAPSULE_COLLIDER_FIELD_RADIUS, 0.25));
		api->commit(w, NULL);
		sk_resource_object_t r = api->read(repo, rid);
		TEST_ASSERT_EQUAL_DOUBLE(0.75, api->get_float(r, SK_CAPSULE_COLLIDER_FIELD_HALF_HEIGHT));
		TEST_ASSERT_EQUAL_DOUBLE(0.25, api->get_float(r, SK_CAPSULE_COLLIDER_FIELD_RADIUS));
	}

	api->destroy(repo);
}

#endif /* SK_TESTS */
