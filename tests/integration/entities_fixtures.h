#pragma once

/**
 * @file entities_fixtures.h
 * @brief Scene / entity resource fixtures (APX-301): load tests/data/entities/
 *        package JSON documents into a repository and spawn them into a world.
 *
 * Every fixture is a self-contained sk.resource_package JSON document (the
 * engine's own resource serialization format) whose root is an
 * EntityResource or a SceneResource carrying built-in component payloads
 * (transform / camera / light / static tag). Loading requires the built-in
 * asset + component payload types to be registered first; this harness does
 * that (sk_resource_asset_builtins_register_types) and returns the root RID.
 *
 * Spawning and assertion helpers live in entities_fixtures.c. Available only
 * in SK_TESTS builds (linked into sk-integration-tests).
 */

#include "common.h"
#include "entities.h"
#include "repository.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SK_TESTS

/** Which on-disk scene/entity fixture to load (tests/data/entities/). */
typedef enum sk_entities_fixture_id_t {
	SK_ENTITIES_FIXTURE_SINGLE_ENTITY = 0,			/* one entity, 4 components */
	SK_ENTITIES_FIXTURE_PARENT_CHILDREN = 1,		/* parent + children + grandchildren */
	SK_ENTITIES_FIXTURE_SCENE_MULTIPLE_ROOTS = 2,	/* scene with 3 roots */
	SK_ENTITIES_FIXTURE_UNREGISTERED_COMPONENT = 3, /* negative: unregistered component type */
	SK_ENTITIES_FIXTURE_COUNT = 4
} sk_entities_fixture_id_t;

/**
 * Catalog entry for one fixture (paths relative to the fixture root unless
 * joined with sk_entities_fixture_dir).
 */
typedef struct sk_entities_fixture_desc_t {
	sk_entities_fixture_id_t id;
	const_chr_t fixture_id;	   /* stable string id, e.g. "entity_single" */
	const_chr_t resource_file; /* package JSON document name */
	const_chr_t root_type;	   /* registered type name of the package root */
	u32 node_count;			   /* authored entities in the tree (incl. root) */
	i32 negative;			   /* 1 = spawning this fixture must fail */
} sk_entities_fixture_desc_t;

/**
 * Resolve the absolute path of tests/data/entities/ (shared resolution with
 * the resource_object fixture harness).
 * @return 0 on success, non-zero if no candidate directory exists.
 */
i32 sk_entities_fixture_dir(char* out, u32 out_cap);

/**
 * Return the static catalog entry for @p id.
 * @param id Fixture id; must be in range [0, SK_ENTITIES_FIXTURE_COUNT).
 * @return Non-NULL descriptor.
 */
const sk_entities_fixture_desc_t* sk_entities_fixture_desc(sk_entities_fixture_id_t id);

/**
 * Build an absolute path to a file under the fixture root (e.g. the manifest).
 */
i32 sk_entities_fixture_path(const_chr_t relative_name, char* out, u32 out_cap);

/**
 * Absolute path to the package JSON document for @p id.
 */
i32 sk_entities_fixture_resource_path(sk_entities_fixture_id_t id, char* out, u32 out_cap);

/**
 * Register the built-in asset + component payload types into @p repository
 * (idempotent on a fresh repository) and deserialize the fixture package
 * document, returning the root RID.
 *
 * @param repository Repository to load into (must not be NULL).
 * @param id         Fixture id.
 * @param out_root   Receives the root resource RID (must not be NULL).
 * @return 0 on success, non-zero on registration / path / parse failure.
 */
i32 sk_entities_fixture_load(sk_repository_t* repository, sk_entities_fixture_id_t id, sk_rid_t* out_root);

#endif /* SK_TESTS */

#ifdef __cplusplus
}
#endif
