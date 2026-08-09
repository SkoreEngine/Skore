/**
 * @file entities.c
 * @brief ECS module implementation (skeleton) + in-source unit tests.
 *
 * Scaffold for the archetype + 16 KiB chunk ECS. Component identity is wired
 * through sk_type_id_t: a module-level registry maps each component type id to
 * its layout (size/align/name). World / archetype / chunk / query / system /
 * entitycommands surfaces are declared in entities.h and built out by
 * follow-up work on top of this identity layer.
 */

#include "entities.h"

#include "app.h"

#include <stddef.h>

enum { SK_ECS_MAX_COMPONENT_TYPES = 256u };

/*
 * Component registry keyed by sk_type_id_t (main-thread ownership).
 * Fixed-capacity module state, filled at load/init; no heap in the path.
 */
static sk_component_info_t ecs_component_registry[SK_ECS_MAX_COMPONENT_TYPES];
static u32 ecs_component_count = 0u;

/* ---- API impl (table-only; no public free-function mirrors) ---- */

static i32 register_component_impl(sk_type_id_t type_id, u32 size, u32 align, const_chr_t name) {
	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO) || size == 0u || align == 0u) {
		return -3;
	}

	for (u32 i = 0u; i < ecs_component_count; ++i) {
		if (SK_TYPE_ID_EQ(ecs_component_registry[i].type_id, type_id)) {
			if (ecs_component_registry[i].size != size || ecs_component_registry[i].align != align) {
				return -1;
			}
			return 0;
		}
	}

	if (ecs_component_count >= (u32)SK_ECS_MAX_COMPONENT_TYPES) {
		return -2;
	}

	ecs_component_registry[ecs_component_count].type_id = type_id;
	ecs_component_registry[ecs_component_count].size = size;
	ecs_component_registry[ecs_component_count].align = align;
	ecs_component_registry[ecs_component_count].name = name;
	ecs_component_count += 1u;
	return 0;
}

static i32 component_info_impl(sk_type_id_t type_id, sk_component_info_t* out) {
	for (u32 i = 0u; i < ecs_component_count; ++i) {
		if (SK_TYPE_ID_EQ(ecs_component_registry[i].type_id, type_id)) {
			if (out != NULL) {
				*out = ecs_component_registry[i];
			}
			return 0;
		}
	}
	return -1;
}

static const sk_entities_api_t entities_api = {
	register_component_impl,
	component_info_impl,
};

/**
 * Register the ECS API on the app context.
 * Called from sk_plugin_entry_point; not part of the public host surface.
 */
void sk_entities_init(sk_app_context_t* context, const sk_app_api_t* app_api);

void sk_entities_init(sk_app_context_t* context, const sk_app_api_t* app_api) {
	app_api->set_api(context, SK_ENTITIES_API_TYPE_ID, &entities_api);
}

#ifdef SK_TESTS
#include "test.h"

/*
 * Unit coverage for the ECS skeleton: type-id wiring, the entity handle, and
 * the sk_type_id-keyed component registry. World / archetype / chunk / query /
 * system / entitycommands coverage lands with those surfaces.
 */

SK_TEST(entities_api_table_is_complete) {
	TEST_ASSERT_NOT_NULL(entities_api.register_component);
	TEST_ASSERT_NOT_NULL(entities_api.component_info);
}

SK_TEST(entities_api_type_id_nonzero) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_ENTITIES_API_TYPE_ID, SK_TYPE_ID_ZERO));
}

SK_TEST(entities_entity_component_id_distinct) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, SK_TYPE_ID_ZERO));
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_ECS_ENTITY_COMPONENT_ID, SK_ENTITIES_API_TYPE_ID));
}

SK_TEST(entities_chunk_size_is_16kib) {
	TEST_ASSERT_EQUAL_UINT32(16384u, SK_ECS_CHUNK_SIZE);
}

SK_TEST(entities_entity_handle) {
	TEST_ASSERT_EQUAL_size_t(8u, sizeof(sk_entity_t));
	TEST_ASSERT_FALSE(sk_entity_is_valid(SK_ENTITY_INVALID));

	sk_entity_t e = {1u, 0u};
	TEST_ASSERT_TRUE(sk_entity_is_valid(e));
	TEST_ASSERT_TRUE(sk_entity_eq(e, (sk_entity_t){1u, 0u}));
	TEST_ASSERT_FALSE(sk_entity_eq(e, (sk_entity_t){1u, 1u}));
	TEST_ASSERT_FALSE(sk_entity_eq(e, (sk_entity_t){2u, 0u}));
	TEST_ASSERT_FALSE(sk_entity_eq(e, SK_ENTITY_INVALID));
}

SK_TEST(entities_register_component_roundtrip) {
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.roundtrip", 0x0102030405060708ULL, 0x1112131415161718ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 12u, 4u, "roundtrip"));

	sk_component_info_t info;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.component_info(id, &info));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, info.type_id));
	TEST_ASSERT_EQUAL_UINT32(12u, info.size);
	TEST_ASSERT_EQUAL_UINT32(4u, info.align);
	TEST_ASSERT_EQUAL_STRING("roundtrip", info.name);
}

SK_TEST(entities_register_component_idempotent) {
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.idem", 0x2222222222222222ULL, 0x3333333333333333ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "idem"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "idem-again"));
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, NULL));
}

SK_TEST(entities_register_component_conflict) {
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.conflict", 0x4444444444444444ULL, 0x5555555555555555ULL);
	TEST_ASSERT_EQUAL_INT32(0, entities_api.register_component(id, 8u, 8u, "conflict"));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.register_component(id, 16u, 8u, "conflict-other"));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.register_component(id, 8u, 16u, "conflict-align"));

	sk_component_info_t info;
	TEST_ASSERT_EQUAL_INT32(0, entities_api.component_info(id, &info));
	TEST_ASSERT_EQUAL_UINT32(8u, info.size);
	TEST_ASSERT_EQUAL_UINT32(8u, info.align);
}

SK_TEST(entities_register_component_invalid) {
	TEST_ASSERT_EQUAL_INT32(-3, entities_api.register_component(SK_TYPE_ID_ZERO, 8u, 8u, "bad-id"));

	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.invalid", 0x6666666666666666ULL, 0x7777777777777777ULL);
	TEST_ASSERT_EQUAL_INT32(-3, entities_api.register_component(id, 0u, 8u, "bad-size"));
	TEST_ASSERT_EQUAL_INT32(-3, entities_api.register_component(id, 8u, 0u, "bad-align"));
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.component_info(id, NULL));
}

SK_TEST(entities_component_info_missing) {
	sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.missing", 0x8888888888888888ULL, 0x9999999999999999ULL);
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.component_info(id, NULL));

	sk_component_info_t info = {0};
	TEST_ASSERT_EQUAL_INT32(-1, entities_api.component_info(id, &info));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(SK_TYPE_ID_ZERO, info.type_id));
}

/*
 * Exhausts the component registry (SK_ECS_MAX_COMPONENT_TYPES entries).
 * Keep this test last in the file: it fills the module registry for the
 * remainder of the plugin test run.
 */
SK_TEST(entities_register_component_capacity) {
	u32 accepted = 0u;
	i32 last = 0;
	for (u32 i = 0u; i < (u32)SK_ECS_MAX_COMPONENT_TYPES + 4u; ++i) {
		sk_type_id_t id = SK_TYPE_ID("sk.test.ecs.capacity", (u64)(i + 1000u), 0xAAAAAAAAAAAAAAAAULL);
		last = entities_api.register_component(id, 4u, 4u, "capacity");
		if (last == 0) {
			accepted += 1u;
		}
	}

	/* Earlier tests may have registered a few components; the registry caps
	 * out at SK_ECS_MAX_COMPONENT_TYPES total entries. */
	TEST_ASSERT_TRUE(accepted >= (u32)SK_ECS_MAX_COMPONENT_TYPES - 8u);
	TEST_ASSERT_EQUAL_INT32(-2, last);

	sk_type_id_t full_id = SK_TYPE_ID("sk.test.ecs.capacity.full", 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
	TEST_ASSERT_EQUAL_INT32(-2, entities_api.register_component(full_id, 4u, 4u, "full"));
}

#endif /* SK_TESTS */
