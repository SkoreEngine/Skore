#pragma once

/**
 * @file repository.h
 * @brief Resource storage foundation: manual type registry + paged RID store.
 *
 * C port of the v1 storage model (Resources / ResourceType / ResourceCommon),
 * restricted to the storage layer: stable UUIDs and RIDs, manual field/type
 * descriptors, a page-allocated resource store keyed by RID with UUID and path
 * uniqueness, and default-value deep copy for types that declare defaults.
 * Reflection, events, serialization, and prototypes are intentionally absent.
 *
 * All state lives on an explicit sk_repository_t instance created with an
 * allocator — there are no process-global repository tables. The module
 * surface is one global sk_repository_api_t (filled once in repository.c);
 * callers use table pointers only (no free-function mirrors of the operations).
 *
 * # Instance storage
 *
 * Each resource is a live slot in a 4096-slot page; RID index 0 is reserved
 * as the invalid sentinel (SK_RID_ZERO). A resource owns a flat instance blob
 * of `instance_size` bytes laid out by its type's field descriptors
 * (offset/size are caller-declared). When the type declares `defaults`, a new
 * resource deep-copies them: indirection fields (String / Blob /
 * ReferenceArray / SubObjectList) get repository-owned heap copies, all other
 * field types are copied by bytes.
 */

#include "allocator.h"
#include "common.h"

#include <stddef.h> /* size_t, offsetof */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Identity types (port of v1 UUID / RID)                            */
/* ------------------------------------------------------------------ */

/** 128-bit universally unique resource identifier. */
typedef struct sk_uuid_t {
	u64 lo;
	u64 hi;
} sk_uuid_t;

/** Zero / invalid UUID. */
#define SK_UUID_ZERO ((sk_uuid_t){0ull, 0ull})

/** Non-zero when both 64-bit halves of @p a and @p b match. */
#define SK_UUID_EQ(a, b) (((a).lo == (b).lo) && ((a).hi == (b).hi))

/** Resource handle: dense index into the repository's storage pages. */
typedef struct sk_rid_t {
	u64 id;
} sk_rid_t;

/** Invalid RID sentinel (index 0 is never allocated). */
#define SK_RID_ZERO ((sk_rid_t){0ull})

/** Non-zero when both ids match. */
#define SK_RID_EQ(a, b) ((a).id == (b).id)

/* ------------------------------------------------------------------ */
/*  Field / type descriptors                                          */
/* ------------------------------------------------------------------ */

/** Field storage categories (port of v1 ResourceFieldType). */
typedef enum sk_resource_field_type_t {
	SK_RESOURCE_FIELD_TYPE_NONE = 0,
	SK_RESOURCE_FIELD_TYPE_BOOL,
	SK_RESOURCE_FIELD_TYPE_INT,
	SK_RESOURCE_FIELD_TYPE_UINT,
	SK_RESOURCE_FIELD_TYPE_FLOAT,
	SK_RESOURCE_FIELD_TYPE_STRING,
	SK_RESOURCE_FIELD_TYPE_VEC2,
	SK_RESOURCE_FIELD_TYPE_VEC3,
	SK_RESOURCE_FIELD_TYPE_VEC4,
	SK_RESOURCE_FIELD_TYPE_QUAT,
	SK_RESOURCE_FIELD_TYPE_MAT4,
	SK_RESOURCE_FIELD_TYPE_COLOR,
	SK_RESOURCE_FIELD_TYPE_ENUM,
	SK_RESOURCE_FIELD_TYPE_BLOB,
	SK_RESOURCE_FIELD_TYPE_REFERENCE,
	SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY,
	SK_RESOURCE_FIELD_TYPE_SUB_OBJECT,
	SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	SK_RESOURCE_FIELD_TYPE_BUFFER,
	SK_RESOURCE_FIELD_TYPE_TYPE_ID,
	SK_RESOURCE_FIELD_TYPE_MAX,
} sk_resource_field_type_t;

/**
 * In-blob storage for a String field: a NUL-terminated heap string owned by
 * the repository (deep-copied from the descriptor defaults).
 */
typedef struct sk_field_string_t {
	char* chars;
} sk_field_string_t;

/**
 * In-blob storage for a Blob field: a repository-owned heap byte range.
 */
typedef struct sk_field_blob_t {
	u8* data;
	u32 size;
} sk_field_blob_t;

/**
 * In-blob storage for ReferenceArray / SubObjectList fields: a
 * repository-owned heap array of RIDs.
 */
typedef struct sk_field_rid_array_t {
	sk_rid_t* items;
	u32 count;
} sk_field_rid_array_t;

/**
 * Manual field descriptor. Offset/size place the field inside the type's
 * instance blob; the caller picks the layout (fixed-width scalar types are
 * POD and copied by @p size bytes; indirection types must use the
 * sk_field_*_t layouts above).
 *
 * @field name     Human-readable field name (must not be NULL).
 * @field index    Stable field index within the type (informational; need not
 *                 be contiguous).
 * @field type     Storage category; drives deep-copy / destroy behavior.
 * @field offset   Byte offset of the field value within the instance blob.
 * @field size     Byte size of the stored field value (must be > 0; offset +
 *                 size must not exceed the type's instance_size).
 * @field sub_type Optional type id (e.g. referenced resource type or enum
 *                 backing type); SK_TYPE_ID_ZERO when unused.
 */
typedef struct sk_resource_field_t {
	const_chr_t name;
	u32 index;
	sk_resource_field_type_t type;
	u32 offset;
	u32 size;
	sk_type_id_t sub_type;
} sk_resource_field_t;

/**
 * Manual resource type descriptor handed to register_type. The repository
 * copies everything it retains (name, field array, and a deep copy of
 * @p defaults when present), so the descriptor may be transient.
 *
 * @field type_id       Compile-time type identity (must not be
 *                      SK_TYPE_ID_ZERO; must be unique within a repository).
 * @field name          Unique type name for find_type_by_name (must not be
 *                      NULL or empty; must be unique within a repository).
 * @field instance_size Byte size of one resource instance blob (must be > 0).
 * @field fields        Field descriptor array (may be NULL when
 *                      @p field_count == 0).
 * @field field_count   Number of field descriptors.
 * @field defaults      Optional default instance blob of @p instance_size
 *                      bytes; when non-NULL, every resource created from this
 *                      type is a deep copy of it, otherwise instances are
 *                      zero-initialized.
 */
typedef struct sk_resource_type_desc_t {
	sk_type_id_t type_id;
	const_chr_t name;
	u32 instance_size;
	const sk_resource_field_t* fields;
	u32 field_count;
	const void* defaults;
} sk_resource_type_desc_t;

/* ------------------------------------------------------------------ */
/*  Opaque module objects                                              */
/* ------------------------------------------------------------------ */

/** Opaque repository: owns pages, type registry, and UUID/path indexes. */
typedef struct sk_repository_t sk_repository_t;

/** Opaque registered resource type (descriptor copy owned by a repository). */
typedef struct sk_resource_type_t sk_resource_type_t;

/**
 * Global repository module API. One table, filled once in repository.c; call
 * sk_repository_api() to obtain it. Every entry takes an explicit
 * sk_repository_t* (or an allocator for create) — no process-global state.
 */
typedef struct sk_repository_api_t {
	/**
	 * Create a repository. All subsequent allocations for this repository use
	 * @p allocator.
	 * @param allocator Allocator for all repository-owned memory (must not be
	 *                  NULL; must outlive the repository).
	 * @return New repository, or NULL on allocation failure.
	 */
	sk_repository_t* (*create)(const sk_allocator_t* allocator);

	/**
	 * Destroy a repository and everything it owns (pages, resources, types,
	 * indexes). All handles obtained from @p repository become invalid.
	 * @param repository Repository to destroy (must not be NULL).
	 */
	void (*destroy)(sk_repository_t* repository);

	/**
	 * Register a resource type. The descriptor's name, fields, and defaults
	 * are copied. Fails when the type id or name is already registered.
	 * @param repository Target repository (must not be NULL).
	 * @param desc       Type descriptor (must not be NULL).
	 * @return 0 on success, -1 on duplicate type id, -2 on duplicate name,
	 *         -3 on allocation failure, -4 on an invalid descriptor.
	 */
	i32 (*register_type)(sk_repository_t* repository, const sk_resource_type_desc_t* desc);

	/**
	 * Look up a registered type by identity.
	 * @param repository Repository (must not be NULL).
	 * @param type_id    Type identity.
	 * @return The type, or NULL when not registered.
	 */
	const sk_resource_type_t* (*find_type)(const sk_repository_t* repository, sk_type_id_t type_id);

	/**
	 * Look up a registered type by name.
	 * @param repository Repository (must not be NULL).
	 * @param name       Type name.
	 * @return The type, or NULL when not registered.
	 */
	const sk_resource_type_t* (*find_type_by_name)(const sk_repository_t* repository, const_chr_t name);

	/**
	 * Create a resource of @p type. When @p uuid is non-zero and already
	 * registered to a live resource, returns that resource's RID (idempotent).
	 * The instance blob is a deep copy of the type's defaults when present,
	 * otherwise zero-initialized.
	 * @param repository Repository (must not be NULL).
	 * @param type       Registered type (must belong to @p repository).
	 * @param uuid       UUID for the resource, or SK_UUID_ZERO for none.
	 * @return The new resource's RID, or SK_RID_ZERO on allocation failure.
	 */
	sk_rid_t (*create_resource)(sk_repository_t* repository, const sk_resource_type_t* type, sk_uuid_t uuid);

	/**
	 * Destroy a resource: frees its instance and path, unregisters its UUID
	 * and path, and releases the storage slot.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @return 0 on success, -1 when @p rid is not a live resource.
	 */
	i32 (*destroy_resource)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Whether @p rid maps to a live resource.
	 * @param repository Repository (must not be NULL).
	 * @param rid        RID to test.
	 * @return Non-zero when live.
	 */
	i32 (*has_resource)(const sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Number of live resources in @p repository.
	 * @param repository Repository (must not be NULL).
	 * @return Live resource count.
	 */
	u64 (*resource_count)(const sk_repository_t* repository);

	/**
	 * Type of a live resource.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @return The resource's type, or NULL when @p rid is not live.
	 */
	const sk_resource_type_t* (*resource_type)(const sk_repository_t* repository, sk_rid_t rid);

	/**
	 * UUID of a live resource (SK_UUID_ZERO when none was assigned).
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @return The resource's UUID.
	 */
	sk_uuid_t (*resource_uuid)(const sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Mutable pointer to a live resource's instance blob (its type's
	 * instance_size bytes). Indirection field pointers inside the blob are
	 * repository-owned — do not replace them.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @return The instance blob, or NULL when @p rid is not live.
	 */
	void_ptr_t (*resource_instance)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Look up the live resource registered under @p uuid.
	 * @param repository Repository (must not be NULL).
	 * @param uuid       UUID to find (SK_UUID_ZERO never matches).
	 * @return The resource's RID, or SK_RID_ZERO when not found.
	 */
	sk_rid_t (*find_by_uuid)(const sk_repository_t* repository, sk_uuid_t uuid);

	/**
	 * Assign a path to a resource. Paths are unique: assigning a path already
	 * held by another live resource fails and leaves both unchanged. Replacing
	 * the path on a resource removes the previous mapping.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @param path       Path to assign (must not be NULL).
	 * @return 0 on success, -1 when @p rid is not live or @p path is NULL,
	 *         -2 when @p path is already held by another resource,
	 *         -3 on allocation failure (path left unchanged).
	 */
	i32 (*set_path)(sk_repository_t* repository, sk_rid_t rid, const_chr_t path);

	/**
	 * Path currently assigned to a live resource.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @return The path, or NULL when unset or @p rid is not live.
	 */
	const_chr_t (*get_path)(const sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Look up the live resource currently holding @p path.
	 * @param repository Repository (must not be NULL).
	 * @param path       Path to find.
	 * @return The resource's RID, or SK_RID_ZERO when not found.
	 */
	sk_rid_t (*find_by_path)(const sk_repository_t* repository, const_chr_t path);
} sk_repository_api_t;

/**
 * The repository module's API table (static, filled once).
 * @return Non-NULL pointer to the process-wide sk_repository_api_t.
 */
SK_API const sk_repository_api_t* sk_repository_api(void);

#ifdef __cplusplus
}
#endif
