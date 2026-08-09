#include "repository.h"

#include "array.h"
#include "hashmap.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal layout                                                   */
/* ------------------------------------------------------------------ */

#define SK_REPOSITORY_PAGE_BITS 12u
#define SK_REPOSITORY_PAGE_SIZE (1u << SK_REPOSITORY_PAGE_BITS)
#define SK_REPOSITORY_PAGE_MASK (SK_REPOSITORY_PAGE_SIZE - 1u)

/* Opaque storage slot: one live resource inside a page. */
typedef struct sk_resource_storage_t {
	sk_rid_t rid;
	sk_uuid_t uuid;
	sk_resource_type_t* type;
	char* path; /* NULL when unset */
	void* instance;
} sk_resource_storage_t;

typedef struct sk_repository_page_t {
	sk_resource_storage_t* elements; /* [SK_REPOSITORY_PAGE_SIZE] */
	u8* used;						 /* [SK_REPOSITORY_PAGE_SIZE] */
} sk_repository_page_t;

typedef SK_HASH_MAP(sk_type_id_t, sk_resource_type_t*) sk_type_id_map_t;
typedef SK_HASH_MAP(const_chr_t, sk_resource_type_t*) sk_type_name_map_t;
typedef SK_HASH_MAP(sk_uuid_t, sk_rid_t) sk_uuid_map_t;
typedef SK_HASH_MAP(const_chr_t, sk_rid_t) sk_path_map_t;

struct sk_repository_t {
	const sk_allocator_t* allocator;
	SK_ARRAY(sk_repository_page_t*) pages;
	u64 rid_counter;	/* starts at 1; RID 0 reserved invalid */
	u64 resource_count; /* live resource count */
	sk_type_id_map_t types_by_id;
	sk_type_name_map_t types_by_name;
	sk_uuid_map_t rids_by_uuid;
	sk_path_map_t rids_by_path;
};

struct sk_resource_type_t {
	sk_type_id_t type_id;
	char* name; /* owned copy */
	u32 instance_size;
	u32 field_count;
	sk_resource_field_t* fields; /* owned array */
	u8* defaults;				 /* owned deep-copied default instance, or NULL */
};

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

/* Release the indirection payload of one field (String / Blob / RID array). */
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
	case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
		sk_field_rid_array_t* a = (sk_field_rid_array_t*)(void_ptr_t)p;
		if (a->items != NULL) {
			repository->allocator->free(repository->allocator->instance, a->items);
			a->items = NULL;
		}
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
	case SK_RESOURCE_FIELD_TYPE_BUFFER:
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

/**
 * Deep-copy @p source into a fresh instance blob. Indirection fields get
 * repository-owned copies; all other fields are copied by bytes. On failure
 * the partially copied blob is released and NULL is returned.
 * @param source Source blob of type->instance_size bytes (may be NULL to get a
 *               zeroed blob).
 */
static u8* sk_repo_instance_copy(const sk_repository_t* repository, const sk_resource_type_t* type, const u8* source) {
	u32 size = type->instance_size;
	if (size == 0u) {
		return NULL;
	}
	u8* dest = (u8*)repository->allocator->alloc(repository->allocator->instance, (size_t)size);
	if (dest == NULL) {
		return NULL;
	}
	memset(dest, 0, (size_t)size);

	if (source == NULL) {
		return dest;
	}

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
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
			const sk_field_rid_array_t* src = (const sk_field_rid_array_t*)(const_ptr_t)(source + (size_t)field->offset);
			sk_field_rid_array_t* out = (sk_field_rid_array_t*)(void_ptr_t)dst;
			if (src->count != 0u && src->items != NULL) {
				size_t bytes = (size_t)src->count * sizeof(sk_rid_t);
				out->items = (sk_rid_t*)(void_ptr_t)sk_repo_copy_bytes(repository, src->items, bytes);
				if (out->items == NULL) {
					goto fail;
				}
				out->count = src->count;
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
		case SK_RESOURCE_FIELD_TYPE_BUFFER:
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			memcpy(dst, source + (size_t)field->offset, (size_t)field->size);
			break;
		}
	}
	return dest;

fail:
	sk_repo_instance_destroy_fields(repository, type, dest);
	repository->allocator->free(repository->allocator->instance, dest);
	return NULL;
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
/*  Storage helpers                                                   */
/* ------------------------------------------------------------------ */

/* Ensure the page holding @p page_index exists (allocated + tracked). */
static i32 sk_repo_ensure_page(sk_repository_t* repository, u32 page_index) {
	if (page_index < repository->pages.count && repository->pages.items[page_index] != NULL) {
		return 0;
	}
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

/* ------------------------------------------------------------------ */
/*  Repository / type / resource ops                                  */
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
		}
		a->free(a->instance, page->elements);
		a->free(a->instance, page);
	}
	sk_array_free(&repository->pages);

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
	a->free(a->instance, repository);
}

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
		type->defaults = sk_repo_instance_copy(repository, type, (const u8*)desc->defaults);
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

static sk_rid_t repository_create_resource(sk_repository_t* repository, const sk_resource_type_t* type, sk_uuid_t uuid) {
	if (type->instance_size == 0u) {
		return SK_RID_ZERO;
	}
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		sk_rid_t existing = repository_find_by_uuid(repository, uuid);
		if (existing.id != 0u) {
			return existing;
		}
	}
	u64 next = repository->rid_counter;
	if (next == 0u) {
		return SK_RID_ZERO;
	}
	repository->rid_counter = next + 1u;
	sk_rid_t rid = {next};
	u32 page_index = (u32)(next >> SK_REPOSITORY_PAGE_BITS);
	u32 offset = (u32)(next & SK_REPOSITORY_PAGE_MASK);
	if (sk_repo_ensure_page(repository, page_index) != 0) {
		return SK_RID_ZERO;
	}
	sk_repository_page_t* page = repository->pages.items[page_index];
	sk_resource_storage_t* storage = &page->elements[offset];
	page->used[offset] = 1u;
	storage->rid = rid;
	storage->uuid = uuid;
	storage->type = SK_CONST_CAST(sk_resource_type_t*, type);
	storage->path = NULL;
	storage->instance = NULL;

	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		if (sk_hash_map_put(&repository->rids_by_uuid, uuid, rid) != 0) {
			page->used[offset] = 0u;
			memset(storage, 0, sizeof(*storage));
			return SK_RID_ZERO;
		}
	}
	void_ptr_t instance = NULL;
	if (type->defaults != NULL) {
		instance = sk_repo_instance_copy(repository, type, type->defaults);
	} else {
		instance = repository->allocator->alloc(repository->allocator->instance, (size_t)type->instance_size);
		if (instance != NULL) {
			memset(instance, 0, (size_t)type->instance_size);
		}
	}
	if (instance == NULL) {
		if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
			sk_hash_map_remove(&repository->rids_by_uuid, uuid);
		}
		page->used[offset] = 0u;
		memset(storage, 0, sizeof(*storage));
		return SK_RID_ZERO;
	}
	storage->instance = instance;
	repository->resource_count += 1u;
	return rid;
}

static i32 repository_destroy_resource(sk_repository_t* repository, sk_rid_t rid) {
	u64 idx = rid.id;
	if (idx == 0u) {
		return -1;
	}
	u32 page_index = (u32)(idx >> SK_REPOSITORY_PAGE_BITS);
	u32 offset = (u32)(idx & SK_REPOSITORY_PAGE_MASK);
	if (page_index >= repository->pages.count) {
		return -1;
	}
	sk_repository_page_t* page = repository->pages.items[page_index];
	if (page == NULL || !page->used[offset]) {
		return -1;
	}
	sk_resource_storage_t* storage = &page->elements[offset];
	if (storage->instance != NULL) {
		sk_repo_instance_destroy(repository, storage->type, storage->instance);
	}
	if (storage->path != NULL) {
		sk_hash_map_remove(&repository->rids_by_path, storage->path);
		repository->allocator->free(repository->allocator->instance, storage->path);
	}
	if (!SK_UUID_EQ(storage->uuid, SK_UUID_ZERO)) {
		sk_hash_map_remove(&repository->rids_by_uuid, storage->uuid);
	}
	page->used[offset] = 0u;
	memset(storage, 0, sizeof(*storage));
	if (repository->resource_count > 0u) {
		repository->resource_count -= 1u;
	}
	return 0;
}

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
	return storage != NULL ? storage->instance : NULL;
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
/*  Module API table                                                  */
/* ------------------------------------------------------------------ */

static const sk_repository_api_t repository_api = {
	repository_create,			 repository_destroy,	  repository_register_type,	 repository_find_type,	   repository_find_type_by_name, repository_create_resource,
	repository_destroy_resource, repository_has_resource, repository_resource_count, repository_resource_type, repository_resource_uuid,	 repository_resource_instance,
	repository_find_by_uuid,	 repository_set_path,	  repository_get_path,		 repository_find_by_path,
};

SK_API const sk_repository_api_t* sk_repository_api(void) {
	return &repository_api;
}

#ifdef SK_TESTS
#include "test.h"

#include <stddef.h>
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
	sk_rid_t rid = api->create_resource(repo_a, ta, uuid);
	TEST_ASSERT_TRUE(rid.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo_a));

	/* repo_b shares no state with repo_a. */
	TEST_ASSERT_FALSE(api->has_resource(repo_b, rid));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo_b, uuid).id == 0u);
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(repo_b));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->find_by_uuid(repo_a, uuid), rid));

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo_a, rid));
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
	sk_rid_t r1 = api->create_resource(repo, type, uuid);
	sk_rid_t r2 = api->create_resource(repo, type, uuid);
	TEST_ASSERT_TRUE(SK_RID_EQ(r1, r2));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	/* Zero UUID → no uniqueness constraint; each create is a fresh resource. */
	sk_rid_t z1 = api->create_resource(repo, type, SK_UUID_ZERO);
	sk_rid_t z2 = api->create_resource(repo, type, SK_UUID_ZERO);
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

	sk_rid_t a = api->create_resource(repo, type, (sk_uuid_t){1u, 0u});
	sk_rid_t b = api->create_resource(repo, type, (sk_uuid_t){2u, 0u});
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

	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, a));
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, b));
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
		sk_rid_t rid = api->create_resource(repo, type, uuid);
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

	sk_rid_t rid1 = api->create_resource(repo, type, (sk_uuid_t){1u, 0u});
	sk_rid_t rid2 = api->create_resource(repo, type, (sk_uuid_t){2u, 0u});
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
	TEST_ASSERT_EQUAL_INT(0, api->destroy_resource(repo, rid1));
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
	sk_rid_t warm = api->create_resource(repo, type, (sk_uuid_t){1u, 0u});
	TEST_ASSERT_TRUE(warm.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));

	/* set_path: the path copy is the first allocation → OOM, path unchanged. */
	state.fail_at = state.allocs_done;
	TEST_ASSERT_NOT_EQUAL(0, api->set_path(repo, warm, "assets/oom.foo"));
	TEST_ASSERT_NULL(api->get_path(repo, warm));
	TEST_ASSERT_TRUE(api->find_by_path(repo, "assets/oom.foo").id == 0u);

	/* create_resource: instance blob OOM → slot + uuid rolled back. */
	state.fail_at = state.allocs_done;
	sk_rid_t failed = api->create_resource(repo, type, (sk_uuid_t){2u, 0u});
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
#endif /* SK_TESTS */
