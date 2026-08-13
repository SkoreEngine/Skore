/*
 * entities_fixtures.c — scene/entity resource fixtures + ECS spawn
 * integration tests (APX-301).
 *
 * Loads on-disk sk.resource_package fixtures from tests/data/entities/
 * (entity_resource / scene_resource trees authored with built-in component
 * payloads) into a repository, spawns them with world_spawn_from_asset, and
 * asserts that the resulting world matches the authored asset: entity count,
 * per-signature structure (the tree's parent/child ownership), and component
 * field values. A negative fixture references a component type that is a
 * registered repository payload type but NOT a registered ECS component and
 * must fail to spawn.
 *
 * The ECS intentionally keeps no Parent component (mapping contract §3.2), so
 * parent/child structure is verified by counting authored nodes per subtree in
 * the repository and matching per-signature entity counts after spawning.
 */

#include "entities_fixtures.h"

#include "allocator.h"
#include "app.h"
#include "entities.h"
#include "entities_builtins.h"
#include "filesystem.h"
#include "path.h"
#include "resource_asset_builtins.h"
#include "resource_assets_types.h"
#include "resource_object_fixtures.h" /* sk_resource_fixture_dir_for_subdir */
#include "resource_serialize.h"
#include "test.h"

#include <math.h>
#include <string.h>

#ifdef SK_TESTS

/* ------------------------------------------------------------------ */
/*  Fixture catalog + path resolution (shared resolver)               */
/* ------------------------------------------------------------------ */

static const sk_entities_fixture_desc_t entities_fixture_catalog[SK_ENTITIES_FIXTURE_COUNT] = {
	{SK_ENTITIES_FIXTURE_SINGLE_ENTITY, "entity_single", "entity_single.json", "EntityResource", 1u, 0},
	{SK_ENTITIES_FIXTURE_PARENT_CHILDREN, "entity_parent_children", "entity_parent_children.json", "EntityResource", 5u, 0},
	{SK_ENTITIES_FIXTURE_SCENE_MULTIPLE_ROOTS, "scene_multiple_roots", "scene_multiple_roots.json", "SceneResource", 6u, 0},
	{SK_ENTITIES_FIXTURE_UNREGISTERED_COMPONENT, "entity_unregistered_component", "entity_unregistered_component.json", "EntityResource", 1u, 1},
};

const sk_entities_fixture_desc_t* sk_entities_fixture_desc(sk_entities_fixture_id_t id) {
	return &entities_fixture_catalog[(u32)id < (u32)SK_ENTITIES_FIXTURE_COUNT ? (u32)id : 0u];
}

i32 sk_entities_fixture_dir(char* out, u32 out_cap) {
	return sk_resource_fixture_dir_for_subdir("entities", out, out_cap);
}

i32 sk_entities_fixture_path(const_chr_t relative_name, char* out, u32 out_cap) {
	char root[SK_FS_PATH_MAX];
	if (relative_name == NULL || relative_name[0] == '\0') {
		return -1;
	}
	if (sk_entities_fixture_dir(root, (u32)sizeof(root)) != 0) {
		return -1;
	}
	return sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(relative_name), out, out_cap) < 0 ? -1 : 0;
}

i32 sk_entities_fixture_resource_path(sk_entities_fixture_id_t id, char* out, u32 out_cap) {
	const sk_entities_fixture_desc_t* desc = sk_entities_fixture_desc(id);
	return sk_entities_fixture_path(desc->resource_file, out, out_cap);
}

i32 sk_entities_fixture_load(sk_repository_t* repository, sk_entities_fixture_id_t id, sk_rid_t* out_root) {
	char path[SK_FS_PATH_MAX];
	if (repository == NULL || out_root == NULL) {
		return -1;
	}
	*out_root = SK_RID_ZERO;
	/* Asset + component payload types (EntityResource / SceneResource /
	 * TransformResource / CameraResource / LightResource / StaticTagResource);
	 * register once per repository. */
	if (sk_resource_assets_register_types(repository) != 0) {
		return -1;
	}
	if (sk_resource_asset_builtins_register_types(repository) != 0) {
		return -1;
	}
	if (sk_entities_fixture_resource_path(id, path, (u32)sizeof(path)) != 0) {
		return -1;
	}
	return sk_resource_deserialize_package_json_from_file(repository, path, out_root);
}

/* ------------------------------------------------------------------ */
/*  Test environment                                                  */
/* ------------------------------------------------------------------ */

typedef struct entities_fixture_env_t {
	sk_app_context_t* app;
	const sk_entities_api_t* ecs;
	sk_repository_t* repository;
	sk_world_t* world;
} entities_fixture_env_t;

static void entities_fixture_env_setup(entities_fixture_env_t* env) {
	memset(env, 0, sizeof(*env));
	/* sk_app_init auto-loads the plugin DLLs from {app_folder}/plugins; the
	 * sk-entities plugin registers its API table + the built-in components. */
	env->app = sk_app_init(0, NULL);
	TEST_ASSERT_NOT_NULL(env->app);
	if (env->app == NULL) {
		return;
	}
	env->ecs = (const sk_entities_api_t*)sk_app_api()->get_api(env->app, SK_ENTITIES_API_TYPE_ID);
	TEST_ASSERT_NOT_NULL(env->ecs);
	env->repository = sk_repository_api()->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(env->repository);
	env->world = env->ecs->world_create();
	TEST_ASSERT_NOT_NULL(env->world);
}

static void entities_fixture_env_teardown(entities_fixture_env_t* env) {
	if (env->world != NULL) {
		env->ecs->world_destroy(env->world);
	}
	if (env->repository != NULL) {
		sk_repository_api()->destroy(env->repository);
	}
	if (env->app != NULL) {
		sk_app_destroy(env->app);
	}
	memset(env, 0, sizeof(*env));
}

/* Guard for the setup-aborted case so the test body never dereferences NULL. */
static i32 entities_fixture_env_ok(const entities_fixture_env_t* env) {
	return (env->app != NULL && env->ecs != NULL && env->repository != NULL && env->world != NULL) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Authored-tree helpers                                             */
/* ------------------------------------------------------------------ */

/* Count entity_resource nodes reachable from @p rid (an EntityResource tree
 * or a SceneResource Roots list). Iterative (no recursion) with an explicit
 * stack so the fixture harness stays clang-tidy clean; fixtures are
 * cycle-free by authoring and far below the depth cap. */
static u32 entities_fixture_count_nodes(sk_repository_t* repository, sk_rid_t root) {
	const sk_repository_api_t* repo = sk_repository_api();
	enum { MAX_STACK = 64u };
	sk_rid_t stack[MAX_STACK];
	u32 sp = 0u;
	u32 count = 0u;
	if (root.id != 0u) {
		stack[sp++] = root;
	}
	while (sp > 0u) {
		const sk_rid_t rid = stack[--sp];
		const sk_resource_type_t* type = repo->resource_type(repository, rid);
		if (type == NULL) {
			continue;
		}
		sk_resource_object_t view = repo->read(repository, rid);
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			continue;
		}
		const sk_type_id_t type_id = repo->type_id(type);
		u32 item_count = 0u;
		const sk_rid_t* items = NULL;
		if (SK_TYPE_ID_EQ(type_id, SK_SCENE_RESOURCE_TYPE_ID)) {
			items = repo->get_subobject_list(view, SK_SCENE_RESOURCE_FIELD_ROOTS, &item_count);
		} else if (SK_TYPE_ID_EQ(type_id, SK_ENTITY_RESOURCE_TYPE_ID)) {
			count += 1u; /* the entity itself */
			items = repo->get_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &item_count);
		} else {
			continue;
		}
		for (u32 i = 0u; i < item_count && sp < MAX_STACK; ++i) {
			if (items[i].id != 0u) {
				stack[sp++] = items[i];
			}
		}
	}
	return count;
}

/* Number of live entities whose signature contains every id in @p required. */
static u32 entities_fixture_query_count(const sk_entities_api_t* ecs, sk_world_t* world, const sk_type_id_t* required, u32 required_count) {
	const sk_query_desc_t desc = {required, required_count, NULL, 0u, NULL, 0u};
	sk_query_t* query = ecs->world_query_create(world, &desc);
	if (query == NULL) {
		return 0u;
	}
	u32 count = 0u;
	SK_ECS_QUERY_EACH(ecs, query, it) {
		count += 1u;
	}
	return count;
}

/* Number of spawned transforms whose authored position is within eps of @p p. */
static u32 entities_fixture_count_transforms_at(const sk_entities_api_t* ecs, sk_world_t* world, f32 x, f32 y, f32 z) {
	const sk_type_id_t required = SK_TRANSFORM_COMPONENT_TYPE_ID;
	const sk_query_desc_t desc = {&required, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* query = ecs->world_query_create(world, &desc);
	u32 count = 0u;
	SK_ECS_QUERY_EACH(ecs, query, it) {
		const sk_transform_t* transform = (const sk_transform_t*)SK_ECS_ITER_AT(it, 1, sk_transform_t);
		if (transform != NULL && fabsf(transform->position.x - x) < 1.0e-5f && fabsf(transform->position.y - y) < 1.0e-5f && fabsf(transform->position.z - z) < 1.0e-5f) {
			count += 1u;
		}
	}
	return count;
}

/* Number of spawned transforms whose authored scale is within eps of @p s. */
static u32 entities_fixture_count_transforms_scaled(const sk_entities_api_t* ecs, sk_world_t* world, f32 x, f32 y, f32 z) {
	const sk_type_id_t required = SK_TRANSFORM_COMPONENT_TYPE_ID;
	const sk_query_desc_t desc = {&required, 1u, NULL, 0u, NULL, 0u};
	sk_query_t* query = ecs->world_query_create(world, &desc);
	u32 count = 0u;
	SK_ECS_QUERY_EACH(ecs, query, it) {
		const sk_transform_t* transform = (const sk_transform_t*)SK_ECS_ITER_AT(it, 1, sk_transform_t);
		if (transform != NULL && fabsf(transform->scale.x - x) < 1.0e-5f && fabsf(transform->scale.y - y) < 1.0e-5f && fabsf(transform->scale.z - z) < 1.0e-5f) {
			count += 1u;
		}
	}
	return count;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

SK_TEST(entities_fixture_catalog_and_node_counts) {
	entities_fixture_env_t env;
	entities_fixture_env_setup(&env);
	if (!entities_fixture_env_ok(&env)) {
		entities_fixture_env_teardown(&env);
		return;
	}
	const sk_repository_api_t* repo = sk_repository_api();

	char dir[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_dir(dir, (u32)sizeof(dir)));
	TEST_ASSERT_TRUE(dir[0] != '\0');
	char manifest[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_path("manifest.json", manifest, (u32)sizeof(manifest)));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(manifest));

	/* Every catalog entry loads as its declared root type and its authored
	 * node count (manifest truth) matches the repository tree. */
	for (u32 i = 0u; i < (u32)SK_ENTITIES_FIXTURE_COUNT; ++i) {
		const sk_entities_fixture_desc_t* desc = sk_entities_fixture_desc((sk_entities_fixture_id_t)i);
		TEST_ASSERT_NOT_NULL(desc);
		TEST_ASSERT_EQUAL_INT((int)i, (int)desc->id);

		char path[SK_FS_PATH_MAX];
		TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_resource_path((sk_entities_fixture_id_t)i, path, (u32)sizeof(path)));
		TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(path));

		sk_repository_t* fixture_repo = repo->create(sk_allocator_default());
		TEST_ASSERT_NOT_NULL(fixture_repo);
		sk_rid_t root = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_load(fixture_repo, (sk_entities_fixture_id_t)i, &root));
		TEST_ASSERT_TRUE(root.id != 0u);
		const sk_resource_type_t* root_type = repo->resource_type(fixture_repo, root);
		TEST_ASSERT_NOT_NULL(root_type);
		TEST_ASSERT_EQUAL_STRING(desc->root_type, repo->type_name(root_type));
		TEST_ASSERT_EQUAL_UINT32(desc->node_count, entities_fixture_count_nodes(fixture_repo, root));
		repo->destroy(fixture_repo);
	}

	entities_fixture_env_teardown(&env);
}

SK_TEST(entities_fixture_single_entity_spawns) {
	entities_fixture_env_t env;
	entities_fixture_env_setup(&env);
	if (!entities_fixture_env_ok(&env)) {
		entities_fixture_env_teardown(&env);
		return;
	}
	const sk_entities_api_t* ecs = env.ecs;

	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_load(env.repository, SK_ENTITIES_FIXTURE_SINGLE_ENTITY, &root));
	TEST_ASSERT_TRUE(root.id != 0u);
	/* Authored asset: a single entity node. */
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_nodes(env.repository, root));

	sk_entity_t spawned = ecs->world_spawn_from_asset(env.world, env.repository, root);
	TEST_ASSERT_TRUE(sk_entity_is_valid(spawned));
	TEST_ASSERT_EQUAL_UINT32(1u, ecs->world_count(env.world));

	/* Component signature: transform + camera + light + static tag. */
	TEST_ASSERT_TRUE(ecs->world_has_component(env.world, spawned, SK_TRANSFORM_COMPONENT_TYPE_ID));
	TEST_ASSERT_TRUE(ecs->world_has_component(env.world, spawned, SK_CAMERA_COMPONENT_TYPE_ID));
	TEST_ASSERT_TRUE(ecs->world_has_component(env.world, spawned, SK_LIGHT_COMPONENT_TYPE_ID));
	TEST_ASSERT_TRUE(ecs->world_has_component(env.world, spawned, SK_STATIC_TAG_COMPONENT_TYPE_ID));

	/* Component field values match the authored component resources. */
	const sk_transform_t* transform = (const sk_transform_t*)ecs->world_component(env.world, spawned, SK_TRANSFORM_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(transform);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, transform->position.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 2.0f, transform->position.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 3.0f, transform->position.z);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.0f, transform->rotation.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.0f, transform->rotation.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.0f, transform->rotation.z);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, transform->rotation.w);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 2.0f, transform->scale.x);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 3.0f, transform->scale.y);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 4.0f, transform->scale.z);

	const sk_camera_t* camera = (const sk_camera_t*)ecs->world_component(env.world, spawned, SK_CAMERA_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(camera);
	TEST_ASSERT_EQUAL_INT((int)SK_CAMERA_PROJECTION_PERSPECTIVE, (int)camera->projection);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0471976f, camera->fov_y); /* 60 deg */
	TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.1f, camera->near_z);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1000.0f, camera->far_z);

	const sk_light_t* light = (const sk_light_t*)ecs->world_component(env.world, spawned, SK_LIGHT_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(light);
	TEST_ASSERT_EQUAL_INT((int)SK_LIGHT_TYPE_POINT, (int)light->type);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.r);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.8f, light->color.g);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.5f, light->color.b);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.a);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 2.5f, light->intensity);
	TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 15.0f, light->range);

	const sk_static_tag_t* tag = (const sk_static_tag_t*)ecs->world_component(env.world, spawned, SK_STATIC_TAG_COMPONENT_TYPE_ID);
	TEST_ASSERT_NOT_NULL(tag);
	TEST_ASSERT_EQUAL_UINT8(0u, tag->marker);

	entities_fixture_env_teardown(&env);
}

SK_TEST(entities_fixture_parent_children_spawns) {
	entities_fixture_env_t env;
	entities_fixture_env_setup(&env);
	if (!entities_fixture_env_ok(&env)) {
		entities_fixture_env_teardown(&env);
		return;
	}
	const sk_entities_api_t* ecs = env.ecs;
	const sk_repository_api_t* repo = sk_repository_api();

	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_load(env.repository, SK_ENTITIES_FIXTURE_PARENT_CHILDREN, &root));
	TEST_ASSERT_TRUE(root.id != 0u);
	/* Authored asset: parent + 2 children + 2 grandchildren = 5 nodes. */
	TEST_ASSERT_EQUAL_UINT32(5u, entities_fixture_count_nodes(env.repository, root));

	sk_entity_t spawned = ecs->world_spawn_from_asset(env.world, env.repository, root);
	TEST_ASSERT_TRUE(sk_entity_is_valid(spawned));
	/* Entity count matches the authored tree exactly: every descendant of the
	 * parent (two levels of children) was instantiated. */
	TEST_ASSERT_EQUAL_UINT32(5u, ecs->world_count(env.world));

	/* Parent/child structure: per-signature entity counts match the authored
	 * per-node component sets (parent: transform+camera, ChildA: transform+
	 * light, GrandChildA1 + ChildB: transform+static, GrandChildA2:
	 * transform+camera). */
	const sk_type_id_t tc[2] = {SK_TRANSFORM_COMPONENT_TYPE_ID, SK_CAMERA_COMPONENT_TYPE_ID};
	const sk_type_id_t tl[2] = {SK_TRANSFORM_COMPONENT_TYPE_ID, SK_LIGHT_COMPONENT_TYPE_ID};
	const sk_type_id_t ts[2] = {SK_TRANSFORM_COMPONENT_TYPE_ID, SK_STATIC_TAG_COMPONENT_TYPE_ID};
	TEST_ASSERT_EQUAL_UINT32(5u, entities_fixture_query_count(ecs, env.world, &tc[0], 1u));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_fixture_query_count(ecs, env.world, tc, 2u));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_query_count(ecs, env.world, tl, 2u));
	TEST_ASSERT_EQUAL_UINT32(2u, entities_fixture_query_count(ecs, env.world, ts, 2u));

	/* Transform field values: every authored position lands on exactly one
	 * spawned entity (positions are distinct across the tree). */
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 1.0f, 0.0f, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 10.0f, 0.0f, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 10.0f, 0.0f, 5.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 10.0f, 0.0f, -5.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 20.0f, 0.0f, 0.0f));
	/* Scale multiset: parent (2,1,1) and GrandChildA1 (0.5,0.5,0.5) are the
	 * only non-unit scales; the other three nodes stay (1,1,1). */
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_scaled(ecs, env.world, 2.0f, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_scaled(ecs, env.world, 0.5f, 0.5f, 0.5f));
	TEST_ASSERT_EQUAL_UINT32(3u, entities_fixture_count_transforms_scaled(ecs, env.world, 1.0f, 1.0f, 1.0f));

	/* Camera field values (2 authored cameras). */
	{
		const sk_type_id_t cam_id = SK_CAMERA_COMPONENT_TYPE_ID;
		const sk_query_desc_t desc = {&cam_id, 1u, NULL, 0u, NULL, 0u};
		sk_query_t* query = ecs->world_query_create(env.world, &desc);
		u32 persp = 0u;
		u32 ortho = 0u;
		u32 fov_12 = 0u;
		u32 fov_08 = 0u;
		SK_ECS_QUERY_EACH(ecs, query, it) {
			const sk_camera_t* camera = (const sk_camera_t*)SK_ECS_ITER_AT(it, 1, sk_camera_t);
			TEST_ASSERT_NOT_NULL(camera);
			if (camera->projection == SK_CAMERA_PROJECTION_PERSPECTIVE) {
				persp += 1u;
			} else if (camera->projection == SK_CAMERA_PROJECTION_ORTHOGRAPHIC) {
				ortho += 1u;
			}
			if (fabsf(camera->fov_y - 1.2f) < 1.0e-5f) {
				fov_12 += 1u;
			}
			if (fabsf(camera->fov_y - 0.8f) < 1.0e-5f) {
				fov_08 += 1u;
			}
		}
		TEST_ASSERT_EQUAL_UINT32(1u, persp);
		TEST_ASSERT_EQUAL_UINT32(1u, ortho);
		TEST_ASSERT_EQUAL_UINT32(1u, fov_12);
		TEST_ASSERT_EQUAL_UINT32(1u, fov_08);
	}

	/* Light field values (1 authored light on ChildA). */
	{
		const sk_type_id_t light_id = SK_LIGHT_COMPONENT_TYPE_ID;
		const sk_query_desc_t desc = {&light_id, 1u, NULL, 0u, NULL, 0u};
		sk_query_t* query = ecs->world_query_create(env.world, &desc);
		u32 lights = 0u;
		SK_ECS_QUERY_EACH(ecs, query, it) {
			const sk_light_t* light = (const sk_light_t*)SK_ECS_ITER_AT(it, 1, sk_light_t);
			TEST_ASSERT_NOT_NULL(light);
			TEST_ASSERT_EQUAL_INT((int)SK_LIGHT_TYPE_DIRECTIONAL, (int)light->type);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.r);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.g);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.b);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.a);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->intensity);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 100.0f, light->range);
			lights += 1u;
		}
		TEST_ASSERT_EQUAL_UINT32(1u, lights);
	}

	/* A child subtree is self-contained: spawning ChildA alone (its own two
	 * grandchildren) yields exactly 3 entities in a fresh world. */
	{
		sk_resource_object_t view = repo->read(env.repository, root);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		u32 child_count = 0u;
		const sk_rid_t* children = repo->get_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_CHILDREN, &child_count);
		TEST_ASSERT_EQUAL_UINT32(2u, child_count);
		TEST_ASSERT_EQUAL_UINT32(3u, entities_fixture_count_nodes(env.repository, children[0]));

		sk_world_t* subtree_world = ecs->world_create();
		TEST_ASSERT_NOT_NULL(subtree_world);
		sk_entity_t subtree_root = ecs->world_spawn_from_asset(subtree_world, env.repository, children[0]);
		TEST_ASSERT_TRUE(sk_entity_is_valid(subtree_root));
		TEST_ASSERT_EQUAL_UINT32(3u, ecs->world_count(subtree_world));
		ecs->world_destroy(subtree_world);
	}

	entities_fixture_env_teardown(&env);
}

SK_TEST(entities_fixture_scene_multiple_roots_spawns) {
	entities_fixture_env_t env;
	entities_fixture_env_setup(&env);
	if (!entities_fixture_env_ok(&env)) {
		entities_fixture_env_teardown(&env);
		return;
	}
	const sk_entities_api_t* ecs = env.ecs;

	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_load(env.repository, SK_ENTITIES_FIXTURE_SCENE_MULTIPLE_ROOTS, &root));
	TEST_ASSERT_TRUE(root.id != 0u);
	/* Authored asset: 3 roots + RootA's child + RootC's child + grandchild. */
	TEST_ASSERT_EQUAL_UINT32(6u, entities_fixture_count_nodes(env.repository, root));

	sk_entity_t first = ecs->world_spawn_from_asset(env.world, env.repository, root);
	TEST_ASSERT_TRUE(sk_entity_is_valid(first));
	TEST_ASSERT_EQUAL_UINT32(6u, ecs->world_count(env.world));

	/* Every root + descendant is present: transform on all 6 entities. */
	const sk_type_id_t transform_id = SK_TRANSFORM_COMPONENT_TYPE_ID;
	TEST_ASSERT_EQUAL_UINT32(6u, entities_fixture_query_count(ecs, env.world, &transform_id, 1u));
	const sk_type_id_t camera_id = SK_CAMERA_COMPONENT_TYPE_ID;
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_query_count(ecs, env.world, &camera_id, 1u));
	const sk_type_id_t light_id = SK_LIGHT_COMPONENT_TYPE_ID;
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_query_count(ecs, env.world, &light_id, 1u));
	const sk_type_id_t static_id = SK_STATIC_TAG_COMPONENT_TYPE_ID;
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_query_count(ecs, env.world, &static_id, 1u));

	/* Authored positions across the scene all appear exactly once. */
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 0.0f, 0.0f, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 0.0f, 0.0f, 1.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 5.0f, 0.0f, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 10.0f, 0.0f, 0.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 10.0f, 0.0f, 2.0f));
	TEST_ASSERT_EQUAL_UINT32(1u, entities_fixture_count_transforms_at(ecs, env.world, 10.0f, 0.0f, 4.0f));

	/* RootB's light: spot, authored color/intensity/range. */
	{
		const sk_query_desc_t desc = {&light_id, 1u, NULL, 0u, NULL, 0u};
		sk_query_t* query = ecs->world_query_create(env.world, &desc);
		u32 lights = 0u;
		SK_ECS_QUERY_EACH(ecs, query, it) {
			const sk_light_t* light = (const sk_light_t*)SK_ECS_ITER_AT(it, 1, sk_light_t);
			TEST_ASSERT_NOT_NULL(light);
			TEST_ASSERT_EQUAL_INT((int)SK_LIGHT_TYPE_SPOT, (int)light->type);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.2f, light->color.r);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.5f, light->color.g);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.b);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, light->color.a);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 4.0f, light->intensity);
			TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 8.0f, light->range);
			lights += 1u;
		}
		TEST_ASSERT_EQUAL_UINT32(1u, lights);
	}

	/* Each scene root is an independent tree: RootA + its child spawn as 2
	 * entities from a fresh world. */
	{
		const sk_repository_api_t* repo = sk_repository_api();
		sk_resource_object_t view = repo->read(env.repository, root);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		u32 root_count = 0u;
		const sk_rid_t* roots = repo->get_subobject_list(view, SK_SCENE_RESOURCE_FIELD_ROOTS, &root_count);
		TEST_ASSERT_EQUAL_UINT32(3u, root_count);
		TEST_ASSERT_EQUAL_UINT32(2u, entities_fixture_count_nodes(env.repository, roots[0]));

		sk_world_t* root_world = ecs->world_create();
		TEST_ASSERT_NOT_NULL(root_world);
		sk_entity_t root_a = ecs->world_spawn_from_asset(root_world, env.repository, roots[0]);
		TEST_ASSERT_TRUE(sk_entity_is_valid(root_a));
		TEST_ASSERT_EQUAL_UINT32(2u, ecs->world_count(root_world));
		ecs->world_destroy(root_world);
	}

	/* Spawning the scene again is deterministic: a second pass duplicates the
	 * whole tree (6 more entities). */
	sk_entity_t again = ecs->world_spawn_from_asset(env.world, env.repository, root);
	TEST_ASSERT_TRUE(sk_entity_is_valid(again));
	TEST_ASSERT_EQUAL_UINT32(12u, ecs->world_count(env.world));

	entities_fixture_env_teardown(&env);
}

SK_TEST(entities_fixture_unregistered_component_fails) {
	entities_fixture_env_t env;
	entities_fixture_env_setup(&env);
	if (!entities_fixture_env_ok(&env)) {
		entities_fixture_env_teardown(&env);
		return;
	}
	const sk_entities_api_t* ecs = env.ecs;
	const sk_repository_api_t* repo = sk_repository_api();

	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_entities_fixture_load(env.repository, SK_ENTITIES_FIXTURE_UNREGISTERED_COMPONENT, &root));
	TEST_ASSERT_TRUE(root.id != 0u);

	/* The first Components entry is a MeshResource: a registered repository
	 * payload type (the mesh renderer's REFERENCE target type) that is NOT a
	 * registered ECS component. */
	sk_resource_object_t view = repo->read(env.repository, root);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	u32 comp_count = 0u;
	const sk_rid_t* comps = repo->get_subobject_list(view, SK_ENTITY_RESOURCE_FIELD_COMPONENTS, &comp_count);
	TEST_ASSERT_EQUAL_UINT32(2u, comp_count);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, comps[0]), SK_MESH_RESOURCE_TYPE_ID));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_entity_component_type_id(env.repository, comps[1]), SK_TRANSFORM_COMPONENT_TYPE_ID));

	/* mesh_resource is not an ECS component: component_info must fail. */
	sk_component_info_t info = {0};
	TEST_ASSERT_EQUAL_INT(-1, ecs->component_info(SK_MESH_RESOURCE_TYPE_ID, &info));

	/* Spawning the entity fails and leaves no half-built entity behind. */
	sk_entity_t spawned = ecs->world_spawn_from_asset(env.world, env.repository, root);
	TEST_ASSERT_FALSE(sk_entity_is_valid(spawned));
	TEST_ASSERT_EQUAL_UINT32(0u, ecs->world_count(env.world));

	entities_fixture_env_teardown(&env);
}

#endif /* SK_TESTS */
