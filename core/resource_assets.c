/**
 * @file resource_assets.c
 * @brief Contract smoke tests for resource_assets.h (no runtime behavior yet).
 */

#include "resource_assets.h"

#include "test.h"

#ifdef SK_TESTS

#include "unity.h"

SK_TEST(resource_asset_handler_type_id_nonzero) {
	sk_type_id_t id = SK_RESOURCE_ASSET_HANDLER_TYPE_ID;
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
	sk_type_id_t expected = SK_TYPE_ID("sk.resource_asset_handler", 0x3bf713f497383666ULL, 0x74e3f6b8c318e3e9ULL);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, expected));
}

SK_TEST(resource_asset_importer_type_id_nonzero) {
	sk_type_id_t id = SK_RESOURCE_ASSET_IMPORTER_TYPE_ID;
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(id, SK_TYPE_ID_ZERO));
	sk_type_id_t expected = SK_TYPE_ID("sk.resource_asset_importer", 0xa0cc8513be01820dULL, 0x190f2cb78426a105ULL);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(id, expected));
}

SK_TEST(resource_asset_handler_importer_type_ids_distinct) {
	TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(SK_RESOURCE_ASSET_HANDLER_TYPE_ID, SK_RESOURCE_ASSET_IMPORTER_TYPE_ID));
}

/* Compile-time shape checks: tables are plain aggregates usable as static
 * initializers and as add_impl payloads. */
SK_TEST(resource_asset_handler_static_init_shape) {
	static sk_resource_asset_handler_t handler = {
		.user_data = NULL,
		.extension = NULL,
		.open_asset = NULL,
		.get_resource_type_id = NULL,
		.get_desc = NULL,
		.load = NULL,
		.save = NULL,
		.create = NULL,
		.reloaded = NULL,
		.after_move = NULL,
		.export_object = NULL,
		.get_icon = NULL,
		.get_load_order = NULL,
		.get_asset_name = NULL,
	};
	const void* as_impl = &handler;
	TEST_ASSERT_NOT_NULL(as_impl);
	TEST_ASSERT_NULL(handler.user_data);
}

SK_TEST(resource_asset_importer_static_init_shape) {
	static sk_resource_asset_importer_t importer = {
		.user_data = NULL,
		.imported_extensions = NULL,
		.output_extension = NULL,
		.cooker_version = NULL,
		.get_settings_type = NULL,
		.ingest = NULL,
		.cook = NULL,
		.import_asset = NULL,
	};
	const void* as_impl = &importer;
	TEST_ASSERT_NOT_NULL(as_impl);
	TEST_ASSERT_NULL(importer.user_data);
}

#endif /* SK_TESTS */
