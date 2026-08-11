#pragma once

/**
 * @file resource_assets_types.h
 * @brief ResourceAsset repository field/type descriptors (C port of the
 *        main-branch ResourceAssetPackage / ResourceAssetFile / ResourceAsset /
 *        ResourceAssetDirectory / ResourceImportedAsset resource types plus the
 *        SubId / Dependency / Extracted entry structs).
 *
 * The resource types are registered into an sk_repository_t via manual field /
 * type descriptors (see repository.h) — there is no C++ Reflection. This module
 * only defines the data model: stable TYPE_IDs, field index constants, and the
 * instance blob layouts the descriptors are laid out over, plus one register
 * entry (sk_resource_assets_register_types) that registers every type.
 *
 * Field order and indices mirror the main-branch enums exactly so serialization
 * (added separately) can round-trip assets with the same field mapping. Field
 * categories map to repository storage as follows:
 *
 *   - String       → sk_field_string_t
 *   - UInt / Int   → u64 / i64 (8 bytes)
 *   - Bool         → i32 (4 bytes; the repository's bool accessors use i32)
 *   - Reference    → sk_rid_t (sub-object / reference fields are both RIDs)
 *   - SubObject    → sk_rid_t
 *   - SubObjectList→ sk_field_subobject_list_t
 *   - TypeID       → sk_type_id_t (16 bytes)
 *   - Buffer       → sk_resource_asset_buffer_t (owned byte payload; see
 *                    below)
 *   - None         → reserved u64 placeholder (mirrors main's untyped
 *                    ResourceAsset::Type field)
 *
 * YAML / binary serialization is intentionally absent (owned by another goal).
 */

#include "common.h"
#include "repository.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Field index constants (mirror main-branch enum order)             */
/* ------------------------------------------------------------------ */

/* ResourceAssetPackage fields. */
enum sk_resource_asset_package_field_t {
	SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME = 0,
	SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH = 1,
	SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES = 2,
	SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT = 3,
};

/* ResourceAssetFile fields. */
enum sk_resource_asset_file_field_t {
	SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF = 0,
	SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH = 1,
	SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH = 2,
	SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION = 3,
	SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK = 4,
	SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME = 5,
};

/* ResourceAsset fields. Index 1 (Type) is reserved and untyped (None), exactly
 * like main-branch registration — it stays for field-index parity. */
enum sk_resource_asset_field_t {
	SK_RESOURCE_ASSET_FIELD_NAME = 0,
	SK_RESOURCE_ASSET_FIELD_TYPE = 1,
	SK_RESOURCE_ASSET_FIELD_EXTENSION = 2,
	SK_RESOURCE_ASSET_FIELD_OBJECT = 3,
	SK_RESOURCE_ASSET_FIELD_PARENT = 4,
	SK_RESOURCE_ASSET_FIELD_PATH_ID = 5,
	SK_RESOURCE_ASSET_FIELD_DIRECTORY = 6,
	SK_RESOURCE_ASSET_FIELD_ASSET_FILE = 7,
	SK_RESOURCE_ASSET_FIELD_SOURCE_PATH = 8,
	SK_RESOURCE_ASSET_FIELD_READ_ONLY = 9,
	SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET = 10,
};

/* ResourceAssetDirectory fields. */
enum sk_resource_asset_directory_field_t {
	SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET = 0,
	SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES = 1,
	SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS = 2,
};

/* ResourceImportedAsset fields. */
enum sk_resource_imported_asset_field_t {
	SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME = 0,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION = 1,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH = 2,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID = 3,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION = 4,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORT_SETTINGS = 5,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA = 6,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE = 7,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES = 8,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES = 9,
	SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES = 10,
};

/* ResourceSubIdEntry fields. */
enum sk_resource_sub_id_entry_field_t {
	SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID = 0,
	SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID = 1,
	SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME = 2,
};

/* ResourceDependencyEntry fields. */
enum sk_resource_dependency_entry_field_t {
	SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH = 0,
	SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA = 1,
	SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE = 2,
};

/* ResourceExtractedEntry fields. */
enum sk_resource_extracted_entry_field_t {
	SK_RESOURCE_EXTRACTED_ENTRY_FIELD_SOURCE_UUID = 0,
	SK_RESOURCE_EXTRACTED_ENTRY_FIELD_TARGET_UUID = 1,
	SK_RESOURCE_EXTRACTED_ENTRY_FIELD_KIND = 2,
};

/* ------------------------------------------------------------------ */
/*  Instance blob layouts                                             */
/* ------------------------------------------------------------------ */

/**
 * In-blob storage for a Buffer field (C port of the main-branch ResourceBuffer
 * payload). The repository owns the byte payload: set_buffer deep-copies
 * caller bytes with the repository allocator, get_buffer returns a borrowed
 * view (see sk_field_buffer_t in repository.h for the ownership contract),
 * and instance copy / destroy handle the payload like Blob fields. An empty
 * buffer (size 0, data NULL) with its has-value bit set is distinct from an
 * unset buffer.
 */
typedef sk_field_buffer_t sk_resource_asset_buffer_t;

typedef struct sk_resource_asset_package_t {
	sk_field_string_t name;			 /* SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME */
	sk_field_string_t absolute_path; /* SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH */
	sk_field_subobject_list_t files; /* SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES */
	sk_rid_t root;					 /* SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT (SubObject) */
} sk_resource_asset_package_t;

typedef struct sk_resource_asset_file_t {
	sk_rid_t asset_ref;				 /* SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF (Reference) */
	sk_field_string_t absolute_path; /* SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH */
	sk_field_string_t relative_path; /* SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH */
	u64 persisted_version;			 /* SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION (UInt) */
	u64 total_size_in_disk;			 /* SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK (UInt) */
	u64 last_modified_time;			 /* SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME (UInt) */
} sk_resource_asset_file_t;

typedef struct sk_resource_asset_t {
	sk_field_string_t name;		   /* SK_RESOURCE_ASSET_FIELD_NAME */
	u64 type;					   /* SK_RESOURCE_ASSET_FIELD_TYPE (None; reserved for parity) */
	sk_field_string_t extension;   /* SK_RESOURCE_ASSET_FIELD_EXTENSION */
	sk_rid_t object;			   /* SK_RESOURCE_ASSET_FIELD_OBJECT (SubObject) */
	sk_rid_t parent;			   /* SK_RESOURCE_ASSET_FIELD_PARENT (Reference) */
	sk_field_string_t path_id;	   /* SK_RESOURCE_ASSET_FIELD_PATH_ID */
	i32 directory;				   /* SK_RESOURCE_ASSET_FIELD_DIRECTORY (Bool) */
	i32 _pad0;					   /* explicit padding: next field is 8-byte aligned */
	sk_rid_t asset_file;		   /* SK_RESOURCE_ASSET_FIELD_ASSET_FILE (Reference) */
	sk_field_string_t source_path; /* SK_RESOURCE_ASSET_FIELD_SOURCE_PATH */
	i32 read_only;				   /* SK_RESOURCE_ASSET_FIELD_READ_ONLY (Bool) */
	i32 _pad1;					   /* explicit padding: next field is 8-byte aligned */
	sk_rid_t imported_asset;	   /* SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET (SubObject) */
} sk_resource_asset_t;

typedef struct sk_resource_asset_directory_t {
	sk_rid_t directory_asset;			   /* SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET (SubObject) */
	sk_field_subobject_list_t directories; /* SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES */
	sk_field_subobject_list_t assets;	   /* SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS */
} sk_resource_asset_directory_t;

typedef struct sk_resource_imported_asset_t {
	sk_field_string_t original_file_name;		   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME */
	sk_field_string_t extension;				   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION */
	sk_field_string_t content_hash;				   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH */
	sk_type_id_t importer_id;					   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID (TypeID) */
	u64 cooker_version;							   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION (UInt) */
	sk_rid_t import_settings;					   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORT_SETTINGS (SubObject) */
	sk_resource_asset_buffer_t original_data;	   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA (Buffer) */
	u64 original_size;							   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE (UInt) */
	sk_field_subobject_list_t sub_resources;	   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES */
	sk_field_subobject_list_t dependencies;		   /* SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES */
	sk_field_subobject_list_t extracted_resources; /* SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES */
} sk_resource_imported_asset_t;

typedef struct sk_resource_sub_id_entry_t {
	sk_field_string_t sub_id;	   /* SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID */
	sk_field_string_t target_uuid; /* SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID */
	sk_field_string_t type_name;   /* SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME */
} sk_resource_sub_id_entry_t;

typedef struct sk_resource_dependency_entry_t {
	sk_field_string_t rel_path;		 /* SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH */
	sk_resource_asset_buffer_t data; /* SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA (Buffer) */
	u64 size;						 /* SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE (UInt) */
} sk_resource_dependency_entry_t;

typedef struct sk_resource_extracted_entry_t {
	sk_field_string_t source_uuid; /* SK_RESOURCE_EXTRACTED_ENTRY_FIELD_SOURCE_UUID */
	sk_field_string_t target_uuid; /* SK_RESOURCE_EXTRACTED_ENTRY_FIELD_TARGET_UUID */
	u64 kind;					   /* SK_RESOURCE_EXTRACTED_ENTRY_FIELD_KIND (UInt) */
} sk_resource_extracted_entry_t;

/* ------------------------------------------------------------------ */
/*  Type ids (MD5("sk.resource_asset_*") split into two u64 halves)   */
/* ------------------------------------------------------------------ */

#define SK_RESOURCE_ASSET_PACKAGE_TYPE_ID_LO 0x70aa13da1372291dULL
#define SK_RESOURCE_ASSET_PACKAGE_TYPE_ID_HI 0xba16a5e2fffcabd9ULL
#define SK_RESOURCE_ASSET_PACKAGE_TYPE_ID SK_TYPE_ID("sk.resource_asset_package", SK_RESOURCE_ASSET_PACKAGE_TYPE_ID_LO, SK_RESOURCE_ASSET_PACKAGE_TYPE_ID_HI)

#define SK_RESOURCE_ASSET_FILE_TYPE_ID_LO 0x0560e023450813a3ULL
#define SK_RESOURCE_ASSET_FILE_TYPE_ID_HI 0xa47b5606cd5df4ebULL
#define SK_RESOURCE_ASSET_FILE_TYPE_ID SK_TYPE_ID("sk.resource_asset_file", SK_RESOURCE_ASSET_FILE_TYPE_ID_LO, SK_RESOURCE_ASSET_FILE_TYPE_ID_HI)

#define SK_RESOURCE_ASSET_TYPE_ID_LO 0x4f3571819c8dac67ULL
#define SK_RESOURCE_ASSET_TYPE_ID_HI 0x7452b6febfb9fb79ULL
#define SK_RESOURCE_ASSET_TYPE_ID SK_TYPE_ID("sk.resource_asset", SK_RESOURCE_ASSET_TYPE_ID_LO, SK_RESOURCE_ASSET_TYPE_ID_HI)

#define SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID_LO 0x56c6818bffe76959ULL
#define SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID_HI 0x4fc4c219278d8289ULL
#define SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID SK_TYPE_ID("sk.resource_asset_directory", SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID_LO, SK_RESOURCE_ASSET_DIRECTORY_TYPE_ID_HI)

#define SK_RESOURCE_IMPORTED_ASSET_TYPE_ID_LO 0x5d987481200bc093ULL
#define SK_RESOURCE_IMPORTED_ASSET_TYPE_ID_HI 0xbb28a95f0c15bd7fULL
#define SK_RESOURCE_IMPORTED_ASSET_TYPE_ID SK_TYPE_ID("sk.resource_imported_asset", SK_RESOURCE_IMPORTED_ASSET_TYPE_ID_LO, SK_RESOURCE_IMPORTED_ASSET_TYPE_ID_HI)

#define SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID_LO 0x2e16d8513f30416cULL
#define SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID_HI 0x6e11e9a9000b6352ULL
#define SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID SK_TYPE_ID("sk.resource_sub_id_entry", SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID_LO, SK_RESOURCE_SUB_ID_ENTRY_TYPE_ID_HI)

#define SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID_LO 0xa730b1498cd8c69cULL
#define SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID_HI 0x2981c2dbccb2d25cULL
#define SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID SK_TYPE_ID("sk.resource_dependency_entry", SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID_LO, SK_RESOURCE_DEPENDENCY_ENTRY_TYPE_ID_HI)

#define SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID_LO 0x68fd2e4fb76af826ULL
#define SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID_HI 0x95d4df513bdcbbdcULL
#define SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID SK_TYPE_ID("sk.resource_extracted_entry", SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID_LO, SK_RESOURCE_EXTRACTED_ENTRY_TYPE_ID_HI)

/* ------------------------------------------------------------------ */
/*  Registration                                                      */
/* ------------------------------------------------------------------ */

/**
 * Register every ResourceAsset repository type into @p repository: the package,
 * asset file, asset, directory, imported asset, and the SubId / Dependency /
 * Extracted entry structs. Fields and defaults are copied by register_type, so
 * the module's static descriptors need not outlive the call.
 * @param repository Target repository (must not be NULL).
 * @return 0 when every type registered; otherwise the first register_type
 *         error (-1 duplicate type id, -2 duplicate name, -3 OOM, -4 invalid
 *         descriptor) with registration stopped at that type.
 */
i32 sk_resource_assets_register_types(sk_repository_t* repository);

#ifdef __cplusplus
}
#endif
