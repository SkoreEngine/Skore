#pragma once

/**
 * @file repository.h
 * @brief Resource storage foundation: manual type registry + paged RID store.
 *
 * C port of the main-branch Skore Resources storage model, restricted to the
 * storage layer: stable UUIDs and RIDs, manual field/type descriptors, a
 * page-allocated resource store keyed by RID with UUID and path uniqueness,
 * default-value deep copy, plus the hierarchy layer: lock-free Read, exclusive
 * Write/Commit with multi-commit CAS publish, Clone, CreateFromPrototype,
 * parent / prototype linkage, sub-objects with SubObjectList propagation,
 * garbage collection of superseded instances, and undo/redo scopes that record
 * before/after instance snapshots for scoped commits and structural mutations.
 * Reflection, events, and serialization are intentionally absent.
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
 *
 * Every instance additionally carries a per-field "has value on this object"
 * bitmap (repository-owned, after the blob in the same allocation). Scalar
 * reads fall back through the prototype chain when a field is unset on this
 * object, so prototype instances inherit values lazily and instance overrides
 * (Set accessors) shadow later prototype edits. Sub-object and reference
 * fields are always materialized on prototype instances (mirrors).
 *
 * Indirection fields (String / Blob / Buffer / ReferenceArray /
 * SubObjectList) hold repository-owned heap payloads deep-copied from the
 * descriptor defaults; all other field types are copied by bytes.
 *
 * # Concurrency
 *
 * Read is lock-free: one atomic load of the current instance pointer pins a
 * consistent snapshot; readers never block behind writers. Write acquires the
 * repository write lock and returns an exclusively-owned mutable copy
 * (copy-on-write); exactly one write view may be live at a time. Commit marks
 * the copy read-only, CAS-publishes it over the instance it was written on
 * top of (a lost CAS discards the uncommitted copy), enqueues the replaced
 * instance for garbage collection, bumps the version, and refreshes parent
 * links. Replaced instances are only freed by garbage_collect (or end_frame);
 * do not collect while any read or write view is live.
 *
 * # Hierarchy
 *
 * Destroy recursively destroys sub-objects, Clone deep-clones a resource with
 * its whole sub-object tree (references into the cloned subtree are remapped),
 * and CreateFromPrototype mirrors a prototype resource (sub-objects are
 * re-created per instance and linked back through GetPrototype; the prototype
 * tracks its instances so later SubObjectList edits are propagated to them on
 * Commit). Parent / prototype pointers are main-thread data.
 *
 * Intentional gaps for this revision: reflection-driven registration, event
 * dispatch, and serialization loaders. Undo/redo scopes ARE implemented:
 * sk_undo_redo_scope_t records deep-copied before/after instance snapshots for
 * every scoped Commit and structural mutation (create_resource,
 * destroy_resource, clone, create_from_prototype); Undo restores the before
 * snapshots in reverse order and Redo reapplies the after snapshots, both
 * bumping versions. Scope-owned copies stay alive until the scope is destroyed
 * (they are never collected). Per-field has-value bits ARE implemented (drives
 * prototype inheritance / overrides); the SubObjectList "removed-from-prototype"
 * set (prototypeRemoved) is implemented and honored by SubObjectList
 * propagation.
 */

#include "allocator.h"
#include "common.h"
#include "math3d.h"

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
 * In-blob storage for a Buffer field: a repository-owned heap byte range.
 *
 * Ownership contract: the repository deep-copies the caller's bytes on
 * set_buffer (allocated with the repository's allocator), so the caller keeps
 * ownership of its input and may free or mutate it immediately after the call.
 * get_buffer returns a borrowed pointer valid until the next write / commit on
 * the resource (or the resource's destruction). An empty buffer (set with
 * size 0) is distinct from an unset buffer: both read back as NULL / size 0,
 * but the empty buffer has its has-value bit set, so has_value_on_this_object
 * distinguishes the two and the empty buffer shadows a prototype value.
 */
typedef struct sk_field_buffer_t {
	u8* data;
	u32 size;
} sk_field_buffer_t;

/**
 * In-blob storage for ReferenceArray fields: a repository-owned heap array of
 * RIDs. @p capacity tracks the allocated slots (== @p count on deep copies).
 */
typedef struct sk_field_rid_array_t {
	sk_rid_t* items;
	u32 count;
	u32 capacity;
} sk_field_rid_array_t;

/**
 * In-blob storage for a SubObjectList field: the owned sub-object RID array
 * plus the owned "removed-from-prototype" set. @p prototype_removed records
 * prototype RIDs this instance explicitly removed (an override); SubObjectList
 * propagation skips those so a later prototype re-add does not resurrect them
 * on this instance. Both arrays are repository-owned heap copies.
 */
typedef struct sk_field_subobject_list_t {
	sk_rid_t* items;
	u32 count;
	u32 capacity;
	sk_rid_t* prototype_removed;
	u32 prototype_removed_count;
	u32 prototype_removed_capacity;
} sk_field_subobject_list_t;

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
 *                 size must not exceed the type's instance_size). SubObjectList
 *                 fields must use the sk_field_subobject_list_t layout, Blob
 *                 fields sk_field_blob_t, Buffer fields sk_field_buffer_t,
 *                 String fields sk_field_string_t, and ReferenceArray fields
 *                 sk_field_rid_array_t.
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
 * Opaque undo/redo scope: records ordered before/after instance snapshots for
 * every scoped Commit and structural mutation. Undo restores the before
 * snapshots in reverse order; Redo reapplies the after snapshots. Snapshot
 * copies are owned by the scope and are never collected by garbage_collect
 * while the scope is alive; destroy the scope before its repository.
 */
typedef struct sk_undo_redo_scope_t sk_undo_redo_scope_t;

/**
 * Resource read/write view (C port of the main-branch ResourceObject).
 *
 * - Read views (read) own nothing: @p instance pins the published instance
 *   snapshot at Read time and is only borrowed.
 * - Write views (write) exclusively own @p instance (a fresh copy) until
 *   Commit or Discard; the repository write lock is held for the view's
 *   lifetime. Field Set/Get on a write view take no repository locks.
 *
 * All fields are internal; construct a view via read/write and test validity
 * with SK_RESOURCE_OBJECT_IS_VALID.
 */
typedef struct sk_resource_object_t {
	sk_repository_t* repo;	  /* owning repository (internal) */
	void_ptr_t storage;		  /* opaque storage slot (internal) */
	void_ptr_t instance;	  /* pinned published block (read) / owned write block (write) */
	void_ptr_t data_on_write; /* instance this write view was copied from (internal) */
	u8 is_write;
	u8 _pad0[7];
} sk_resource_object_t;

/** Zero / invalid resource object view. */
#define SK_RESOURCE_OBJECT_ZERO ((sk_resource_object_t){NULL, NULL, NULL, NULL, 0u, {0u}})

/** Non-zero when @p view references a live storage slot. */
#define SK_RESOURCE_OBJECT_IS_VALID(view) ((view).storage != NULL)

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
	 * otherwise zero-initialized. When @p scope is non-NULL and the create
	 * succeeds, a change (before = no value, after = the new instance) is
	 * pushed so Undo drops the value and Redo restores it.
	 * @param repository Repository (must not be NULL).
	 * @param type       Registered type (must belong to @p repository).
	 * @param uuid       UUID for the resource, or SK_UUID_ZERO for none.
	 * @param scope      Optional undo/redo scope to record the create into, or
	 *                   NULL to skip recording.
	 * @return The new resource's RID, or SK_RID_ZERO on allocation failure.
	 */
	sk_rid_t (*create_resource)(sk_repository_t* repository, const sk_resource_type_t* type, sk_uuid_t uuid, sk_undo_redo_scope_t* scope);

	/**
	 * Destroy a resource: frees its instance and path, unregisters its UUID
	 * and path, and releases the storage slot. When @p scope is non-NULL a
	 * change (before = the live instance, after = no value) is pushed so Undo
	 * restores the resource and Redo drops it again. The parent detach and any
	 * recursively destroyed sub-objects are recorded into the same scope.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Live resource RID.
	 * @param scope      Optional undo/redo scope to record the destroy into, or
	 *                   NULL to skip recording.
	 * @return 0 on success, -1 when @p rid is not a live resource.
	 */
	i32 (*destroy_resource)(sk_repository_t* repository, sk_rid_t rid, sk_undo_redo_scope_t* scope);

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

	/* ---- read / write / commit ---- */

	/**
	 * Lock-free read: atomically loads and pins the current published instance.
	 * Never takes the write lock, so concurrent readers and writers never block
	 * on each other. The returned read view owns nothing and needs no teardown.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Resource id.
	 * @return Read view; an invalid view for an unknown / destroyed rid.
	 */
	sk_resource_object_t (*read)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Begin a write transaction: acquires the repository write lock, snapshots
	 * the current published instance (copy-on-write) or allocates a fresh one,
	 * and returns an exclusively-owned mutable write view. Field Set/Get on the
	 * returned view take no repository locks. Exactly one write view may be
	 * live per repository — commit or discard it before starting another Write.
	 * @param repository Repository (must not be NULL).
	 * @param rid        Resource id.
	 * @return Mutable write view; an invalid view on failure (unknown rid,
	 *         untyped slot, OOM). The write lock is released on failure.
	 */
	sk_resource_object_t (*write)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Publish a write view: mark the instance read-only, CAS-publish it over
	 * the instance it was written on top of (a lost CAS discards the
	 * uncommitted copy), enqueue the replaced instance for garbage collection,
	 * bump the version, refresh sub-object parent links, propagate SubObjectList
	 * edits to prototype instances, and release the write lock. When @p scope
	 * is non-NULL and the publish succeeds, a change (before = the instance the
	 * view was written on top of, after = the published instance) is pushed.
	 * Prototype propagation performed by this commit is recorded into the same
	 * scope. Calling Commit on a read / invalid / already-committed-or-discarded
	 * view is a no-op.
	 * @param view  Write view from write.
	 * @param scope Optional undo/redo scope to record the commit into, or NULL
	 *              to skip recording.
	 */
	void (*commit)(sk_resource_object_t view, sk_undo_redo_scope_t* scope);

	/**
	 * Abandon a write view: free the uncommitted instance and release the write
	 * lock. Safe on read / invalid / already-committed views (no-op).
	 * @param view Write view from write.
	 */
	void (*discard)(sk_resource_object_t view);

	/**
	 * Current storage version of a live resource (starts at 1; lock-free load).
	 * @param repository Repository (must not be NULL).
	 * @param rid        Resource id.
	 * @return Version, or 0 for an unknown rid.
	 */
	u64 (*get_version)(const sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Whether a live resource has a published instance (lock-free load).
	 * @param repository Repository (must not be NULL).
	 * @param rid        Resource id.
	 * @return Non-zero when published.
	 */
	i32 (*has_value)(const sk_repository_t* repository, sk_rid_t rid);

	/**
	 * Free all instances replaced by Commit / Destroy that were queued for
	 * garbage collection. Must not run while any read or write view is live
	 * (read snapshots borrow queued instances). Does not take the write lock.
	 * @param repository Repository (must not be NULL).
	 */
	void (*garbage_collect)(sk_repository_t* repository);

	/**
	 * End-of-frame helper (Resources::EndFrame equivalent): frees all instances
	 * superseded by Commit / Destroy since the previous call via garbage_collect.
	 * @param repository Repository (must not be NULL).
	 */
	void (*end_frame)(sk_repository_t* repository);

	/* ---- clone / prototype / hierarchy ---- */

	/**
	 * Deep clone a resource together with its whole sub-object tree.
	 *
	 * Sub-objects of @p origin are re-created as new resources whose parent is
	 * the cloned resource; Reference / ReferenceArray fields that point at
	 * resources inside @p origin's sub-object subtree are remapped to their
	 * clones. Everything else is deep-copied (the clone's per-field has-value
	 * bits match the origin's). Clones of prototype instances keep their
	 * prototype pointer and stay registered in that prototype's instance set,
	 * so later prototype edits propagate to the clone on Commit. When @p uuid
	 * is valid it is registered in the by-uuid map (a uuid already owned by
	 * another live resource fails the clone); otherwise a fresh uuid is
	 * generated when @p origin carries one. On OOM every already-reserved clone
	 * slot is rolled back and the repository is left usable. When @p scope is
	 * non-NULL, every freshly created clone slot (root and each cloned
	 * sub-object) records a change (before = no value, after = the new
	 * instance).
	 * @param repository Repository (must not be NULL).
	 * @param origin     Existing resource id.
	 * @param uuid       Optional uuid; SK_UUID_ZERO to auto-generate.
	 * @param scope      Optional undo/redo scope to record the clone into, or
	 *                   NULL to skip recording.
	 * @return The new resource's RID, or SK_RID_ZERO on failure.
	 */
	sk_rid_t (*clone)(sk_repository_t* repository, sk_rid_t origin, sk_uuid_t uuid, sk_undo_redo_scope_t* scope);

	/**
	 * Create a new resource that mirrors @p prototype.
	 *
	 * The new instance mirrors the prototype's sub-object tree: sub-objects are
	 * re-created per instance and linked back to the prototype's sub-objects via
	 * get_prototype; references into the prototype subtree are remapped; scalar
	 * fields stay unset on this object and inherit lazily through the prototype
	 * chain. The new RID is registered in the prototype's instance set so later
	 * prototype SubObjectList edits propagate to it on Commit. When @p uuid is
	 * valid it is registered in the by-uuid map (a duplicate fails the call);
	 * otherwise a fresh uuid is generated when @p prototype carries one. On OOM
	 * every already-reserved mirror slot is rolled back. When @p scope is
	 * non-NULL, every freshly created mirror slot (root and each sub-object
	 * mirror) records a change (before = no value, after = the new instance).
	 * @param repository Repository (must not be NULL).
	 * @param prototype  Existing resource used as the prototype (must be typed).
	 * @param uuid       Optional uuid; SK_UUID_ZERO to auto-generate.
	 * @param scope      Optional undo/redo scope to record the create into, or
	 *                   NULL to skip recording.
	 * @return The new resource's RID, or SK_RID_ZERO on failure.
	 */
	sk_rid_t (*create_from_prototype)(sk_repository_t* repository, sk_rid_t prototype, sk_uuid_t uuid, sk_undo_redo_scope_t* scope);

	/**
	 * @return The RID of the resource that owns @p rid as a sub-object, or
	 *         SK_RID_ZERO when it has no parent / @p rid is unknown.
	 */
	sk_rid_t (*get_parent)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * @return The RID this instance was created from via create_from_prototype
	 *         (or cloned from an instance of one), or SK_RID_ZERO.
	 */
	sk_rid_t (*get_prototype)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * @return The topmost ancestor of @p rid's parent chain (the root owner),
	 *         or @p rid itself when it has no parent.
	 */
	sk_rid_t (*get_top_parent)(sk_repository_t* repository, sk_rid_t rid);

	/**
	 * @return Non-zero when @p child is a strict descendant of @p parent through
	 *         the sub-object parent chain.
	 */
	i32 (*is_parent_of)(sk_repository_t* repository, sk_rid_t parent, sk_rid_t child);

	/* ---- field accessors (no reflection; field looked up by registered index) ---- */

	/*
	 * Set accessors mutate the caller's write instance without any repository
	 * lock and return 0 on success, non-zero on failure (read view / unknown
	 * field index / field-type mismatch / OOM). Get accessors work on both read
	 * and write views and return the stored value (zero / empty when unset);
	 * scalar fields fall back through the prototype chain when unset on this
	 * object. @p index must be a field registered on the view's type.
	 */

	i32 (*set_bool)(sk_resource_object_t view, u32 index, i32 value);
	i32 (*set_int)(sk_resource_object_t view, u32 index, i64 value);
	i32 (*set_uint)(sk_resource_object_t view, u32 index, u64 value);
	i32 (*set_float)(sk_resource_object_t view, u32 index, f64 value);
	i32 (*set_vec2)(sk_resource_object_t view, u32 index, sk_vec2_t value);
	i32 (*set_vec3)(sk_resource_object_t view, u32 index, sk_vec3_t value);
	i32 (*set_vec4)(sk_resource_object_t view, u32 index, sk_vec4_t value);
	i32 (*set_quat)(sk_resource_object_t view, u32 index, sk_quat_t value);
	i32 (*set_mat4)(sk_resource_object_t view, u32 index, sk_mat44_t value);
	i32 (*set_color)(sk_resource_object_t view, u32 index, sk_color_t value);
	i32 (*set_enum)(sk_resource_object_t view, u32 index, u64 value);
	i32 (*set_string)(sk_resource_object_t view, u32 index, const_chr_t value);
	/** Replace the whole blob (bytes are deep copied; @p data may be NULL when
	 *  @p size is 0, which clears the field). */
	i32 (*set_blob)(sk_resource_object_t view, u32 index, const void* data, u32 size);
	/** Replace the whole buffer payload. The bytes are deep copied with the
	 *  repository's allocator — the repository owns the copy and the caller
	 *  keeps ownership of @p data, which may be freed or mutated immediately
	 *  after the call. @p data may be NULL when @p size is 0, which sets an
	 *  EMPTY buffer (has-value bit set; distinct from unset, shadows a
	 *  prototype value). Overwriting releases the previous payload. */
	i32 (*set_buffer)(sk_resource_object_t view, u32 index, const void* data, u32 size);
	i32 (*set_type_id)(sk_resource_object_t view, u32 index, sk_type_id_t value);
	i32 (*set_reference)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	/** Replace the whole reference array (items are deep copied). */
	i32 (*set_reference_array)(sk_resource_object_t view, u32 index, const sk_rid_t* items, u32 count);
	/** Append one RID to a reference array (grows as needed). */
	i32 (*add_to_reference_array)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	/** Remove the first RID equal to @p rid (no-op when absent). */
	i32 (*remove_from_reference_array)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	i32 (*set_subobject)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	/** Replace the whole sub-object list (items are deep copied). */
	i32 (*set_subobject_list)(sk_resource_object_t view, u32 index, const sk_rid_t* items, u32 count);
	/** Append one sub-object RID (grows as needed; clears its prototypeRemoved). */
	i32 (*add_to_subobject_list)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	/** Remove the first sub-object RID equal to @p rid; records prototypeRemoved
	 *  when this instance is a prototype instance and the sub-object mirrors a
	 *  prototype sub-object. */
	i32 (*remove_from_subobject_list)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	/** Remove every sub-object whose prototype is @p prototype from a
	 *  sub-object-list field (used by prototype propagation); removed RIDs are
	 *  written to @p out_items (up to @p out_capacity) and their parent link is
	 *  cleared at Commit. @return number removed (0 when none / bad field). */
	u32 (*remove_from_subobject_list_by_prototype)(sk_resource_object_t view, u32 index, sk_rid_t prototype, sk_rid_t* out_items, u32 out_capacity);
	/** @return Non-zero when a sub-object-list field contains @p rid. */
	i32 (*has_on_subobject_list)(sk_resource_object_t view, u32 index, sk_rid_t rid);
	/** @return Number of live sub-objects in a sub-object-list field. */
	u32 (*subobject_list_count)(sk_resource_object_t view, u32 index);
	/** @return Non-zero when the field carries a value on this object (no chain). */
	i32 (*has_value_on_this_object)(sk_resource_object_t view, u32 index);
	/** @return Non-zero when the field is set on this object AND this object is
	 *         a prototype instance (i.e. it shadows the prototype). */
	i32 (*is_value_overridden)(sk_resource_object_t view, u32 index);

	i32 (*get_bool)(sk_resource_object_t view, u32 index);
	i64 (*get_int)(sk_resource_object_t view, u32 index);
	u64 (*get_uint)(sk_resource_object_t view, u32 index);
	f64 (*get_float)(sk_resource_object_t view, u32 index);
	sk_vec2_t (*get_vec2)(sk_resource_object_t view, u32 index);
	sk_vec3_t (*get_vec3)(sk_resource_object_t view, u32 index);
	sk_vec4_t (*get_vec4)(sk_resource_object_t view, u32 index);
	sk_quat_t (*get_quat)(sk_resource_object_t view, u32 index);
	sk_mat44_t (*get_mat4)(sk_resource_object_t view, u32 index);
	sk_color_t (*get_color)(sk_resource_object_t view, u32 index);
	u64 (*get_enum)(sk_resource_object_t view, u32 index);
	/** @return Borrowed NUL-terminated string, or NULL when unset. */
	const_chr_t (*get_string)(sk_resource_object_t view, u32 index);
	/** @return Borrowed blob bytes, or NULL when unset / empty; @p out_size
	 *         receives the size (0 when unset; @p out_size may be NULL). */
	const u8* (*get_blob)(sk_resource_object_t view, u32 index, u32* out_size);
	/** @return Borrowed buffer bytes (repository-owned; valid until the next
	 *         write / commit on the resource or its destruction), or NULL when
	 *         unset / empty; @p out_size receives the size (0 when unset or
	 *         empty; @p out_size may be NULL). Falls back through the
	 *         prototype chain when unset on this object. Use
	 *         has_value_on_this_object to distinguish an empty-but-set buffer
	 *         from an unset one. */
	const u8* (*get_buffer)(sk_resource_object_t view, u32 index, u32* out_size);
	sk_type_id_t (*get_type_id)(sk_resource_object_t view, u32 index);
	sk_rid_t (*get_reference)(sk_resource_object_t view, u32 index);
	/** @return Borrowed items array; @p out_count receives the count (0 when
	 *         unset; @p out_count may be NULL). */
	const sk_rid_t* (*get_reference_array)(sk_resource_object_t view, u32 index, u32* out_count);
	sk_rid_t (*get_subobject)(sk_resource_object_t view, u32 index);
	/** @return Borrowed items array; @p out_count receives the count (0 when
	 *         unset; @p out_count may be NULL). */
	const sk_rid_t* (*get_subobject_list)(sk_resource_object_t view, u32 index, u32* out_count);

	/* ---- undo / redo scopes ---- */

	/**
	 * Create an undo/redo scope. The scope owns deep copies of every recorded
	 * before/after instance snapshot; they are never collected by
	 * garbage_collect and are released by undo_redo_scope_destroy. A scope must
	 * be destroyed before the repository its changes reference.
	 * @param allocator Allocator for the scope's own bookkeeping (must not be
	 *                  NULL; must outlive the scope).
	 * @param name      Human-readable scope name; copied (may be NULL).
	 * @return New scope, or NULL on allocation failure.
	 */
	sk_undo_redo_scope_t* (*undo_redo_scope_create)(const sk_allocator_t* allocator, const_chr_t name);

	/**
	 * Destroy a scope and release its snapshot copies. Safe on NULL. Any
	 * repository referenced by the scope's changes must outlive the scope.
	 * @param scope Scope to destroy, or NULL.
	 */
	void (*undo_redo_scope_destroy)(sk_undo_redo_scope_t* scope);

	/**
	 * Undo every change recorded in @p scope in reverse order: for each change
	 * the storage's published instance is replaced with a fresh deep copy of
	 * the change's before snapshot (or cleared when the before snapshot is no
	 * value), the version (and each ancestor's) is bumped, and the superseded
	 * instance is queued for garbage collection. A destroyed resource's slot is
	 * re-created (uuid, path, prototype link restored). Changes whose storage
	 * slot no longer exists and whose before snapshot is no value (rolled-back
	 * operations) are skipped.
	 * @param scope Scope to undo (must not be NULL).
	 */
	void (*undo_redo_scope_undo)(sk_undo_redo_scope_t* scope);

	/**
	 * Redo every change recorded in @p scope in forward order: for each change
	 * the storage's published instance is replaced with a fresh deep copy of
	 * the change's after snapshot (or cleared when the after snapshot is no
	 * value), the version chain is bumped, and the superseded instance is
	 * queued for garbage collection.
	 * @param scope Scope to redo (must not be NULL).
	 */
	void (*undo_redo_scope_redo)(sk_undo_redo_scope_t* scope);

	/**
	 * @return The scope's name (borrowed; valid until the scope is destroyed).
	 */
	const_chr_t (*undo_redo_scope_get_name)(const sk_undo_redo_scope_t* scope);
} sk_repository_api_t;

/**
 * The repository module's API table (static, filled once).
 * @return Non-NULL pointer to the process-wide sk_repository_api_t.
 */
SK_API const sk_repository_api_t* sk_repository_api(void);

#ifdef __cplusplus
}
#endif
