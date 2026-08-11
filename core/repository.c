#include "repository.h"

#include "array.h"
#include "atomics.h"
#include "hashmap.h"
#include "mutex.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal layout                                                   */
/* ------------------------------------------------------------------ */

#define SK_REPOSITORY_PAGE_BITS 12u
#define SK_REPOSITORY_PAGE_SIZE (1u << SK_REPOSITORY_PAGE_BITS)
#define SK_REPOSITORY_PAGE_MASK (SK_REPOSITORY_PAGE_SIZE - 1u)

/* Owned RID list used by storage and propagation bookkeeping. */
typedef SK_ARRAY(sk_rid_t) sk_rid_list_t;

/* Opaque storage slot: one live resource inside a page. `instance` and
 * `version` are accessed atomically (lock-free readers); the parent /
 * prototype linkage is main-thread data. */
typedef struct sk_resource_storage_t {
	sk_rid_t rid;
	sk_uuid_t uuid;
	sk_resource_type_t* type;
	char* path; /* NULL when unset */
	void_ptr_t instance;
	u64 version;
	sk_rid_t parent;
	u32 parent_field_index;
	u32 _pad0;
	sk_rid_t prototype;
	sk_rid_list_t prototype_instances;
} sk_resource_storage_t;

typedef struct sk_repository_page_t {
	sk_resource_storage_t* elements; /* [SK_REPOSITORY_PAGE_SIZE] */
	u8* used;						 /* [SK_REPOSITORY_PAGE_SIZE] */
} sk_repository_page_t;

/* Replaced instances wait here until garbage_collect (lock-free readers may
 * still pin them). */
typedef struct sk_resource_gc_item_t {
	sk_resource_type_t* type;
	void_ptr_t instance;
} sk_resource_gc_item_t;

typedef SK_HASH_MAP(sk_type_id_t, sk_resource_type_t*) sk_type_id_map_t;
typedef SK_HASH_MAP(const_chr_t, sk_resource_type_t*) sk_type_name_map_t;
typedef SK_HASH_MAP(sk_uuid_t, sk_rid_t) sk_uuid_map_t;
typedef SK_HASH_MAP(const_chr_t, sk_rid_t) sk_path_map_t;

/* Clone / prototype build context: remaps source RIDs inside the origin's
 * subtree to freshly reserved destination slots. */
typedef struct sk_clone_entry_t {
	sk_rid_t dst;
	sk_uuid_t uuid;
} sk_clone_entry_t;

typedef SK_HASH_MAP(sk_rid_t, sk_clone_entry_t) sk_clone_map_t;

typedef struct sk_clone_context_t {
	sk_repository_t* repo;
	sk_rid_t origin;
	sk_clone_map_t map;
	sk_undo_redo_scope_t* scope; /* optional scope that records each created slot */
	i32 failed;
} sk_clone_context_t;

struct sk_repository_t {
	const sk_allocator_t* allocator;
	SK_ARRAY(sk_repository_page_t*) pages;
	u64 rid_counter;	/* starts at 1; RID 0 reserved invalid */
	u64 resource_count; /* live resource count */
	sk_type_id_map_t types_by_id;
	sk_type_name_map_t types_by_name;
	sk_uuid_map_t rids_by_uuid;
	sk_path_map_t rids_by_path;
	SK_ARRAY(sk_resource_gc_item_t) to_collect;
	sk_mutex_t* write_lock;
	u64 uuid_counter; /* per-repository uuid source; uuid hi seeds by repo */
};

struct sk_resource_type_t {
	sk_type_id_t type_id;
	char* name; /* owned copy */
	u32 instance_size;
	u32 field_count;
	sk_resource_field_t* fields; /* owned array */
	u32 bitmap_bytes;			 /* (field_count + 7) / 8 */
	u8* defaults;				 /* owned block (blob + bitmap, all bits set), or NULL */
};

/* One recorded mutation: deep-copied before/after instance snapshots for a
 * storage slot. `before` / `after` are repository-allocated instance blocks
 * owned by the scope (NULL = no value); `path` is a scope-owned string (or
 * NULL) used to restore a destroyed resource's slot on Undo. `structural`
 * marks mutations that own the slot lifecycle (create / destroy / clone /
 * create_from_prototype): applying their no-value snapshot fully releases the
 * slot again instead of leaving an empty one. The type pointer lives in the
 * repository and must outlive the scope. */
typedef struct sk_undo_redo_change_t {
	sk_repository_t* repo;
	sk_rid_t rid;
	sk_uuid_t uuid;
	sk_resource_type_t* type;
	sk_rid_t prototype;
	char* path;
	void_ptr_t before;
	void_ptr_t after;
	sk_rid_t parent; /* parent link restored when the slot is re-created */
	u32 parent_field_index;
	u8 structural;
	u8 _pad0[3];
} sk_undo_redo_change_t;

struct sk_undo_redo_scope_t {
	const sk_allocator_t* allocator; /* scope struct / name / changes / path strings */
	char* name;						 /* owned copy */
	SK_ARRAY(sk_undo_redo_change_t) changes;
};

static void sk_repo_scope_push_change(sk_undo_redo_scope_t* scope, sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t before, void_ptr_t after,
									  i32 structural);

/* ------------------------------------------------------------------ */
/*  Instance block helpers                                            */
/* ------------------------------------------------------------------ */

/* Every instance is one allocation: the caller-laid-out blob followed by the
 * per-field "has value on this object" bitmap. The instance pointer returned
 * by resource_instance / read / write is the blob start. */

static const u8* sk_repo_instance_bitmap(const sk_resource_type_t* type, const u8* instance) {
	return instance + (size_t)type->instance_size;
}

static int sk_repo_instance_has_value(const sk_resource_type_t* type, const u8* instance, u32 position) {
	const u8* bitmap = sk_repo_instance_bitmap(type, instance);
	return (bitmap[position >> 3u] & (u8)(1u << (position & 7u))) != 0u;
}

static void sk_repo_instance_set_value_bit(const sk_resource_type_t* type, void_ptr_t instance, u32 position, int value) {
	u8* bitmap = (u8*)instance + (size_t)type->instance_size;
	if (value != 0) {
		bitmap[position >> 3u] |= (u8)(1u << (position & 7u));
	} else {
		bitmap[position >> 3u] &= (u8) ~(1u << (position & 7u));
	}
}

static u8* sk_repo_block_alloc_zero(const sk_repository_t* repository, const sk_resource_type_t* type) {
	size_t total = (size_t)type->instance_size + (size_t)type->bitmap_bytes;
	u8* block = (u8*)repository->allocator->alloc(repository->allocator->instance, total);
	if (block == NULL) {
		return NULL;
	}
	memset(block, 0, total);
	return block;
}

static i32 sk_repo_field_position(const sk_resource_type_t* type, u32 index) {
	for (u32 i = 0u; i < type->field_count; ++i) {
		if (type->fields[i].index == index) {
			return (i32)i;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/*  Copy helpers                                                      */
/* ------------------------------------------------------------------ */

static u8* sk_repo_copy_bytes(const sk_repository_t* repository, const void* src, size_t size) {
	void* dst = repository->allocator->alloc(repository->allocator->instance, size);
	if (dst == NULL) {
		return NULL;
	}
	memcpy(dst, src, size);
	return (u8*)dst;
}

static char* sk_repo_copy_string(const sk_repository_t* repository, const_chr_t src) {
	size_t len = strlen(src);
	char* dst = (char*)repository->allocator->alloc(repository->allocator->instance, len + 1u);
	if (dst == NULL) {
		return NULL;
	}
	memcpy(dst, src, len + 1u);
	return dst;
}

/* Release the indirection payload of one field (String / Blob / RID arrays /
 * SubObjectList with its removed-from-prototype set). */
static void sk_repo_field_destroy(const sk_repository_t* repository, const sk_resource_field_t* field, u8* instance) {
	u8* p = instance + (size_t)field->offset;
	switch (field->type) {
	case SK_RESOURCE_FIELD_TYPE_STRING: {
		sk_field_string_t* s = (sk_field_string_t*)(void_ptr_t)p;
		if (s->chars != NULL) {
			repository->allocator->free(repository->allocator->instance, s->chars);
			s->chars = NULL;
		}
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_BLOB: {
		sk_field_blob_t* b = (sk_field_blob_t*)(void_ptr_t)p;
		if (b->data != NULL) {
			repository->allocator->free(repository->allocator->instance, b->data);
			b->data = NULL;
		}
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_BUFFER: {
		sk_field_buffer_t* b = (sk_field_buffer_t*)(void_ptr_t)p;
		if (b->data != NULL) {
			repository->allocator->free(repository->allocator->instance, b->data);
			b->data = NULL;
		}
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
		sk_field_rid_array_t* a = (sk_field_rid_array_t*)(void_ptr_t)p;
		if (a->items != NULL) {
			repository->allocator->free(repository->allocator->instance, a->items);
			a->items = NULL;
		}
		a->count = 0u;
		a->capacity = 0u;
		break;
	}
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
		sk_field_subobject_list_t* l = (sk_field_subobject_list_t*)(void_ptr_t)p;
		if (l->items != NULL) {
			repository->allocator->free(repository->allocator->instance, l->items);
			l->items = NULL;
		}
		l->count = 0u;
		l->capacity = 0u;
		if (l->prototype_removed != NULL) {
			repository->allocator->free(repository->allocator->instance, l->prototype_removed);
			l->prototype_removed = NULL;
		}
		l->prototype_removed_count = 0u;
		l->prototype_removed_capacity = 0u;
		break;
	}
	/* POD field types own no heap storage. */
	case SK_RESOURCE_FIELD_TYPE_NONE:
	case SK_RESOURCE_FIELD_TYPE_BOOL:
	case SK_RESOURCE_FIELD_TYPE_INT:
	case SK_RESOURCE_FIELD_TYPE_UINT:
	case SK_RESOURCE_FIELD_TYPE_FLOAT:
	case SK_RESOURCE_FIELD_TYPE_VEC2:
	case SK_RESOURCE_FIELD_TYPE_VEC3:
	case SK_RESOURCE_FIELD_TYPE_VEC4:
	case SK_RESOURCE_FIELD_TYPE_QUAT:
	case SK_RESOURCE_FIELD_TYPE_MAT4:
	case SK_RESOURCE_FIELD_TYPE_COLOR:
	case SK_RESOURCE_FIELD_TYPE_ENUM:
	case SK_RESOURCE_FIELD_TYPE_REFERENCE:
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT:
	case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
	case SK_RESOURCE_FIELD_TYPE_MAX:
		break;
	}
}

static void sk_repo_instance_destroy_fields(const sk_repository_t* repository, const sk_resource_type_t* type, u8* instance) {
	for (u32 i = 0u; i < type->field_count; ++i) {
		sk_repo_field_destroy(repository, &type->fields[i], instance);
	}
}

static void sk_repo_instance_destroy(const sk_repository_t* repository, const sk_resource_type_t* type, void* instance) {
	if (instance == NULL) {
		return;
	}
	sk_repo_instance_destroy_fields(repository, type, (u8*)instance);
	repository->allocator->free(repository->allocator->instance, instance);
}

/* Deep-copy @p source's blob fields into @p dest (both of type->instance_size
 * bytes). Indirection fields get repository-owned copies. On failure the
 * partially copied blob is released (its indirections are destroyed) and
 * non-zero is returned. */
static i32 sk_repo_copy_blob_fields(const sk_repository_t* repository, const sk_resource_type_t* type, u8* dest, const u8* source) {
	for (u32 i = 0u; i < type->field_count; ++i) {
		const sk_resource_field_t* field = &type->fields[i];
		u8* dst = dest + (size_t)field->offset;
		switch (field->type) {
		case SK_RESOURCE_FIELD_TYPE_STRING: {
			const sk_field_string_t* src = (const sk_field_string_t*)(const_ptr_t)(source + (size_t)field->offset);
			sk_field_string_t* out = (sk_field_string_t*)(void_ptr_t)dst;
			if (src->chars != NULL) {
				out->chars = sk_repo_copy_string(repository, src->chars);
				if (out->chars == NULL) {
					goto fail;
				}
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_BLOB: {
			const sk_field_blob_t* src = (const sk_field_blob_t*)(const_ptr_t)(source + (size_t)field->offset);
			sk_field_blob_t* out = (sk_field_blob_t*)(void_ptr_t)dst;
			if (src->size != 0u && src->data != NULL) {
				out->data = (u8*)(void_ptr_t)sk_repo_copy_bytes(repository, src->data, (size_t)src->size);
				if (out->data == NULL) {
					goto fail;
				}
				out->size = src->size;
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_BUFFER: {
			const sk_field_buffer_t* src = (const sk_field_buffer_t*)(const_ptr_t)(source + (size_t)field->offset);
			sk_field_buffer_t* out = (sk_field_buffer_t*)(void_ptr_t)dst;
			if (src->size != 0u && src->data != NULL) {
				out->data = (u8*)(void_ptr_t)sk_repo_copy_bytes(repository, src->data, (size_t)src->size);
				if (out->data == NULL) {
					goto fail;
				}
				out->size = src->size;
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
			const sk_field_rid_array_t* src = (const sk_field_rid_array_t*)(const_ptr_t)(source + (size_t)field->offset);
			sk_field_rid_array_t* out = (sk_field_rid_array_t*)(void_ptr_t)dst;
			if (src->count != 0u && src->items != NULL) {
				size_t bytes = (size_t)src->count * sizeof(sk_rid_t);
				out->items = (sk_rid_t*)(void_ptr_t)sk_repo_copy_bytes(repository, src->items, bytes);
				if (out->items == NULL) {
					goto fail;
				}
				out->count = src->count;
				out->capacity = src->count;
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
			const sk_field_subobject_list_t* src = (const sk_field_subobject_list_t*)(const_ptr_t)(source + (size_t)field->offset);
			sk_field_subobject_list_t* out = (sk_field_subobject_list_t*)(void_ptr_t)dst;
			if (src->count != 0u && src->items != NULL) {
				size_t bytes = (size_t)src->count * sizeof(sk_rid_t);
				out->items = (sk_rid_t*)(void_ptr_t)sk_repo_copy_bytes(repository, src->items, bytes);
				if (out->items == NULL) {
					goto fail;
				}
				out->count = src->count;
				out->capacity = src->count;
			}
			if (src->prototype_removed_count != 0u && src->prototype_removed != NULL) {
				size_t bytes = (size_t)src->prototype_removed_count * sizeof(sk_rid_t);
				out->prototype_removed = (sk_rid_t*)(void_ptr_t)sk_repo_copy_bytes(repository, src->prototype_removed, bytes);
				if (out->prototype_removed == NULL) {
					goto fail;
				}
				out->prototype_removed_count = src->prototype_removed_count;
				out->prototype_removed_capacity = src->prototype_removed_count;
			}
			break;
		}
		/* POD field types are copied by bytes. */
		case SK_RESOURCE_FIELD_TYPE_NONE:
		case SK_RESOURCE_FIELD_TYPE_BOOL:
		case SK_RESOURCE_FIELD_TYPE_INT:
		case SK_RESOURCE_FIELD_TYPE_UINT:
		case SK_RESOURCE_FIELD_TYPE_FLOAT:
		case SK_RESOURCE_FIELD_TYPE_VEC2:
		case SK_RESOURCE_FIELD_TYPE_VEC3:
		case SK_RESOURCE_FIELD_TYPE_VEC4:
		case SK_RESOURCE_FIELD_TYPE_QUAT:
		case SK_RESOURCE_FIELD_TYPE_MAT4:
		case SK_RESOURCE_FIELD_TYPE_COLOR:
		case SK_RESOURCE_FIELD_TYPE_ENUM:
		case SK_RESOURCE_FIELD_TYPE_REFERENCE:
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT:
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			memcpy(dst, source + (size_t)field->offset, (size_t)field->size);
			break;
		}
	}
	return 0;

fail:
	sk_repo_instance_destroy_fields(repository, type, dest);
	return -1;
}

/* Deep-copy a full instance block (blob + has-value bitmap). @p source may be
 * NULL to get a zeroed block (all fields unset). */
static u8* sk_repo_instance_copy(const sk_repository_t* repository, const sk_resource_type_t* type, const u8* source) {
	u8* dest = sk_repo_block_alloc_zero(repository, type);
	if (dest == NULL) {
		return NULL;
	}
	if (source == NULL) {
		return dest;
	}
	memcpy(dest + (size_t)type->instance_size, source + (size_t)type->instance_size, (size_t)type->bitmap_bytes);
	if (sk_repo_copy_blob_fields(repository, type, dest, source) != 0) {
		repository->allocator->free(repository->allocator->instance, dest);
		return NULL;
	}
	return dest;
}

/* Deep-copy a raw caller-provided default blob and mark every field as set. */
static u8* sk_repo_instance_from_raw(const sk_repository_t* repository, const sk_resource_type_t* type, const void* raw) {
	u8* dest = sk_repo_block_alloc_zero(repository, type);
	if (dest == NULL) {
		return NULL;
	}
	if (sk_repo_copy_blob_fields(repository, type, dest, (const u8*)raw) != 0) {
		repository->allocator->free(repository->allocator->instance, dest);
		return NULL;
	}
	memset(dest + (size_t)type->instance_size, 0xFF, (size_t)type->bitmap_bytes);
	return dest;
}

static void sk_repo_type_free(const sk_repository_t* repository, sk_resource_type_t* type) {
	const sk_allocator_t* a = repository->allocator;
	if (type->defaults != NULL) {
		sk_repo_instance_destroy(repository, type, type->defaults);
	}
	if (type->fields != NULL) {
		a->free(a->instance, type->fields);
	}
	if (type->name != NULL) {
		a->free(a->instance, type->name);
	}
	a->free(a->instance, type);
}

/* ------------------------------------------------------------------ */
/*  RID list helpers (owned arrays used for prototype_instances,      */
/*  sub-object lists, reference arrays)                               */
/* ------------------------------------------------------------------ */

static int sk_repo_rid_list_contains(const sk_rid_t* items, u32 count, sk_rid_t rid) {
	for (u32 i = 0u; i < count; ++i) {
		if (SK_RID_EQ(items[i], rid)) {
			return 1;
		}
	}
	return 0;
}

static i32 sk_repo_rid_list_grow(const sk_allocator_t* a, sk_rid_t** items, u32* capacity, u32 need) {
	if (need <= *capacity) {
		return 0;
	}
	u32 new_cap = *capacity != 0u ? *capacity : 4u;
	while (new_cap < need) {
		new_cap *= 2u;
	}
	sk_rid_t* new_items = (sk_rid_t*)a->realloc(a->instance, *items, (size_t)new_cap * sizeof(sk_rid_t));
	if (new_items == NULL) {
		return -1;
	}
	*items = new_items;
	*capacity = new_cap;
	return 0;
}

static void sk_repo_rid_list_add_unique(sk_rid_t** items, u32* count, u32* capacity, const sk_allocator_t* a, sk_rid_t rid) {
	if (sk_repo_rid_list_contains(*items, *count, rid)) {
		return;
	}
	if (sk_repo_rid_list_grow(a, items, capacity, *count + 1u) != 0) {
		return;
	}
	(*items)[(*count)++] = rid;
}

static void sk_repo_rid_list_remove(sk_rid_t** items, u32* count, sk_rid_t rid) {
	for (u32 i = 0u; i < *count; ++i) {
		if (SK_RID_EQ((*items)[i], rid)) {
			memmove(&(*items)[i], &(*items)[i + 1u], (size_t)(*count - i - 1u) * sizeof(sk_rid_t));
			*count -= 1u;
			return;
		}
	}
}

static void sk_repo_list_add_unique(sk_rid_list_t* list, sk_rid_t rid) {
	if (sk_repo_rid_list_contains(list->items, list->count, rid)) {
		return;
	}
	(void)sk_array_push(list, rid);
}

static void sk_repo_list_remove(sk_rid_list_t* list, sk_rid_t rid) {
	sk_repo_rid_list_remove(&list->items, &list->count, rid);
}

/* ------------------------------------------------------------------ */
/*  Storage helpers                                                   */
/* ------------------------------------------------------------------ */

/* Ensure the page holding @p page_index exists (allocated + tracked). */
static i32 sk_repo_ensure_page(sk_repository_t* repository, u32 page_index) {
	if (page_index < repository->pages.count && repository->pages.items[page_index] != NULL) {
		return 0;
	}
	/* NOLINTNEXTLINE(bugprone-sizeof-expression): array element is sk_repository_page_t*, so the macro's sizeof(*(arr)->items) legitimately sizes a pointer. */
	if (sk_array_resize(&repository->pages, page_index + 1u) != 0) {
		return -1;
	}
	if (repository->pages.items[page_index] != NULL) {
		return 0;
	}
	const sk_allocator_t* a = repository->allocator;
	size_t storage_bytes = sizeof(sk_resource_storage_t) * SK_REPOSITORY_PAGE_SIZE;
	void* block = a->alloc(a->instance, storage_bytes + (size_t)SK_REPOSITORY_PAGE_SIZE);
	sk_repository_page_t* page = (sk_repository_page_t*)a->alloc(a->instance, sizeof(*page));
	if (block == NULL || page == NULL) {
		if (block != NULL) {
			a->free(a->instance, block);
		}
		if (page != NULL) {
			a->free(a->instance, page);
		}
		return -1;
	}
	page->elements = (sk_resource_storage_t*)block;
	page->used = (u8*)block + storage_bytes;
	memset(page->used, 0, (size_t)SK_REPOSITORY_PAGE_SIZE);
	repository->pages.items[page_index] = page;
	return 0;
}

static sk_resource_storage_t* sk_repo_storage(const sk_repository_t* repository, sk_rid_t rid) {
	u64 idx = rid.id;
	if (idx == 0u) {
		return NULL;
	}
	u32 page_index = (u32)(idx >> SK_REPOSITORY_PAGE_BITS);
	u32 offset = (u32)(idx & SK_REPOSITORY_PAGE_MASK);
	if (page_index >= repository->pages.count) {
		return NULL;
	}
	sk_repository_page_t* page = repository->pages.items[page_index];
	if (page == NULL || !page->used[offset]) {
		return NULL;
	}
	return &page->elements[offset];
}

/* Allocate a fresh slot for @p rid and register @p uuid (when non-zero). Does
 * not touch resource_count. Returns NULL on OOM / duplicate uuid. */
static sk_resource_storage_t* sk_repo_allocate_slot_id(sk_repository_t* repository, sk_rid_t rid, sk_uuid_t uuid) {
	u32 page_index = (u32)(rid.id >> SK_REPOSITORY_PAGE_BITS);
	u32 offset = (u32)(rid.id & SK_REPOSITORY_PAGE_MASK);
	if (sk_repo_ensure_page(repository, page_index) != 0) {
		return NULL;
	}
	sk_repository_page_t* page = repository->pages.items[page_index];
	sk_resource_storage_t* storage = &page->elements[offset];
	page->used[offset] = 1u;
	memset(storage, 0, sizeof(*storage));
	storage->rid = rid;
	storage->uuid = uuid;
	storage->parent_field_index = (u32)-1;
	sk_atomic_ptr_init(&storage->instance, NULL);
	sk_atomic_u64_init(&storage->version, 1u);
	sk_array_init(&storage->prototype_instances, repository->allocator);
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		if (sk_hash_map_put(&repository->rids_by_uuid, uuid, rid) != 0) {
			sk_array_free(&storage->prototype_instances);
			page->used[offset] = 0u;
			memset(storage, 0, sizeof(*storage));
			return NULL;
		}
	}
	return storage;
}

/* Allocate the next free slot (RIDs are never recycled). */
static sk_resource_storage_t* sk_repo_allocate_slot(sk_repository_t* repository, sk_uuid_t uuid, sk_rid_t* out_rid) {
	u64 next = repository->rid_counter;
	if (next == 0u) {
		return NULL;
	}
	repository->rid_counter = next + 1u;
	sk_rid_t rid = {next};
	sk_resource_storage_t* storage = sk_repo_allocate_slot_id(repository, rid, uuid);
	if (storage != NULL) {
		*out_rid = rid;
	}
	return storage;
}

/* Tear down a freshly allocated but failed slot (removes uuid/path mappings,
 * frees the prototype_instances array, releases the slot). Does not touch
 * resource_count. */
static void sk_repo_release_slot(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	if (storage == NULL) {
		return;
	}
	if (!SK_UUID_EQ(storage->uuid, SK_UUID_ZERO)) {
		sk_hash_map_remove(&repository->rids_by_uuid, storage->uuid);
	}
	if (storage->path != NULL) {
		sk_hash_map_remove(&repository->rids_by_path, storage->path);
		repository->allocator->free(repository->allocator->instance, storage->path);
	}
	if (storage->prototype.id != 0u) {
		sk_resource_storage_t* prototype = sk_repo_storage(repository, storage->prototype);
		if (prototype != NULL) {
			sk_repo_list_remove(&prototype->prototype_instances, rid);
		}
	}
	sk_array_free(&storage->prototype_instances);
	u32 page_index = (u32)(rid.id >> SK_REPOSITORY_PAGE_BITS);
	u32 offset = (u32)(rid.id & SK_REPOSITORY_PAGE_MASK);
	sk_repository_page_t* page = repository->pages.items[page_index];
	page->used[offset] = 0u;
	memset(storage, 0, sizeof(*storage));
}

/* ------------------------------------------------------------------ */
/*  Repository lifecycle                                              */
/* ------------------------------------------------------------------ */

static sk_rid_t repository_find_by_uuid(const sk_repository_t* repository, sk_uuid_t uuid);
static sk_rid_t repository_find_by_path(const sk_repository_t* repository, const_chr_t path);

static sk_repository_t* repository_create(const sk_allocator_t* allocator) {
	sk_repository_t* repository = (sk_repository_t*)allocator->alloc(allocator->instance, sizeof(*repository));
	if (repository == NULL) {
		return NULL;
	}
	memset(repository, 0, sizeof(*repository));
	repository->allocator = allocator;
	repository->rid_counter = 1u;
	sk_array_init(&repository->pages, allocator);
	sk_array_init(&repository->to_collect, allocator);
	repository->write_lock = sk_mutex_create();
	if (repository->write_lock == NULL) {
		allocator->free(allocator->instance, repository);
		return NULL;
	}
	/* Untyped init with explicit sizes: the typed macro's sizeof of the
	 * pointer-valued _val_tmp trips bugprone-sizeof-expression. */
	sk_hash_map_init_(&repository->types_by_id._hm, allocator, (u32)sizeof(sk_type_id_t), (u32)sizeof(sk_resource_type_t*), NULL, NULL);
	sk_hash_map_init_(&repository->types_by_name._hm, allocator, (u32)sizeof(const_chr_t), (u32)sizeof(sk_resource_type_t*), sk_hash_cstr, sk_equals_cstr);
	sk_hash_map_init_(&repository->rids_by_uuid._hm, allocator, (u32)sizeof(sk_uuid_t), (u32)sizeof(sk_rid_t), NULL, NULL);
	sk_hash_map_init_(&repository->rids_by_path._hm, allocator, (u32)sizeof(const_chr_t), (u32)sizeof(sk_rid_t), sk_hash_cstr, sk_equals_cstr);
	return repository;
}

static void repository_destroy(sk_repository_t* repository) {
	const sk_allocator_t* a = repository->allocator;
	for (u32 p = 0u; p < repository->pages.count; ++p) {
		sk_repository_page_t* page = repository->pages.items[p];
		if (page == NULL) {
			continue;
		}
		for (u32 i = 0u; i < SK_REPOSITORY_PAGE_SIZE; ++i) {
			if (!page->used[i]) {
				continue;
			}
			sk_resource_storage_t* storage = &page->elements[i];
			if (storage->instance != NULL) {
				sk_repo_instance_destroy(repository, storage->type, storage->instance);
			}
			if (storage->path != NULL) {
				a->free(a->instance, storage->path);
			}
			sk_array_free(&storage->prototype_instances);
		}
		a->free(a->instance, page->elements);
		a->free(a->instance, page);
	}
	sk_array_free(&repository->pages);

	/* Flush instances superseded by Commit / Destroy. */
	for (u32 i = 0u; i < repository->to_collect.count; ++i) {
		sk_repo_instance_destroy(repository, repository->to_collect.items[i].type, repository->to_collect.items[i].instance);
	}
	sk_array_free(&repository->to_collect);

	for (u32 slot = 0u; slot < repository->types_by_id._hm.capacity; ++slot) {
		if (!sk_hash_map_slot_occupied_(&repository->types_by_id._hm, slot)) {
			continue;
		}
		sk_resource_type_t* type = *(sk_resource_type_t**)sk_hash_map_value_at_(&repository->types_by_id._hm, slot);
		sk_repo_type_free(repository, type);
	}
	sk_hash_map_free(&repository->types_by_id);
	sk_hash_map_free(&repository->types_by_name);
	sk_hash_map_free(&repository->rids_by_uuid);
	sk_hash_map_free(&repository->rids_by_path);
	sk_mutex_destroy(repository->write_lock);
	a->free(a->instance, repository);
}

/* ------------------------------------------------------------------ */
/*  Type registry                                                     */
/* ------------------------------------------------------------------ */

static const sk_resource_type_t* repository_find_type(const sk_repository_t* repository, sk_type_id_t type_id) {
	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
		return NULL;
	}
	sk_resource_type_t* type = NULL;
	sk_type_id_t key = type_id;
	if (sk_hash_map_get_(&repository->types_by_id._hm, &key, &type) != 0) {
		return NULL;
	}
	return type;
}

static const sk_resource_type_t* repository_find_type_by_name(const sk_repository_t* repository, const_chr_t name) {
	if (name == NULL) {
		return NULL;
	}
	sk_resource_type_t* type = NULL;
	const_chr_t key = name;
	if (sk_hash_map_get_(&repository->types_by_name._hm, &key, &type) != 0) {
		return NULL;
	}
	return type;
}

static i32 repository_register_type(sk_repository_t* repository, const sk_resource_type_desc_t* desc) {
	if (desc->name == NULL || desc->name[0] == '\0') {
		return -4;
	}
	if (SK_TYPE_ID_EQ(desc->type_id, SK_TYPE_ID_ZERO)) {
		return -4;
	}
	if (desc->instance_size == 0u) {
		return -4;
	}
	if (desc->field_count > 0u && desc->fields == NULL) {
		return -4;
	}
	for (u32 i = 0u; i < desc->field_count; ++i) {
		const sk_resource_field_t* field = &desc->fields[i];
		if (field->size == 0u) {
			return -4;
		}
		if ((u64)field->offset + (u64)field->size > (u64)desc->instance_size) {
			return -4;
		}
	}
	if (repository_find_type(repository, desc->type_id) != NULL) {
		return -1;
	}
	if (repository_find_type_by_name(repository, desc->name) != NULL) {
		return -2;
	}

	const sk_allocator_t* a = repository->allocator;
	sk_resource_type_t* type = (sk_resource_type_t*)a->alloc(a->instance, sizeof(*type));
	if (type == NULL) {
		return -3;
	}
	memset(type, 0, sizeof(*type));
	type->type_id = desc->type_id;
	type->instance_size = desc->instance_size;
	type->field_count = desc->field_count;
	type->bitmap_bytes = (desc->field_count + 7u) / 8u;

	type->name = sk_repo_copy_string(repository, desc->name);
	if (type->name == NULL) {
		a->free(a->instance, type);
		return -3;
	}
	if (desc->field_count > 0u) {
		size_t bytes = (size_t)desc->field_count * sizeof(*type->fields);
		type->fields = (sk_resource_field_t*)a->alloc(a->instance, bytes);
		if (type->fields == NULL) {
			sk_repo_type_free(repository, type);
			return -3;
		}
		memcpy(type->fields, desc->fields, bytes);
	}
	if (desc->defaults != NULL) {
		type->defaults = sk_repo_instance_from_raw(repository, type, desc->defaults);
		if (type->defaults == NULL) {
			sk_repo_type_free(repository, type);
			return -3;
		}
	}
	if (sk_hash_map_put(&repository->types_by_id, type->type_id, type) != 0) {
		sk_repo_type_free(repository, type);
		return -3;
	}
	if (sk_hash_map_put(&repository->types_by_name, type->name, type) != 0) {
		sk_hash_map_remove(&repository->types_by_id, type->type_id);
		sk_repo_type_free(repository, type);
		return -3;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Resource lifecycle                                                */
/* ------------------------------------------------------------------ */

static sk_rid_t repository_create_resource(sk_repository_t* repository, const sk_resource_type_t* type, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	if (type->instance_size == 0u) {
		return SK_RID_ZERO;
	}
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		sk_rid_t existing = repository_find_by_uuid(repository, uuid);
		if (existing.id != 0u) {
			return existing;
		}
	}
	sk_rid_t rid = SK_RID_ZERO;
	sk_resource_storage_t* storage = sk_repo_allocate_slot(repository, uuid, &rid);
	if (storage == NULL) {
		return SK_RID_ZERO;
	}
	storage->type = SK_CONST_CAST(sk_resource_type_t*, type);
	void_ptr_t instance = NULL;
	if (type->defaults != NULL) {
		instance = sk_repo_instance_copy(repository, type, type->defaults);
	} else {
		instance = sk_repo_block_alloc_zero(repository, type);
	}
	if (instance == NULL) {
		sk_repo_release_slot(repository, rid);
		return SK_RID_ZERO;
	}
	sk_atomic_ptr_store(&storage->instance, instance);
	repository->resource_count += 1u;
	if (scope != NULL) {
		sk_repo_scope_push_change(scope, repository, storage, NULL, instance, 1);
	}
	return rid;
}

/* Recursively destroy every sub-object of @p instance (used by the destroy
 * cascade; runs with the instance already retired to the GC queue). */
static void sk_repo_destroy_instance_subobjects(sk_repository_t* repository, const sk_resource_type_t* type, void_ptr_t instance, sk_undo_redo_scope_t* scope);

static i32 repository_destroy_resource(sk_repository_t* repository, sk_rid_t rid, sk_undo_redo_scope_t* scope);
static sk_resource_object_t repository_write(sk_repository_t* repository, sk_rid_t rid);
static void repository_commit(sk_resource_object_t view, sk_undo_redo_scope_t* scope);
static void sk_repo_remove_subobject(sk_resource_object_t view, u32 index, sk_rid_t rid);

// NOLINTBEGIN(misc-no-recursion) -- the destroy cascade recurses through
// sub-objects (each acquire/release of the write lock is independent).

static void sk_repo_destroy_instance_subobjects(sk_repository_t* repository, const sk_resource_type_t* type, void_ptr_t instance, sk_undo_redo_scope_t* scope) {
	const u8* base = (const u8*)instance;
	for (u32 i = 0u; i < type->field_count; ++i) {
		const sk_resource_field_t* field = &type->fields[i];
		switch (field->type) {
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
			sk_rid_t sub = SK_RID_ZERO;
			memcpy(&sub, base + (size_t)field->offset, sizeof(sub));
			if (sub.id != 0u) {
				(void)repository_destroy_resource(repository, sub, scope);
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
			const sk_field_subobject_list_t* list = (const sk_field_subobject_list_t*)(const_ptr_t)(base + (size_t)field->offset);
			for (u32 k = 0u; k < list->count; ++k) {
				if (list->items[k].id != 0u) {
					(void)repository_destroy_resource(repository, list->items[k], scope);
				}
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_NONE:
		case SK_RESOURCE_FIELD_TYPE_BOOL:
		case SK_RESOURCE_FIELD_TYPE_INT:
		case SK_RESOURCE_FIELD_TYPE_UINT:
		case SK_RESOURCE_FIELD_TYPE_FLOAT:
		case SK_RESOURCE_FIELD_TYPE_VEC2:
		case SK_RESOURCE_FIELD_TYPE_VEC3:
		case SK_RESOURCE_FIELD_TYPE_VEC4:
		case SK_RESOURCE_FIELD_TYPE_QUAT:
		case SK_RESOURCE_FIELD_TYPE_MAT4:
		case SK_RESOURCE_FIELD_TYPE_COLOR:
		case SK_RESOURCE_FIELD_TYPE_ENUM:
		case SK_RESOURCE_FIELD_TYPE_STRING:
		case SK_RESOURCE_FIELD_TYPE_BLOB:
		case SK_RESOURCE_FIELD_TYPE_REFERENCE:
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
		case SK_RESOURCE_FIELD_TYPE_BUFFER:
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			break;
		}
	}
}

static i32 repository_destroy_resource(sk_repository_t* repository, sk_rid_t rid, sk_undo_redo_scope_t* scope) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	if (storage == NULL) {
		return -1;
	}

	/* Make room in the GC queue for this instance AND any instance the parent
	 * detach commit retires, so neither teardown step can fail mid-way. */
	void_ptr_t instance = sk_atomic_ptr_load(&storage->instance);
	if (storage->type != NULL && instance != NULL) {
		if (sk_array_reserve(&repository->to_collect, repository->to_collect.count + 2u) != 0) {
			return -1; /* OOM: leave the resource intact; retry the destroy later */
		}
	}

	/* Detach from the parent first (Resources::Destroy): the parent is written
	 * and committed so it stops owning this resource as a sub-object. */
	if (storage->parent.id != 0u) {
		sk_rid_t parent_rid = storage->parent;
		sk_resource_storage_t* parent = sk_repo_storage(repository, parent_rid);
		if (parent != NULL && sk_atomic_ptr_load(&parent->instance) != NULL) {
			sk_resource_object_t view = repository_write(repository, parent_rid);
			if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
				sk_repo_remove_subobject(view, storage->parent_field_index, rid);
				repository_commit(view, scope);
			}
		}
	}

	/* Serialize the slot teardown with Write/Commit under the write lock. The
	 * exchanged instance is queued for GC like a Commit replacement: lock-free
	 * readers may still pin it, so it stays alive until garbage_collect. */
	sk_mutex_lock(repository->write_lock);

	storage = sk_repo_storage(repository, rid);
	if (storage == NULL) {
		sk_mutex_unlock(repository->write_lock);
		return 0; /* already torn down by the parent commit chain */
	}
	instance = sk_atomic_ptr_load(&storage->instance);
	if (storage->type != NULL && instance != NULL) {
		/* Make room for the GC item BEFORE dropping the slot. */
		if (sk_array_reserve(&repository->to_collect, repository->to_collect.count + 1u) != 0) {
			sk_mutex_unlock(repository->write_lock);
			return -1; /* OOM: leave the resource intact; retry the destroy later */
		}
	}

	/* Record the change while the storage still carries its uuid, path, and
	 * prototype link (Undo needs them to re-create the released slot). The
	 * before snapshot deep-copies the published instance before it is retired
	 * to the GC queue. */
	if (scope != NULL && storage->type != NULL) {
		sk_repo_scope_push_change(scope, repository, storage, instance, NULL, 1);
	}

	/* Unregister from the prototype's instance set. */
	if (storage->prototype.id != 0u) {
		sk_resource_storage_t* prototype = sk_repo_storage(repository, storage->prototype);
		if (prototype != NULL) {
			sk_repo_list_remove(&prototype->prototype_instances, rid);
		}
	}
	if (storage->path != NULL) {
		sk_hash_map_remove(&repository->rids_by_path, storage->path);
		repository->allocator->free(repository->allocator->instance, storage->path);
		storage->path = NULL;
	}
	if (!SK_UUID_EQ(storage->uuid, SK_UUID_ZERO)) {
		sk_hash_map_remove(&repository->rids_by_uuid, storage->uuid);
	}
	void_ptr_t exchanged = sk_atomic_ptr_exchange(&storage->instance, NULL);
	sk_resource_type_t* type = storage->type;
	if (exchanged != NULL && type != NULL) {
		(void)sk_array_push(&repository->to_collect, ((sk_resource_gc_item_t){type, exchanged})); /* capacity pre-reserved */
	}
	sk_mutex_unlock(repository->write_lock);

	/* Recursively destroy every sub-object of the retired instance. Each
	 * recursive destroy acquires the write lock on its own. */
	if (exchanged != NULL && type != NULL) {
		sk_repo_destroy_instance_subobjects(repository, type, exchanged, scope);
	}

	/* Release the page slot. */
	sk_mutex_lock(repository->write_lock);
	storage = sk_repo_storage(repository, rid);
	if (storage != NULL) {
		sk_array_free(&storage->prototype_instances);
		u32 page_index = (u32)(rid.id >> SK_REPOSITORY_PAGE_BITS);
		u32 offset = (u32)(rid.id & SK_REPOSITORY_PAGE_MASK);
		sk_repository_page_t* page = repository->pages.items[page_index];
		page->used[offset] = 0u;
		memset(storage, 0, sizeof(*storage));
		if (repository->resource_count > 0u) {
			repository->resource_count -= 1u;
		}
	}
	sk_mutex_unlock(repository->write_lock);
	return 0;
}

// NOLINTEND(misc-no-recursion)

static i32 repository_has_resource(const sk_repository_t* repository, sk_rid_t rid) {
	return sk_repo_storage(repository, rid) != NULL ? 1 : 0;
}

static u64 repository_resource_count(const sk_repository_t* repository) {
	return repository->resource_count;
}

static const sk_resource_type_t* repository_resource_type(const sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? storage->type : NULL;
}

static sk_uuid_t repository_resource_uuid(const sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? storage->uuid : SK_UUID_ZERO;
}

static void_ptr_t repository_resource_instance(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? sk_atomic_ptr_load(&storage->instance) : NULL;
}

static sk_rid_t repository_find_by_uuid(const sk_repository_t* repository, sk_uuid_t uuid) {
	if (SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		return SK_RID_ZERO;
	}
	sk_rid_t out = SK_RID_ZERO;
	sk_uuid_t key = uuid;
	if (sk_hash_map_get_(&repository->rids_by_uuid._hm, &key, &out) != 0) {
		return SK_RID_ZERO;
	}
	return out;
}

static i32 repository_set_path(sk_repository_t* repository, sk_rid_t rid, const_chr_t path) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	if (storage == NULL) {
		return -1;
	}
	if (path == NULL) {
		return -1;
	}
	sk_rid_t existing = repository_find_by_path(repository, path);
	if (existing.id != 0u) {
		return SK_RID_EQ(existing, rid) ? 0 : -2;
	}
	char* copy = sk_repo_copy_string(repository, path);
	if (copy == NULL) {
		return -3;
	}
	if (storage->path != NULL) {
		sk_hash_map_remove(&repository->rids_by_path, storage->path);
		repository->allocator->free(repository->allocator->instance, storage->path);
	}
	if (sk_hash_map_put(&repository->rids_by_path, copy, rid) != 0) {
		repository->allocator->free(repository->allocator->instance, copy);
		storage->path = NULL;
		return -3;
	}
	storage->path = copy;
	return 0;
}

static const_chr_t repository_get_path(const sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? storage->path : NULL;
}

static sk_rid_t repository_find_by_path(const sk_repository_t* repository, const_chr_t path) {
	if (path == NULL) {
		return SK_RID_ZERO;
	}
	sk_rid_t out = SK_RID_ZERO;
	const_chr_t key = path;
	if (sk_hash_map_get_(&repository->rids_by_path._hm, &key, &out) != 0) {
		return SK_RID_ZERO;
	}
	return out;
}

/* ------------------------------------------------------------------ */
/*  Hierarchy accessors                                               */
/* ------------------------------------------------------------------ */

static sk_rid_t repository_get_parent(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? storage->parent : SK_RID_ZERO;
}

static sk_rid_t repository_get_prototype(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? storage->prototype : SK_RID_ZERO;
}

static sk_rid_t repository_get_top_parent(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	if (storage == NULL) {
		return SK_RID_ZERO;
	}
	sk_rid_t top = rid;
	while (storage->parent.id != 0u) {
		top = storage->parent;
		storage = sk_repo_storage(repository, top);
		if (storage == NULL) {
			break;
		}
	}
	return top;
}

static i32 repository_is_parent_of(sk_repository_t* repository, sk_rid_t parent, sk_rid_t child) {
	const sk_resource_storage_t* current = sk_repo_storage(repository, child);
	if (current == NULL) {
		return 0;
	}
	while (current->parent.id != 0u) {
		if (SK_RID_EQ(current->parent, parent)) {
			return 1;
		}
		current = sk_repo_storage(repository, current->parent);
		if (current == NULL) {
			return 0;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Read / write / commit                                             */
/* ------------------------------------------------------------------ */

static void sk_repo_update_subobject_parents(sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t old_instance, void_ptr_t new_instance);
static void sk_repo_propagate_prototype_changes(sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t old_instance, void_ptr_t new_instance,
												sk_undo_redo_scope_t* scope);
static void sk_repo_finalize_commit(sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t old_instance, void_ptr_t new_instance, sk_undo_redo_scope_t* scope);
static i32 repository_add_to_subobject_list(sk_resource_object_t view, u32 index, sk_rid_t rid);
static u32 repository_remove_from_subobject_list_by_prototype(sk_resource_object_t view, u32 index, sk_rid_t prototype, sk_rid_t* out_items, u32 out_capacity);

static sk_resource_object_t repository_read(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	if (storage == NULL) {
		return SK_RESOURCE_OBJECT_ZERO;
	}
	sk_resource_object_t view;
	view.repo = repository;
	view.storage = storage;
	view.instance = sk_atomic_ptr_load(&storage->instance);
	view.data_on_write = NULL;
	view.is_write = 0u;
	memset(view._pad0, 0, sizeof(view._pad0));
	return view;
}

static sk_resource_object_t repository_write(sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	if (storage == NULL || storage->type == NULL) {
		return SK_RESOURCE_OBJECT_ZERO;
	}
	sk_mutex_lock(repository->write_lock);
	void_ptr_t current = sk_atomic_ptr_load(&storage->instance);
	void_ptr_t copy = sk_repo_instance_copy(repository, storage->type, (const u8*)current);
	if (copy == NULL) {
		sk_mutex_unlock(repository->write_lock);
		return SK_RESOURCE_OBJECT_ZERO;
	}
	sk_resource_object_t view;
	view.repo = repository;
	view.storage = storage;
	view.instance = copy;
	view.data_on_write = current;
	view.is_write = 1u;
	memset(view._pad0, 0, sizeof(view._pad0));
	return view;
}

/* Bump the version of @p storage and every ancestor in the parent chain. */
static void sk_repo_update_version_chain(sk_repository_t* repository, sk_resource_storage_t* storage) {
	sk_resource_storage_t* current = storage;
	while (current != NULL) {
		sk_atomic_u64_fetch_add(&current->version, 1ull);
		if (current->parent.id == 0u) {
			break;
		}
		current = sk_repo_storage(repository, current->parent);
	}
}

// NOLINTNEXTLINE(misc-no-recursion) -- commit re-enters via prototype propagation / destroy.
static void repository_commit(sk_resource_object_t view, sk_undo_redo_scope_t* scope) {
	if (view.is_write == 0u || view.storage == NULL || view.instance == NULL || view.repo == NULL) {
		return;
	}
	sk_repository_t* repository = view.repo;
	sk_resource_storage_t* storage = (sk_resource_storage_t*)view.storage;
	void_ptr_t expected = view.data_on_write;
	if (sk_atomic_ptr_compare_exchange(&storage->instance, &expected, view.instance) == 0) {
		/* Lost the publish race: the uncommitted copy is discarded, no version
		 * bump. (Under the exclusive write lock this only happens on teardown
		 * races; kept for multi-commit parity.) */
		sk_repo_instance_destroy(repository, storage->type, view.instance);
		sk_mutex_unlock(repository->write_lock);
		return;
	}
	/* Record before / after BEFORE the replaced instance is retired to the GC
	 * queue (the snapshot deep-copies the still-live instance). */
	if (scope != NULL) {
		sk_repo_scope_push_change(scope, repository, storage, expected, view.instance, 0);
	}
	if (expected != NULL) {
		(void)sk_array_push(&repository->to_collect, ((sk_resource_gc_item_t){storage->type, expected}));
	}
	sk_repo_update_version_chain(repository, storage);
	sk_mutex_unlock(repository->write_lock);
	sk_repo_finalize_commit(repository, storage, expected, view.instance, scope);
}

static void repository_discard(sk_resource_object_t view) {
	if (view.is_write == 0u || view.storage == NULL || view.repo == NULL) {
		return;
	}
	sk_resource_storage_t* storage = (sk_resource_storage_t*)view.storage;
	if (view.instance != NULL) {
		sk_repo_instance_destroy(view.repo, storage->type, view.instance);
	}
	sk_mutex_unlock(view.repo->write_lock);
}

static u64 repository_get_version(const sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL ? sk_atomic_u64_load(&storage->version) : 0ull;
}

static i32 repository_has_value(const sk_repository_t* repository, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_storage(repository, rid);
	return storage != NULL && sk_atomic_ptr_load(&storage->instance) != NULL ? 1 : 0;
}

static void repository_garbage_collect(sk_repository_t* repository) {
	for (u32 i = 0u; i < repository->to_collect.count; ++i) {
		sk_repo_instance_destroy(repository, repository->to_collect.items[i].type, repository->to_collect.items[i].instance);
	}
	sk_array_clear(&repository->to_collect);
}

static void repository_end_frame(sk_repository_t* repository) {
	repository_garbage_collect(repository);
}

/* Refresh sub-object parent links after a commit: every sub-object reachable
 * from the new instance points back at @p storage; sub-objects that left the
 * new instance (present in the old instance only) lose their parent link. */
static void sk_repo_update_subobject_parents(sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t old_instance, void_ptr_t new_instance) {
	if (storage->type == NULL || new_instance == NULL) {
		return;
	}
	const sk_resource_type_t* type = storage->type;
	const u8* nbase = (const u8*)new_instance;
	const u8* obase = (old_instance != NULL) ? (const u8*)old_instance : NULL;

	for (u32 i = 0u; i < type->field_count; ++i) {
		const sk_resource_field_t* field = &type->fields[i];
		switch (field->type) {
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
			sk_rid_t new_sub = SK_RID_ZERO;
			memcpy(&new_sub, nbase + (size_t)field->offset, sizeof(new_sub));
			if (new_sub.id != 0u) {
				sk_resource_storage_t* sub_storage = sk_repo_storage(repository, new_sub);
				if (sub_storage != NULL) {
					sub_storage->parent = storage->rid;
					sub_storage->parent_field_index = field->index;
				}
			}
			if (obase != NULL) {
				sk_rid_t old_sub = SK_RID_ZERO;
				memcpy(&old_sub, obase + (size_t)field->offset, sizeof(old_sub));
				if (old_sub.id != 0u && !SK_RID_EQ(old_sub, new_sub)) {
					sk_resource_storage_t* sub_storage = sk_repo_storage(repository, old_sub);
					if (sub_storage != NULL && SK_RID_EQ(sub_storage->parent, storage->rid)) {
						sub_storage->parent = SK_RID_ZERO;
						sub_storage->parent_field_index = (u32)-1;
					}
				}
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
			const sk_field_subobject_list_t* new_list = (const sk_field_subobject_list_t*)(const_ptr_t)(nbase + (size_t)field->offset);
			const sk_field_subobject_list_t* old_list = (obase != NULL) ? (const sk_field_subobject_list_t*)(const_ptr_t)(obase + (size_t)field->offset) : NULL;
			for (u32 k = 0u; k < new_list->count; ++k) {
				sk_resource_storage_t* sub_storage = sk_repo_storage(repository, new_list->items[k]);
				if (sub_storage != NULL) {
					sub_storage->parent = storage->rid;
					sub_storage->parent_field_index = field->index;
				}
			}
			if (old_list != NULL) {
				for (u32 k = 0u; k < old_list->count; ++k) {
					if (!sk_repo_rid_list_contains(new_list->items, new_list->count, old_list->items[k])) {
						sk_resource_storage_t* sub_storage = sk_repo_storage(repository, old_list->items[k]);
						if (sub_storage != NULL && SK_RID_EQ(sub_storage->parent, storage->rid)) {
							sub_storage->parent = SK_RID_ZERO;
							sub_storage->parent_field_index = (u32)-1;
						}
					}
				}
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_NONE:
		case SK_RESOURCE_FIELD_TYPE_BOOL:
		case SK_RESOURCE_FIELD_TYPE_INT:
		case SK_RESOURCE_FIELD_TYPE_UINT:
		case SK_RESOURCE_FIELD_TYPE_FLOAT:
		case SK_RESOURCE_FIELD_TYPE_VEC2:
		case SK_RESOURCE_FIELD_TYPE_VEC3:
		case SK_RESOURCE_FIELD_TYPE_VEC4:
		case SK_RESOURCE_FIELD_TYPE_QUAT:
		case SK_RESOURCE_FIELD_TYPE_MAT4:
		case SK_RESOURCE_FIELD_TYPE_COLOR:
		case SK_RESOURCE_FIELD_TYPE_ENUM:
		case SK_RESOURCE_FIELD_TYPE_STRING:
		case SK_RESOURCE_FIELD_TYPE_BLOB:
		case SK_RESOURCE_FIELD_TYPE_REFERENCE:
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
		case SK_RESOURCE_FIELD_TYPE_BUFFER:
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			break;
		}
	}
}

/* After a prototype's SubObjectList field changed, mirror the change on every
 * registered prototype instance (CreateFromPrototype for additions, Destroy for
 * removals). Runs on the main thread after the write lock is released. */
static sk_rid_t sk_repo_create_from_prototype_internal(sk_clone_context_t* ctx, sk_rid_t prototype_rid, sk_uuid_t uuid, sk_resource_storage_t* parent_storage, u32 field_index);

// NOLINTBEGIN(misc-no-recursion) -- propagation re-enters Write/Commit and
// CreateFromPrototype / Destroy per instance.

static void sk_repo_propagate_prototype_changes(sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t old_instance, void_ptr_t new_instance,
												sk_undo_redo_scope_t* scope) {
	if (old_instance == NULL || new_instance == NULL || storage->type == NULL) {
		return;
	}
	if (storage->prototype_instances.count == 0u) {
		return;
	}
	const sk_allocator_t* a = repository->allocator;
	const u8* nbase = (const u8*)new_instance;

	for (u32 i = 0u; i < storage->type->field_count; ++i) {
		const sk_resource_field_t* field = &storage->type->fields[i];
		if (field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
			continue;
		}
		const sk_field_subobject_list_t* proto_list = (const sk_field_subobject_list_t*)(const_ptr_t)(nbase + (size_t)field->offset);

		u32 instance_count = storage->prototype_instances.count;
		for (u32 k = 0u; k < instance_count; ++k) {
			sk_rid_t inst_rid = storage->prototype_instances.items[k];
			sk_resource_storage_t* inst_storage = sk_repo_storage(repository, inst_rid);
			if (inst_storage == NULL || inst_storage->type == NULL) {
				continue;
			}
			void_ptr_t inst_current = sk_atomic_ptr_load(&inst_storage->instance);
			if (inst_current == NULL) {
				continue;
			}
			const sk_field_subobject_list_t* inst_list = (const sk_field_subobject_list_t*)(const_ptr_t)((const u8*)inst_current + (size_t)field->offset);

			sk_rid_list_t to_remove;
			sk_rid_list_t removed_mirrors;
			sk_rid_list_t to_add;
			sk_rid_list_t created;
			sk_array_init(&to_remove, a);
			sk_array_init(&removed_mirrors, a);
			sk_array_init(&to_add, a);
			sk_array_init(&created, a);

			/* Prototype sub-objects that left the prototype's list must leave
			 * this instance too (only sub-objects linked to a prototype). */
			for (u32 m = 0u; m < inst_list->count; ++m) {
				sk_resource_storage_t* sub_storage = sk_repo_storage(repository, inst_list->items[m]);
				if (sub_storage == NULL || sub_storage->prototype.id == 0u) {
					continue;
				}
				if (!sk_repo_rid_list_contains(proto_list->items, proto_list->count, sub_storage->prototype)) {
					sk_repo_list_add_unique(&to_remove, sub_storage->prototype);
					(void)sk_array_push(&removed_mirrors, inst_list->items[m]);
				}
			}
			/* Prototype sub-objects the instance does not mirror yet, skipping
			 * prototypes this instance explicitly removed (prototypeRemoved). */
			for (u32 m = 0u; m < proto_list->count; ++m) {
				int found = 0;
				for (u32 n = 0u; n < inst_list->count; ++n) {
					sk_resource_storage_t* sub_storage = sk_repo_storage(repository, inst_list->items[n]);
					if (sub_storage != NULL && SK_RID_EQ(sub_storage->prototype, proto_list->items[m])) {
						found = 1;
						break;
					}
				}
				if (found || sk_repo_rid_list_contains(inst_list->prototype_removed, inst_list->prototype_removed_count, proto_list->items[m])) {
					continue;
				}
				(void)sk_array_push(&to_add, proto_list->items[m]);
			}

			if (to_remove.count != 0u || to_add.count != 0u) {
				/* Create mirrors first: each CreateFromPrototype publishes
				 * under the write lock, so it must not run while a write view
				 * on this instance is live. */
				for (u32 m = 0u; m < to_add.count; ++m) {
					sk_clone_context_t cctx;
					cctx.repo = repository;
					cctx.origin = to_add.items[m];
					cctx.scope = scope;
					cctx.failed = 0;
					sk_hash_map_init(&cctx.map, a, NULL, NULL);
					sk_rid_t mirror = sk_repo_create_from_prototype_internal(&cctx, to_add.items[m], SK_UUID_ZERO, NULL, (u32)-1);
					sk_hash_map_free(&cctx.map);
					if (!SK_RID_EQ(mirror, SK_RID_ZERO)) {
						(void)sk_array_push(&created, mirror);
						continue;
					}
					break; /* stop mirroring on failure (OOM) */
				}

				sk_resource_object_t view = repository_write(repository, inst_rid);
				i32 instance_committed = 0;
				if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
					for (u32 m = 0u; m < to_remove.count; ++m) {
						(void)repository_remove_from_subobject_list_by_prototype(view, field->index, to_remove.items[m], NULL, 0u);
					}
					for (u32 m = 0u; m < created.count; ++m) {
						(void)repository_add_to_subobject_list(view, field->index, created.items[m]);
					}
					repository_commit(view, scope);
					instance_committed = 1;
				}

				/* Destroy the mirrors that left the instance (post-commit:
				 * their parent link was cleared, so the destroy is clean). */
				if (instance_committed != 0) {
					for (u32 m = 0u; m < removed_mirrors.count; ++m) {
						(void)repository_destroy_resource(repository, removed_mirrors.items[m], scope);
					}
				}
			}

			sk_array_free(&to_remove);
			sk_array_free(&removed_mirrors);
			sk_array_free(&to_add);
			sk_array_free(&created);
		}
	}
}

static void sk_repo_finalize_commit(sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t old_instance, void_ptr_t new_instance, sk_undo_redo_scope_t* scope) {
	sk_repo_update_subobject_parents(repository, storage, old_instance, new_instance);
	sk_repo_propagate_prototype_changes(repository, storage, old_instance, new_instance, scope);
}

// NOLINTEND(misc-no-recursion)

/* ------------------------------------------------------------------ */
/*  Clone / CreateFromPrototype                                       */
/* ------------------------------------------------------------------ */

/* Per-repository uuid source (no global state; hi half seeded by the repo
 * pointer so two repositories never collide). */
static sk_uuid_t sk_repo_make_uuid(sk_repository_t* repository) {
	u64 n = sk_atomic_u64_fetch_add(&repository->uuid_counter, 1ull) + 1ull;
	return (sk_uuid_t){n, (uintptr_t)repository};
}

static int sk_repo_clone_find(sk_clone_context_t* ctx, sk_rid_t src, sk_rid_t* out_dst) {
	sk_clone_entry_t* entry = (sk_clone_entry_t*)sk_hash_map_get_ptr(&ctx->map, src);
	if (entry == NULL) {
		return 0;
	}
	*out_dst = entry->dst;
	return 1;
}

/* UUID for a cloned resource: the caller's uuid, else a fresh one when the
 * source carries a uuid (the clone must stay unique per repository). */
static sk_uuid_t sk_repo_clone_uuid(sk_clone_context_t* ctx, sk_rid_t src, sk_uuid_t requested) {
	if (!SK_UUID_EQ(requested, SK_UUID_ZERO)) {
		return requested;
	}
	const sk_resource_storage_t* src_storage = sk_repo_storage(ctx->repo, src);
	if (src_storage != NULL && !SK_UUID_EQ(src_storage->uuid, SK_UUID_ZERO)) {
		return sk_repo_make_uuid(ctx->repo);
	}
	return SK_UUID_ZERO;
}

/* Reserve a fresh dest slot for @p src, remember it in the clone map, register
 * its uuid (a uuid already owned by another live resource is a hard failure),
 * and bump the live resource count. Returns 1 on success; 0 on failure (the
 * caller rolls back via clone_cleanup). */
static int sk_repo_clone_reserve(sk_clone_context_t* ctx, sk_rid_t src, sk_uuid_t uuid, sk_rid_t* out_dst) {
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		sk_rid_t existing = repository_find_by_uuid(ctx->repo, uuid);
		if (existing.id != 0u) {
			ctx->failed = 1;
			return 0;
		}
	}
	sk_rid_t dst;
	dst.id = ctx->repo->rid_counter;
	if (dst.id == 0u) {
		ctx->failed = 1;
		return 0;
	}
	ctx->repo->rid_counter = dst.id + 1u;
	sk_clone_entry_t entry = {dst, uuid};
	if (sk_hash_map_put(&ctx->map, src, entry) != 0) {
		ctx->failed = 1;
		return 0;
	}
	if (sk_repo_allocate_slot_id(ctx->repo, dst, uuid) == NULL) {
		sk_hash_map_remove(&ctx->map, src);
		ctx->failed = 1;
		return 0;
	}
	ctx->repo->resource_count += 1u;
	*out_dst = dst;
	return 1;
}

/* Copy @p prototype onto @p storage and register @p storage->rid in that
 * prototype's instance set, so Commit-side prototype propagation reaches the
 * new resource. */
static void sk_repo_link_prototype(sk_repository_t* repository, sk_resource_storage_t* storage, sk_rid_t prototype) {
	storage->prototype = prototype;
	if (prototype.id != 0u) {
		sk_resource_storage_t* proto = sk_repo_storage(repository, prototype);
		if (proto != NULL) {
			sk_repo_list_add_unique(&proto->prototype_instances, storage->rid);
		}
	}
}

/* Roll back a failed clone / prototype build: every reserved dest slot is torn
 * down in place so a partial deep clone leaves no orphaned slots. Pure frees —
 * never allocates — so it works even while the allocator is failing. */
static void sk_repo_clone_cleanup(sk_repository_t* repository, sk_clone_context_t* ctx) {
	for (u32 slot = 0u; slot < ctx->map._hm.capacity; ++slot) {
		if (!sk_hash_map_slot_occupied_(&ctx->map._hm, slot)) {
			continue;
		}
		sk_rid_t dst = (*(sk_clone_entry_t*)sk_hash_map_value_at_(&ctx->map._hm, slot)).dst;
		sk_resource_storage_t* storage = sk_repo_storage(repository, dst);
		if (storage == NULL) {
			continue;
		}
		if (storage->prototype.id != 0u) {
			sk_resource_storage_t* prototype = sk_repo_storage(repository, storage->prototype);
			if (prototype != NULL) {
				sk_repo_list_remove(&prototype->prototype_instances, dst);
			}
		}
		if (!SK_UUID_EQ(storage->uuid, SK_UUID_ZERO)) {
			sk_hash_map_remove(&repository->rids_by_uuid, storage->uuid);
		}
		if (storage->path != NULL) {
			sk_hash_map_remove(&repository->rids_by_path, storage->path);
			repository->allocator->free(repository->allocator->instance, storage->path);
		}
		if (storage->type != NULL) {
			void_ptr_t instance = sk_atomic_ptr_load(&storage->instance);
			if (instance != NULL) {
				sk_repo_instance_destroy(repository, storage->type, instance);
			}
		}
		if (repository->resource_count > 0u) {
			repository->resource_count -= 1u;
		}
		sk_repo_release_slot(repository, dst);
	}
}

static u8* sk_repo_build_instance(sk_clone_context_t* ctx, const sk_resource_type_t* type, const u8* src_block, sk_resource_storage_t* dest_storage, i32 is_prototype);
static sk_rid_t sk_repo_clone_subobject(sk_clone_context_t* ctx, sk_resource_storage_t* parent_storage, u32 field_index, sk_rid_t origin);

// NOLINTBEGIN(misc-no-recursion) -- clone / prototype build is intentionally
// recursive over the sub-object tree (guarded by the clone map deduplication).

/* Remap a Reference / ReferenceArray entry when it points inside the cloned
 * subtree (the origin). Outer references stay unchanged. */
static sk_rid_t sk_repo_clone_reference(sk_clone_context_t* ctx, sk_rid_t reference) {
	if (reference.id == 0u) {
		return reference;
	}
	if (!repository_is_parent_of(ctx->repo, ctx->origin, reference)) {
		return reference;
	}
	sk_rid_t dst = SK_RID_ZERO;
	if (!sk_repo_clone_find(ctx, reference, &dst)) {
		sk_uuid_t uuid = sk_repo_clone_uuid(ctx, reference, SK_UUID_ZERO);
		if (!sk_repo_clone_reserve(ctx, reference, uuid, &dst)) {
			return SK_RID_ZERO;
		}
	}
	return dst;
}

/* Clone one sub-object of @p origin under @p parent_storage. The clone is
 * created once (later visits only fill a previously-reserved bare slot). */
static sk_rid_t sk_repo_clone_subobject(sk_clone_context_t* ctx, sk_resource_storage_t* parent_storage, u32 field_index, sk_rid_t origin) {
	if (origin.id == 0u) {
		return origin;
	}
	sk_rid_t dst = SK_RID_ZERO;
	if (!sk_repo_clone_find(ctx, origin, &dst)) {
		sk_uuid_t uuid = sk_repo_clone_uuid(ctx, origin, SK_UUID_ZERO);
		if (!sk_repo_clone_reserve(ctx, origin, uuid, &dst)) {
			return SK_RID_ZERO;
		}
	}
	sk_resource_storage_t* dst_storage = sk_repo_storage(ctx->repo, dst);
	sk_resource_storage_t* origin_storage = sk_repo_storage(ctx->repo, origin);
	if (dst_storage == NULL) {
		return SK_RID_ZERO;
	}
	if (dst_storage->type == NULL && origin_storage != NULL && origin_storage->type != NULL) {
		dst_storage->type = origin_storage->type;
		sk_repo_link_prototype(ctx->repo, dst_storage, origin_storage->prototype);
		u8* instance = sk_repo_build_instance(ctx, origin_storage->type, (const u8*)sk_atomic_ptr_load(&origin_storage->instance), dst_storage, 0);
		if (ctx->failed) {
			return SK_RID_ZERO;
		}
		if (instance != NULL) {
			sk_atomic_ptr_store(&dst_storage->instance, instance);
			if (ctx->scope != NULL) {
				sk_repo_scope_push_change(ctx->scope, ctx->repo, dst_storage, NULL, instance, 1);
			}
		}
	}
	dst_storage->parent = parent_storage->rid;
	dst_storage->parent_field_index = field_index;
	return dst;
}

/* Deep copy @p src's field data for one clone/prototype instance. Sub-objects
 * are either cloned (is_prototype == 0) or re-created from their prototype
 * (is_prototype == 1); references inside the cloned subtree are remapped.
 * Scalar fields are only copied for full clones (prototype instances inherit
 * them lazily through the prototype chain). With @p src_block == NULL an empty
 * zeroed instance is produced. */
static u8* sk_repo_build_instance(sk_clone_context_t* ctx, const sk_resource_type_t* type, const u8* src_block, sk_resource_storage_t* dest_storage, i32 is_prototype) {
	u8* dst = sk_repo_block_alloc_zero(ctx->repo, type);
	if (dst == NULL) {
		ctx->failed = 1;
		return NULL;
	}
	if (src_block == NULL) {
		return dst;
	}
	const sk_allocator_t* a = ctx->repo->allocator;
	const u8* sbase = src_block;
	u8* dbase = dst;

	for (u32 i = 0u; i < type->field_count; ++i) {
		const sk_resource_field_t* field = &type->fields[i];
		if (!sk_repo_instance_has_value(type, src_block, i)) {
			continue;
		}
		switch (field->type) {
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
			sk_rid_t sub = SK_RID_ZERO;
			memcpy(&sub, sbase + (size_t)field->offset, sizeof(sub));
			if (sub.id != 0u) {
				sub = is_prototype ? sk_repo_create_from_prototype_internal(ctx, sub, SK_UUID_ZERO, dest_storage, field->index) :
									 sk_repo_clone_subobject(ctx, dest_storage, field->index, sub);
				if (ctx->failed) {
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
			}
			memcpy(dbase + (size_t)field->offset, &sub, sizeof(sub));
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
			const sk_field_subobject_list_t* src_list = (const sk_field_subobject_list_t*)(const_ptr_t)(sbase + (size_t)field->offset);
			sk_field_subobject_list_t out;
			out.items = NULL;
			out.count = 0u;
			out.capacity = 0u;
			out.prototype_removed = NULL;
			out.prototype_removed_count = 0u;
			out.prototype_removed_capacity = 0u;
			if (src_list->count != 0u) {
				out.items = (sk_rid_t*)a->alloc(a->instance, (size_t)src_list->count * sizeof(sk_rid_t));
				if (out.items == NULL) {
					ctx->failed = 1;
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
				out.capacity = src_list->count;
				for (u32 k = 0u; k < src_list->count; ++k) {
					sk_rid_t sub = src_list->items[k];
					out.items[k] = (sub.id != 0u) ? (is_prototype ? sk_repo_create_from_prototype_internal(ctx, sub, SK_UUID_ZERO, dest_storage, field->index) :
																	sk_repo_clone_subobject(ctx, dest_storage, field->index, sub)) :
													sub;
					if (ctx->failed) {
						a->free(a->instance, out.items);
						sk_repo_instance_destroy(ctx->repo, type, dst);
						return NULL;
					}
				}
				out.count = src_list->count;
			}
			/* Copy the removed-from-prototype set only for full clones. */
			if (!is_prototype && src_list->prototype_removed_count != 0u) {
				out.prototype_removed = (sk_rid_t*)a->alloc(a->instance, (size_t)src_list->prototype_removed_count * sizeof(sk_rid_t));
				if (out.prototype_removed == NULL) {
					a->free(a->instance, out.items);
					ctx->failed = 1;
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
				memcpy(out.prototype_removed, src_list->prototype_removed, (size_t)src_list->prototype_removed_count * sizeof(sk_rid_t));
				out.prototype_removed_count = src_list->prototype_removed_count;
				out.prototype_removed_capacity = src_list->prototype_removed_count;
			}
			memcpy(dbase + (size_t)field->offset, &out, sizeof(out));
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_REFERENCE: {
			sk_rid_t ref = SK_RID_ZERO;
			memcpy(&ref, sbase + (size_t)field->offset, sizeof(ref));
			sk_rid_t mapped = sk_repo_clone_reference(ctx, ref);
			if (ctx->failed) {
				sk_repo_instance_destroy(ctx->repo, type, dst);
				return NULL;
			}
			memcpy(dbase + (size_t)field->offset, &mapped, sizeof(mapped));
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
			const sk_field_rid_array_t* src_arr = (const sk_field_rid_array_t*)(const_ptr_t)(sbase + (size_t)field->offset);
			sk_field_rid_array_t out;
			out.items = NULL;
			out.count = 0u;
			out.capacity = 0u;
			if (src_arr->count != 0u) {
				out.items = (sk_rid_t*)a->alloc(a->instance, (size_t)src_arr->count * sizeof(sk_rid_t));
				if (out.items == NULL) {
					ctx->failed = 1;
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
				out.capacity = src_arr->count;
				for (u32 k = 0u; k < src_arr->count; ++k) {
					out.items[k] = sk_repo_clone_reference(ctx, src_arr->items[k]);
					if (ctx->failed) {
						a->free(a->instance, out.items);
						sk_repo_instance_destroy(ctx->repo, type, dst);
						return NULL;
					}
				}
				out.count = src_arr->count;
			}
			memcpy(dbase + (size_t)field->offset, &out, sizeof(out));
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_STRING: {
			if (is_prototype) {
				break; /* scalars inherit lazily through the prototype chain */
			}
			const sk_field_string_t* src = (const sk_field_string_t*)(const_ptr_t)(sbase + (size_t)field->offset);
			sk_field_string_t* out = (sk_field_string_t*)(void_ptr_t)(dbase + (size_t)field->offset);
			if (src->chars != NULL) {
				out->chars = sk_repo_copy_string(ctx->repo, src->chars);
				if (out->chars == NULL) {
					ctx->failed = 1;
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
			}
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_BLOB: {
			if (is_prototype) {
				break;
			}
			const sk_field_blob_t* src = (const sk_field_blob_t*)(const_ptr_t)(sbase + (size_t)field->offset);
			sk_field_blob_t* out = (sk_field_blob_t*)(void_ptr_t)(dbase + (size_t)field->offset);
			if (src->size != 0u && src->data != NULL) {
				out->data = (u8*)(void_ptr_t)sk_repo_copy_bytes(ctx->repo, src->data, (size_t)src->size);
				if (out->data == NULL) {
					ctx->failed = 1;
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
				out->size = src->size;
			}
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_BUFFER: {
			if (is_prototype) {
				break; /* buffer payloads inherit lazily through the prototype chain */
			}
			const sk_field_buffer_t* src = (const sk_field_buffer_t*)(const_ptr_t)(sbase + (size_t)field->offset);
			sk_field_buffer_t* out = (sk_field_buffer_t*)(void_ptr_t)(dbase + (size_t)field->offset);
			if (src->size != 0u && src->data != NULL) {
				out->data = (u8*)(void_ptr_t)sk_repo_copy_bytes(ctx->repo, src->data, (size_t)src->size);
				if (out->data == NULL) {
					ctx->failed = 1;
					sk_repo_instance_destroy(ctx->repo, type, dst);
					return NULL;
				}
				out->size = src->size;
			}
			sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			break;
		}
		/* POD field types are deep-copied for clones, inherited for prototypes. */
		case SK_RESOURCE_FIELD_TYPE_NONE:
		case SK_RESOURCE_FIELD_TYPE_BOOL:
		case SK_RESOURCE_FIELD_TYPE_INT:
		case SK_RESOURCE_FIELD_TYPE_UINT:
		case SK_RESOURCE_FIELD_TYPE_FLOAT:
		case SK_RESOURCE_FIELD_TYPE_VEC2:
		case SK_RESOURCE_FIELD_TYPE_VEC3:
		case SK_RESOURCE_FIELD_TYPE_VEC4:
		case SK_RESOURCE_FIELD_TYPE_QUAT:
		case SK_RESOURCE_FIELD_TYPE_MAT4:
		case SK_RESOURCE_FIELD_TYPE_COLOR:
		case SK_RESOURCE_FIELD_TYPE_ENUM:
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			if (!is_prototype) {
				memcpy(dbase + (size_t)field->offset, sbase + (size_t)field->offset, (size_t)field->size);
				sk_repo_instance_set_value_bit(type, (void_ptr_t)dst, i, 1);
			}
			break;
		}
	}
	return dst;
}

/* Create one prototype instance. The clone map deduplicates a prototype
 * reached both as a sub-object and through a reference remap. */
static sk_rid_t sk_repo_create_from_prototype_internal(sk_clone_context_t* ctx, sk_rid_t prototype_rid, sk_uuid_t uuid, sk_resource_storage_t* parent_storage, u32 field_index) {
	sk_resource_storage_t* prototype_storage = sk_repo_storage(ctx->repo, prototype_rid);
	if (prototype_storage == NULL || prototype_storage->type == NULL) {
		return SK_RID_ZERO;
	}

	sk_rid_t dst = SK_RID_ZERO;
	if (!sk_repo_clone_find(ctx, prototype_rid, &dst)) {
		sk_uuid_t dst_uuid = sk_repo_clone_uuid(ctx, prototype_rid, uuid);
		if (!sk_repo_clone_reserve(ctx, prototype_rid, dst_uuid, &dst)) {
			return SK_RID_ZERO;
		}
	}

	sk_resource_storage_t* storage = sk_repo_storage(ctx->repo, dst);
	if (storage == NULL) {
		ctx->failed = 1;
		return SK_RID_ZERO;
	}
	if (storage->type == NULL) {
		storage->type = prototype_storage->type;
	}
	sk_repo_link_prototype(ctx->repo, storage, prototype_rid);
	if (parent_storage != NULL) {
		storage->parent = parent_storage->rid;
		storage->parent_field_index = field_index;
	}

	if (sk_atomic_ptr_load(&storage->instance) == NULL) {
		u8* instance = sk_repo_build_instance(ctx, storage->type, (const u8*)sk_atomic_ptr_load(&prototype_storage->instance), storage, 1);
		if (ctx->failed) {
			return SK_RID_ZERO;
		}
		sk_mutex_lock(ctx->repo->write_lock);
		sk_atomic_ptr_store(&storage->instance, instance);
		sk_repo_update_version_chain(ctx->repo, storage);
		sk_mutex_unlock(ctx->repo->write_lock);
		if (ctx->scope != NULL) {
			sk_repo_scope_push_change(ctx->scope, ctx->repo, storage, NULL, instance, 1);
		}
		sk_repo_finalize_commit(ctx->repo, storage, NULL, instance, ctx->scope);
	}
	return dst;
}

// NOLINTEND(misc-no-recursion)

/* Fill a pre-reserved clone slot (the root is reserved by the caller so its
 * uuid lands in by_uuid with duplicate rejection). */
static void sk_repo_clone_internal(sk_clone_context_t* ctx, sk_rid_t origin, sk_rid_t dest, sk_resource_storage_t* parent, u32 field_index) {
	sk_resource_storage_t* origin_storage = sk_repo_storage(ctx->repo, origin);
	if (origin_storage == NULL || origin_storage->type == NULL) {
		ctx->failed = 1;
		return;
	}
	sk_resource_storage_t* dest_storage = sk_repo_storage(ctx->repo, dest);
	if (dest_storage == NULL) {
		ctx->failed = 1;
		return;
	}
	dest_storage->type = origin_storage->type;
	sk_repo_link_prototype(ctx->repo, dest_storage, origin_storage->prototype);
	if (parent != NULL) {
		dest_storage->parent = parent->rid;
		dest_storage->parent_field_index = field_index;
	}
	if (sk_atomic_ptr_load(&dest_storage->instance) == NULL) {
		u8* instance = sk_repo_build_instance(ctx, origin_storage->type, (const u8*)sk_atomic_ptr_load(&origin_storage->instance), dest_storage, 0);
		if (ctx->failed) {
			return;
		}
		if (instance != NULL) {
			sk_atomic_ptr_store(&dest_storage->instance, instance);
			if (ctx->scope != NULL) {
				sk_repo_scope_push_change(ctx->scope, ctx->repo, dest_storage, NULL, instance, 1);
			}
		}
	}
}

static sk_rid_t repository_clone(sk_repository_t* repository, sk_rid_t origin, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	sk_resource_storage_t* origin_storage = sk_repo_storage(repository, origin);
	if (origin_storage == NULL || origin_storage->type == NULL) {
		return SK_RID_ZERO;
	}

	sk_clone_context_t ctx;
	ctx.repo = repository;
	ctx.origin = origin;
	ctx.scope = scope;
	ctx.failed = 0;
	sk_hash_map_init(&ctx.map, repository->allocator, NULL, NULL);

	/* The root is reserved into the clone map like any sub-object so that
	 * references to the origin's subtree inside the clone remap to their
	 * clones, and so its uuid lands in by_uuid with duplicate rejection. */
	sk_uuid_t dest_uuid = sk_repo_clone_uuid(&ctx, origin, uuid);
	sk_rid_t dest = SK_RID_ZERO;
	if (!sk_repo_clone_reserve(&ctx, origin, dest_uuid, &dest)) {
		sk_hash_map_free(&ctx.map);
		return SK_RID_ZERO;
	}

	sk_repo_clone_internal(&ctx, origin, dest, NULL, (u32)-1);

	if (ctx.failed) {
		sk_repo_clone_cleanup(repository, &ctx);
		sk_hash_map_free(&ctx.map);
		return SK_RID_ZERO;
	}
	sk_hash_map_free(&ctx.map);
	return dest;
}

static sk_rid_t repository_create_from_prototype(sk_repository_t* repository, sk_rid_t prototype, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	sk_resource_storage_t* prototype_storage = sk_repo_storage(repository, prototype);
	if (prototype_storage == NULL || prototype_storage->type == NULL) {
		return SK_RID_ZERO;
	}

	sk_clone_context_t ctx;
	ctx.repo = repository;
	ctx.origin = prototype;
	ctx.scope = scope;
	ctx.failed = 0;
	sk_hash_map_init(&ctx.map, repository->allocator, NULL, NULL);

	sk_rid_t rid = sk_repo_create_from_prototype_internal(&ctx, prototype, uuid, NULL, (u32)-1);
	if (ctx.failed) {
		sk_repo_clone_cleanup(repository, &ctx);
		sk_hash_map_free(&ctx.map);
		return SK_RID_ZERO;
	}
	sk_hash_map_free(&ctx.map);
	return rid;
}

/* ------------------------------------------------------------------ */
/*  Field accessors                                                   */
/* ------------------------------------------------------------------ */

static sk_resource_storage_t* sk_repo_view_storage(sk_resource_object_t view) {
	return (sk_resource_storage_t*)view.storage;
}

/* Locate the blob (own instance or an ancestor's published instance) that
 * carries a value for the field, walking the prototype chain when the field is
 * unset on this object. Returns NULL when unset everywhere. */
static const u8* sk_repo_get_field_blob(sk_resource_object_t view, u32 index, const sk_resource_field_t** out_field) {
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	if (storage == NULL || storage->type == NULL) {
		return NULL;
	}
	i32 pos = sk_repo_field_position(storage->type, index);
	if (pos < 0) {
		return NULL;
	}
	*out_field = &storage->type->fields[pos];
	const u8* blob = (const u8*)view.instance;
	sk_resource_storage_t* cur = storage;
	while (blob != NULL) {
		if (sk_repo_instance_has_value(storage->type, blob, (u32)pos)) {
			return blob;
		}
		if (cur->prototype.id == 0u) {
			break;
		}
		cur = sk_repo_storage(view.repo, cur->prototype);
		if (cur == NULL) {
			break;
		}
		blob = (const u8*)sk_atomic_ptr_load(&cur->instance);
	}
	return NULL;
}

/* Shared setter validation: returns the field array position or -1, and a
 * pointer to the field. Only write views may set. */
static i32 sk_repo_setup_write_set(sk_resource_object_t view, u32 index, const sk_resource_field_t** out_field) {
	if (view.is_write == 0u) {
		return -1;
	}
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	if (storage == NULL || storage->type == NULL || view.instance == NULL) {
		return -1;
	}
	i32 pos = sk_repo_field_position(storage->type, index);
	if (pos < 0) {
		return -1;
	}
	*out_field = &storage->type->fields[pos];
	return pos;
}

static i32 repository_set_bool(sk_resource_object_t view, u32 index, i32 value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_BOOL) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_int(sk_resource_object_t view, u32 index, i64 value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_INT) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_uint(sk_resource_object_t view, u32 index, u64 value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_UINT) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_float(sk_resource_object_t view, u32 index, f64 value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_FLOAT) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_vec2(sk_resource_object_t view, u32 index, sk_vec2_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_VEC2) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_vec3(sk_resource_object_t view, u32 index, sk_vec3_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_VEC3) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_vec4(sk_resource_object_t view, u32 index, sk_vec4_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_VEC4) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_quat(sk_resource_object_t view, u32 index, sk_quat_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_QUAT) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_mat4(sk_resource_object_t view, u32 index, sk_mat44_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_MAT4) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_color(sk_resource_object_t view, u32 index, sk_color_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_COLOR) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_enum(sk_resource_object_t view, u32 index, u64 value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_ENUM) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_string(sk_resource_object_t view, u32 index, const_chr_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_STRING) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_string_t* s = (sk_field_string_t*)(void_ptr_t)(blob + (size_t)field->offset);
	char* copy = NULL;
	if (value != NULL) {
		copy = sk_repo_copy_string(view.repo, value);
		if (copy == NULL) {
			return -3;
		}
	}
	if (s->chars != NULL) {
		view.repo->allocator->free(view.repo->allocator->instance, s->chars);
	}
	s->chars = copy;
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_blob(sk_resource_object_t view, u32 index, const void* data, u32 size) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_BLOB) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_blob_t* b = (sk_field_blob_t*)(void_ptr_t)(blob + (size_t)field->offset);
	u8* copy = NULL;
	if (size != 0u) {
		copy = sk_repo_copy_bytes(view.repo, data, (size_t)size);
		if (copy == NULL) {
			return -3;
		}
	}
	if (b->data != NULL) {
		view.repo->allocator->free(view.repo->allocator->instance, b->data);
	}
	b->data = copy;
	b->size = size;
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_buffer(sk_resource_object_t view, u32 index, const void* data, u32 size) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_BUFFER) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_buffer_t* b = (sk_field_buffer_t*)(void_ptr_t)(blob + (size_t)field->offset);
	/* Deep copy: the repository owns the payload, the caller keeps @p data
	 * (see the ownership contract on sk_field_buffer_t). A NULL / size-0
	 * input sets an EMPTY buffer (has-value bit set; shadows a prototype). */
	u8* copy = NULL;
	if (size != 0u) {
		copy = sk_repo_copy_bytes(view.repo, data, (size_t)size);
		if (copy == NULL) {
			return -3;
		}
	}
	if (b->data != NULL) {
		view.repo->allocator->free(view.repo->allocator->instance, b->data);
	}
	b->data = copy;
	b->size = size;
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_type_id(sk_resource_object_t view, u32 index, sk_type_id_t value) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_TYPE_ID) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &value, sizeof(value));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_reference(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_REFERENCE) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &rid, sizeof(rid));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_reference_array(sk_resource_object_t view, u32 index, const sk_rid_t* items, u32 count) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_rid_array_t* arr = (sk_field_rid_array_t*)(void_ptr_t)(blob + (size_t)field->offset);
	sk_rid_t* copy = NULL;
	if (count != 0u) {
		copy = (sk_rid_t*)(void_ptr_t)sk_repo_copy_bytes(view.repo, items, (size_t)count * sizeof(sk_rid_t));
		if (copy == NULL) {
			return -3;
		}
	}
	if (arr->items != NULL) {
		view.repo->allocator->free(view.repo->allocator->instance, arr->items);
	}
	arr->items = copy;
	arr->count = count;
	arr->capacity = count;
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_add_to_reference_array(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_rid_array_t* arr = (sk_field_rid_array_t*)(void_ptr_t)(blob + (size_t)field->offset);
	if (sk_repo_rid_list_grow(view.repo->allocator, &arr->items, &arr->capacity, arr->count + 1u) != 0) {
		return -3;
	}
	arr->items[arr->count] = rid;
	arr->count += 1u;
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_remove_from_reference_array(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_rid_array_t* arr = (sk_field_rid_array_t*)(void_ptr_t)(blob + (size_t)field->offset);
	sk_repo_rid_list_remove(&arr->items, &arr->count, rid);
	return 0;
}

static i32 repository_set_subobject(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	memcpy(blob + (size_t)field->offset, &rid, sizeof(rid));
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_set_subobject_list(sk_resource_object_t view, u32 index, const sk_rid_t* items, u32 count) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_subobject_list_t* list = (sk_field_subobject_list_t*)(void_ptr_t)(blob + (size_t)field->offset);
	sk_rid_t* copy = NULL;
	if (count != 0u) {
		copy = (sk_rid_t*)(void_ptr_t)sk_repo_copy_bytes(view.repo, items, (size_t)count * sizeof(sk_rid_t));
		if (copy == NULL) {
			return -3;
		}
	}
	if (list->items != NULL) {
		view.repo->allocator->free(view.repo->allocator->instance, list->items);
	}
	list->items = copy;
	list->count = count;
	list->capacity = count;
	if (list->prototype_removed != NULL) {
		view.repo->allocator->free(view.repo->allocator->instance, list->prototype_removed);
	}
	list->prototype_removed = NULL;
	list->prototype_removed_count = 0u;
	list->prototype_removed_capacity = 0u;
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_add_to_subobject_list(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_subobject_list_t* list = (sk_field_subobject_list_t*)(void_ptr_t)(blob + (size_t)field->offset);
	if (sk_repo_rid_list_grow(view.repo->allocator, &list->items, &list->capacity, list->count + 1u) != 0) {
		return -3;
	}
	list->items[list->count] = rid;
	list->count += 1u;
	/* Explicitly re-adding a sub-object cancels its removed-from-prototype
	 * marker (parity with AddToSubObjectList clearing prototypeRemoved). */
	sk_resource_storage_t* sub_storage = sk_repo_storage(view.repo, rid);
	if (sub_storage != NULL && sub_storage->prototype.id != 0u) {
		sk_repo_rid_list_remove(&list->prototype_removed, &list->prototype_removed_count, sub_storage->prototype);
	}
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	sk_repo_instance_set_value_bit(storage->type, (void_ptr_t)blob, (u32)pos, 1);
	return 0;
}

static i32 repository_remove_from_subobject_list(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return -1;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return -2;
	}
	u8* blob = (u8*)view.instance;
	sk_field_subobject_list_t* list = (sk_field_subobject_list_t*)(void_ptr_t)(blob + (size_t)field->offset);
	for (u32 i = 0u; i < list->count; ++i) {
		if (!SK_RID_EQ(list->items[i], rid)) {
			continue;
		}
		memmove(&list->items[i], &list->items[i + 1u], (size_t)(list->count - i - 1u) * sizeof(sk_rid_t));
		list->count -= 1u;

		/* Clear the sub-object's parent link. */
		sk_resource_storage_t* sub_storage = sk_repo_storage(view.repo, rid);
		if (sub_storage != NULL) {
			sub_storage->parent = SK_RID_ZERO;
			sub_storage->parent_field_index = (u32)-1;
		}
		/* Record prototypeRemoved when this instance is a prototype instance
		 * and the removed sub-object mirrors a prototype sub-object. */
		sk_resource_storage_t* storage = sk_repo_view_storage(view);
		if (sub_storage != NULL && storage->prototype.id != 0u && sub_storage->prototype.id != 0u) {
			sk_resource_storage_t* proto_sub = sk_repo_storage(view.repo, sub_storage->prototype);
			if (proto_sub != NULL && SK_RID_EQ(proto_sub->parent, storage->prototype)) {
				sk_repo_rid_list_add_unique(&list->prototype_removed, &list->prototype_removed_count, &list->prototype_removed_capacity, view.repo->allocator,
											sub_storage->prototype);
			}
		}
		return 0;
	}
	return 0; /* no-op when absent */
}

static u32 repository_remove_from_subobject_list_by_prototype(sk_resource_object_t view, u32 index, sk_rid_t prototype, sk_rid_t* out_items, u32 out_capacity) {
	const sk_resource_field_t* field = NULL;
	i32 pos = sk_repo_setup_write_set(view, index, &field);
	if (pos < 0) {
		return 0u;
	}
	if (field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return 0u;
	}
	u8* blob = (u8*)view.instance;
	sk_field_subobject_list_t* list = (sk_field_subobject_list_t*)(void_ptr_t)(blob + (size_t)field->offset);
	u32 removed_count = 0u;
	u32 write_i = 0u;
	for (u32 i = 0u; i < list->count; ++i) {
		sk_rid_t item = list->items[i];
		sk_resource_storage_t* sub_storage = sk_repo_storage(view.repo, item);
		if (sub_storage != NULL && SK_RID_EQ(sub_storage->prototype, prototype)) {
			if (out_items != NULL && removed_count < out_capacity) {
				out_items[removed_count] = item;
			}
			removed_count += 1u;
			sub_storage->parent = SK_RID_ZERO;
			sub_storage->parent_field_index = (u32)-1;
		} else {
			list->items[write_i] = item;
			write_i += 1u;
		}
	}
	list->count = write_i;
	return removed_count;
}

static i32 repository_has_on_subobject_list(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return 0;
	}
	const sk_field_subobject_list_t* list = (const sk_field_subobject_list_t*)(const_ptr_t)(blob + (size_t)field->offset);
	return sk_repo_rid_list_contains(list->items, list->count, rid);
}

static u32 repository_subobject_list_count(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return 0u;
	}
	const sk_field_subobject_list_t* list = (const sk_field_subobject_list_t*)(const_ptr_t)(blob + (size_t)field->offset);
	return list->count;
}

static i32 repository_has_value_on_this_object(sk_resource_object_t view, u32 index) {
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	if (storage == NULL || storage->type == NULL || view.instance == NULL) {
		return 0;
	}
	i32 pos = sk_repo_field_position(storage->type, index);
	if (pos < 0) {
		return 0;
	}
	return sk_repo_instance_has_value(storage->type, view.instance, (u32)pos);
}

static i32 repository_is_value_overridden(sk_resource_object_t view, u32 index) {
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	if (storage == NULL || storage->prototype.id == 0u) {
		return 0;
	}
	return repository_has_value_on_this_object(view, index);
}

static i32 repository_get_bool(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_BOOL) {
		return 0;
	}
	i32 value = 0;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static i64 repository_get_int(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_INT) {
		return 0;
	}
	i64 value = 0;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static u64 repository_get_uint(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_UINT) {
		return 0u;
	}
	u64 value = 0u;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static f64 repository_get_float(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_FLOAT) {
		return 0.0;
	}
	f64 value = 0.0;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_vec2_t repository_get_vec2(sk_resource_object_t view, u32 index) {
	sk_vec2_t value = {0.0f, 0.0f};
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_VEC2) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_vec3_t repository_get_vec3(sk_resource_object_t view, u32 index) {
	sk_vec3_t value = {0.0f, 0.0f, 0.0f};
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_VEC3) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_vec4_t repository_get_vec4(sk_resource_object_t view, u32 index) {
	sk_vec4_t value = {0.0f, 0.0f, 0.0f, 0.0f};
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_VEC4) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_quat_t repository_get_quat(sk_resource_object_t view, u32 index) {
	sk_quat_t value = {0.0f, 0.0f, 0.0f, 0.0f};
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_QUAT) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_mat44_t repository_get_mat4(sk_resource_object_t view, u32 index) {
	sk_mat44_t value;
	memset(&value, 0, sizeof(value));
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_MAT4) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_color_t repository_get_color(sk_resource_object_t view, u32 index) {
	sk_color_t value = {0.0f, 0.0f, 0.0f, 0.0f};
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_COLOR) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static u64 repository_get_enum(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_ENUM) {
		return 0u;
	}
	u64 value = 0u;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static const_chr_t repository_get_string(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_STRING) {
		return NULL;
	}
	const sk_field_string_t* s = (const sk_field_string_t*)(const_ptr_t)(blob + (size_t)field->offset);
	return s->chars;
}

static const u8* repository_get_blob(sk_resource_object_t view, u32 index, u32* out_size) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (out_size != NULL) {
		*out_size = 0u;
	}
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_BLOB) {
		return NULL;
	}
	const sk_field_blob_t* b = (const sk_field_blob_t*)(const_ptr_t)(blob + (size_t)field->offset);
	if (out_size != NULL) {
		*out_size = b->size;
	}
	return b->data;
}

static const u8* repository_get_buffer(sk_resource_object_t view, u32 index, u32* out_size) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (out_size != NULL) {
		*out_size = 0u;
	}
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_BUFFER) {
		return NULL;
	}
	const sk_field_buffer_t* b = (const sk_field_buffer_t*)(const_ptr_t)(blob + (size_t)field->offset);
	if (out_size != NULL) {
		*out_size = b->size;
	}
	return b->data;
}

static sk_type_id_t repository_get_type_id(sk_resource_object_t view, u32 index) {
	sk_type_id_t value = SK_TYPE_ID_ZERO;
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_TYPE_ID) {
		return value;
	}
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static sk_rid_t repository_get_reference(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_REFERENCE) {
		return SK_RID_ZERO;
	}
	sk_rid_t value = SK_RID_ZERO;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static const sk_rid_t* repository_get_reference_array(sk_resource_object_t view, u32 index, u32* out_count) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (out_count != NULL) {
		*out_count = 0u;
	}
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) {
		return NULL;
	}
	const sk_field_rid_array_t* arr = (const sk_field_rid_array_t*)(const_ptr_t)(blob + (size_t)field->offset);
	if (out_count != NULL) {
		*out_count = arr->count;
	}
	return arr->items;
}

static sk_rid_t repository_get_subobject(sk_resource_object_t view, u32 index) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT) {
		return SK_RID_ZERO;
	}
	sk_rid_t value = SK_RID_ZERO;
	memcpy(&value, blob + (size_t)field->offset, sizeof(value));
	return value;
}

static const sk_rid_t* repository_get_subobject_list(sk_resource_object_t view, u32 index, u32* out_count) {
	const sk_resource_field_t* field = NULL;
	const u8* blob = sk_repo_get_field_blob(view, index, &field);
	if (out_count != NULL) {
		*out_count = 0u;
	}
	if (blob == NULL || field->type != SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		return NULL;
	}
	const sk_field_subobject_list_t* list = (const sk_field_subobject_list_t*)(const_ptr_t)(blob + (size_t)field->offset);
	if (out_count != NULL) {
		*out_count = list->count;
	}
	return list->items;
}

/* Clear a sub-object link on a write view (Resources::RemoveSubObject
 * equivalent): a SubObject field is cleared when it equals @p rid; a
 * SubObjectList field has @p rid removed. */
static void sk_repo_remove_subobject(sk_resource_object_t view, u32 index, sk_rid_t rid) {
	sk_resource_storage_t* storage = sk_repo_view_storage(view);
	if (storage == NULL || storage->type == NULL || view.instance == NULL) {
		return;
	}
	i32 pos = sk_repo_field_position(storage->type, index);
	if (pos < 0) {
		return;
	}
	const sk_resource_field_t* field = &storage->type->fields[pos];
	if (field->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT) {
		sk_rid_t* value = (sk_rid_t*)(void_ptr_t)((u8*)view.instance + (size_t)field->offset);
		if (SK_RID_EQ(*value, rid)) {
			*value = SK_RID_ZERO;
			sk_repo_instance_set_value_bit(storage->type, view.instance, (u32)pos, 0);
		}
	} else if (field->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
		(void)repository_remove_from_subobject_list(view, index, rid);
	}
}

/* ------------------------------------------------------------------ */
/*  Undo / redo scopes                                                 */
/* ------------------------------------------------------------------ */

/* Record one mutation into @p scope: deep-copy the before / after instance
 * snapshots (repository-allocated, owned by the scope) plus the storage's
 * identity (rid, uuid, path, prototype) so a destroyed slot can be re-created
 * on Undo. Any allocation failure drops the change (the mutation still
 * succeeds; it simply is not undoable). */
static void sk_repo_scope_push_change(sk_undo_redo_scope_t* scope, sk_repository_t* repository, sk_resource_storage_t* storage, void_ptr_t before, void_ptr_t after,
									  i32 structural) {
	if (scope == NULL || storage->type == NULL) {
		return;
	}
	sk_undo_redo_change_t change;
	memset(&change, 0, sizeof(change));
	change.repo = repository;
	change.rid = storage->rid;
	change.uuid = storage->uuid;
	change.type = storage->type;
	change.prototype = storage->prototype;
	change.parent = storage->parent;
	change.parent_field_index = storage->parent_field_index;
	change.structural = (u8)(structural != 0);
	if (storage->path != NULL) {
		size_t len = strlen(storage->path);
		change.path = (char*)scope->allocator->alloc(scope->allocator->instance, len + 1u);
		if (change.path == NULL) {
			return; /* OOM: skip recording */
		}
		memcpy(change.path, storage->path, len + 1u);
	}
	if (before != NULL) {
		change.before = sk_repo_instance_copy(repository, storage->type, (const u8*)before);
		if (change.before == NULL) {
			if (change.path != NULL) {
				scope->allocator->free(scope->allocator->instance, change.path);
			}
			return;
		}
	}
	if (after != NULL) {
		change.after = sk_repo_instance_copy(repository, storage->type, (const u8*)after);
		if (change.after == NULL) {
			if (change.before != NULL) {
				sk_repo_instance_destroy(repository, storage->type, change.before);
			}
			if (change.path != NULL) {
				scope->allocator->free(scope->allocator->instance, change.path);
			}
			return;
		}
	}
	if (sk_array_push(&scope->changes, change) != 0) {
		if (change.before != NULL) {
			sk_repo_instance_destroy(repository, storage->type, change.before);
		}
		if (change.after != NULL) {
			sk_repo_instance_destroy(repository, storage->type, change.after);
		}
		if (change.path != NULL) {
			scope->allocator->free(scope->allocator->instance, change.path);
		}
	}
}

/* Apply one change's @p snapshot to its storage slot under the write lock:
 * publish a fresh deep copy (or clear when the snapshot is NULL), queue the
 * superseded instance for GC, bump the version chain, and refresh hierarchy
 * links without re-running prototype propagation. When the slot was released
 * (destroyed resource) the slot is re-created first from the change's recorded
 * identity; a change whose slot is gone and whose snapshot is no value (a
 * rolled-back create) is skipped. */
static void sk_repo_scope_apply_change(const sk_undo_redo_change_t* change, void_ptr_t snapshot) {
	sk_repository_t* repository = change->repo;
	if (change->type == NULL) {
		return;
	}
	sk_mutex_lock(repository->write_lock);

	sk_resource_storage_t* storage = sk_repo_storage(repository, change->rid);
	if (storage == NULL) {
		if (snapshot == NULL) {
			sk_mutex_unlock(repository->write_lock);
			return;
		}
		storage = sk_repo_allocate_slot_id(repository, change->rid, change->uuid);
		if (storage == NULL) {
			sk_mutex_unlock(repository->write_lock);
			return; /* uuid conflict / OOM: leave the slot gone */
		}
		storage->type = change->type;
		if (change->path != NULL) {
			char* copy = sk_repo_copy_string(repository, change->path);
			if (copy != NULL) {
				if (sk_hash_map_put(&repository->rids_by_path, copy, change->rid) == 0) {
					storage->path = copy;
				} else {
					repository->allocator->free(repository->allocator->instance, copy);
				}
			}
		}
		if (change->prototype.id != 0u) {
			storage->prototype = change->prototype;
			sk_resource_storage_t* prototype = sk_repo_storage(repository, change->prototype);
			if (prototype != NULL) {
				sk_repo_list_add_unique(&prototype->prototype_instances, change->rid);
			}
		}
		storage->parent = change->parent;
		storage->parent_field_index = change->parent_field_index;
		repository->resource_count += 1u;
	} else if (storage->type == NULL) {
		storage->type = change->type;
	}

	if (snapshot == NULL && change->structural != 0) {
		/* Undo a create / clone / create_from_prototype or redo a destroy:
		 * the mutation owns the slot, so applying the no-value snapshot fully
		 * releases it again (symmetry with the slot re-creation above). */
		void_ptr_t current = sk_atomic_ptr_load(&storage->instance);
		if (current != NULL) {
			(void)sk_array_push(&repository->to_collect, ((sk_resource_gc_item_t){change->type, current}));
		}
		sk_repo_release_slot(repository, change->rid);
		if (repository->resource_count > 0u) {
			repository->resource_count -= 1u;
		}
		sk_mutex_unlock(repository->write_lock);
		return;
	}

	void_ptr_t old_instance = sk_atomic_ptr_load(&storage->instance);
	void_ptr_t new_instance = NULL;
	if (snapshot != NULL) {
		new_instance = sk_repo_instance_copy(repository, change->type, (const u8*)snapshot);
		if (new_instance == NULL) {
			sk_mutex_unlock(repository->write_lock);
			return; /* OOM: leave the slot as-is */
		}
	}
	if (old_instance != NULL) {
		(void)sk_array_push(&repository->to_collect, ((sk_resource_gc_item_t){change->type, old_instance}));
	}
	sk_atomic_ptr_store(&storage->instance, new_instance);
	sk_repo_update_version_chain(repository, storage);
	sk_mutex_unlock(repository->write_lock);
	sk_repo_finalize_commit(repository, storage, old_instance, new_instance, NULL);
}

static sk_undo_redo_scope_t* repository_undo_redo_scope_create(const sk_allocator_t* allocator, const_chr_t name) {
	sk_undo_redo_scope_t* scope = (sk_undo_redo_scope_t*)allocator->alloc(allocator->instance, sizeof(*scope));
	if (scope == NULL) {
		return NULL;
	}
	memset(scope, 0, sizeof(*scope));
	scope->allocator = allocator;
	sk_array_init(&scope->changes, allocator);
	if (name != NULL) {
		size_t len = strlen(name);
		scope->name = (char*)allocator->alloc(allocator->instance, len + 1u);
		if (scope->name == NULL) {
			allocator->free(allocator->instance, scope);
			return NULL;
		}
		memcpy(scope->name, name, len + 1u);
	}
	return scope;
}

static void repository_undo_redo_scope_destroy(sk_undo_redo_scope_t* scope) {
	if (scope == NULL) {
		return;
	}
	for (u32 i = 0u; i < scope->changes.count; ++i) {
		const sk_undo_redo_change_t* change = &scope->changes.items[i];
		if (change->before != NULL) {
			sk_repo_instance_destroy(change->repo, change->type, change->before);
		}
		if (change->after != NULL) {
			sk_repo_instance_destroy(change->repo, change->type, change->after);
		}
		if (change->path != NULL) {
			scope->allocator->free(scope->allocator->instance, change->path);
		}
	}
	sk_array_free(&scope->changes);
	if (scope->name != NULL) {
		scope->allocator->free(scope->allocator->instance, scope->name);
	}
	scope->allocator->free(scope->allocator->instance, scope);
}

static void repository_undo_redo_scope_undo(sk_undo_redo_scope_t* scope) {
	for (u32 i = scope->changes.count; i > 0u; --i) {
		const sk_undo_redo_change_t* change = &scope->changes.items[i - 1u];
		sk_repo_scope_apply_change(change, change->before);
	}
}

static void repository_undo_redo_scope_redo(sk_undo_redo_scope_t* scope) {
	for (u32 i = 0u; i < scope->changes.count; ++i) {
		const sk_undo_redo_change_t* change = &scope->changes.items[i];
		sk_repo_scope_apply_change(change, change->after);
	}
}

static const_chr_t repository_undo_redo_scope_get_name(const sk_undo_redo_scope_t* scope) {
	return scope->name;
}

static const_chr_t repository_type_name(const sk_resource_type_t* type) {
	return type->name;
}

static sk_type_id_t repository_type_id(const sk_resource_type_t* type) {
	return type->type_id;
}

static u32 repository_type_field_count(const sk_resource_type_t* type) {
	return type->field_count;
}

static const sk_resource_field_t* repository_type_field_at(const sk_resource_type_t* type, u32 position) {
	if (position >= type->field_count) {
		return NULL;
	}
	return &type->fields[position];
}

/* ------------------------------------------------------------------ */
/*  Module API table                                                  */
/* ------------------------------------------------------------------ */

static const sk_repository_api_t repository_api = {
	repository_create,
	repository_destroy,
	repository_register_type,
	repository_find_type,
	repository_find_type_by_name,
	repository_type_name,
	repository_type_id,
	repository_type_field_count,
	repository_type_field_at,
	repository_create_resource,
	repository_destroy_resource,
	repository_has_resource,
	repository_resource_count,
	repository_resource_type,
	repository_resource_uuid,
	repository_resource_instance,
	repository_find_by_uuid,
	repository_set_path,
	repository_get_path,
	repository_find_by_path,
	repository_read,
	repository_write,
	repository_commit,
	repository_discard,
	repository_get_version,
	repository_has_value,
	repository_garbage_collect,
	repository_end_frame,
	repository_clone,
	repository_create_from_prototype,
	repository_get_parent,
	repository_get_prototype,
	repository_get_top_parent,
	repository_is_parent_of,
	repository_set_bool,
	repository_set_int,
	repository_set_uint,
	repository_set_float,
	repository_set_vec2,
	repository_set_vec3,
	repository_set_vec4,
	repository_set_quat,
	repository_set_mat4,
	repository_set_color,
	repository_set_enum,
	repository_set_string,
	repository_set_blob,
	repository_set_buffer,
	repository_set_type_id,
	repository_set_reference,
	repository_set_reference_array,
	repository_add_to_reference_array,
	repository_remove_from_reference_array,
	repository_set_subobject,
	repository_set_subobject_list,
	repository_add_to_subobject_list,
	repository_remove_from_subobject_list,
	repository_remove_from_subobject_list_by_prototype,
	repository_has_on_subobject_list,
	repository_subobject_list_count,
	repository_has_value_on_this_object,
	repository_is_value_overridden,
	repository_get_bool,
	repository_get_int,
	repository_get_uint,
	repository_get_float,
	repository_get_vec2,
	repository_get_vec3,
	repository_get_vec4,
	repository_get_quat,
	repository_get_mat4,
	repository_get_color,
	repository_get_enum,
	repository_get_string,
	repository_get_blob,
	repository_get_buffer,
	repository_get_type_id,
	repository_get_reference,
	repository_get_reference_array,
	repository_get_subobject,
	repository_get_subobject_list,
	repository_undo_redo_scope_create,
	repository_undo_redo_scope_destroy,
	repository_undo_redo_scope_undo,
	repository_undo_redo_scope_redo,
	repository_undo_redo_scope_get_name,
};

SK_API const sk_repository_api_t* sk_repository_api(void) {
	return &repository_api;
}

#ifdef SK_TESTS
#include "test.h"

#include <stdio.h>
#include <string.h>

/* Test payload: one scalar field + one indirection (String) field. */
typedef struct test_payload_t {
	i64 value;
	sk_field_string_t label;
} test_payload_t;

static const sk_resource_field_t test_payload_fields[2] = {
	{"value", 0u, SK_RESOURCE_FIELD_TYPE_INT, 0u, (u32)sizeof(i64), {0ull, 0ull}},
	{"label", 1u, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(test_payload_t, label), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
};

static const sk_resource_field_t test_int_field = {
	"value", 0u, SK_RESOURCE_FIELD_TYPE_INT, 0u, (u32)sizeof(i64), {0ull, 0ull},
};

/* Distinct per-tag type ids (tests use separate repositories, so values may
 * repeat across tests). */
static sk_type_id_t test_type_id(u64 tag) {
	return (sk_type_id_t){tag * 0x9e3779b97f4a7c15ull + 1ull, tag};
}

static sk_resource_type_desc_t test_payload_desc(sk_type_id_t tid, const_chr_t name, const void* defaults) {
	sk_resource_type_desc_t desc;
	desc.type_id = tid;
	desc.name = name;
	desc.instance_size = (u32)sizeof(test_payload_t);
	desc.fields = test_payload_fields;
	desc.field_count = 2u;
	desc.defaults = defaults;
	return desc;
}

static sk_resource_type_desc_t test_int_desc(sk_type_id_t tid, const_chr_t name) {
	sk_resource_type_desc_t desc;
	desc.type_id = tid;
	desc.name = name;
	desc.instance_size = (u32)sizeof(i64);
	desc.fields = &test_int_field;
	desc.field_count = 1u;
	desc.defaults = NULL;
	return desc;
}

SK_TEST(repository_create_destroy) {
	const sk_repository_api_t* api = sk_repository_api();
	TEST_ASSERT_NOT_NULL(api);
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, SK_UUID_ZERO).id == 0u);
	TEST_ASSERT_NULL(api->find_type(repo, (sk_type_id_t){1u, 2u}));
	api->destroy(repo);
}

SK_TEST(repository_type_register_lookup_dupe) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	sk_type_id_t tid = test_type_id(600u);
	sk_resource_type_desc_t desc = test_payload_desc(tid, "register.type", NULL);
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));

	const sk_resource_type_t* type = api->find_type(repo, tid);
	TEST_ASSERT_NOT_NULL(type);
	TEST_ASSERT_EQUAL_PTR(type, api->find_type_by_name(repo, "register.type"));
	TEST_ASSERT_NULL(api->find_type(repo, test_type_id(999u)));
	TEST_ASSERT_NULL(api->find_type_by_name(repo, "register.missing"));

	/* Same id + same name → duplicate id. */
	TEST_ASSERT_NOT_EQUAL(0, api->register_type(repo, &desc));
	/* Same id + different name → duplicate id. */
	{
		sk_resource_type_desc_t d2 = test_payload_desc(tid, "register.type2", NULL);
		TEST_ASSERT_NOT_EQUAL(0, api->register_type(repo, &d2));
	}
	/* Different id + same name → duplicate name. */
	{
		sk_resource_type_desc_t d3 = test_payload_desc(test_type_id(601u), "register.type", NULL);
		TEST_ASSERT_NOT_EQUAL(0, api->register_type(repo, &d3));
	}
	/* Invalid descriptor: zero instance size. */
	{
		sk_resource_type_desc_t d4 = test_payload_desc(test_type_id(602u), "register.type4", NULL);
		d4.instance_size = 0u;
		TEST_ASSERT_NOT_EQUAL(0, api->register_type(repo, &d4));
	}
	/* Duplicates must not have disturbed the original registration. */
	TEST_ASSERT_EQUAL_PTR(type, api->find_type_by_name(repo, "register.type"));

	api->destroy(repo);
}

SK_TEST(repository_independent_instances) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo_a = api->create(sk_allocator_default());
	sk_repository_t* repo_b = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo_a);
	TEST_ASSERT_NOT_NULL(repo_b);

	sk_type_id_t tid = test_type_id(300u);
	sk_resource_type_desc_t desc = test_payload_desc(tid, "shared.type", NULL);
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo_a, &desc));
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo_b, &desc));

	const sk_resource_type_t* ta = api->find_type(repo_a, tid);
	const sk_resource_type_t* tb = api->find_type(repo_b, tid);
	TEST_ASSERT_NOT_NULL(ta);
	TEST_ASSERT_NOT_NULL(tb);
	TEST_ASSERT_NOT_EQUAL_PTR(ta, tb);

	sk_uuid_t uuid = {0x1234ull, 0x5678ull};
	sk_rid_t rid = api->create_resource(repo_a, ta, uuid, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo_a));

	/* repo_b shares no state with repo_a. */
	TEST_ASSERT_FALSE(api->has_resource(repo_b, rid));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo_b, uuid).id == 0u);
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo_b));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo_a, uuid), rid));

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo_a, rid, NULL));
	TEST_ASSERT_FALSE(api->has_resource(repo_a, rid));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo_a, uuid).id == 0u);

	api->destroy(repo_a);
	api->destroy(repo_b);
}

SK_TEST(repository_uuid_uniqueness) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	sk_resource_type_desc_t desc = test_int_desc(test_type_id(700u), "uuid.type");
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	const sk_resource_type_t* type = api->find_type_by_name(repo, "uuid.type");
	TEST_ASSERT_NOT_NULL(type);

	sk_uuid_t uuid = {0xaabbccddull, 0x11223344ull};
	sk_rid_t r1 = api->create_resource(repo, type, uuid, NULL);
	sk_rid_t r2 = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_TRUE(SK_RID_EQ(r1, r2));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	/* Zero UUID → no uniqueness constraint; each create is a fresh resource. */
	sk_rid_t z1 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t z2 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_FALSE(SK_RID_EQ(z1, z2));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, uuid), r1));
	TEST_ASSERT_EQUAL_UINT64(3u, api->resource_count(repo));

	api->destroy(repo);
}

SK_TEST(repository_default_deep_copy) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	char hello[] = "hello world";
	test_payload_t defaults = {42, {hello}};
	sk_resource_type_desc_t desc = test_payload_desc(test_type_id(200u), "deep.payload", &defaults);
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));

	const sk_resource_type_t* type = api->find_type_by_name(repo, "deep.payload");
	TEST_ASSERT_NOT_NULL(type);

	sk_rid_t a = api->create_resource(repo, type, (sk_uuid_t){1u, 0u}, NULL);
	sk_rid_t b = api->create_resource(repo, type, (sk_uuid_t){2u, 0u}, NULL);
	TEST_ASSERT_TRUE(a.id != 0u);
	TEST_ASSERT_TRUE(b.id != 0u);

	test_payload_t* pa = (test_payload_t*)api->resource_instance(repo, a);
	test_payload_t* pb = (test_payload_t*)api->resource_instance(repo, b);
	TEST_ASSERT_NOT_NULL(pa);
	TEST_ASSERT_NOT_NULL(pb);
	TEST_ASSERT_EQUAL_INT64(42, pa->value);
	TEST_ASSERT_EQUAL_STRING("hello world", pa->label.chars);
	TEST_ASSERT_EQUAL_STRING("hello world", pb->label.chars);
	/* Deep copies: distinct heap strings, not shared pointers. */
	TEST_ASSERT_NOT_EQUAL_PTR(hello, pa->label.chars);
	TEST_ASSERT_NOT_EQUAL_PTR(pa->label.chars, pb->label.chars);

	/* Instances are independent. */
	pa->value = 99;
	TEST_ASSERT_EQUAL_INT64(99, pa->value);
	TEST_ASSERT_EQUAL_INT64(42, pb->value);

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, a, NULL));
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, b, NULL));
	api->destroy(repo);
}

SK_TEST(repository_multi_page_rids) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	sk_resource_type_desc_t desc = test_int_desc(test_type_id(400u), "multi.page");
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	const sk_resource_type_t* type = api->find_type(repo, test_type_id(400u));
	TEST_ASSERT_NOT_NULL(type);

	const u32 resource_count = 6000u;
	sk_rid_t first = SK_RID_ZERO;
	sk_rid_t last = SK_RID_ZERO;
	for (u32 i = 0u; i < resource_count; ++i) {
		sk_uuid_t uuid = {(u64)i + 1u, 0u};
		sk_rid_t rid = api->create_resource(repo, type, uuid, NULL);
		TEST_ASSERT_TRUE(rid.id != 0u);
		TEST_ASSERT_TRUE(api->has_resource(repo, rid));
		if (i == 0u) {
			first = rid;
		}
		if (i == resource_count - 1u) {
			last = rid;
		}
	}
	TEST_ASSERT_EQUAL_UINT64((u64)resource_count, api->resource_count(repo));
	TEST_ASSERT_EQUAL_UINT64(1u, first.id);
	TEST_ASSERT_EQUAL_UINT64((u64)resource_count, last.id);
	TEST_ASSERT_TRUE(last.id > 4096u); /* spans multiple pages */

	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, (sk_uuid_t){1u, 0u}), first));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, (sk_uuid_t){(u64)resource_count, 0u}), last));
	TEST_ASSERT_EQUAL_PTR(type, api->resource_type(repo, last));
	sk_uuid_t expected_uuid = {(u64)resource_count, 0u};
	TEST_ASSERT_TRUE(SK_UUID_EQ(api->resource_uuid(repo, last), expected_uuid));
	TEST_ASSERT_NOT_NULL(api->resource_instance(repo, last));

	api->destroy(repo);
}

SK_TEST(repository_path_uniqueness) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);

	sk_resource_type_desc_t desc = test_int_desc(test_type_id(500u), "path.type");
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	const sk_resource_type_t* type = api->find_type_by_name(repo, "path.type");
	TEST_ASSERT_NOT_NULL(type);

	sk_rid_t rid1 = api->create_resource(repo, type, (sk_uuid_t){1u, 0u}, NULL);
	sk_rid_t rid2 = api->create_resource(repo, type, (sk_uuid_t){2u, 0u}, NULL);
	TEST_ASSERT_TRUE(rid1.id != 0u);
	TEST_ASSERT_TRUE(rid2.id != 0u);

	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid1, "assets/one.foo"));
	TEST_ASSERT_EQUAL_STRING("assets/one.foo", api->get_path(repo, rid1));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/one.foo"), rid1));

	/* Path is unique: a second resource cannot claim it. */
	TEST_ASSERT_NOT_EQUAL(0, api->set_path(repo, rid2, "assets/one.foo"));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/one.foo"), rid1));
	TEST_ASSERT_NULL(api->get_path(repo, rid2));

	/* Destroying the owner releases the path. */
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, rid1, NULL));
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/one.foo").id == 0u);
	TEST_ASSERT_NULL(api->get_path(repo, rid1));

	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid2, "assets/one.foo"));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/one.foo"), rid2));

	/* Replacing a path drops the old mapping. */
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid2, "assets/two.bar"));
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/one.foo").id == 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/two.bar"), rid2));

	api->destroy(repo);
}

/* Fail-once allocator: fails exactly the allocation whose index equals fail_at
 * (0xFFFFFFFF = never), then recovers. Used to exercise OOM rollback paths. */
typedef struct test_fail_alloc_t {
	const sk_allocator_t* base;
	u32 allocs_done;
	u32 fail_at;
} test_fail_alloc_t;

static void_ptr_t test_fail_alloc(void_ptr_t instance, size_t size) {
	test_fail_alloc_t* state = (test_fail_alloc_t*)instance;
	if (state->allocs_done == state->fail_at) {
		state->allocs_done += 1u;
		return NULL;
	}
	state->allocs_done += 1u;
	return state->base->alloc(state->base->instance, size);
}

static void test_fail_free(void_ptr_t instance, void_ptr_t ptr) {
	test_fail_alloc_t* state = (test_fail_alloc_t*)instance;
	state->base->free(state->base->instance, ptr);
}

static void_ptr_t test_fail_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	test_fail_alloc_t* state = (test_fail_alloc_t*)instance;
	if (state->allocs_done == state->fail_at) {
		state->allocs_done += 1u;
		return NULL;
	}
	state->allocs_done += 1u;
	return state->base->realloc(state->base->instance, ptr, size);
}

SK_TEST(repository_oom_safety) {
	const sk_repository_api_t* api = sk_repository_api();
	test_fail_alloc_t state = {sk_allocator_default(), 0u, 0xFFFFFFFFu};
	sk_allocator_t fail_allocator = {&state, test_fail_alloc, test_fail_free, test_fail_realloc};

	sk_repository_t* repo = api->create(&fail_allocator);
	TEST_ASSERT_NOT_NULL(repo);

	sk_resource_type_desc_t desc = test_payload_desc(test_type_id(100u), "oom.payload", NULL);
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	const sk_resource_type_t* type = api->find_type(repo, test_type_id(100u));
	TEST_ASSERT_NOT_NULL(type);

	/* Warm the page + uuid index so the next create only allocates the blob. */
	sk_rid_t warm = api->create_resource(repo, type, (sk_uuid_t){1u, 0u}, NULL);
	TEST_ASSERT_TRUE(warm.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	/* set_path: the path copy is the first allocation → OOM, path unchanged. */
	state.fail_at = state.allocs_done;
	TEST_ASSERT_NOT_EQUAL(0, api->set_path(repo, warm, "assets/oom.foo"));
	TEST_ASSERT_NULL(api->get_path(repo, warm));
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/oom.foo").id == 0u);

	/* create_resource: instance blob OOM → slot + uuid rolled back. */
	state.fail_at = state.allocs_done;
	sk_rid_t failed = api->create_resource(repo, type, (sk_uuid_t){2u, 0u}, NULL);
	TEST_ASSERT_TRUE(failed.id == 0u);
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, (sk_uuid_t){2u, 0u}).id == 0u);
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	/* register_type: OOM inside the defaults deep copy → nothing registered. */
	{
		char hello[] = "hello";
		test_payload_t defaults = {1, {hello}};
		sk_resource_type_desc_t desc2 = test_payload_desc(test_type_id(101u), "oom.payload2", &defaults);
		/* Register allocs: type, name, fields, defaults blob, then string
		 * chars; fail_at = start + 4 fails the string deep copy. */
		state.fail_at = state.allocs_done + 4u;
		TEST_ASSERT_NOT_EQUAL(0, api->register_type(repo, &desc2));
		TEST_ASSERT_NULL(api->find_type(repo, test_type_id(101u)));
		TEST_ASSERT_NULL(api->find_type_by_name(repo, "oom.payload2"));
	}

	/* The repository remains fully usable after failed operations. */
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, warm, "assets/after.fail"));
	TEST_ASSERT_EQUAL_STRING("assets/after.fail", api->get_path(repo, warm));

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Hierarchy test helpers                                            */
/* ------------------------------------------------------------------ */

#define RT_FIELD_INT 0u
#define RT_FIELD_STRING 1u
#define RT_FIELD_REFERENCE 2u
#define RT_FIELD_REFERENCE_ARRAY 3u
#define RT_FIELD_SUBOBJECT 4u
#define RT_FIELD_SUBOBJECT_LIST 5u

/* Rich test payload exercising every field category the hierarchy layer uses. */
typedef struct rt_object_t {
	i64 int_value;
	u64 uint_value;
	f64 float_value;
	i32 bool_value;
	sk_field_string_t string_value;
	sk_rid_t reference;
	sk_field_rid_array_t reference_array;
	sk_rid_t subobject;
	sk_field_subobject_list_t subobject_list;
} rt_object_t;

static const sk_resource_field_t rt_fields[6] = {
	{"int", RT_FIELD_INT, SK_RESOURCE_FIELD_TYPE_INT, 0u, (u32)sizeof(i64), {0ull, 0ull}},
	{"string", RT_FIELD_STRING, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(rt_object_t, string_value), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"reference", RT_FIELD_REFERENCE, SK_RESOURCE_FIELD_TYPE_REFERENCE, (u32)offsetof(rt_object_t, reference), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
	{"refArray", RT_FIELD_REFERENCE_ARRAY, SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY, (u32)offsetof(rt_object_t, reference_array), (u32)sizeof(sk_field_rid_array_t), {0ull, 0ull}},
	{"subobject", RT_FIELD_SUBOBJECT, SK_RESOURCE_FIELD_TYPE_SUB_OBJECT, (u32)offsetof(rt_object_t, subobject), (u32)sizeof(sk_rid_t), {0ull, 0ull}},
	{"subobjectList",
	 RT_FIELD_SUBOBJECT_LIST,
	 SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST,
	 (u32)offsetof(rt_object_t, subobject_list),
	 (u32)sizeof(sk_field_subobject_list_t),
	 {0ull, 0ull}},
};

static sk_repository_t* rt_repo_full(const sk_allocator_t* allocator, const sk_resource_type_t** out_type, u64 tag) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(allocator);
	TEST_ASSERT_NOT_NULL(repo);
	sk_resource_type_desc_t desc;
	desc.type_id = test_type_id(tag);
	desc.name = "rt.type";
	desc.instance_size = (u32)sizeof(rt_object_t);
	desc.fields = rt_fields;
	desc.field_count = 6u;
	desc.defaults = NULL;
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	*out_type = api->find_type_by_name(repo, "rt.type");
	TEST_ASSERT_NOT_NULL(*out_type);
	return repo;
}

static sk_repository_t* rt_repo(const sk_resource_type_t** out_type, u64 tag) {
	return rt_repo_full(sk_allocator_default(), out_type, tag);
}

static sk_rid_t rt_make_sub(const sk_repository_api_t* api, sk_repository_t* repo, const sk_resource_type_t* type, i64 value) {
	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	sk_resource_object_t view = api->write(repo, rid);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, value));
	api->commit(view, NULL);
	return rid;
}

static void rt_set_int(const sk_repository_api_t* api, sk_repository_t* repo, sk_rid_t rid, i64 value) {
	sk_resource_object_t view = api->write(repo, rid);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
	TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, value));
	api->commit(view, NULL);
}

/* Verify an instance mirrors exactly the given prototype sub-objects: each
 * list entry links back through GetPrototype to one expected prototype and its
 * scalar int matches the prototype's. */
static void rt_verify_mirror(const sk_repository_api_t* api, sk_repository_t* repo, sk_rid_t instance, const sk_rid_t* expected_protos, u32 expected_count) {
	sk_resource_object_t read = api->read(repo, instance);
	u32 count = 0u;
	const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(expected_count, count);
	for (u32 i = 0u; i < count; ++i) {
		sk_rid_t proto = api->get_prototype(repo, items[i]);
		TEST_ASSERT_TRUE(proto.id != 0u);
		int found = 0;
		for (u32 j = 0u; j < expected_count; ++j) {
			if (SK_RID_EQ(proto, expected_protos[j])) {
				found = 1;
				break;
			}
		}
		TEST_ASSERT_TRUE(found);
		sk_resource_object_t sub_read = api->read(repo, items[i]);
		sk_resource_object_t proto_read = api->read(repo, proto);
		TEST_ASSERT_EQUAL_INT64(api->get_int(proto_read, RT_FIELD_INT), api->get_int(sub_read, RT_FIELD_INT));
	}
}

/* ------------------------------------------------------------------ */
/*  Read / Write / Commit                                             */
/* ------------------------------------------------------------------ */

SK_TEST(repository_read_write_commit_discard) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 10u);

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	TEST_ASSERT_TRUE(api->has_value(repo, rid)); /* create materializes a zeroed instance */
	TEST_ASSERT_EQUAL_UINT64(1u, api->get_version(repo, rid));

	/* Write a value, discard it: nothing published, version unchanged. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 5));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "discarded"));
		api->discard(view);
	}
	TEST_ASSERT_TRUE(api->has_value(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->get_version(repo, rid));
	TEST_ASSERT_EQUAL_INT64(0, api->get_int(api->read(repo, rid), RT_FIELD_INT)); /* discarded edit not published */

	/* Commit publishes and bumps the version. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 10));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(api->has_value(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(2u, api->get_version(repo, rid));
	sk_resource_object_t read = api->read(repo, rid);
	TEST_ASSERT_EQUAL_INT64(10, api->get_int(read, RT_FIELD_INT));

	/* Second commit bumps again and the read sees the fresh value. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 20));
		api->commit(view, NULL);
	}
	TEST_ASSERT_EQUAL_UINT64(3u, api->get_version(repo, rid));
	read = api->read(repo, rid);
	TEST_ASSERT_EQUAL_INT64(20, api->get_int(read, RT_FIELD_INT));

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Prototype hierarchy and scalar inheritance                        */
/* ------------------------------------------------------------------ */

SK_TEST(repository_prototype_create_hierarchy) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 20u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub = rt_make_sub(api, repo, type, 1);
	sk_rid_t outer = api->create_resource(repo, type, SK_UUID_ZERO, NULL);

	/* prototype owns sub as a sub-object. */
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 7));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, sub), prototype));

	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, instance), prototype));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, instance), SK_RID_ZERO));

	/* The mirror sub-object links back to the prototype sub-object. */
	sk_resource_object_t read = api->read(repo, instance);
	u32 count = 0u;
	const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, items[0]), sub));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, items[0]), instance));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_top_parent(repo, items[0]), instance));
	TEST_ASSERT_TRUE(api->is_parent_of(repo, instance, items[0]));
	TEST_ASSERT_FALSE(api->is_parent_of(repo, instance, instance));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_top_parent(repo, sub), prototype));

	/* References to the prototype root stay unremapped (parity with the
	 * main-branch SubObjectListPrototypes test). */
	{
		sk_resource_object_t view = api->write(repo, sub);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, prototype));
		api->commit(view, NULL);
	}
	sk_resource_object_t mirror_read = api->read(repo, items[0]);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(mirror_read, RT_FIELD_REFERENCE), prototype));

	/* outer stays unrelated. */
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, outer), SK_RID_ZERO));
	TEST_ASSERT_FALSE(api->is_parent_of(repo, instance, outer));

	api->destroy(repo);
}

SK_TEST(repository_prototype_scalar_inheritance_and_override) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 21u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	rt_set_int(api, repo, prototype, 10);

	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance.id != 0u);

	/* Unset on the instance, inherited from the prototype. */
	sk_resource_object_t read = api->read(repo, instance);
	TEST_ASSERT_EQUAL_INT64(10, api->get_int(read, RT_FIELD_INT));
	TEST_ASSERT_FALSE(api->has_value_on_this_object(read, RT_FIELD_INT));
	TEST_ASSERT_FALSE(api->is_value_overridden(read, RT_FIELD_INT));

	/* Instance override shadows the prototype. */
	rt_set_int(api, repo, instance, 99);
	read = api->read(repo, instance);
	TEST_ASSERT_EQUAL_INT64(99, api->get_int(read, RT_FIELD_INT));
	TEST_ASSERT_TRUE(api->has_value_on_this_object(read, RT_FIELD_INT));
	TEST_ASSERT_TRUE(api->is_value_overridden(read, RT_FIELD_INT));

	/* Prototype update does not clobber the instance override. */
	rt_set_int(api, repo, prototype, 30);
	read = api->read(repo, instance);
	TEST_ASSERT_EQUAL_INT64(99, api->get_int(read, RT_FIELD_INT));

	/* A fresh instance sees the new prototype value. */
	sk_rid_t instance2 = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	read = api->read(repo, instance2);
	TEST_ASSERT_EQUAL_INT64(30, api->get_int(read, RT_FIELD_INT));
	TEST_ASSERT_FALSE(api->is_value_overridden(read, RT_FIELD_INT));

	api->destroy(repo);
}

SK_TEST(repository_prototype_string_inheritance) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 22u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "blegh"));
		api->commit(view, NULL);
	}
	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	sk_resource_object_t read = api->read(repo, instance);
	TEST_ASSERT_EQUAL_STRING("blegh", api->get_string(read, RT_FIELD_STRING));

	/* Prototype string update propagates to the instance (lazy chain read). */
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "updated"));
		api->commit(view, NULL);
	}
	read = api->read(repo, instance);
	TEST_ASSERT_EQUAL_STRING("updated", api->get_string(read, RT_FIELD_STRING));

	/* Instance override keeps its own value. */
	{
		sk_resource_object_t view = api->write(repo, instance);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "mine"));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "again"));
		api->commit(view, NULL);
	}
	read = api->read(repo, instance);
	TEST_ASSERT_EQUAL_STRING("mine", api->get_string(read, RT_FIELD_STRING));

	api->destroy(repo);
}

SK_TEST(repository_prototype_chain_scalars) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 23u);

	/* Prototype chain A → B → C with parallel instances. */
	sk_rid_t a = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	rt_set_int(api, repo, a, 10);
	sk_rid_t b = api->create_from_prototype(repo, a, SK_UUID_ZERO, NULL);
	sk_rid_t c = api->create_from_prototype(repo, b, SK_UUID_ZERO, NULL);

	sk_rid_t i1 = api->create_from_prototype(repo, c, SK_UUID_ZERO, NULL);
	sk_rid_t i2 = api->create_from_prototype(repo, c, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, i1), c));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, c), b));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, b), a));

	/* Values inherited through the whole chain. */
	sk_resource_object_t read = api->read(repo, i1);
	TEST_ASSERT_EQUAL_INT64(10, api->get_int(read, RT_FIELD_INT));
	TEST_ASSERT_FALSE(api->has_value_on_this_object(read, RT_FIELD_INT));
	read = api->read(repo, i2);
	TEST_ASSERT_EQUAL_INT64(10, api->get_int(read, RT_FIELD_INT));

	/* Prototype update propagates down the chain. */
	rt_set_int(api, repo, a, 30);
	read = api->read(repo, i1);
	TEST_ASSERT_EQUAL_INT64(30, api->get_int(read, RT_FIELD_INT));
	read = api->read(repo, i2);
	TEST_ASSERT_EQUAL_INT64(30, api->get_int(read, RT_FIELD_INT));

	/* Mid-chain override shadows the root for everyone below it. */
	rt_set_int(api, repo, b, 25);
	read = api->read(repo, i1);
	TEST_ASSERT_EQUAL_INT64(25, api->get_int(read, RT_FIELD_INT));
	read = api->read(repo, i2);
	TEST_ASSERT_EQUAL_INT64(25, api->get_int(read, RT_FIELD_INT));

	/* Instance override beats everything. */
	rt_set_int(api, repo, i1, 99);
	read = api->read(repo, i1);
	TEST_ASSERT_EQUAL_INT64(99, api->get_int(read, RT_FIELD_INT));
	read = api->read(repo, i2);
	TEST_ASSERT_EQUAL_INT64(25, api->get_int(read, RT_FIELD_INT));

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  SubObjectList prototypes and propagation                          */
/* ------------------------------------------------------------------ */

/* Parity with the main-branch Resource::SubObjectListPrototypes test. */
SK_TEST(repository_subobject_list_prototypes) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 30u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub1 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub2 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub3 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);

	{
		sk_resource_object_t view = api->write(repo, sub1);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, prototype));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 10));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "blegh"));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub1));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}

	sk_rid_t item = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(item.id != 0u);

	{
		sk_resource_object_t read = api->read(repo, item);
		u32 count = 0u;
		const sk_rid_t* arr = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(2u, count);
		TEST_ASSERT_FALSE(SK_RID_EQ(arr[0], sub1));
		TEST_ASSERT_FALSE(SK_RID_EQ(arr[1], sub2));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, arr[0]), sub1));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, arr[1]), sub2));

		/* Mutate a mirror's own scalar: independent of the prototype. */
		sk_resource_object_t sub_write = api->write(repo, arr[0]);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(sub_write, RT_FIELD_STRING, "str"));
		api->commit(sub_write, NULL);

		sk_resource_object_t sub_read = api->read(repo, arr[0]);
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(sub_read, RT_FIELD_REFERENCE), prototype));
	}

	{
		sk_resource_object_t write = api->write(repo, item);
		u32 count = 0u;
		const sk_rid_t* arr = api->get_subobject_list(write, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(2u, count);
		/* Copy the mirror-of-sub2 RID first: add_to_subobject_list may realloc
		 * the list, invalidating the borrowed pointer. */
		sk_rid_t mirror_of_sub2 = SK_RID_ZERO;
		for (u32 i = 0u; i < count; ++i) {
			if (SK_RID_EQ(api->get_prototype(repo, arr[i]), sub2)) {
				mirror_of_sub2 = arr[i];
			}
		}
		TEST_ASSERT_TRUE(mirror_of_sub2.id != 0u);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(write, RT_FIELD_INT, 222));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(write, RT_FIELD_SUBOBJECT_LIST, sub3));
		TEST_ASSERT_EQUAL_INT(0, api->remove_from_subobject_list(write, RT_FIELD_SUBOBJECT_LIST, mirror_of_sub2));
		api->commit(write, NULL);
	}

	{
		sk_resource_object_t read = api->read(repo, item);
		TEST_ASSERT_EQUAL_INT64(222, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("blegh", api->get_string(read, RT_FIELD_STRING));
		u32 count = 0u;
		const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(2u, count);
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, items[0]), sub1));
		TEST_ASSERT_TRUE(SK_RID_EQ(items[1], sub3));
	}

	api->destroy(repo);
}

/* Parity with the main-branch Resource::SubObjectListPrototypePropagation
 * test: prototype SubObjectList edits mirror to every prototype instance. */
SK_TEST(repository_subobject_list_prototype_propagation) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 31u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub1 = rt_make_sub(api, repo, type, 1);
	sk_rid_t sub2 = rt_make_sub(api, repo, type, 2);

	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "prototype"));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub1));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}

	sk_rid_t instance1 = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	sk_rid_t instance2 = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	sk_rid_t instance3 = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance1.id != 0u);
	TEST_ASSERT_TRUE(instance2.id != 0u);
	TEST_ASSERT_TRUE(instance3.id != 0u);

	{
		sk_rid_t expected[2] = {sub1, sub2};
		rt_verify_mirror(api, repo, instance1, expected, 2u);
		rt_verify_mirror(api, repo, instance2, expected, 2u);
		rt_verify_mirror(api, repo, instance3, expected, 2u);
	}

	sk_rid_t sub3 = rt_make_sub(api, repo, type, 3);
	sk_rid_t sub4 = rt_make_sub(api, repo, type, 4);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub3));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub4));
		TEST_ASSERT_EQUAL_INT(0, api->remove_from_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub1));
		api->commit(view, NULL);
	}
	{
		sk_rid_t expected[3] = {sub2, sub3, sub4};
		rt_verify_mirror(api, repo, instance1, expected, 3u);
		rt_verify_mirror(api, repo, instance2, expected, 3u);
		rt_verify_mirror(api, repo, instance3, expected, 3u);
	}

	sk_rid_t sub5 = rt_make_sub(api, repo, type, 5);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->remove_from_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub5));
		api->commit(view, NULL);
	}
	{
		sk_rid_t expected[3] = {sub3, sub4, sub5};
		rt_verify_mirror(api, repo, instance1, expected, 3u);
		rt_verify_mirror(api, repo, instance2, expected, 3u);
		rt_verify_mirror(api, repo, instance3, expected, 3u);
	}

	/* The prototype's own list is what the mirrors reflect. */
	sk_resource_object_t read = api->read(repo, prototype);
	u32 count = 0u;
	const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(3u, count);
	int has3 = 0, has4 = 0, has5 = 0;
	for (u32 i = 0u; i < count; ++i) {
		if (SK_RID_EQ(items[i], sub3))
			has3 = 1;
		if (SK_RID_EQ(items[i], sub4))
			has4 = 1;
		if (SK_RID_EQ(items[i], sub5))
			has5 = 1;
	}
	TEST_ASSERT_TRUE(has3);
	TEST_ASSERT_TRUE(has4);
	TEST_ASSERT_TRUE(has5);

	api->destroy(repo);
}

/* An instance that explicitly removes a prototype sub-object keeps that
 * removal (prototypeRemoved) across later prototype re-adds. */
SK_TEST(repository_subobject_list_remove_override) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 32u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub1 = rt_make_sub(api, repo, type, 1);
	sk_rid_t sub2 = rt_make_sub(api, repo, type, 2);

	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub1));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}

	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance.id != 0u);

	/* Instance removes its mirror of sub2 (an override). */
	{
		sk_resource_object_t view = api->write(repo, instance);
		u32 count = 0u;
		const sk_rid_t* items = api->get_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(2u, count);
		sk_rid_t mirror2 = SK_RID_ZERO;
		for (u32 i = 0u; i < count; ++i) {
			if (SK_RID_EQ(api->get_prototype(repo, items[i]), sub2)) {
				mirror2 = items[i];
			}
		}
		TEST_ASSERT_TRUE(mirror2.id != 0u);
		TEST_ASSERT_EQUAL_INT(0, api->remove_from_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, mirror2));
		api->commit(view, NULL);
	}
	{
		sk_rid_t expected[1] = {sub1};
		rt_verify_mirror(api, repo, instance, expected, 1u);
	}

	/* Prototype removes then re-adds sub2: the instance's explicit removal
	 * persists (prototypeRemoved), so it is not resurrected. */
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->remove_from_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}
	{
		sk_rid_t expected[1] = {sub1};
		rt_verify_mirror(api, repo, instance, expected, 1u);
	}

	/* Explicitly re-adding a mirror of sub2 cancels the removal override. */
	sk_rid_t fresh_mirror = api->create_from_prototype(repo, sub2, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(fresh_mirror.id != 0u);
	{
		sk_resource_object_t instance_view = api->write(repo, instance);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(instance_view));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(instance_view, RT_FIELD_SUBOBJECT_LIST, fresh_mirror));
		api->commit(instance_view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}
	{
		sk_rid_t expected[2] = {sub1, sub2};
		rt_verify_mirror(api, repo, instance, expected, 2u);
	}

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Clone                                                             */
/* ------------------------------------------------------------------ */

SK_TEST(repository_clone_deep_subtree_remap) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 40u);

	sk_rid_t subobject = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, subobject);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "subobject"));
		api->commit(view, NULL);
	}
	sk_rid_t subobject_to_list = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, subobject_to_list);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "subobjectToSet"));
		api->commit(view, NULL);
	}
	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 10));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "blegh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, RT_FIELD_SUBOBJECT, subobject));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, subobject_to_list));
		api->commit(view, NULL);
	}

	sk_rid_t clone = api->clone(repo, rid, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone.id != 0u);
	TEST_ASSERT_FALSE(SK_RID_EQ(clone, rid));

	sk_resource_object_t read_clone = api->read(repo, clone);
	TEST_ASSERT_EQUAL_INT64(10, api->get_int(read_clone, RT_FIELD_INT));
	TEST_ASSERT_EQUAL_STRING("blegh", api->get_string(read_clone, RT_FIELD_STRING));

	/* The sub-object was re-created and its content deep-copied. */
	sk_rid_t subobject_clone = api->get_subobject(read_clone, RT_FIELD_SUBOBJECT);
	TEST_ASSERT_TRUE(subobject_clone.id != 0u);
	TEST_ASSERT_FALSE(SK_RID_EQ(subobject_clone, subobject));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, subobject_clone), clone));
	sk_resource_object_t sub_read = api->read(repo, subobject_clone);
	TEST_ASSERT_EQUAL_STRING("subobject", api->get_string(sub_read, RT_FIELD_STRING));

	/* The list sub-object was re-created too. */
	u32 count = 0u;
	const sk_rid_t* list = api->get_subobject_list(read_clone, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_FALSE(SK_RID_EQ(list[0], subobject_to_list));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, list[0]), clone));
	sk_resource_object_t list_read = api->read(repo, list[0]);
	TEST_ASSERT_EQUAL_STRING("subobjectToSet", api->get_string(list_read, RT_FIELD_STRING));

	api->destroy(repo);
}

/* A reference into the cloned subtree is remapped to its clone (parity with
 * the main-branch DuplicateReference test); a reference to the clone root
 * stays pointing at the original root. */
SK_TEST(repository_clone_reference_remap) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 41u);

	sk_rid_t parent = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t child = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t ref = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(parent.id != 0u);
	TEST_ASSERT_TRUE(child.id != 0u);
	TEST_ASSERT_TRUE(ref.id != 0u);

	{
		sk_resource_object_t view = api->write(repo, child);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, ref));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, parent);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, child));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, ref));
		api->commit(view, NULL);
	}

	sk_rid_t clone = api->clone(repo, parent, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone.id != 0u);
	TEST_ASSERT_FALSE(SK_RID_EQ(clone, parent));

	sk_resource_object_t read = api->read(repo, clone);
	u32 count = 0u;
	const sk_rid_t* list = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	/* The clone preserves sub-object list order: [child, ref]. */
	sk_rid_t cloned_child = list[0];
	sk_rid_t cloned_ref = list[1];
	TEST_ASSERT_FALSE(SK_RID_EQ(cloned_child, child));
	TEST_ASSERT_FALSE(SK_RID_EQ(cloned_ref, ref));

	/* The cloned child's reference to the (cloned) ref points at the clone. */
	sk_resource_object_t child_read = api->read(repo, cloned_child);
	sk_rid_t remapped = api->get_reference(child_read, RT_FIELD_REFERENCE);
	TEST_ASSERT_TRUE(SK_RID_EQ(cloned_ref, remapped));
	TEST_ASSERT_FALSE(SK_RID_EQ(remapped, ref));

	/* A reference to the clone root itself stays on the original root (the
	 * main-branch IsParentOf is strict-descendant). */
	{
		sk_resource_object_t view = api->write(repo, child);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, parent));
		api->commit(view, NULL);
	}
	sk_rid_t clone2 = api->clone(repo, parent, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone2.id != 0u);
	sk_resource_object_t read2 = api->read(repo, clone2);
	u32 count2 = 0u;
	const sk_rid_t* list2 = api->get_subobject_list(read2, RT_FIELD_SUBOBJECT_LIST, &count2);
	sk_rid_t child_clone2 = list2[0];
	sk_resource_object_t child_read2 = api->read(repo, child_clone2);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(child_read2, RT_FIELD_REFERENCE), parent));

	api->destroy(repo);
}

/* A reference array into the cloned subtree is remapped entry by entry. */
SK_TEST(repository_clone_reference_array_remap) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 42u);

	sk_rid_t parent = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t child = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t outer = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, child);
		sk_rid_t refs[2] = {child, outer}; /* self (descendant of parent) + outer */
		TEST_ASSERT_EQUAL_INT(0, api->set_reference_array(view, RT_FIELD_REFERENCE_ARRAY, refs, 2u));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t view = api->write(repo, parent);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, RT_FIELD_SUBOBJECT, child));
		api->commit(view, NULL);
	}

	sk_rid_t clone = api->clone(repo, parent, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone.id != 0u);

	sk_resource_object_t read = api->read(repo, clone);
	sk_rid_t cloned_child = api->get_subobject(read, RT_FIELD_SUBOBJECT);
	TEST_ASSERT_TRUE(cloned_child.id != 0u);
	TEST_ASSERT_FALSE(SK_RID_EQ(cloned_child, child));
	sk_resource_object_t child_read = api->read(repo, cloned_child);
	u32 count = 0u;
	const sk_rid_t* refs = api->get_reference_array(child_read, RT_FIELD_REFERENCE_ARRAY, &count);
	TEST_ASSERT_EQUAL_UINT32(2u, count);
	TEST_ASSERT_TRUE(SK_RID_EQ(refs[0], cloned_child)); /* descendant remapped to its clone */
	TEST_ASSERT_TRUE(SK_RID_EQ(refs[1], outer));		/* outside the subtree, unchanged */

	api->destroy(repo);
}

/* A clone of a prototype instance keeps its prototype pointer and stays
 * registered in the prototype's instance set, so later prototype edits
 * propagate to the clone on Commit. */
SK_TEST(repository_clone_of_prototype_instance_propagation) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 43u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub1 = rt_make_sub(api, repo, type, 1);
	sk_rid_t sub2 = rt_make_sub(api, repo, type, 2);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub1));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		api->commit(view, NULL);
	}

	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, instance), prototype));

	/* Cloning the instance copies the prototype pointer. */
	sk_rid_t clone = api->clone(repo, instance, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, clone), prototype));

	/* A later prototype edit propagates to both the instance and the clone. */
	sk_rid_t sub3 = rt_make_sub(api, repo, type, 3);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub3));
		TEST_ASSERT_EQUAL_INT(0, api->remove_from_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub1));
		api->commit(view, NULL);
	}
	{
		sk_rid_t expected[2] = {sub2, sub3};
		rt_verify_mirror(api, repo, instance, expected, 2u);
		rt_verify_mirror(api, repo, clone, expected, 2u);
	}

	api->destroy(repo);
}

/* Clone with an explicit uuid registers it; a duplicate uuid fails the clone
 * and leaves no orphaned slots. */
SK_TEST(repository_clone_uuid_uniqueness) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 44u);

	sk_rid_t origin = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t owner = api->create_resource(repo, type, (sk_uuid_t){0x55ull, 0xaaull}, NULL);
	TEST_ASSERT_TRUE(origin.id != 0u);
	TEST_ASSERT_TRUE(owner.id != 0u);

	u64 count_before = api->resource_count(repo);

	/* A uuid already owned by another resource fails the clone. */
	sk_rid_t failed = api->clone(repo, origin, (sk_uuid_t){0x55ull, 0xaaull}, NULL);
	TEST_ASSERT_TRUE(failed.id == 0u);
	TEST_ASSERT_EQUAL_UINT64(count_before, api->resource_count(repo));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, (sk_uuid_t){0x55ull, 0xaaull}), owner));

	/* A fresh uuid lands in the by-uuid map. */
	sk_rid_t clone = api->clone(repo, origin, (sk_uuid_t){0x11ull, 0x22ull}, NULL);
	TEST_ASSERT_TRUE(clone.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, (sk_uuid_t){0x11ull, 0x22ull}), clone));

	/* create_from_prototype follows the same rule. */
	sk_rid_t inst_failed = api->create_from_prototype(repo, origin, (sk_uuid_t){0x11ull, 0x22ull}, NULL);
	TEST_ASSERT_TRUE(inst_failed.id == 0u);
	TEST_ASSERT_EQUAL_UINT64(count_before + 1u, api->resource_count(repo));

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Destroy cascade and GC                                            */
/* ------------------------------------------------------------------ */

/* Parity with the main-branch Resource::Subobjects test. */
SK_TEST(repository_destroy_cascade) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 50u);

	sk_rid_t object = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub1 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub2 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub3 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, object);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, RT_FIELD_SUBOBJECT, sub1));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub2));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub3));
		api->commit(view, NULL);
	}

	/* Destroying a sub-object detaches it from the parent first. */
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, sub3, NULL));
	sk_resource_object_t read = api->write(repo, object);
	TEST_ASSERT_FALSE(api->has_on_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, sub3));
	api->discard(read);

	TEST_ASSERT_TRUE(api->has_value(repo, object));
	TEST_ASSERT_TRUE(api->has_value(repo, sub1));
	TEST_ASSERT_TRUE(api->has_value(repo, sub2));
	TEST_ASSERT_FALSE(api->has_value(repo, sub3));

	/* Destroying the parent cascades to its sub-objects. */
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, object, NULL));
	TEST_ASSERT_FALSE(api->has_value(repo, object));
	TEST_ASSERT_FALSE(api->has_value(repo, sub1));
	TEST_ASSERT_FALSE(api->has_value(repo, sub2));
	TEST_ASSERT_FALSE(api->has_resource(repo, sub1));
	TEST_ASSERT_FALSE(api->has_resource(repo, sub2));
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));

	api->end_frame(repo);
	api->destroy(repo);
}

/* Counting allocator: tracks live allocation count to prove GC reclaims
 * instances superseded by successive commits. */
typedef struct test_counting_alloc_t {
	const sk_allocator_t* base;
	u64 live;
} test_counting_alloc_t;

static void_ptr_t test_counting_alloc(void_ptr_t instance, size_t size) {
	test_counting_alloc_t* state = (test_counting_alloc_t*)instance;
	void_ptr_t p = state->base->alloc(state->base->instance, size);
	if (p != NULL) {
		state->live += 1u;
	}
	return p;
}

static void test_counting_free(void_ptr_t instance, void_ptr_t ptr) {
	test_counting_alloc_t* state = (test_counting_alloc_t*)instance;
	if (ptr != NULL) {
		state->live -= 1u;
	}
	state->base->free(state->base->instance, ptr);
}

static void_ptr_t test_counting_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	test_counting_alloc_t* state = (test_counting_alloc_t*)instance;
	void_ptr_t p = state->base->realloc(state->base->instance, ptr, size);
	if (p != NULL && ptr == NULL) {
		state->live += 1u;
	}
	return p;
}

SK_TEST(repository_garbage_collect_reclaims) {
	const sk_repository_api_t* api = sk_repository_api();
	test_counting_alloc_t state = {sk_allocator_default(), 0u};
	sk_allocator_t counting_allocator = {&state, test_counting_alloc, test_counting_free, test_counting_realloc};

	sk_repository_t* repo = api->create(&counting_allocator);
	TEST_ASSERT_NOT_NULL(repo);
	const sk_resource_type_t* type = NULL;
	{
		sk_resource_type_desc_t desc = test_payload_desc(test_type_id(51u), "gc.type", NULL);
		TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
		type = api->find_type_by_name(repo, "gc.type");
		TEST_ASSERT_NOT_NULL(type);
	}

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	u64 live_after_create = state.live;

	/* Two commits retire the first two instance blocks to the GC queue. */
	for (i64 v = 1; v <= 2; ++v) {
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, v));
		api->commit(view, NULL);
	}
	u64 live_after_commits = state.live;
	TEST_ASSERT_TRUE(live_after_commits > live_after_create); /* superseded blocks still live */

	api->garbage_collect(repo);
	TEST_ASSERT_TRUE(state.live < live_after_commits); /* retired blocks reclaimed */
	sk_resource_object_t read = api->read(repo, rid);
	TEST_ASSERT_EQUAL_INT64(2, api->get_int(read, RT_FIELD_INT)); /* current value intact */

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  OOM rollback for clone / prototype                                */
/* ------------------------------------------------------------------ */

SK_TEST(repository_oom_clone_cleanup) {
	const sk_repository_api_t* api = sk_repository_api();
	test_fail_alloc_t state = {sk_allocator_default(), 0u, 0xFFFFFFFFu};
	sk_allocator_t fail_allocator = {&state, test_fail_alloc, test_fail_free, test_fail_realloc};

	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo_full(&fail_allocator, &type, 52u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub = rt_make_sub(api, repo, type, 1);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 7));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub));
		api->commit(view, NULL);
	}
	u64 baseline = api->resource_count(repo);
	TEST_ASSERT_EQUAL_UINT64(2u, baseline); /* prototype + sub */

	/* Sweep a window of failure points inside clone: every failed attempt must
	 * leave the repository fully rolled back and usable. */
	for (u32 fail_offset = 0u; fail_offset < 14u; ++fail_offset) {
		state.fail_at = state.allocs_done + fail_offset;
		sk_rid_t clone = api->clone(repo, prototype, SK_UUID_ZERO, NULL);
		if (clone.id == 0u) {
			TEST_ASSERT_EQUAL_UINT64(baseline, api->resource_count(repo));
		} else {
			TEST_ASSERT_EQUAL_UINT64(baseline + 2u, api->resource_count(repo)); /* root + sub clone */
			state.fail_at = 0xFFFFFFFFu;										/* destroy must not hit the sweep's fail point */
			TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, clone, NULL));
			TEST_ASSERT_EQUAL_UINT64(baseline, api->resource_count(repo));
			api->end_frame(repo);
		}
	}

	/* The repository stays usable after the failures. */
	sk_resource_object_t read = api->read(repo, prototype);
	TEST_ASSERT_EQUAL_INT64(7, api->get_int(read, RT_FIELD_INT));
	u32 count = 0u;
	const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_TRUE(SK_RID_EQ(items[0], sub)); /* the prototype still owns the original sub */

	api->destroy(repo);
}

SK_TEST(repository_oom_create_from_prototype_cleanup) {
	const sk_repository_api_t* api = sk_repository_api();
	test_fail_alloc_t state = {sk_allocator_default(), 0u, 0xFFFFFFFFu};
	sk_allocator_t fail_allocator = {&state, test_fail_alloc, test_fail_free, test_fail_realloc};

	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo_full(&fail_allocator, &type, 53u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub = rt_make_sub(api, repo, type, 1);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 7));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub));
		api->commit(view, NULL);
	}
	u64 baseline = api->resource_count(repo);
	TEST_ASSERT_EQUAL_UINT64(2u, baseline);

	for (u32 fail_offset = 0u; fail_offset < 14u; ++fail_offset) {
		state.fail_at = state.allocs_done + fail_offset;
		sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
		if (instance.id == 0u) {
			TEST_ASSERT_EQUAL_UINT64(baseline, api->resource_count(repo));
		} else {
			TEST_ASSERT_EQUAL_UINT64(baseline + 2u, api->resource_count(repo)); /* instance + mirror */
			state.fail_at = 0xFFFFFFFFu;										/* destroy must not hit the sweep's fail point */
			TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, instance, NULL));
			TEST_ASSERT_EQUAL_UINT64(baseline, api->resource_count(repo));
			api->end_frame(repo);
		}
	}

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Cross-repository isolation                                        */
/* ------------------------------------------------------------------ */

SK_TEST(repository_two_repositories_isolation) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type_a = NULL;
	const sk_resource_type_t* type_b = NULL;
	sk_repository_t* repo_a = rt_repo(&type_a, 60u);
	sk_repository_t* repo_b = rt_repo(&type_b, 61u);

	sk_rid_t proto_a = api->create_resource(repo_a, type_a, SK_UUID_ZERO, NULL);
	sk_rid_t proto_b = api->create_resource(repo_b, type_b, SK_UUID_ZERO, NULL);
	rt_set_int(api, repo_a, proto_a, 100);
	rt_set_int(api, repo_b, proto_b, 200);

	sk_rid_t inst_a = api->create_from_prototype(repo_a, proto_a, SK_UUID_ZERO, NULL);
	sk_rid_t inst_b = api->create_from_prototype(repo_b, proto_b, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(inst_a.id != 0u);
	TEST_ASSERT_TRUE(inst_b.id != 0u);

	/* Each repository only knows its own resources (numeric rids may alias
	 * across repositories, so compare by uuid / count, not has_resource). */
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo_a, inst_a), proto_a));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo_b, inst_b), proto_b));
	u64 count_b = api->resource_count(repo_b);

	sk_resource_object_t read_a = api->read(repo_a, inst_a);
	TEST_ASSERT_EQUAL_INT64(100, api->get_int(read_a, RT_FIELD_INT));
	sk_resource_object_t read_b = api->read(repo_b, inst_b);
	TEST_ASSERT_EQUAL_INT64(200, api->get_int(read_b, RT_FIELD_INT));

	/* Clones stay inside their repository. */
	sk_rid_t clone_a = api->clone(repo_a, proto_a, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone_a.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(count_b, api->resource_count(repo_b)); /* repo_b untouched */
	sk_resource_object_t clone_read = api->read(repo_a, clone_a);
	TEST_ASSERT_EQUAL_INT64(100, api->get_int(clone_read, RT_FIELD_INT));

	/* A clone in repo_b is independent of repo_a's state. */
	sk_rid_t clone_b = api->clone(repo_b, proto_b, SK_UUID_ZERO, NULL);
	sk_resource_object_t clone_read_b = api->read(repo_b, clone_b);
	TEST_ASSERT_EQUAL_INT64(200, api->get_int(clone_read_b, RT_FIELD_INT));

	api->destroy(repo_a);
	api->destroy(repo_b);
}

/* ------------------------------------------------------------------ */
/*  Multi-level Write/Commit under concurrent readers                 */
/* ------------------------------------------------------------------ */

#include "thread.h"

typedef struct rt_reader_args_t {
	const sk_repository_api_t* api;
	sk_repository_t* repo;
	sk_rid_t* resources;
	u32 resource_count;
	i32 stop;
	i32 violations;
} rt_reader_args_t;

static i32 rt_reader_thread(void_ptr_t arg) {
	rt_reader_args_t* args = (rt_reader_args_t*)arg;
	u32 round = 0u;
	while (args->stop == 0 && round < 200000u) {
		for (u32 i = 0u; i < args->resource_count; ++i) {
			sk_resource_object_t view = args->api->read(args->repo, args->resources[i]);
			i64 value = args->api->get_int(view, RT_FIELD_INT);
			const_chr_t text = args->api->get_string(view, RT_FIELD_STRING);
			if (text != NULL) {
				char expected[32];
				snprintf(expected, sizeof(expected), "v%lld", value);
				if (strcmp(expected, text) != 0) {
					args->violations += 1;
				}
			}
		}
		round += 1u;
	}
	return 0;
}

typedef struct rt_writer_args_t {
	const sk_repository_api_t* api;
	sk_repository_t* repo;
	sk_rid_t* resources;
	u32 resource_count;
	u32 rounds;
	i32 done;
} rt_writer_args_t;

static i32 rt_writer_thread(void_ptr_t arg) {
	rt_writer_args_t* args = (rt_writer_args_t*)arg;
	for (u32 r = 0u; r < args->rounds; ++r) {
		for (u32 i = 0u; i < args->resource_count; ++i) {
			sk_resource_object_t view = args->api->write(args->repo, args->resources[i]);
			if (SK_RESOURCE_OBJECT_IS_VALID(view)) {
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "v%u", r);
				(void)args->api->set_int(view, RT_FIELD_INT, (i64)r);
				(void)args->api->set_string(view, RT_FIELD_STRING, buffer);
				args->api->commit(view, NULL);
			}
		}
	}
	args->done = 1;
	return 0;
}

SK_TEST(repository_concurrent_readers_multi_commit) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 70u);

	const u32 resource_count = 6u;
	sk_rid_t resources[6];
	for (u32 i = 0u; i < resource_count; ++i) {
		resources[i] = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(resources[i].id != 0u);
		/* Publish a valid (int, string) pair first so readers never see unset. */
		sk_resource_object_t view = api->write(repo, resources[i]);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 0));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "v0"));
		api->commit(view, NULL);
	}

	rt_reader_args_t ra;
	memset(&ra, 0, sizeof(ra));
	ra.api = api;
	ra.repo = repo;
	ra.resources = resources;
	ra.resource_count = resource_count;
	rt_writer_args_t wa;
	memset(&wa, 0, sizeof(wa));
	wa.api = api;
	wa.repo = repo;
	wa.resources = resources;
	wa.resource_count = resource_count;
	wa.rounds = 400u;

	sk_thread_t* writer = sk_thread_create(rt_writer_thread, &wa);
	sk_thread_t* reader1 = sk_thread_create(rt_reader_thread, &ra);
	sk_thread_t* reader2 = sk_thread_create(rt_reader_thread, &ra);
	TEST_ASSERT_NOT_NULL(writer);
	TEST_ASSERT_NOT_NULL(reader1);
	TEST_ASSERT_NOT_NULL(reader2);

	TEST_ASSERT_EQUAL_INT(0, sk_thread_join(writer));
	sk_thread_destroy(writer);
	ra.stop = 1;
	TEST_ASSERT_EQUAL_INT(0, sk_thread_join(reader1));
	TEST_ASSERT_EQUAL_INT(0, sk_thread_join(reader2));
	sk_thread_destroy(reader1);
	sk_thread_destroy(reader2);

	/* No reader ever observed a torn (int, string) pair. */
	TEST_ASSERT_EQUAL_INT(0, ra.violations);

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Undo / redo scopes                                                */
/* ------------------------------------------------------------------ */

/* Parity with the main-branch Resource::UndoRedo test: a scoped Commit pushes
 * a change whose Undo restores the previous published value. */
SK_TEST(repository_undo_redo_scope_commit) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 80u);

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t subobject = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t subobject2 = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 10));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "blegh"));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, subobject));
		api->commit(view, NULL);
	}

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "test scope");
	TEST_ASSERT_NOT_NULL(scope);
	TEST_ASSERT_EQUAL_STRING("test scope", api->undo_redo_scope_get_name(scope));
	TEST_ASSERT_NULL(api->undo_redo_scope_get_name(api->undo_redo_scope_create(sk_allocator_default(), NULL)));

	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 33));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "44"));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, subobject2));
		api->commit(view, scope);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT64(33, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("44", api->get_string(read, RT_FIELD_STRING));
		u32 count = 0u;
		(void)api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(2u, count);
	}

	api->undo_redo_scope_undo(scope);
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT64(10, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("blegh", api->get_string(read, RT_FIELD_STRING));
		u32 count = 0u;
		const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(1u, count);
		TEST_ASSERT_TRUE(SK_RID_EQ(items[0], subobject));
		/* The scoped commit bumped the version; Undo bumps again. */
		TEST_ASSERT_TRUE(api->get_version(repo, rid) >= 4u);
	}

	api->undo_redo_scope_redo(scope);
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT64(33, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("44", api->get_string(read, RT_FIELD_STRING));
		u32 count = 0u;
		(void)api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
		TEST_ASSERT_EQUAL_UINT32(2u, count);
	}

	api->undo_redo_scope_destroy(scope);
	api->undo_redo_scope_destroy(NULL); /* safe on NULL */
	api->destroy(repo);
}

/* A scoped create's Undo releases the slot (uuid unregistered) and Redo
 * re-creates it with its value and uuid. */
SK_TEST(repository_undo_redo_scope_create) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 81u);

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "create");
	TEST_ASSERT_NOT_NULL(scope);

	sk_uuid_t uuid = {0x10ull, 0x20ull};
	sk_rid_t rid = api->create_resource(repo, type, uuid, scope);
	TEST_ASSERT_TRUE(rid.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, uuid), rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->has_value(repo, rid));

	api->undo_redo_scope_undo(scope);
	TEST_ASSERT_FALSE(api->has_resource(repo, rid));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, uuid).id == 0u);
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));

	api->undo_redo_scope_redo(scope);
	TEST_ASSERT_TRUE(api->has_resource(repo, rid));
	TEST_ASSERT_TRUE(api->has_value(repo, rid));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, uuid), rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* A scoped destroy records the uuid / path / value; Undo re-creates the slot
 * with all of them and Redo releases it again. */
SK_TEST(repository_undo_redo_scope_destroy) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 82u);

	sk_rid_t rid = api->create_resource(repo, type, (sk_uuid_t){1u, 2u}, NULL);
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid, "assets/undo.foo"));
	rt_set_int(api, repo, rid, 7);

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "destroy");
	TEST_ASSERT_NOT_NULL(scope);

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, rid, scope));
	TEST_ASSERT_FALSE(api->has_resource(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, (sk_uuid_t){1u, 2u}).id == 0u);
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/undo.foo").id == 0u);

	api->undo_redo_scope_undo(scope);
	TEST_ASSERT_TRUE(api->has_resource(repo, rid));
	TEST_ASSERT_TRUE(api->has_value(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, (sk_uuid_t){1u, 2u}), rid));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/undo.foo"), rid));
	TEST_ASSERT_EQUAL_INT64(7, api->get_int(api->read(repo, rid), RT_FIELD_INT));

	api->undo_redo_scope_redo(scope);
	TEST_ASSERT_FALSE(api->has_resource(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, (sk_uuid_t){1u, 2u}).id == 0u);

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* A scoped clone records every new slot; Undo drops the clone and its cloned
 * sub-objects (leaving the origin subtree intact) and Redo restores them with
 * their sub-object parent links. */
SK_TEST(repository_undo_redo_scope_clone) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 83u);

	sk_rid_t origin = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, origin);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 5));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "clone-me"));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub));
		api->commit(view, NULL);
	}

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "clone");
	sk_rid_t clone = api->clone(repo, origin, SK_UUID_ZERO, scope);
	TEST_ASSERT_TRUE(clone.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(4u, api->resource_count(repo)); /* origin + sub + clone + clone-sub */

	api->undo_redo_scope_undo(scope);
	TEST_ASSERT_EQUAL_UINT64(2u, api->resource_count(repo));
	TEST_ASSERT_FALSE(api->has_resource(repo, clone));

	api->undo_redo_scope_redo(scope);
	TEST_ASSERT_EQUAL_UINT64(4u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->has_resource(repo, clone));
	sk_resource_object_t read = api->read(repo, clone);
	TEST_ASSERT_EQUAL_INT64(5, api->get_int(read, RT_FIELD_INT));
	TEST_ASSERT_EQUAL_STRING("clone-me", api->get_string(read, RT_FIELD_STRING));
	u32 count = 0u;
	const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_TRUE(api->has_value(repo, items[0]));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, items[0]), clone));

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* Scoped create_from_prototype records the instance and its sub-object mirrors;
 * Undo drops them and Redo restores the mirrors with their prototype link. */
SK_TEST(repository_undo_redo_scope_create_from_prototype) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 84u);

	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub = rt_make_sub(api, repo, type, 1);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, sub));
		api->commit(view, NULL);
	}

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "proto");
	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, scope);
	TEST_ASSERT_TRUE(instance.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(4u, api->resource_count(repo)); /* prototype + sub + instance + mirror */

	api->undo_redo_scope_undo(scope);
	TEST_ASSERT_EQUAL_UINT64(2u, api->resource_count(repo));
	TEST_ASSERT_FALSE(api->has_resource(repo, instance));

	api->undo_redo_scope_redo(scope);
	TEST_ASSERT_EQUAL_UINT64(4u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->has_resource(repo, instance));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, instance), prototype));
	sk_resource_object_t read = api->read(repo, instance);
	u32 count = 0u;
	const sk_rid_t* items = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &count);
	TEST_ASSERT_EQUAL_UINT32(1u, count);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_prototype(repo, items[0]), sub));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, items[0]), instance));

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* Scope-owned snapshots survive garbage_collect and end_frame, and the scope
 * stays redoable/undoable after collection. */
SK_TEST(repository_undo_redo_scope_gc_safety) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 85u);

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	rt_set_int(api, repo, rid, 1);

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "gc");
	for (i64 v = 2; v <= 4; ++v) {
		rt_set_int(api, repo, rid, v); /* un-scoped commits: superseded instances to GC */
	}
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 5));
		api->commit(view, scope);
	}
	api->garbage_collect(repo);
	api->end_frame(repo);

	api->undo_redo_scope_undo(scope);
	TEST_ASSERT_EQUAL_INT64(4, api->get_int(api->read(repo, rid), RT_FIELD_INT));
	api->undo_redo_scope_redo(scope);
	TEST_ASSERT_EQUAL_INT64(5, api->get_int(api->read(repo, rid), RT_FIELD_INT));

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* Undo / redo of a destroyed parent cascade restores the whole sub-object
 * tree and re-links parents. */
SK_TEST(repository_undo_redo_scope_destroy_cascade) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 86u);

	sk_rid_t parent = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t child1 = rt_make_sub(api, repo, type, 11);
	sk_rid_t child2 = rt_make_sub(api, repo, type, 22);
	{
		sk_resource_object_t view = api->write(repo, parent);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 99));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, child1));
		TEST_ASSERT_EQUAL_INT(0, api->add_to_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, child2));
		api->commit(view, NULL);
	}

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "cascade");
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, parent, scope));
	TEST_ASSERT_FALSE(api->has_resource(repo, parent));
	TEST_ASSERT_FALSE(api->has_resource(repo, child1));
	TEST_ASSERT_FALSE(api->has_resource(repo, child2));
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));

	api->undo_redo_scope_undo(scope);
	TEST_ASSERT_EQUAL_UINT64(3u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->has_value(repo, parent));
	TEST_ASSERT_TRUE(api->has_value(repo, child1));
	TEST_ASSERT_TRUE(api->has_value(repo, child2));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, child1), parent));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, child2), parent));
	TEST_ASSERT_EQUAL_INT64(99, api->get_int(api->read(repo, parent), RT_FIELD_INT));
	TEST_ASSERT_EQUAL_INT64(11, api->get_int(api->read(repo, child1), RT_FIELD_INT));
	TEST_ASSERT_EQUAL_INT64(22, api->get_int(api->read(repo, child2), RT_FIELD_INT));

	api->undo_redo_scope_redo(scope);
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));
	TEST_ASSERT_FALSE(api->has_resource(repo, parent));
	TEST_ASSERT_FALSE(api->has_resource(repo, child1));
	TEST_ASSERT_FALSE(api->has_resource(repo, child2));

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* Parity field-matrix coverage for a scoped Commit: every field category the
 * rt type exposes (scalar, String, Reference, ReferenceArray, SubObject, and
 * SubObjectList) is snapshot deep-copied by the scope, so Undo restores the
 * exact pre-commit state and Redo re-applies the committed state. Create and
 * destroy under the same scope mix structural changes into the change record
 * so one Undo/Redo round restores the whole scoped edit. */
SK_TEST(repository_undo_redo_scope_field_matrix) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 87u);

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t sub1 = rt_make_sub(api, repo, type, 1);
	sk_rid_t sub2 = rt_make_sub(api, repo, type, 2);
	sk_rid_t ref = rt_make_sub(api, repo, type, 3);
	sk_rid_t solo = rt_make_sub(api, repo, type, 4);
	TEST_ASSERT_TRUE(rid.id != 0u);

	/* Baseline state (un-scoped): every field category carries a value. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 10));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "base"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, ref));
		sk_rid_t base_refs[1] = {ref};
		TEST_ASSERT_EQUAL_INT(0, api->set_reference_array(view, RT_FIELD_REFERENCE_ARRAY, base_refs, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, RT_FIELD_SUBOBJECT, solo));
		sk_rid_t base_subs[1] = {sub1};
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, base_subs, 1u));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, solo), rid));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, sub1), rid));

	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "field matrix");
	TEST_ASSERT_NOT_NULL(scope);

	/* Scoped commit mutating every field category. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 33));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "changed"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, sub2));
		sk_rid_t refs[2] = {sub1, sub2};
		TEST_ASSERT_EQUAL_INT(0, api->set_reference_array(view, RT_FIELD_REFERENCE_ARRAY, refs, 2u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, RT_FIELD_SUBOBJECT, sub2));
		sk_rid_t subs[2] = {sub1, sub2};
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(view, RT_FIELD_SUBOBJECT_LIST, subs, 2u));
		api->commit(view, scope);
	}

	/* Structural create under the same scope (value edit committed with scope). */
	sk_uuid_t created_uuid = {0x73ull, 0x99ull};
	sk_rid_t created = api->create_resource(repo, type, created_uuid, scope);
	TEST_ASSERT_TRUE(created.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, created);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 77));
		api->commit(view, scope);
	}

	/* Structural destroy under the same scope. */
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, solo, scope));

	/* Committed state: all fields changed, created live, solo gone. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT64(33, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("changed", api->get_string(read, RT_FIELD_STRING));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(read, RT_FIELD_REFERENCE), sub2));
		u32 ref_count = 0u;
		const sk_rid_t* refs = api->get_reference_array(read, RT_FIELD_REFERENCE_ARRAY, &ref_count);
		TEST_ASSERT_EQUAL_UINT32(2u, ref_count);
		TEST_ASSERT_TRUE(SK_RID_EQ(refs[0], sub1));
		TEST_ASSERT_TRUE(SK_RID_EQ(refs[1], sub2));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(read, RT_FIELD_SUBOBJECT), sub2));
		u32 sub_count = 0u;
		const sk_rid_t* subs = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &sub_count);
		TEST_ASSERT_EQUAL_UINT32(2u, sub_count);
		TEST_ASSERT_TRUE(SK_RID_EQ(subs[0], sub1));
		TEST_ASSERT_TRUE(SK_RID_EQ(subs[1], sub2));
	}
	TEST_ASSERT_TRUE(api->has_resource(repo, created));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, created_uuid), created));
	TEST_ASSERT_FALSE(api->has_resource(repo, solo));

	/* Undo restores the pre-scope snapshot and rolls back the structural ops. */
	api->undo_redo_scope_undo(scope);
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT64(10, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("base", api->get_string(read, RT_FIELD_STRING));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(read, RT_FIELD_REFERENCE), ref));
		u32 ref_count = 0u;
		const sk_rid_t* refs = api->get_reference_array(read, RT_FIELD_REFERENCE_ARRAY, &ref_count);
		TEST_ASSERT_EQUAL_UINT32(1u, ref_count);
		TEST_ASSERT_TRUE(SK_RID_EQ(refs[0], ref));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(read, RT_FIELD_SUBOBJECT), solo));
		u32 sub_count = 0u;
		const sk_rid_t* subs = api->get_subobject_list(read, RT_FIELD_SUBOBJECT_LIST, &sub_count);
		TEST_ASSERT_EQUAL_UINT32(1u, sub_count);
		TEST_ASSERT_TRUE(SK_RID_EQ(subs[0], sub1));
	}
	TEST_ASSERT_FALSE(api->has_resource(repo, created));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, created_uuid).id == 0u);
	TEST_ASSERT_TRUE(api->has_resource(repo, solo));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, solo), rid));

	/* Redo re-applies the committed snapshot and the structural ops. */
	api->undo_redo_scope_redo(scope);
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT64(33, api->get_int(read, RT_FIELD_INT));
		TEST_ASSERT_EQUAL_STRING("changed", api->get_string(read, RT_FIELD_STRING));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(read, RT_FIELD_REFERENCE), sub2));
		u32 ref_count = 0u;
		const sk_rid_t* refs = api->get_reference_array(read, RT_FIELD_REFERENCE_ARRAY, &ref_count);
		TEST_ASSERT_EQUAL_UINT32(2u, ref_count);
		TEST_ASSERT_TRUE(SK_RID_EQ(refs[0], sub1));
		TEST_ASSERT_TRUE(SK_RID_EQ(refs[1], sub2));
		TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(read, RT_FIELD_SUBOBJECT), sub2));
	}
	TEST_ASSERT_TRUE(api->has_resource(repo, created));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, created_uuid), created));
	TEST_ASSERT_EQUAL_INT64(77, api->get_int(api->read(repo, created), RT_FIELD_INT));
	TEST_ASSERT_FALSE(api->has_resource(repo, solo));

	api->undo_redo_scope_destroy(scope);
	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Lifecycle + handle semantics (APX-189)                              */
/*  Independent of file I/O: create/lookup/path/uuid, stale RID after  */
/*  destroy, in-place replace via write/commit, soft refs, ownership,  */
/*  clear via destroy(repository), no public enumerate API.            */
/* ------------------------------------------------------------------ */

SK_TEST(repository_lifecycle_add_and_retrieve_by_handle_uuid_path) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 800u);

	sk_uuid_t uuid = {0xabcdu, 0x1234u};
	sk_rid_t rid = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	TEST_ASSERT_TRUE(api->has_resource(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));
	TEST_ASSERT_EQUAL_PTR(type, api->resource_type(repo, rid));
	TEST_ASSERT_TRUE(SK_UUID_EQ(api->resource_uuid(repo, rid), uuid));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo, uuid), rid));
	TEST_ASSERT_NOT_NULL(api->resource_instance(repo, rid));

	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid, "assets/hero.entity"));
	TEST_ASSERT_EQUAL_STRING("assets/hero.entity", api->get_path(repo, rid));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/hero.entity"), rid));

	/* Handle (RID) read observes published data after write/commit. */
	rt_set_int(api, repo, rid, 42);
	TEST_ASSERT_EQUAL_INT64(42, api->get_int(api->read(repo, rid), RT_FIELD_INT));

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_duplicate_uuid_and_path) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 801u);

	sk_uuid_t uuid = {0xd11eull, 0x1ull};
	sk_rid_t a = api->create_resource(repo, type, uuid, NULL);
	sk_rid_t b = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_TRUE(SK_RID_EQ(a, b));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	sk_rid_t c = api->create_resource(repo, type, (sk_uuid_t){0xd11eull, 0x2ull}, NULL);
	TEST_ASSERT_FALSE(SK_RID_EQ(a, c));
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, a, "assets/shared.path"));
	TEST_ASSERT_NOT_EQUAL(0, api->set_path(repo, c, "assets/shared.path"));
	TEST_ASSERT_NULL(api->get_path(repo, c));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "assets/shared.path"), a));

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_nonexistent_lookups) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 802u);

	sk_rid_t ghost = (sk_rid_t){99999ull};
	TEST_ASSERT_FALSE(api->has_resource(repo, ghost));
	TEST_ASSERT_FALSE(api->has_resource(repo, SK_RID_ZERO));
	TEST_ASSERT_NULL(api->resource_type(repo, ghost));
	TEST_ASSERT_NULL(api->resource_instance(repo, ghost));
	TEST_ASSERT_NULL(api->get_path(repo, ghost));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_version(repo, ghost));
	TEST_ASSERT_FALSE(api->has_value(repo, ghost));
	TEST_ASSERT_EQUAL_INT(-1, api->destroy_resource(repo, ghost, NULL));
	TEST_ASSERT_FALSE(SK_RESOURCE_OBJECT_IS_VALID(api->read(repo, ghost)));
	TEST_ASSERT_FALSE(SK_RESOURCE_OBJECT_IS_VALID(api->write(repo, ghost)));

	TEST_ASSERT_TRUE(api->find_by_uuid(repo, (sk_uuid_t){1ull, 2ull}).id == 0u);
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, SK_UUID_ZERO).id == 0u);
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/missing.foo").id == 0u);
	TEST_ASSERT_TRUE(api->find_by_path(repo, "").id == 0u);

	(void)type;
	api->destroy(repo);
}

SK_TEST(repository_lifecycle_stale_handle_after_destroy) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 803u);

	sk_uuid_t uuid = {0x57a1eull, 0x1ull};
	sk_rid_t rid = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid, "assets/stale.asset"));
	rt_set_int(api, repo, rid, 7);
	const u64 rid_id = rid.id;

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, rid, NULL));
	TEST_ASSERT_FALSE(api->has_resource(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, uuid).id == 0u);
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/stale.asset").id == 0u);
	TEST_ASSERT_NULL(api->resource_instance(repo, rid));
	TEST_ASSERT_FALSE(SK_RESOURCE_OBJECT_IS_VALID(api->read(repo, rid)));
	TEST_ASSERT_FALSE(SK_RESOURCE_OBJECT_IS_VALID(api->write(repo, rid)));
	TEST_ASSERT_EQUAL_INT(-1, api->destroy_resource(repo, rid, NULL));

	/* RIDs are never recycled: a new create gets a different id; the stale
	 * handle does not alias the new resource (documented handle semantics). */
	sk_rid_t next = api->create_resource(repo, type, (sk_uuid_t){0x57a1eull, 0x2ull}, NULL);
	TEST_ASSERT_TRUE(next.id != 0u);
	TEST_ASSERT_FALSE(SK_RID_EQ(next, rid));
	TEST_ASSERT_TRUE(next.id > rid_id);
	TEST_ASSERT_FALSE(api->has_resource(repo, rid));
	TEST_ASSERT_TRUE(api->has_resource(repo, next));

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_replace_in_place_rid_stable) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 804u);

	sk_rid_t rid = api->create_resource(repo, type, (sk_uuid_t){0x4e91ull, 0x1ull}, NULL);
	rt_set_int(api, repo, rid, 1);
	TEST_ASSERT_EQUAL_UINT64(2u, api->get_version(repo, rid)); /* create=1, first commit bumps */

	/* Existing holders of @rid observe new data after commit (same handle). */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_int(view, RT_FIELD_INT, 99));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(view, RT_FIELD_STRING, "replaced"));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(api->has_resource(repo, rid));
	TEST_ASSERT_EQUAL_UINT64(3u, api->get_version(repo, rid));
	sk_resource_object_t read = api->read(repo, rid);
	TEST_ASSERT_EQUAL_INT64(99, api->get_int(read, RT_FIELD_INT));
	TEST_ASSERT_EQUAL_STRING("replaced", api->get_string(read, RT_FIELD_STRING));

	/* UUID-idempotent create returns the same RID (reload shell). */
	sk_rid_t same = api->create_resource(repo, type, (sk_uuid_t){0x4e91ull, 0x1ull}, NULL);
	TEST_ASSERT_TRUE(SK_RID_EQ(same, rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_reference_missing_and_self) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 805u);

	sk_rid_t a = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t b = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(a.id != 0u);
	TEST_ASSERT_TRUE(b.id != 0u);

	/* Soft reference to a live peer. */
	{
		sk_resource_object_t view = api->write(repo, a);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, b));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(repo, a), RT_FIELD_REFERENCE), b));

	/* Self-reference is allowed (soft link; no ownership / no cycle destroy). */
	{
		sk_resource_object_t view = api->write(repo, a);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, a));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(repo, a), RT_FIELD_REFERENCE), a));

	/* Missing / zero target stores SK_RID_ZERO (soft refs do not keep targets alive). */
	{
		sk_resource_object_t view = api->write(repo, a);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, SK_RID_ZERO));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(api->get_reference(api->read(repo, a), RT_FIELD_REFERENCE).id == 0u);

	/* Destroying a soft-referenced target leaves a dangling RID in the field
	 * (no refcount); has_resource on the dangling id is false. */
	{
		sk_resource_object_t view = api->write(repo, a);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, b));
		api->commit(view, NULL);
	}
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, b, NULL));
	sk_rid_t dangling = api->get_reference(api->read(repo, a), RT_FIELD_REFERENCE);
	TEST_ASSERT_TRUE(SK_RID_EQ(dangling, b));
	TEST_ASSERT_FALSE(api->has_resource(repo, dangling));
	/* Owner @a is still live — soft REFERENCE does not cascade. */
	TEST_ASSERT_TRUE(api->has_resource(repo, a));

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_ownership_subobject_vs_reference) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 806u);

	sk_rid_t parent = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t owned = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t soft = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	{
		sk_resource_object_t view = api->write(repo, parent);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(view, RT_FIELD_SUBOBJECT, owned));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(view, RT_FIELD_REFERENCE, soft));
		api->commit(view, NULL);
	}
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, owned), parent));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_parent(repo, soft), SK_RID_ZERO));

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, parent, NULL));
	TEST_ASSERT_FALSE(api->has_resource(repo, parent));
	TEST_ASSERT_FALSE(api->has_resource(repo, owned)); /* cascade ownership */
	TEST_ASSERT_TRUE(api->has_resource(repo, soft));   /* soft ref not owned */

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_clear_via_destroy_repository) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 807u);

	sk_rid_t r1 = api->create_resource(repo, type, (sk_uuid_t){1ull, 0ull}, NULL);
	sk_rid_t r2 = api->create_resource(repo, type, (sk_uuid_t){2ull, 0ull}, NULL);
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, r1, "a"));
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, r2, "b"));
	TEST_ASSERT_EQUAL_UINT64(2u, api->resource_count(repo));

	/* No clear() API: destroy(repository) drops every resource, path, and type.
	 * Handles obtained before destroy must not be used afterward. */
	api->destroy(repo);

	/* Fresh repository is empty (isolation already covered elsewhere). */
	repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_path(repo, "a").id == 0u);
	TEST_ASSERT_NULL(api->find_type_by_name(repo, "rt.type"));
	api->destroy(repo);
	(void)type;
}

SK_TEST(repository_lifecycle_no_enumeration_order_api) {
	/* Contract: resource_count reports live count; there is no public
	 * for_each / iterate_resources. RID assignment is sequential and never
	 * recycled, but callers must not treat dense RID ranges as an enumerator
	 * (gaps appear after destroy). Documented: no iteration-order guarantee. */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 808u);

	sk_rid_t a = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t b = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	sk_rid_t c = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(a.id + 1u == b.id);
	TEST_ASSERT_TRUE(b.id + 1u == c.id);
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, b, NULL));
	TEST_ASSERT_EQUAL_UINT64(2u, api->resource_count(repo));
	/* Gap: b is dead; scanning [a.id, c.id] would see a hole — not an API. */
	TEST_ASSERT_TRUE(api->has_resource(repo, a));
	TEST_ASSERT_FALSE(api->has_resource(repo, b));
	TEST_ASSERT_TRUE(api->has_resource(repo, c));

	api->destroy(repo);
}

SK_TEST(repository_lifecycle_failed_create_leaves_repo_usable) {
	/* OOM / failed structural ops must not leave half-registered state
	 * (see repository_oom_safety). Also: failed set_path does not change maps. */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = rt_repo(&type, 809u);

	sk_rid_t rid = api->create_resource(repo, type, (sk_uuid_t){1ull, 0ull}, NULL);
	TEST_ASSERT_EQUAL_INT(0, api->set_path(repo, rid, "keep.me"));
	TEST_ASSERT_NOT_EQUAL(0, api->set_path(repo, rid, NULL)); /* contract: NULL path fails */
	TEST_ASSERT_EQUAL_STRING("keep.me", api->get_path(repo, rid));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_path(repo, "keep.me"), rid));

	/* Destroy of non-live is a no-op failure; live resources untouched. */
	TEST_ASSERT_EQUAL_INT(-1, api->destroy_resource(repo, (sk_rid_t){0ull}, NULL));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Extended field-type accessor tests (APX-181)                      */
/* ------------------------------------------------------------------ */

/* Test payload covering every accessor added for the non-buffer field
 * types: VEC2 / VEC3 / VEC4 / QUAT / MAT4 / COLOR / ENUM / BLOB / TYPE_ID. */
typedef struct ext_object_t {
	sk_vec2_t vec2;
	sk_vec3_t vec3;
	sk_vec4_t vec4;
	sk_quat_t quat;
	sk_mat44_t mat4;
	sk_color_t color;
	u64 enum_value;
	sk_field_blob_t blob;
	sk_type_id_t type_id;
} ext_object_t;

#define EXT_FIELD_VEC2 0u
#define EXT_FIELD_VEC3 1u
#define EXT_FIELD_VEC4 2u
#define EXT_FIELD_QUAT 3u
#define EXT_FIELD_MAT4 4u
#define EXT_FIELD_COLOR 5u
#define EXT_FIELD_ENUM 6u
#define EXT_FIELD_BLOB 7u
#define EXT_FIELD_TYPE_ID 8u

static const sk_resource_field_t ext_fields[9] = {
	{"vec2", EXT_FIELD_VEC2, SK_RESOURCE_FIELD_TYPE_VEC2, (u32)offsetof(ext_object_t, vec2), (u32)sizeof(sk_vec2_t), {0ull, 0ull}},
	{"vec3", EXT_FIELD_VEC3, SK_RESOURCE_FIELD_TYPE_VEC3, (u32)offsetof(ext_object_t, vec3), (u32)sizeof(sk_vec3_t), {0ull, 0ull}},
	{"vec4", EXT_FIELD_VEC4, SK_RESOURCE_FIELD_TYPE_VEC4, (u32)offsetof(ext_object_t, vec4), (u32)sizeof(sk_vec4_t), {0ull, 0ull}},
	{"quat", EXT_FIELD_QUAT, SK_RESOURCE_FIELD_TYPE_QUAT, (u32)offsetof(ext_object_t, quat), (u32)sizeof(sk_quat_t), {0ull, 0ull}},
	{"mat4", EXT_FIELD_MAT4, SK_RESOURCE_FIELD_TYPE_MAT4, (u32)offsetof(ext_object_t, mat4), (u32)sizeof(sk_mat44_t), {0ull, 0ull}},
	{"color", EXT_FIELD_COLOR, SK_RESOURCE_FIELD_TYPE_COLOR, (u32)offsetof(ext_object_t, color), (u32)sizeof(sk_color_t), {0ull, 0ull}},
	{"enum", EXT_FIELD_ENUM, SK_RESOURCE_FIELD_TYPE_ENUM, (u32)offsetof(ext_object_t, enum_value), (u32)sizeof(u64), {0ull, 0ull}},
	{"blob", EXT_FIELD_BLOB, SK_RESOURCE_FIELD_TYPE_BLOB, (u32)offsetof(ext_object_t, blob), (u32)sizeof(sk_field_blob_t), {0ull, 0ull}},
	{"typeId", EXT_FIELD_TYPE_ID, SK_RESOURCE_FIELD_TYPE_TYPE_ID, (u32)offsetof(ext_object_t, type_id), (u32)sizeof(sk_type_id_t), {0ull, 0ull}},
};

static sk_repository_t* ext_repo(const sk_resource_type_t** out_type, u64 tag) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	sk_resource_type_desc_t desc;
	desc.type_id = test_type_id(tag);
	desc.name = "ext.type";
	desc.instance_size = (u32)sizeof(ext_object_t);
	desc.fields = ext_fields;
	desc.field_count = 9u;
	desc.defaults = NULL;
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	*out_type = api->find_type_by_name(repo, "ext.type");
	TEST_ASSERT_NOT_NULL(*out_type);
	return repo;
}

SK_TEST(repository_extended_field_accessors) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = ext_repo(&type, 88u);

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);

	const sk_vec2_t v2 = {1.5f, -2.25f};
	const sk_vec3_t v3 = {3.0f, 4.0f, 5.0f};
	const sk_vec4_t v4 = {0.25f, 0.5f, 0.75f, 1.0f};
	const sk_quat_t quat = {0.0f, 0.0f, 0.0f, 1.0f};
	sk_mat44_t mat4;
	memset(&mat4, 0, sizeof(mat4));
	mat4.m[0] = 1.0f;
	mat4.m[5] = 1.0f;
	mat4.m[10] = 1.0f;
	mat4.m[15] = 1.0f;
	const sk_color_t color = {1.0f, 0.5f, 0.25f, 0.125f};
	const u8 blob_bytes[6] = {1u, 2u, 3u, 4u, 5u, 6u};
	const sk_type_id_t type_id = {0x1111222233334444ull, 0x5555666677778888ull};
	const u64 enum_value = 42u;

	/* Default / unset behavior on a fresh instance. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(read));
		sk_vec2_t got = api->get_vec2(read, EXT_FIELD_VEC2);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, got.x);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, got.y);
		sk_vec3_t got3 = api->get_vec3(read, EXT_FIELD_VEC3);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, got3.z);
		sk_vec4_t got4 = api->get_vec4(read, EXT_FIELD_VEC4);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, got4.w);
		sk_quat_t gotq = api->get_quat(read, EXT_FIELD_QUAT);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, gotq.w);
		sk_mat44_t gotm = api->get_mat4(read, EXT_FIELD_MAT4);
		for (u32 i = 0u; i < 16u; ++i) {
			TEST_ASSERT_EQUAL_FLOAT(0.0f, gotm.m[i]);
		}
		sk_color_t gotc = api->get_color(read, EXT_FIELD_COLOR);
		TEST_ASSERT_EQUAL_FLOAT(0.0f, gotc.a);
		TEST_ASSERT_EQUAL_UINT64(0u, api->get_enum(read, EXT_FIELD_ENUM));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_blob(read, EXT_FIELD_BLOB, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(api->get_type_id(read, EXT_FIELD_TYPE_ID), SK_TYPE_ID_ZERO));
		TEST_ASSERT_FALSE(api->has_value_on_this_object(read, EXT_FIELD_VEC2));
	}

	/* Set + get round-trip on the write view, then on a fresh read view. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_vec2(view, EXT_FIELD_VEC2, v2));
		TEST_ASSERT_EQUAL_INT(0, api->set_vec3(view, EXT_FIELD_VEC3, v3));
		TEST_ASSERT_EQUAL_INT(0, api->set_vec4(view, EXT_FIELD_VEC4, v4));
		TEST_ASSERT_EQUAL_INT(0, api->set_quat(view, EXT_FIELD_QUAT, quat));
		TEST_ASSERT_EQUAL_INT(0, api->set_mat4(view, EXT_FIELD_MAT4, mat4));
		TEST_ASSERT_EQUAL_INT(0, api->set_color(view, EXT_FIELD_COLOR, color));
		TEST_ASSERT_EQUAL_INT(0, api->set_enum(view, EXT_FIELD_ENUM, enum_value));
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, EXT_FIELD_BLOB, blob_bytes, 6u));
		TEST_ASSERT_EQUAL_INT(0, api->set_type_id(view, EXT_FIELD_TYPE_ID, type_id));

		sk_vec2_t got = api->get_vec2(view, EXT_FIELD_VEC2);
		TEST_ASSERT_EQUAL_FLOAT(v2.x, got.x);
		TEST_ASSERT_EQUAL_FLOAT(v2.y, got.y);
		sk_vec3_t got3 = api->get_vec3(view, EXT_FIELD_VEC3);
		TEST_ASSERT_EQUAL_FLOAT(v3.z, got3.z);
		sk_vec4_t got4 = api->get_vec4(view, EXT_FIELD_VEC4);
		TEST_ASSERT_EQUAL_FLOAT(v4.w, got4.w);
		sk_quat_t gotq = api->get_quat(view, EXT_FIELD_QUAT);
		TEST_ASSERT_EQUAL_FLOAT(quat.w, gotq.w);
		sk_mat44_t gotm = api->get_mat4(view, EXT_FIELD_MAT4);
		TEST_ASSERT_EQUAL_FLOAT(mat4.m[0], gotm.m[0]);
		TEST_ASSERT_EQUAL_FLOAT(mat4.m[15], gotm.m[15]);
		sk_color_t gotc = api->get_color(view, EXT_FIELD_COLOR);
		TEST_ASSERT_EQUAL_FLOAT(color.a, gotc.a);
		TEST_ASSERT_EQUAL_UINT64(enum_value, api->get_enum(view, EXT_FIELD_ENUM));
		u32 size = 0u;
		const u8* data = api->get_blob(view, EXT_FIELD_BLOB, &size);
		TEST_ASSERT_EQUAL_UINT32(6u, size);
		TEST_ASSERT_EQUAL_MEMORY(blob_bytes, data, 6u);
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(type_id, api->get_type_id(view, EXT_FIELD_TYPE_ID)));
		TEST_ASSERT_TRUE(api->has_value_on_this_object(view, EXT_FIELD_VEC2));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(read));
		sk_vec2_t got = api->get_vec2(read, EXT_FIELD_VEC2);
		TEST_ASSERT_EQUAL_FLOAT(v2.x, got.x);
		TEST_ASSERT_EQUAL_FLOAT(v2.y, got.y);
		sk_vec3_t got3 = api->get_vec3(read, EXT_FIELD_VEC3);
		TEST_ASSERT_EQUAL_FLOAT(v3.z, got3.z);
		sk_vec4_t got4 = api->get_vec4(read, EXT_FIELD_VEC4);
		TEST_ASSERT_EQUAL_FLOAT(v4.w, got4.w);
		sk_quat_t gotq = api->get_quat(read, EXT_FIELD_QUAT);
		TEST_ASSERT_EQUAL_FLOAT(quat.w, gotq.w);
		sk_mat44_t gotm = api->get_mat4(read, EXT_FIELD_MAT4);
		TEST_ASSERT_EQUAL_FLOAT(mat4.m[0], gotm.m[0]);
		TEST_ASSERT_EQUAL_FLOAT(mat4.m[15], gotm.m[15]);
		sk_color_t gotc = api->get_color(read, EXT_FIELD_COLOR);
		TEST_ASSERT_EQUAL_FLOAT(color.a, gotc.a);
		TEST_ASSERT_EQUAL_UINT64(enum_value, api->get_enum(read, EXT_FIELD_ENUM));
		u32 size = 0u;
		const u8* data = api->get_blob(read, EXT_FIELD_BLOB, &size);
		TEST_ASSERT_EQUAL_UINT32(6u, size);
		TEST_ASSERT_EQUAL_MEMORY(blob_bytes, data, 6u);
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(type_id, api->get_type_id(read, EXT_FIELD_TYPE_ID)));
	}

	/* Error paths: read view, unknown index, field-type mismatch. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT(-1, api->set_vec2(read, EXT_FIELD_VEC2, v2));
		TEST_ASSERT_EQUAL_INT(-1, api->set_blob(read, EXT_FIELD_BLOB, blob_bytes, 6u));
		TEST_ASSERT_EQUAL_INT(-1, api->set_type_id(read, EXT_FIELD_TYPE_ID, type_id));
	}
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(-1, api->set_vec3(view, 999u, v3));
		TEST_ASSERT_EQUAL_INT(-2, api->set_vec2(view, EXT_FIELD_VEC3, v2));
		TEST_ASSERT_EQUAL_INT(-2, api->set_blob(view, EXT_FIELD_MAT4, blob_bytes, 6u));
		TEST_ASSERT_EQUAL_INT(-2, api->set_type_id(view, EXT_FIELD_ENUM, type_id));
		TEST_ASSERT_EQUAL_INT(-2, api->set_enum(view, EXT_FIELD_TYPE_ID, enum_value));
		api->discard(view);
	}

	/* Blob deep copy: the repository owns its own bytes, so mutating the
	 * caller's buffer after the set cannot affect the stored value. */
	{
		u8 mutated[6];
		memcpy(mutated, blob_bytes, sizeof(mutated));
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, EXT_FIELD_BLOB, mutated, 6u));
		mutated[0] = 200u;
		u32 size = 0u;
		const u8* data = api->get_blob(view, EXT_FIELD_BLOB, &size);
		TEST_ASSERT_EQUAL_UINT32(6u, size);
		TEST_ASSERT_EQUAL_MEMORY(blob_bytes, data, 6u);

		/* Overwrite with a smaller blob, then clear (NULL + 0). */
		const u8 tail[2] = {9u, 9u};
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, EXT_FIELD_BLOB, tail, 2u));
		size = 0u;
		data = api->get_blob(view, EXT_FIELD_BLOB, &size);
		TEST_ASSERT_EQUAL_UINT32(2u, size);
		TEST_ASSERT_EQUAL_MEMORY(tail, data, 2u);
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, EXT_FIELD_BLOB, NULL, 0u));
		size = 123u;
		TEST_ASSERT_NULL(api->get_blob(view, EXT_FIELD_BLOB, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(view, EXT_FIELD_BLOB));
		api->discard(view); /* the published blob stays the committed 6 bytes */
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_blob(read, EXT_FIELD_BLOB, &size);
		TEST_ASSERT_EQUAL_UINT32(6u, size);
		TEST_ASSERT_EQUAL_MEMORY(blob_bytes, data, 6u);
	}

	/* Prototype-chain fallback: unset on the instance, inherited from the
	 * prototype; an instance override shadows it. */
	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(prototype.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_vec4(view, EXT_FIELD_VEC4, v4));
		TEST_ASSERT_EQUAL_INT(0, api->set_enum(view, EXT_FIELD_ENUM, 7u));
		TEST_ASSERT_EQUAL_INT(0, api->set_type_id(view, EXT_FIELD_TYPE_ID, type_id));
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, EXT_FIELD_BLOB, blob_bytes, 6u));
		api->commit(view, NULL);
	}
	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance.id != 0u);
	{
		sk_resource_object_t read = api->read(repo, instance);
		TEST_ASSERT_FALSE(api->has_value_on_this_object(read, EXT_FIELD_VEC4));
		TEST_ASSERT_FALSE(api->is_value_overridden(read, EXT_FIELD_VEC4));
		sk_vec4_t got = api->get_vec4(read, EXT_FIELD_VEC4);
		TEST_ASSERT_EQUAL_FLOAT(v4.x, got.x);
		TEST_ASSERT_EQUAL_FLOAT(v4.y, got.y);
		TEST_ASSERT_EQUAL_FLOAT(v4.z, got.z);
		TEST_ASSERT_EQUAL_FLOAT(v4.w, got.w);
		TEST_ASSERT_EQUAL_UINT64(7u, api->get_enum(read, EXT_FIELD_ENUM));
		TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(type_id, api->get_type_id(read, EXT_FIELD_TYPE_ID)));
		u32 size = 0u;
		const u8* data = api->get_blob(read, EXT_FIELD_BLOB, &size);
		TEST_ASSERT_EQUAL_UINT32(6u, size);
		TEST_ASSERT_EQUAL_MEMORY(blob_bytes, data, 6u);
	}
	{
		sk_resource_object_t view = api->write(repo, instance);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		const sk_vec4_t own = {9.0f, 9.0f, 9.0f, 9.0f};
		TEST_ASSERT_EQUAL_INT(0, api->set_vec4(view, EXT_FIELD_VEC4, own));
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, EXT_FIELD_BLOB, NULL, 0u)); /* clear shadows the prototype */
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, instance);
		TEST_ASSERT_TRUE(api->is_value_overridden(read, EXT_FIELD_VEC4));
		sk_vec4_t got = api->get_vec4(read, EXT_FIELD_VEC4);
		TEST_ASSERT_EQUAL_FLOAT(9.0f, got.x);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(read, EXT_FIELD_BLOB));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_blob(read, EXT_FIELD_BLOB, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
	}

	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Buffer field accessor tests (APX-182)                             */
/* ------------------------------------------------------------------ */

/* Test payload: one Buffer field plus a Blob field (for cross-type
 * mismatch checks). */
typedef struct buf_object_t {
	sk_field_buffer_t buffer;
	sk_field_blob_t blob;
} buf_object_t;

#define BUF_FIELD_BUFFER 0u
#define BUF_FIELD_BLOB 1u

static const sk_resource_field_t buf_fields[2] = {
	{"buffer", BUF_FIELD_BUFFER, SK_RESOURCE_FIELD_TYPE_BUFFER, (u32)offsetof(buf_object_t, buffer), (u32)sizeof(sk_field_buffer_t), {0ull, 0ull}},
	{"blob", BUF_FIELD_BLOB, SK_RESOURCE_FIELD_TYPE_BLOB, (u32)offsetof(buf_object_t, blob), (u32)sizeof(sk_field_blob_t), {0ull, 0ull}},
};

static sk_repository_t* buf_repo(const sk_resource_type_t** out_type, const sk_allocator_t* allocator, u64 tag) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(allocator);
	TEST_ASSERT_NOT_NULL(repo);
	sk_resource_type_desc_t desc;
	desc.type_id = test_type_id(tag);
	desc.name = "buf.type";
	desc.instance_size = (u32)sizeof(buf_object_t);
	desc.fields = buf_fields;
	desc.field_count = 2u;
	desc.defaults = NULL;
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	*out_type = api->find_type_by_name(repo, "buf.type");
	TEST_ASSERT_NOT_NULL(*out_type);
	return repo;
}

SK_TEST(repository_buffer_field_accessors) {
	const sk_repository_api_t* api = sk_repository_api();
	/* Counting allocator proves overwrites / destroys release payloads. */
	test_counting_alloc_t state = {sk_allocator_default(), 0u};
	sk_allocator_t counting_allocator = {&state, test_counting_alloc, test_counting_free, test_counting_realloc};
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = buf_repo(&type, &counting_allocator, 89u);

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	u64 live_after_create = state.live;
	TEST_ASSERT_TRUE(live_after_create > 0u); /* instance block + bitmap live */

	static const u8 payload[8] = {0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u, 0x80u};
	const u8 small[3] = {9u, 8u, 7u};

	/* Unset: borrowed NULL + size 0, no has-value bit. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(read));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(read, BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_FALSE(api->has_value_on_this_object(read, BUF_FIELD_BUFFER));
	}

	/* Set from caller bytes: the repository deep-copies, so mutating the
	 * caller's bytes after the set cannot affect the stored value. */
	{
		u8 mutable_bytes[8];
		memcpy(mutable_bytes, payload, sizeof(mutable_bytes));
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, mutable_bytes, 8u));
		mutable_bytes[0] = 0xFFu;
		u32 size = 0u;
		const u8* data = api->get_buffer(view, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(8u, size);
		TEST_ASSERT_EQUAL_MEMORY(payload, data, 8u);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(view, BUF_FIELD_BUFFER));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(8u, size);
		TEST_ASSERT_EQUAL_MEMORY(payload, data, 8u);
	}

	/* Overwrite: a larger payload, then a smaller one; each set releases the
	 * previous payload (no leak) and the committed value matches the last
	 * set. */
	{
		u8 big[64];
		memset(big, 0xABu, sizeof(big));
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, big, 64u));
		u32 size = 0u;
		const u8* data = api->get_buffer(view, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(64u, size);
		TEST_ASSERT_EQUAL_MEMORY(big, data, 64u);
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, small, 3u));
		size = 0u;
		data = api->get_buffer(view, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(3u, size);
		TEST_ASSERT_EQUAL_MEMORY(small, data, 3u);
		api->commit(view, NULL);
	}

	/* Empty (NULL + size 0) is distinct from unset: the has-value bit stays
	 * set, reads return NULL / 0, and the field still owns nothing. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, NULL, 0u));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(view, BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(view, BUF_FIELD_BUFFER));
		api->discard(view); /* published value stays the committed 3 bytes */
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(3u, size);
		TEST_ASSERT_EQUAL_MEMORY(small, data, 3u);
	}

	/* Error paths: read view, unknown index, field-type mismatch, OOM. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		TEST_ASSERT_EQUAL_INT(-1, api->set_buffer(read, BUF_FIELD_BUFFER, payload, 8u));
	}
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(-1, api->set_buffer(view, 999u, payload, 8u));
		TEST_ASSERT_EQUAL_INT(-2, api->set_buffer(view, BUF_FIELD_BLOB, payload, 8u));
		TEST_ASSERT_EQUAL_INT(-2, api->set_blob(view, BUF_FIELD_BUFFER, payload, 8u));
		u32 size = 55u;
		TEST_ASSERT_NULL(api->get_buffer(view, BUF_FIELD_BLOB, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_NULL(api->get_blob(view, BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		api->discard(view);
	}

	/* Prototype chain: an instance inherits the prototype's payload (borrowed,
	 * not copied); an empty override shadows it; a real override shadows too. */
	sk_rid_t prototype = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(prototype.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, payload, 8u));
		api->commit(view, NULL);
	}
	sk_rid_t instance = api->create_from_prototype(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(instance.id != 0u);
	{
		sk_resource_object_t read = api->read(repo, instance);
		TEST_ASSERT_FALSE(api->has_value_on_this_object(read, BUF_FIELD_BUFFER));
		TEST_ASSERT_FALSE(api->is_value_overridden(read, BUF_FIELD_BUFFER));
		u32 size = 0u;
		const u8* data = api->get_buffer(read, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(8u, size);
		TEST_ASSERT_EQUAL_MEMORY(payload, data, 8u);
	}
	{
		sk_resource_object_t view = api->write(repo, instance);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		/* Empty override: shadows the prototype while staying NULL / 0. */
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, NULL, 0u));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, instance);
		TEST_ASSERT_TRUE(api->is_value_overridden(read, BUF_FIELD_BUFFER));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(read, BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
	}

	/* Clone deep-copies the payload: mutating the origin after cloning cannot
	 * affect the clone, and the clone owns its own bytes. */
	{
		sk_resource_object_t view = api->write(repo, prototype);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		u8 other[5] = {1u, 2u, 3u, 4u, 5u};
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, other, 5u));
		api->commit(view, NULL);
	}
	sk_rid_t clone = api->clone(repo, prototype, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(clone.id != 0u);
	{
		const u8 other[5] = {1u, 2u, 3u, 4u, 5u};
		sk_resource_object_t read = api->read(repo, clone);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(5u, size);
		TEST_ASSERT_EQUAL_MEMORY(other, data, 5u);
	}

	/* OOM: set_buffer fails with -3 and leaves the previous payload intact. */
	{
		test_fail_alloc_t fail_state = {sk_allocator_default(), 0u, 0xFFFFFFFFu};
		sk_allocator_t fail_allocator = {&fail_state, test_fail_alloc, test_fail_free, test_fail_realloc};
		const sk_resource_type_t* fail_type = NULL;
		sk_repository_t* fail_repo = buf_repo(&fail_type, &fail_allocator, 90u);
		sk_rid_t fail_rid = api->create_resource(fail_repo, fail_type, SK_UUID_ZERO, NULL);
		TEST_ASSERT_TRUE(fail_rid.id != 0u);
		{
			sk_resource_object_t view = api->write(fail_repo, fail_rid);
			TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, BUF_FIELD_BUFFER, payload, 8u));
			api->commit(view, NULL);
		}
		{
			sk_resource_object_t view = api->write(fail_repo, fail_rid);
			TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
			fail_state.fail_at = fail_state.allocs_done; /* next alloc fails */
			u8 big[32];
			memset(big, 0x77u, sizeof(big));
			TEST_ASSERT_EQUAL_INT(-3, api->set_buffer(view, BUF_FIELD_BUFFER, big, 32u));
			u32 size = 0u;
			const u8* data = api->get_buffer(view, BUF_FIELD_BUFFER, &size);
			TEST_ASSERT_EQUAL_UINT32(8u, size);
			TEST_ASSERT_EQUAL_MEMORY(payload, data, 8u);
			api->discard(view);
		}
		api->destroy(fail_repo);
	}

	/* No leaks: GC reclaims superseded instance payloads, and destroying the
	 * repository returns every live allocation. */
	api->garbage_collect(repo);
	api->destroy(repo);
	TEST_ASSERT_EQUAL_UINT64(0u, state.live);
}

#endif /* SK_TESTS */
