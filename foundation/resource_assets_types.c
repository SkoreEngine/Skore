#include "resource_assets_types.h"

#ifdef SK_TESTS
#include "test.h"
#endif

#include <stddef.h> /* offsetof */

/* ------------------------------------------------------------------ */
/*  Field descriptor arrays (manual registration; no C++ Reflection)  */
/* ------------------------------------------------------------------ */

static const sk_resource_field_t resource_asset_package_fields[] = {
	{"Name", SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_resource_asset_package_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"AbsolutePath",
	 SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_asset_package_t, absolute_path),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"Files",
	 SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_resource_asset_package_t, files),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
	{"Root", SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, SK_RESOURCE_FIELD_TYPE_SUB_OBJECT, (u32)offsetof(sk_resource_asset_package_t, root), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
};

static const sk_resource_field_t resource_asset_file_fields[] = {
	{"AssetRef", SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF, SK_RESOURCE_FIELD_TYPE_REFERENCE, (u32)offsetof(sk_resource_asset_file_t, asset_ref), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
	{"AbsolutePath",
	 SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_asset_file_t, absolute_path),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"RelativePath",
	 SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_asset_file_t, relative_path),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"PersistedVersion",
	 SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_resource_asset_file_t, persisted_version),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
	{"TotalSizeInDisk",
	 SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_resource_asset_file_t, total_size_in_disk),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
	{"LastModifiedTime",
	 SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_resource_asset_file_t, last_modified_time),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
};

static const sk_resource_field_t resource_asset_fields[] = {
	{"Name", SK_RESOURCE_ASSET_FIELD_NAME, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_resource_asset_t, name), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Type", SK_RESOURCE_ASSET_FIELD_TYPE, SK_RESOURCE_FIELD_TYPE_NONE, (u32)offsetof(sk_resource_asset_t, type), (u32)sizeof(u64), {0ull, 0ull}},
	{"Extension", SK_RESOURCE_ASSET_FIELD_EXTENSION, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_resource_asset_t, extension), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Object", SK_RESOURCE_ASSET_FIELD_OBJECT, SK_RESOURCE_FIELD_TYPE_SUB_OBJECT, (u32)offsetof(sk_resource_asset_t, object), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
	{"Parent", SK_RESOURCE_ASSET_FIELD_PARENT, SK_RESOURCE_FIELD_TYPE_REFERENCE, (u32)offsetof(sk_resource_asset_t, parent), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
	{"PathId", SK_RESOURCE_ASSET_FIELD_PATH_ID, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_resource_asset_t, path_id), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Directory", SK_RESOURCE_ASSET_FIELD_DIRECTORY, SK_RESOURCE_FIELD_TYPE_BOOL, (u32)offsetof(sk_resource_asset_t, directory), (u32)sizeof(i32), {0ull, 0ull}},
	{"AssetFile", SK_RESOURCE_ASSET_FIELD_ASSET_FILE, SK_RESOURCE_FIELD_TYPE_REFERENCE, (u32)offsetof(sk_resource_asset_t, asset_file), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
	{"SourcePath",
	 SK_RESOURCE_ASSET_FIELD_SOURCE_PATH,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_asset_t, source_path),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"ReadOnly", SK_RESOURCE_ASSET_FIELD_READ_ONLY, SK_RESOURCE_FIELD_TYPE_BOOL, (u32)offsetof(sk_resource_asset_t, read_only), (u32)sizeof(i32), {0ull, 0ull}},
	{"ImportedAsset",
	 SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT,
	 (u32)offsetof(sk_resource_asset_t, imported_asset),
	 (u32)sizeof(sk_rid_t),
	 {0ull, 0ull}},
};

static const sk_resource_field_t resource_asset_directory_fields[] = {
	{"DirectoryAsset",
	 SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT,
	 (u32)offsetof(sk_resource_asset_directory_t, directory_asset),
	 (u32)sizeof(sk_rid_t),
	 {0ull, 0ull}},
	{"Directories",
	 SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_resource_asset_directory_t, directories),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
	{"Assets",
	 SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_resource_asset_directory_t, assets),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
};

static const sk_resource_field_t resource_imported_asset_fields[] = {
	{"OriginalFileName",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_imported_asset_t, original_file_name),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"Extension",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_imported_asset_t, extension),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"ContentHash",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_imported_asset_t, content_hash),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"ImporterId",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID,
	 SK_RESOURCE_FIELD_TYPE_TYPE_ID,
	 (u32)offsetof(sk_resource_imported_asset_t, importer_id),
	 (u32)sizeof(sk_type_id_t),
	 {0ull, 0ull}},
	{"CookerVersion",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_resource_imported_asset_t, cooker_version),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
	{"ImportSettings",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORT_SETTINGS,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT,
	 (u32)offsetof(sk_resource_imported_asset_t, import_settings),
	 (u32)sizeof(sk_rid_t),
	 {0ull, 0ull}},
	{"OriginalData",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA,
	 SK_RESOURCE_FIELD_TYPE_BUFFER,
	 (u32)offsetof(sk_resource_imported_asset_t, original_data),
	 (u32)sizeof(sk_resource_asset_buffer_t),
	 {0ull, 0ull}},
	{"OriginalSize",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE,
	 SK_RESOURCE_FIELD_TYPE_UINT,
	 (u32)offsetof(sk_resource_imported_asset_t, original_size),
	 (u32)sizeof(u64),
	 {0ull, 0ull}},
	{"SubResources",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_resource_imported_asset_t, sub_resources),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
	{"Dependencies",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_resource_imported_asset_t, dependencies),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
	{"ExtractedResources",
	 SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(sk_resource_imported_asset_t, extracted_resources),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
};

static const sk_resource_field_t resource_sub_id_entry_fields[] = {
	{"SubId",
	 SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_sub_id_entry_t, sub_id),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"TargetUUID",
	 SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_sub_id_entry_t, target_uuid),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"TypeName",
	 SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_sub_id_entry_t, type_name),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
};

static const sk_resource_field_t resource_dependency_entry_fields[] = {
	{"RelPath",
	 SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_dependency_entry_t, rel_path),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"Data",
	 SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA,
	 SK_RESOURCE_FIELD_TYPE_BUFFER,
	 (u32)offsetof(sk_resource_dependency_entry_t, data),
	 (u32)sizeof(sk_resource_asset_buffer_t),
	 {0ull, 0ull}},
	{"Size", SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE, SK_RESOURCE_FIELD_TYPE_UINT, (u32)offsetof(sk_resource_dependency_entry_t, size), (u32)sizeof(u64), {0ull, 0ull}},
};

static const sk_resource_field_t resource_extracted_entry_fields[] = {
	{"SourceUUID",
	 SK_RESOURCE_EXTRACTED_ENTRY_FIELD_SOURCE_UUID,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_extracted_entry_t, source_uuid),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"TargetUUID",
	 SK_RESOURCE_EXTRACTED_ENTRY_FIELD_TARGET_UUID,
	 SK_RESOURCE_FIELD_TYPE_STRING,
	 (u32)offsetof(sk_resource_extracted_entry_t, target_uuid),
	 (u32)sizeof(sk_field_string_t),
	 {0ull, 0ull}},
	{"Kind", SK_RESOURCE_EXTRACTED_ENTRY_FIELD_KIND, SK_RESOURCE_FIELD_TYPE_UINT, (u32)offsetof(sk_resource_extracted_entry_t, kind), (u32)sizeof(u64), {0ull, 0ull}},
};

/* ------------------------------------------------------------------ */
/*  Type descriptors                                                  */
/* ------------------------------------------------------------------ */

static const sk_resource_type_desc_t resource_asset_package_desc = {
	{SK_RESOURCE_ASSET_PACKAGE_TYPE_ID_LO, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID_HI},
	"ResourceAssetPackage",
	(u32)sizeof(sk_resource_asset_package_t),
	resource_asset_package_fields,
	(u32)(sizeof(resource_asset_package_fields) / sizeof(resource_asset_package_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_asset_file_desc = {
	{SK_RESOURCE_ASSET_FILE_TYPE_ID_LO, SK_RESOURCE_ASSET_FILE_TYPE_ID_HI},
	"ResourceAssetFile",
	(u32)sizeof(sk_resource_asset_file_t),
	resource_asset_file_fields,
	(u32)(sizeof(resource_asset_file_fields) / sizeof(resource_asset_file_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_asset_desc = {
	{SK_RESOURCE_ASSET_TYPE_ID_LO, SK_RESOURCE_ASSET_TYPE_ID_HI},
	"ResourceAsset",
	(u32)sizeof(sk_resource_asset_t),
	resource_asset_fields,
	(u32)(sizeof(resource_asset_fields) / sizeof(resource_asset_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_asset_directory_desc = {
	{SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID_LO, SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID_HI},
	"ResourceAssetDirectory",
	(u32)sizeof(sk_resource_asset_directory_t),
	resource_asset_directory_fields,
	(u32)(sizeof(resource_asset_directory_fields) / sizeof(resource_asset_directory_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_imported_asset_desc = {
	{SK_RESOURCE_IMPORTED_ASSET_TYPE_ID_LO, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID_HI},
	"ResourceImportedAsset",
	(u32)sizeof(sk_resource_imported_asset_t),
	resource_imported_asset_fields,
	(u32)(sizeof(resource_imported_asset_fields) / sizeof(resource_imported_asset_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_sub_id_entry_desc = {
	{SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID_LO, SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID_HI},
	"ResourceSubIdEntry",
	(u32)sizeof(sk_resource_sub_id_entry_t),
	resource_sub_id_entry_fields,
	(u32)(sizeof(resource_sub_id_entry_fields) / sizeof(resource_sub_id_entry_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_dependency_entry_desc = {
	{SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID_LO, SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID_HI},
	"ResourceDependencyEntry",
	(u32)sizeof(sk_resource_dependency_entry_t),
	resource_dependency_entry_fields,
	(u32)(sizeof(resource_dependency_entry_fields) / sizeof(resource_dependency_entry_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t resource_extracted_entry_desc = {
	{SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID_LO, SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID_HI},
	"ResourceExtractedEntry",
	(u32)sizeof(sk_resource_extracted_entry_t),
	resource_extracted_entry_fields,
	(u32)(sizeof(resource_extracted_entry_fields) / sizeof(resource_extracted_entry_fields[0])),
	NULL,
};

static const sk_resource_type_desc_t* const resource_asset_type_descs[] = {
	&resource_asset_package_desc, &resource_asset_file_desc,	   &resource_asset_desc,		   &resource_asset_directory_desc, &resource_imported_asset_desc,
	&resource_sub_id_entry_desc,  &resource_dependency_entry_desc, &resource_extracted_entry_desc,
};

static const u32 resource_asset_type_desc_count = (u32)(sizeof(resource_asset_type_descs) / sizeof(resource_asset_type_descs[0]));

/* ------------------------------------------------------------------ */
/*  Registration entry                                                */
/* ------------------------------------------------------------------ */

i32 sk_resource_assets_register_types(sk_repository_t* repository, const sk_repository_api_t* repo_api) {
	const sk_repository_api_t* api = repo_api;
	for (u32 i = 0u; i < resource_asset_type_desc_count; ++i) {
		i32 result = api->register_type(repository, resource_asset_type_descs[i]);
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

#ifdef SK_TESTS

#include "app.h"

static const sk_repository_api_t* ra_types_repo_api(void) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	return api;
}

SK_TEST(resource_assets_types_type_ids_distinct) {
	sk_type_id_t ids[] = {
		SK_RESOURCE_ASSET_PACKAGE_TYPE_ID,	  SK_RESOURCE_ASSET_FILE_TYPE_ID,	   SK_RESOURCE_ASSET_TYPE_ID,
		SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID,  SK_RESOURCE_IMPORTED_ASSET_TYPE_ID,  SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID,
		SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID, SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID,
	};
	u32 count = (u32)(sizeof(ids) / sizeof(ids[0]));
	for (u32 i = 0u; i < count; ++i) {
		TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(ids[i], SK_TYPE_ID_ZERO));
		for (u32 j = i + 1u; j < count; ++j) {
			TEST_ASSERT_FALSE(SK_TYPE_ID_EQ(ids[i], ids[j]));
		}
	}
}

SK_TEST(resource_assets_types_register_all) {
	const sk_repository_api_t* api = ra_types_repo_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));

	/* Every type is discoverable by id and by name (names match main-branch). */
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_ASSET_FILE_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_ASSET_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID));
	TEST_ASSERT_NOT_NULL(api->find_type(repo, SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID));

	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID), api->find_type_by_name(repo, "ResourceAssetPackage"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_ASSET_FILE_TYPE_ID), api->find_type_by_name(repo, "ResourceAssetFile"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_ASSET_TYPE_ID), api->find_type_by_name(repo, "ResourceAsset"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID), api->find_type_by_name(repo, "ResourceAssetDirectory"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID), api->find_type_by_name(repo, "ResourceImportedAsset"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID), api->find_type_by_name(repo, "ResourceSubIdEntry"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID), api->find_type_by_name(repo, "ResourceDependencyEntry"));
	TEST_ASSERT_EQUAL_PTR(api->find_type(repo, SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID), api->find_type_by_name(repo, "ResourceExtractedEntry"));

	/* Re-registration fails cleanly (duplicate type id on the first type). */
	TEST_ASSERT_NOT_EQUAL(0, sk_resource_assets_register_types(repo, api));

	api->destroy(repo);
}

SK_TEST(resource_assets_types_directory_hierarchy) {
	const sk_repository_api_t* api = ra_types_repo_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));

	const sk_resource_type_t* asset_type = api->find_type(repo, SK_RESOURCE_ASSET_TYPE_ID);
	const sk_resource_type_t* directory_type = api->find_type(repo, SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID);
	TEST_ASSERT_NOT_NULL(asset_type);
	TEST_ASSERT_NOT_NULL(directory_type);

	/* Directory asset resource + its ResourceAssetDirectory node. */
	sk_rid_t dir_asset = api->create_resource(repo, asset_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(dir_asset.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, dir_asset);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, "Assets"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, ""));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 1));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets"));
		api->commit(view, NULL);
	}
	sk_rid_t directory = api->create_resource(repo, directory_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(directory.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, directory);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET, dir_asset));
		api->commit(view, NULL);
	}

	/* A regular asset under the directory. */
	sk_rid_t asset = api->create_resource(repo, asset_type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(asset.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, asset);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_ASSET_FIELD_NAME, "scene.material"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".material"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(view, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, SK_RESOURCE_ASSET_FIELD_PARENT, dir_asset));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, SK_RESOURCE_ASSET_FIELD_OBJECT, asset));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, directory);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, asset));
		api->commit(view, NULL);
	}

	{
		sk_resource_object_t dir_asset_read = api->read(repo, dir_asset);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(dir_asset_read));
		TEST_ASSERT_EQUAL_STRING("Assets", api->get_string(dir_asset_read, SK_RESOURCE_ASSET_FIELD_NAME));
		TEST_ASSERT_EQUAL_STRING("", api->get_string(dir_asset_read, SK_RESOURCE_ASSET_FIELD_EXTENSION));
		TEST_ASSERT_EQUAL_INT(1, api->get_bool(dir_asset_read, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
		TEST_ASSERT_EQUAL_STRING("Assets", api->get_string(dir_asset_read, SK_RESOURCE_ASSET_FIELD_PATH_ID));
	}
	{
		sk_resource_object_t directory_read = api->read(repo, directory);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(directory_read));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(directory_read, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET), dir_asset));
		u32 count = 0u;
		const sk_rid_t* items = api->get_subobject_list(directory_read, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
		TEST_ASSERT_EQUAL_UINT32(1u, count);
		TEST_ASSERT_TRUE(SK_RID_EQ(items[0], asset));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, asset), directory));
	}
	{
		sk_resource_object_t asset_read = api->read(repo, asset);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(asset_read));
		TEST_ASSERT_EQUAL_STRING("scene.material", api->get_string(asset_read, SK_RESOURCE_ASSET_FIELD_NAME));
		TEST_ASSERT_EQUAL_STRING(".material", api->get_string(asset_read, SK_RESOURCE_ASSET_FIELD_EXTENSION));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(asset_read, SK_RESOURCE_ASSET_FIELD_PARENT), dir_asset));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(asset_read, SK_RESOURCE_ASSET_FIELD_OBJECT), asset));
		TEST_ASSERT_EQUAL_INT(0, api->get_bool(asset_read, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
	}

	api->destroy(repo);
}

SK_TEST(resource_assets_types_imported_asset) {
	const sk_repository_api_t* api = ra_types_repo_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo, api));

	sk_rid_t wrapper = api->create_resource(repo, api->find_type(repo, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID), SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(wrapper.id != 0u);

	sk_type_id_t importer_id = SK_TYPE_ID("sk.material_importer", 0x1111111111111111ULL, 0x2222222222222222ULL);
	{
		sk_resource_object_t view = api->write(repo, wrapper);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME, "mesh.fbx"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION, ".fbx"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH, "abc123"));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION, 2u));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE, 12345u));
		/* TypeID / Buffer fields have no public accessors yet: a mismatched accessor is rejected. */
		TEST_ASSERT_NOT_EQUAL(0, api->set_uint(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID, 7u));
		TEST_ASSERT_NOT_EQUAL(0, api->set_uint(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA, 9u));
		/* Direct blob write stores the TypeID at the descriptor's offset. */
		sk_resource_imported_asset_t* blob = (sk_resource_imported_asset_t*)view.instance;
		blob->importer_id = importer_id;
		api->commit(view, NULL);
	}

	{
		sk_resource_object_t read = api->read(repo, wrapper);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(read));
		TEST_ASSERT_EQUAL_STRING("mesh.fbx", api->get_string(read, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME));
		TEST_ASSERT_EQUAL_STRING(".fbx", api->get_string(read, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION));
		TEST_ASSERT_EQUAL_STRING("abc123", api->get_string(read, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH));
		TEST_ASSERT_EQUAL_UINT64(2u, api->get_uint(read, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION));
		TEST_ASSERT_EQUAL_UINT64(12345u, api->get_uint(read, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE));
		const sk_resource_imported_asset_t* blob = (const sk_resource_imported_asset_t*)read.instance;
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(blob->importer_id, importer_id));
	}

	/* A SubIdEntry sub-resource registered on the wrapper's SubResources list. */
	sk_rid_t entry = api->create_resource(repo, api->find_type(repo, SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID), SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(entry.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, entry);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID, "mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID, "uuid-1"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME, "Mesh"));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, wrapper);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, entry));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, wrapper);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(read));
		u32 count = 0u;
		const sk_rid_t* items = api->get_subobject_list(read, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, &count);
		TEST_ASSERT_EQUAL_UINT32(1u, count);
		TEST_ASSERT_TRUE(SK_RID_EQ(items[0], entry));
	}

	api->destroy(repo);
}

#endif /* SK_TESTS */
