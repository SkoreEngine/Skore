/**
 * @file resource_serialize.c
 * @brief JSON (de)serialization for repository resources.
 */

#include "resource_serialize.h"

#include "allocator.h"
#include "array.h"
#include "filesystem.h"
#include "hashmap.h"
#include "path.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#include "resource_asset_builtins.h"
#include "resource_assets_types.h"
#include "test.h"

#include <math.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
#endif

/*
 * Error codes (non-zero = failure; match core convention / contract §3.7).
 * Package loads surface unresolvable UUID targets as SK_RES_SER_MISSING_REF
 * rather than committing a silent SK_RID_ZERO (APX-194).
 */
enum {
	SK_RES_SER_OK = 0,
	SK_RES_SER_ERR = -1,		 /* OOM / generic / parse */
	SK_RES_SER_INVALID = -2,	 /* bad format / version / type / shape */
	SK_RES_SER_MISSING_REF = -3, /* package: unresolved UUID target */
	SK_RES_SER_FIELD = -4,		 /* field set failure */
};

/* Canonical UUID text: 16 hex + '-' + 16 hex + NUL */
enum { SK_UUID_STR_LEN = 33, SK_UUID_STR_CAP = 34 };

static void uuid_format(char out[SK_UUID_STR_CAP], sk_uuid_t uuid) {
	/* u64 is unsigned long long on LP64 and LLP64; %llx matches without a cast. */
	(void)snprintf(out, SK_UUID_STR_CAP, "%016llx-%016llx", uuid.lo, uuid.hi);
}

static i32 hex_nibble(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static i32 uuid_parse(sk_str_view_t text, sk_uuid_t* out) {
	if (out == NULL || text.data == NULL || text.size != (u32)SK_UUID_STR_LEN || text.data[16] != '-') {
		return SK_RES_SER_INVALID;
	}
	u64 lo = 0u;
	u64 hi = 0u;
	for (u32 i = 0u; i < 16u; ++i) {
		i32 n = hex_nibble(text.data[i]);
		if (n < 0) {
			return SK_RES_SER_INVALID;
		}
		lo = (lo << 4) | (u64)n;
	}
	for (u32 i = 17u; i < 33u; ++i) {
		i32 n = hex_nibble(text.data[i]);
		if (n < 0) {
			return SK_RES_SER_INVALID;
		}
		hi = (hi << 4) | (u64)n;
	}
	out->lo = lo;
	out->hi = hi;
	return SK_RES_SER_OK;
}

static i32 str_eq_cstr(sk_str_view_t view, const_chr_t cstr) {
	if (cstr == NULL) {
		return view.size == 0u ? 1 : 0;
	}
	u32 n = 0u;
	while (cstr[n] != '\0') {
		++n;
	}
	if (view.size != n) {
		return 0;
	}
	if (n == 0u) {
		return 1;
	}
	return memcmp(view.data, cstr, (size_t)n) == 0 ? 1 : 0;
}

/* ---- collect reachable resources for package serialize ---- */

typedef SK_ARRAY(sk_rid_t) sk_rid_list_t;
typedef SK_HASH_SET(sk_rid_t) sk_rid_set_t;

/**
 * Push @p rid into BFS order when not yet visited.
 * Visited set is O(1) membership so large cyclic graphs stay linear-time
 * (APX-191: large asset sets / soft-reference cycles).
 */
static i32 rid_list_push_unique(sk_rid_list_t* list, sk_rid_set_t* visited, sk_rid_t rid) {
	if (rid.id == 0u || sk_hash_set_contains(visited, rid)) {
		return SK_RES_SER_OK;
	}
	if (sk_hash_set_add(visited, rid) != 0) {
		return SK_RES_SER_ERR;
	}
	if (sk_array_push(list, rid) != 0) {
		return SK_RES_SER_ERR;
	}
	return SK_RES_SER_OK;
}

/* BFS (non-recursive) collect of resources reachable via ref / subobject edges.
 * Soft-reference cycles terminate via the visited set (no stack recursion). */
static i32 collect_reachable(sk_repository_t* repository, sk_rid_t root, sk_rid_list_t* out) {
	const sk_repository_api_t* api = sk_repository_api();
	if (!api->has_resource(repository, root)) {
		return SK_RES_SER_INVALID;
	}

	sk_rid_set_t visited;
	if (sk_hash_set_init(&visited, sk_allocator_default(), NULL, NULL) != 0) {
		return SK_RES_SER_ERR;
	}

	if (rid_list_push_unique(out, &visited, root) != SK_RES_SER_OK) {
		sk_hash_set_free(&visited);
		return SK_RES_SER_ERR;
	}

	for (u32 cursor = 0u; cursor < out->count; ++cursor) {
		sk_rid_t rid = out->items[cursor];
		const sk_resource_type_t* type = api->resource_type(repository, rid);
		if (type == NULL) {
			continue;
		}
		sk_resource_object_t view = api->read(repository, rid);
		u32 field_count = api->type_field_count(type);
		for (u32 fi = 0u; fi < field_count; ++fi) {
			const sk_resource_field_t* field = api->type_field_at(type, fi);
			if (field == NULL) {
				continue;
			}
			switch (field->type) {
			case SK_RESOURCE_FIELD_TYPE_REFERENCE:
			case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
				sk_rid_t child = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE) ? api->get_reference(view, field->index) : api->get_subobject(view, field->index);
				if (child.id != 0u && api->has_resource(repository, child)) {
					if (rid_list_push_unique(out, &visited, child) != SK_RES_SER_OK) {
						sk_hash_set_free(&visited);
						return SK_RES_SER_ERR;
					}
				}
				break;
			}
			case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
				u32 count = 0u;
				const sk_rid_t* items = api->get_reference_array(view, field->index, &count);
				for (u32 i = 0u; i < count; ++i) {
					if (items[i].id != 0u && api->has_resource(repository, items[i])) {
						if (rid_list_push_unique(out, &visited, items[i]) != SK_RES_SER_OK) {
							sk_hash_set_free(&visited);
							return SK_RES_SER_ERR;
						}
					}
				}
				break;
			}
			case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
				u32 count = 0u;
				const sk_rid_t* items = api->get_subobject_list(view, field->index, &count);
				for (u32 i = 0u; i < count; ++i) {
					if (items[i].id != 0u && api->has_resource(repository, items[i])) {
						if (rid_list_push_unique(out, &visited, items[i]) != SK_RES_SER_OK) {
							sk_hash_set_free(&visited);
							return SK_RES_SER_ERR;
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
			case SK_RESOURCE_FIELD_TYPE_STRING:
			case SK_RESOURCE_FIELD_TYPE_VEC2:
			case SK_RESOURCE_FIELD_TYPE_VEC3:
			case SK_RESOURCE_FIELD_TYPE_VEC4:
			case SK_RESOURCE_FIELD_TYPE_QUAT:
			case SK_RESOURCE_FIELD_TYPE_MAT4:
			case SK_RESOURCE_FIELD_TYPE_COLOR:
			case SK_RESOURCE_FIELD_TYPE_ENUM:
			case SK_RESOURCE_FIELD_TYPE_BLOB:
			case SK_RESOURCE_FIELD_TYPE_BUFFER:
			case SK_RESOURCE_FIELD_TYPE_TYPE_ID:
			case SK_RESOURCE_FIELD_TYPE_MAX:
				break;
			}
		}
	}
	sk_hash_set_free(&visited);
	return SK_RES_SER_OK;
}

/* ---- field write ---- */

static i32 write_uuid_string(sk_archive_writer_t* writer, sk_str_view_t name, sk_uuid_t uuid) {
	char buf[SK_UUID_STR_CAP];
	uuid_format(buf, uuid);
	writer->write_string(writer->instance, name, sk_str_view_cstr(buf));
	return SK_RES_SER_OK;
}

static i32 write_uuid_add(sk_archive_writer_t* writer, sk_uuid_t uuid) {
	char buf[SK_UUID_STR_CAP];
	uuid_format(buf, uuid);
	writer->add_string(writer->instance, sk_str_view_cstr(buf));
	return SK_RES_SER_OK;
}

static i32 write_field_value(sk_repository_t* repository, sk_resource_object_t view, const sk_resource_field_t* field, sk_archive_writer_t* writer) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_str_view_t name = sk_str_view_cstr(field->name);

	switch (field->type) {
	case SK_RESOURCE_FIELD_TYPE_NONE:
		return SK_RES_SER_OK;

	case SK_RESOURCE_FIELD_TYPE_BOOL:
		writer->write_bool(writer->instance, name, api->get_bool(view, field->index));
		return SK_RES_SER_OK;

	case SK_RESOURCE_FIELD_TYPE_INT:
		writer->write_int(writer->instance, name, api->get_int(view, field->index));
		return SK_RES_SER_OK;

	case SK_RESOURCE_FIELD_TYPE_UINT:
		writer->write_uint(writer->instance, name, api->get_uint(view, field->index));
		return SK_RES_SER_OK;

	case SK_RESOURCE_FIELD_TYPE_FLOAT:
		writer->write_float(writer->instance, name, api->get_float(view, field->index));
		return SK_RES_SER_OK;

	case SK_RESOURCE_FIELD_TYPE_STRING: {
		const_chr_t s = api->get_string(view, field->index);
		writer->write_string(writer->instance, name, sk_str_view_cstr(s != NULL ? s : ""));
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_BLOB: {
		u32 size = 0u;
		const u8* data = api->get_blob(view, field->index, &size);
		writer->write_blob(writer->instance, name, data, (u64)size);
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_REFERENCE:
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
		sk_rid_t child = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE) ? api->get_reference(view, field->index) : api->get_subobject(view, field->index);
		if (child.id == 0u) {
			return SK_RES_SER_OK; /* omit zero refs */
		}
		sk_uuid_t uuid = api->resource_uuid(repository, child);
		if (SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
			return SK_RES_SER_INVALID;
		}
		return write_uuid_string(writer, name, uuid);
	}

	case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
		u32 count = 0u;
		const sk_rid_t* items = api->get_reference_array(view, field->index, &count);
		writer->begin_seq_named(writer->instance, name);
		for (u32 i = 0u; i < count; ++i) {
			if (items[i].id == 0u) {
				continue;
			}
			sk_uuid_t uuid = api->resource_uuid(repository, items[i]);
			if (SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
				writer->end_seq(writer->instance);
				return SK_RES_SER_INVALID;
			}
			(void)write_uuid_add(writer, uuid);
		}
		writer->end_seq(writer->instance);
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
		u32 count = 0u;
		const sk_rid_t* items = api->get_subobject_list(view, field->index, &count);
		writer->begin_seq_named(writer->instance, name);
		for (u32 i = 0u; i < count; ++i) {
			if (items[i].id == 0u) {
				continue;
			}
			sk_uuid_t uuid = api->resource_uuid(repository, items[i]);
			if (SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
				writer->end_seq(writer->instance);
				return SK_RES_SER_INVALID;
			}
			(void)write_uuid_add(writer, uuid);
		}
		writer->end_seq(writer->instance);
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_TYPE_ID: {
		sk_type_id_t tid = api->get_type_id(view, field->index);
		writer->begin_map_named(writer->instance, name);
		const sk_resource_type_t* named = api->find_type(repository, tid);
		if (named != NULL) {
			writer->write_string(writer->instance, sk_str_view_cstr("name"), sk_str_view_cstr(api->type_name(named)));
		}
		writer->write_uint(writer->instance, sk_str_view_cstr("lo"), tid.lo);
		writer->write_uint(writer->instance, sk_str_view_cstr("hi"), tid.hi);
		writer->end_map(writer->instance);
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_BUFFER: {
		/* v2 Buffer owns a payload (sk_field_buffer_t), same wire form as Blob. */
		u32 size = 0u;
		const u8* data = api->get_buffer(view, field->index, &size);
		writer->write_blob(writer->instance, name, data, (u64)size);
		return SK_RES_SER_OK;
	}

	/* Vectors / enums not used by asset types today. */
	case SK_RESOURCE_FIELD_TYPE_VEC2:
	case SK_RESOURCE_FIELD_TYPE_VEC3:
	case SK_RESOURCE_FIELD_TYPE_VEC4:
	case SK_RESOURCE_FIELD_TYPE_QUAT:
	case SK_RESOURCE_FIELD_TYPE_MAT4:
	case SK_RESOURCE_FIELD_TYPE_COLOR:
	case SK_RESOURCE_FIELD_TYPE_ENUM:
	case SK_RESOURCE_FIELD_TYPE_MAX:
		return SK_RES_SER_OK;
	}
	return SK_RES_SER_OK;
}

static i32 serialize_resource_body(sk_repository_t* repository, sk_rid_t rid, sk_archive_writer_t* writer) {
	const sk_repository_api_t* api = sk_repository_api();
	if (!api->has_resource(repository, rid)) {
		return SK_RES_SER_INVALID;
	}
	const sk_resource_type_t* type = api->resource_type(repository, rid);
	if (type == NULL) {
		return SK_RES_SER_INVALID;
	}

	writer->write_string(writer->instance, sk_str_view_cstr("format"), sk_str_view_cstr(SK_RESOURCE_JSON_FORMAT));
	writer->write_uint(writer->instance, sk_str_view_cstr("format_version"), (u64)SK_RESOURCE_JSON_FORMAT_VERSION);
	writer->write_string(writer->instance, sk_str_view_cstr("type"), sk_str_view_cstr(api->type_name(type)));

	sk_uuid_t uuid = api->resource_uuid(repository, rid);
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		char buf[SK_UUID_STR_CAP];
		uuid_format(buf, uuid);
		writer->write_string(writer->instance, sk_str_view_cstr("uuid"), sk_str_view_cstr(buf));
	}

	sk_resource_object_t view = api->read(repository, rid);
	writer->begin_map_named(writer->instance, sk_str_view_cstr("fields"));
	u32 field_count = api->type_field_count(type);
	for (u32 fi = 0u; fi < field_count; ++fi) {
		const sk_resource_field_t* field = api->type_field_at(type, fi);
		if (field == NULL || field->type == SK_RESOURCE_FIELD_TYPE_NONE) {
			continue;
		}
		/* Serialize fields set on this object; also emit zeros for simple
		 * value types that tests set through accessors (has_value bit). */
		if (!api->has_value_on_this_object(view, field->index)) {
			continue;
		}
		i32 rc = write_field_value(repository, view, field, writer);
		if (rc != SK_RES_SER_OK) {
			writer->end_map(writer->instance);
			return rc;
		}
	}
	writer->end_map(writer->instance);
	return SK_RES_SER_OK;
}

i32 sk_resource_serialize_json(sk_repository_t* repository, sk_rid_t rid, sk_archive_writer_t* writer) {
	return serialize_resource_body(repository, rid, writer);
}

/* ---- field read ---- */

typedef struct sk_res_ser_resolve_ctx_t {
	sk_repository_t* repository;
	i32 package_mode; /* hard-fail missing UUIDs when non-zero */
} sk_res_ser_resolve_ctx_t;

/**
 * Resolve a canonical UUID text handle to a live RID.
 *
 * - Empty text → SK_RID_ZERO (omit / null / absent).
 * - Malformed text → SK_RES_SER_INVALID.
 * - Well-formed but not live: package_mode → SK_RES_SER_MISSING_REF (hard
 *   fail; never leave a silent dangling handle); single-document → OK with
 *   SK_RID_ZERO (contract §3.6 single-asset policy).
 */
static i32 resolve_uuid_to_rid(sk_res_ser_resolve_ctx_t* ctx, sk_str_view_t text, sk_rid_t* out_rid) {
	const sk_repository_api_t* api = sk_repository_api();
	*out_rid = SK_RID_ZERO;
	if (text.size == 0u) {
		return SK_RES_SER_OK;
	}
	sk_uuid_t uuid = SK_UUID_ZERO;
	i32 prc = uuid_parse(text, &uuid);
	if (prc != SK_RES_SER_OK) {
		return prc;
	}
	/* Zero UUID is never a durable identity; treat as empty handle. */
	if (SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		return SK_RES_SER_OK;
	}
	sk_rid_t rid = api->find_by_uuid(ctx->repository, uuid);
	if (rid.id == 0u) {
		if (ctx->package_mode) {
			return SK_RES_SER_MISSING_REF;
		}
		/* Single-document: dangling ref becomes SK_RID_ZERO (no hard fail). */
		return SK_RES_SER_OK;
	}
	*out_rid = rid;
	return SK_RES_SER_OK;
}

static i32 read_type_id_field(sk_repository_t* repository, sk_archive_reader_t* reader, sk_str_view_t name, sk_type_id_t* out) {
	const sk_repository_api_t* api = sk_repository_api();
	*out = SK_TYPE_ID_ZERO;
	if (!reader->begin_map_named(reader->instance, name)) {
		return SK_RES_SER_OK; /* absent */
	}
	sk_str_view_t type_name = reader->read_string(reader->instance, sk_str_view_cstr("name"));
	u64 lo = reader->read_uint(reader->instance, sk_str_view_cstr("lo"));
	u64 hi = reader->read_uint(reader->instance, sk_str_view_cstr("hi"));
	reader->end_map(reader->instance);

	if (type_name.size > 0u) {
		/* NUL-terminate a stack copy for find_type_by_name */
		char name_buf[256];
		if (type_name.size >= (u32)sizeof(name_buf)) {
			return SK_RES_SER_INVALID;
		}
		memcpy(name_buf, type_name.data, (size_t)type_name.size);
		name_buf[type_name.size] = '\0';
		const sk_resource_type_t* found = api->find_type_by_name(repository, name_buf);
		if (found != NULL) {
			*out = api->type_id(found);
			return SK_RES_SER_OK;
		}
	}
	out->lo = lo;
	out->hi = hi;
	return SK_RES_SER_OK;
}

static i32 apply_field_value(sk_res_ser_resolve_ctx_t* ctx, sk_resource_object_t view, const sk_resource_field_t* field, sk_archive_reader_t* reader) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_str_view_t name = sk_str_view_cstr(field->name);

	switch (field->type) {
	case SK_RESOURCE_FIELD_TYPE_NONE:
		return SK_RES_SER_OK;

	case SK_RESOURCE_FIELD_TYPE_BOOL: {
		/* Absent keys soft-default; present bool applied. We cannot detect
		 * absence vs false with the archive API alone — always set when walking
		 * known fields only if the parent fields map is being applied for
		 * keys that exist. Callers only invoke apply for keys present. */
		i32 v = reader->read_bool(reader->instance, name);
		if (api->set_bool(view, field->index, v) != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_INT: {
		i64 v = reader->read_int(reader->instance, name);
		if (api->set_int(view, field->index, v) != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_UINT: {
		u64 v = reader->read_uint(reader->instance, name);
		if (api->set_uint(view, field->index, v) != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_FLOAT: {
		f64 v = reader->read_float(reader->instance, name);
		if (api->set_float(view, field->index, v) != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_STRING: {
		sk_str_view_t s = reader->read_string(reader->instance, name);
		char stack[4096];
		char* heap = NULL;
		const_chr_t cstr = "";
		const sk_allocator_t* a = sk_allocator_default();
		if (s.size > 0u) {
			if (s.size < (u32)sizeof(stack)) {
				memcpy(stack, s.data, (size_t)s.size);
				stack[s.size] = '\0';
				cstr = stack;
			} else {
				heap = (char*)a->alloc(a->instance, (size_t)s.size + 1u);
				if (heap == NULL) {
					return SK_RES_SER_ERR;
				}
				memcpy(heap, s.data, (size_t)s.size);
				heap[s.size] = '\0';
				cstr = heap;
			}
		}
		i32 rc = api->set_string(view, field->index, cstr);
		if (heap != NULL) {
			a->free(a->instance, heap);
		}
		if (rc != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_BLOB: {
		if (!reader->begin_seq_named(reader->instance, name)) {
			return SK_RES_SER_OK;
		}
		/* Decode blob byte sequence into a temporary buffer. */
		const sk_allocator_t* a = sk_allocator_default();
		SK_ARRAY(u8) bytes;
		sk_array_init(&bytes, a);
		while (reader->next_seq_entry(reader->instance)) {
			u64 v = reader->get_uint(reader->instance);
			if (v > 255u) {
				sk_array_free(&bytes);
				reader->end_seq(reader->instance);
				return SK_RES_SER_INVALID;
			}
			if (sk_array_push(&bytes, (u8)v) != 0) {
				sk_array_free(&bytes);
				reader->end_seq(reader->instance);
				return SK_RES_SER_ERR;
			}
		}
		reader->end_seq(reader->instance);
		i32 rc = api->set_blob(view, field->index, bytes.items, bytes.count);
		sk_array_free(&bytes);
		if (rc != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_REFERENCE:
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
		sk_str_view_t us = reader->read_string(reader->instance, name);
		if (us.size == 0u) {
			/* absent or empty → leave zero */
			return SK_RES_SER_OK;
		}
		sk_rid_t child = SK_RID_ZERO;
		i32 rrc = resolve_uuid_to_rid(ctx, us, &child);
		if (rrc != SK_RES_SER_OK) {
			return rrc;
		}
		i32 src = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE) ? api->set_reference(view, field->index, child) : api->set_subobject(view, field->index, child);
		if (src != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
	case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
		if (!reader->begin_seq_named(reader->instance, name)) {
			return SK_RES_SER_OK;
		}
		sk_rid_list_t items;
		sk_array_init(&items, sk_allocator_default());
		while (reader->next_seq_entry(reader->instance)) {
			sk_str_view_t us = reader->get_string(reader->instance);
			sk_rid_t child = SK_RID_ZERO;
			i32 rrc = resolve_uuid_to_rid(ctx, us, &child);
			if (rrc != SK_RES_SER_OK) {
				sk_array_free(&items);
				reader->end_seq(reader->instance);
				return rrc;
			}
			if (child.id != 0u) {
				if (sk_array_push(&items, child) != 0) {
					sk_array_free(&items);
					reader->end_seq(reader->instance);
					return SK_RES_SER_ERR;
				}
			}
		}
		reader->end_seq(reader->instance);
		i32 src = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) ? api->set_reference_array(view, field->index, items.items, items.count) :
																			api->set_subobject_list(view, field->index, items.items, items.count);
		sk_array_free(&items);
		if (src != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_TYPE_ID: {
		sk_type_id_t tid = SK_TYPE_ID_ZERO;
		i32 rc = read_type_id_field(ctx->repository, reader, name, &tid);
		if (rc != SK_RES_SER_OK) {
			return rc;
		}
		if (SK_TYPE_ID_EQ(tid, SK_TYPE_ID_ZERO)) {
			return SK_RES_SER_OK;
		}
		if (api->set_type_id(view, field->index, tid) != 0) {
			return SK_RES_SER_FIELD;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_BUFFER: {
		/* Accept byte-array form (v2 payload). Legacy {"id": u64} maps are
		 * ignored as empty so older fixtures do not hard-fail the load. */
		if (reader->begin_seq_named(reader->instance, name)) {
			const sk_allocator_t* a = sk_allocator_default();
			SK_ARRAY(u8) bytes;
			sk_array_init(&bytes, a);
			while (reader->next_seq_entry(reader->instance)) {
				u64 v = reader->get_uint(reader->instance);
				if (v > 255u) {
					sk_array_free(&bytes);
					reader->end_seq(reader->instance);
					return SK_RES_SER_INVALID;
				}
				if (sk_array_push(&bytes, (u8)v) != 0) {
					sk_array_free(&bytes);
					reader->end_seq(reader->instance);
					return SK_RES_SER_ERR;
				}
			}
			reader->end_seq(reader->instance);
			i32 rc = api->set_buffer(view, field->index, bytes.items, bytes.count);
			sk_array_free(&bytes);
			if (rc != 0) {
				return SK_RES_SER_FIELD;
			}
			return SK_RES_SER_OK;
		}
		if (reader->begin_map_named(reader->instance, name)) {
			/* Legacy opaque-id object: consume and leave field unset/empty. */
			(void)reader->read_uint(reader->instance, sk_str_view_cstr("id"));
			reader->end_map(reader->instance);
			return SK_RES_SER_OK;
		}
		return SK_RES_SER_OK;
	}

	case SK_RESOURCE_FIELD_TYPE_VEC2:
	case SK_RESOURCE_FIELD_TYPE_VEC3:
	case SK_RESOURCE_FIELD_TYPE_VEC4:
	case SK_RESOURCE_FIELD_TYPE_QUAT:
	case SK_RESOURCE_FIELD_TYPE_MAT4:
	case SK_RESOURCE_FIELD_TYPE_COLOR:
	case SK_RESOURCE_FIELD_TYPE_ENUM:
	case SK_RESOURCE_FIELD_TYPE_MAX:
		return SK_RES_SER_OK;
	}
	return SK_RES_SER_OK;
}

static i32 validate_envelope(sk_archive_reader_t* reader, const_chr_t expected_format, u64* out_version) {
	sk_str_view_t format = reader->read_string(reader->instance, sk_str_view_cstr("format"));
	if (!str_eq_cstr(format, expected_format)) {
		return SK_RES_SER_INVALID;
	}
	u64 version = reader->read_uint(reader->instance, sk_str_view_cstr("format_version"));
	if (version == 0u) {
		return SK_RES_SER_INVALID; /* missing or zero */
	}
	if (version > (u64)SK_RESOURCE_JSON_FORMAT_VERSION) {
		return SK_RES_SER_INVALID; /* unsupported future version */
	}
	if (out_version != NULL) {
		*out_version = version;
	}
	return SK_RES_SER_OK;
}

static i32 apply_fields_map(sk_res_ser_resolve_ctx_t* ctx, sk_resource_object_t view, const sk_resource_type_t* type, sk_archive_reader_t* reader) {
	const sk_repository_api_t* api = sk_repository_api();
	if (!reader->begin_map_named(reader->instance, sk_str_view_cstr("fields"))) {
		return SK_RES_SER_INVALID; /* fields required */
	}

	/* Iterate present keys; unknown keys ignored (forward compatible). */
	while (reader->next_map_entry(reader->instance)) {
		sk_str_view_t key = reader->get_current_key(reader->instance);
		const sk_resource_field_t* field = NULL;
		u32 field_count = api->type_field_count(type);
		for (u32 fi = 0u; fi < field_count; ++fi) {
			const sk_resource_field_t* f = api->type_field_at(type, fi);
			if (f != NULL && str_eq_cstr(key, f->name)) {
				field = f;
				break;
			}
		}
		if (field == NULL || field->type == SK_RESOURCE_FIELD_TYPE_NONE) {
			continue; /* ignore unknown / reserved */
		}
		/* Field values live under the fields map; named readers look up by key
		 * in the current map, so we re-enter apply which uses field->name. */
		i32 rc = apply_field_value(ctx, view, field, reader);
		if (rc != SK_RES_SER_OK) {
			reader->end_map(reader->instance);
			return rc;
		}
	}
	reader->end_map(reader->instance);
	return SK_RES_SER_OK;
}

static i32 deserialize_one_resource(sk_res_ser_resolve_ctx_t* ctx, sk_archive_reader_t* reader, i32 apply_fields, sk_rid_t* out_rid) {
	const sk_repository_api_t* api = sk_repository_api();
	*out_rid = SK_RID_ZERO;

	i32 vrc = validate_envelope(reader, SK_RESOURCE_JSON_FORMAT, NULL);
	if (vrc != SK_RES_SER_OK) {
		return vrc;
	}

	sk_str_view_t type_name = reader->read_string(reader->instance, sk_str_view_cstr("type"));
	if (type_name.size == 0u) {
		return SK_RES_SER_INVALID;
	}
	char type_buf[256];
	if (type_name.size >= (u32)sizeof(type_buf)) {
		return SK_RES_SER_INVALID;
	}
	memcpy(type_buf, type_name.data, (size_t)type_name.size);
	type_buf[type_name.size] = '\0';

	const sk_resource_type_t* type = api->find_type_by_name(ctx->repository, type_buf);
	if (type == NULL) {
		return SK_RES_SER_INVALID;
	}

	sk_uuid_t uuid = SK_UUID_ZERO;
	sk_str_view_t uuid_s = reader->read_string(reader->instance, sk_str_view_cstr("uuid"));
	if (uuid_s.size > 0u) {
		i32 prc = uuid_parse(uuid_s, &uuid);
		if (prc != SK_RES_SER_OK) {
			return prc;
		}
	}

	/* Detect create vs UUID-idempotent reuse so a failed field apply can roll
	 * back only a resource this call introduced (no partial mutation). */
	const i32 existed = (!SK_UUID_EQ(uuid, SK_UUID_ZERO) && api->find_by_uuid(ctx->repository, uuid).id != 0u) ? 1 : 0;
	sk_rid_t rid = api->create_resource(ctx->repository, type, uuid, NULL);
	if (rid.id == 0u) {
		return SK_RES_SER_ERR;
	}

	if (!apply_fields) {
		*out_rid = rid;
		return SK_RES_SER_OK;
	}

	sk_resource_object_t view = api->write(ctx->repository, rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		if (!existed) {
			(void)api->destroy_resource(ctx->repository, rid, NULL);
		}
		return SK_RES_SER_ERR;
	}
	i32 arc = apply_fields_map(ctx, view, type, reader);
	if (arc != SK_RES_SER_OK) {
		api->discard(view);
		/* New shell stays defaulted only; drop it so the repository is unchanged
		 * aside from any UUID-reused live resource (which was never committed). */
		if (!existed) {
			(void)api->destroy_resource(ctx->repository, rid, NULL);
		}
		*out_rid = SK_RID_ZERO;
		return arc;
	}
	api->commit(view, NULL);
	*out_rid = rid;
	return SK_RES_SER_OK;
}

i32 sk_resource_deserialize_json(sk_repository_t* repository, sk_archive_reader_t* reader, sk_rid_t* out_rid) {
	sk_res_ser_resolve_ctx_t ctx;
	ctx.repository = repository;
	ctx.package_mode = 0;
	return deserialize_one_resource(&ctx, reader, 1, out_rid);
}

/* ---- package ---- */

i32 sk_resource_serialize_package_json(sk_repository_t* repository, sk_rid_t root_rid, sk_archive_writer_t* writer) {
	const sk_repository_api_t* api = sk_repository_api();
	if (!api->has_resource(repository, root_rid)) {
		return SK_RES_SER_INVALID;
	}

	const sk_allocator_t* allocator = NULL;
	/* Borrow allocator from a temporary array via default; package collect needs it. */
	/* Use repository-owned resources only; allocate via default allocator. */
	allocator = sk_allocator_default();

	sk_rid_list_t rids;
	sk_array_init(&rids, allocator);
	i32 crc = collect_reachable(repository, root_rid, &rids);
	if (crc != SK_RES_SER_OK) {
		sk_array_free(&rids);
		return crc;
	}

	writer->write_string(writer->instance, sk_str_view_cstr("format"), sk_str_view_cstr(SK_RESOURCE_PACKAGE_JSON_FORMAT));
	writer->write_uint(writer->instance, sk_str_view_cstr("format_version"), (u64)SK_RESOURCE_JSON_FORMAT_VERSION);

	sk_uuid_t root_uuid = api->resource_uuid(repository, root_rid);
	if (SK_UUID_EQ(root_uuid, SK_UUID_ZERO)) {
		sk_array_free(&rids);
		return SK_RES_SER_INVALID;
	}
	char root_buf[SK_UUID_STR_CAP];
	uuid_format(root_buf, root_uuid);
	writer->write_string(writer->instance, sk_str_view_cstr("root_uuid"), sk_str_view_cstr(root_buf));

	writer->begin_seq_named(writer->instance, sk_str_view_cstr("resources"));
	for (u32 i = 0u; i < rids.count; ++i) {
		writer->begin_map(writer->instance);
		i32 src = serialize_resource_body(repository, rids.items[i], writer);
		if (src != SK_RES_SER_OK) {
			writer->end_map(writer->instance);
			writer->end_seq(writer->instance);
			sk_array_free(&rids);
			return src;
		}
		writer->end_map(writer->instance);
	}
	writer->end_seq(writer->instance);
	sk_array_free(&rids);
	return SK_RES_SER_OK;
}

i32 sk_resource_deserialize_package_json(sk_repository_t* repository, sk_archive_reader_t* reader, sk_rid_t* out_root) {
	const sk_repository_api_t* api = sk_repository_api();
	*out_root = SK_RID_ZERO;

	i32 vrc = validate_envelope(reader, SK_RESOURCE_PACKAGE_JSON_FORMAT, NULL);
	if (vrc != SK_RES_SER_OK) {
		return vrc;
	}

	sk_str_view_t root_s = reader->read_string(reader->instance, sk_str_view_cstr("root_uuid"));
	sk_uuid_t root_uuid = SK_UUID_ZERO;
	if (root_s.size == 0u || uuid_parse(root_s, &root_uuid) != SK_RES_SER_OK) {
		return SK_RES_SER_INVALID;
	}

	if (!reader->begin_seq_named(reader->instance, sk_str_view_cstr("resources"))) {
		return SK_RES_SER_INVALID;
	}

	/* Transaction scope: any failure undoes creates and field commits so the
	 * repository is not left partially mutated by a failed package load. */
	sk_undo_redo_scope_t* scope = api->undo_redo_scope_create(sk_allocator_default(), "resource_package_load");
	if (scope == NULL) {
		reader->end_seq(reader->instance);
		return SK_RES_SER_ERR;
	}

	sk_res_ser_resolve_ctx_t ctx;
	ctx.repository = repository;
	ctx.package_mode = 1;

	/* Create shells + apply scalar fields; queue ref UUID patches for a second
	 * pass so forward references resolve after every resource exists. */
	typedef struct sk_pending_ref_t {
		sk_rid_t owner;
		u32 field_index;
		u8 kind; /* 0=ref, 1=sub, 2=ref_array element, 3=sub_list element */
		u8 _pad0[3];
		sk_uuid_t uuid;
	} sk_pending_ref_t;

	typedef SK_ARRAY(sk_pending_ref_t) sk_pending_ref_list_t;
	sk_pending_ref_list_t pending;
	sk_array_init(&pending, sk_allocator_default());

	i32 fail_rc = SK_RES_SER_OK;

	while (fail_rc == SK_RES_SER_OK && reader->next_seq_entry(reader->instance)) {
		reader->begin_map(reader->instance);

		i32 env = validate_envelope(reader, SK_RESOURCE_JSON_FORMAT, NULL);
		if (env != SK_RES_SER_OK) {
			reader->end_map(reader->instance);
			fail_rc = env;
			break;
		}

		sk_str_view_t type_name = reader->read_string(reader->instance, sk_str_view_cstr("type"));
		if (type_name.size == 0u || type_name.size >= 256u) {
			reader->end_map(reader->instance);
			fail_rc = SK_RES_SER_INVALID;
			break;
		}
		char type_buf[256];
		memcpy(type_buf, type_name.data, (size_t)type_name.size);
		type_buf[type_name.size] = '\0';
		const sk_resource_type_t* type = api->find_type_by_name(repository, type_buf);
		if (type == NULL) {
			reader->end_map(reader->instance);
			fail_rc = SK_RES_SER_INVALID;
			break;
		}

		sk_uuid_t uuid = SK_UUID_ZERO;
		sk_str_view_t uuid_s = reader->read_string(reader->instance, sk_str_view_cstr("uuid"));
		if (uuid_s.size > 0u && uuid_parse(uuid_s, &uuid) != SK_RES_SER_OK) {
			reader->end_map(reader->instance);
			fail_rc = SK_RES_SER_INVALID;
			break;
		}

		sk_rid_t rid = api->create_resource(repository, type, uuid, scope);
		if (rid.id == 0u) {
			reader->end_map(reader->instance);
			fail_rc = SK_RES_SER_ERR;
			break;
		}

		/* Apply non-ref fields now; queue refs. */
		if (!reader->begin_map_named(reader->instance, sk_str_view_cstr("fields"))) {
			reader->end_map(reader->instance);
			fail_rc = SK_RES_SER_INVALID;
			break;
		}

		sk_resource_object_t view = api->write(repository, rid);
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			reader->end_map(reader->instance); /* fields */
			reader->end_map(reader->instance); /* resource */
			fail_rc = SK_RES_SER_ERR;
			break;
		}

		while (fail_rc == SK_RES_SER_OK && reader->next_map_entry(reader->instance)) {
			sk_str_view_t key = reader->get_current_key(reader->instance);
			const sk_resource_field_t* field = NULL;
			u32 fc = api->type_field_count(type);
			for (u32 fi = 0u; fi < fc; ++fi) {
				const sk_resource_field_t* f = api->type_field_at(type, fi);
				if (f != NULL && str_eq_cstr(key, f->name)) {
					field = f;
					break;
				}
			}
			if (field == NULL || field->type == SK_RESOURCE_FIELD_TYPE_NONE) {
				continue;
			}

			if (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE || field->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT) {
				sk_str_view_t us = reader->read_string(reader->instance, sk_str_view_cstr(field->name));
				if (us.size == 0u) {
					continue;
				}
				sk_uuid_t target = SK_UUID_ZERO;
				if (uuid_parse(us, &target) != SK_RES_SER_OK) {
					api->discard(view);
					fail_rc = SK_RES_SER_INVALID;
					break;
				}
				sk_pending_ref_t p;
				memset(&p, 0, sizeof(p));
				p.owner = rid;
				p.field_index = field->index;
				p.kind = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE) ? (u8)0u : (u8)1u;
				p.uuid = target;
				if (sk_array_push(&pending, p) != 0) {
					api->discard(view);
					fail_rc = SK_RES_SER_ERR;
					break;
				}
				continue;
			}

			if (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY || field->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
				if (!reader->begin_seq_named(reader->instance, sk_str_view_cstr(field->name))) {
					continue;
				}
				u32 queued = 0u;
				while (fail_rc == SK_RES_SER_OK && reader->next_seq_entry(reader->instance)) {
					sk_str_view_t us = reader->get_string(reader->instance);
					if (us.size == 0u) {
						continue;
					}
					sk_uuid_t target = SK_UUID_ZERO;
					if (uuid_parse(us, &target) != SK_RES_SER_OK) {
						api->discard(view);
						reader->end_seq(reader->instance);
						fail_rc = SK_RES_SER_INVALID;
						break;
					}
					sk_pending_ref_t p;
					memset(&p, 0, sizeof(p));
					p.owner = rid;
					p.field_index = field->index;
					p.kind = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) ? (u8)2u : (u8)3u;
					p.uuid = target;
					if (sk_array_push(&pending, p) != 0) {
						api->discard(view);
						reader->end_seq(reader->instance);
						fail_rc = SK_RES_SER_ERR;
						break;
					}
					queued += 1u;
				}
				if (fail_rc == SK_RES_SER_OK) {
					reader->end_seq(reader->instance);
					/* Empty arrays must still mark has_value so re-serialize
					 * emits [] (round-trip identity for present empty lists). */
					if (queued == 0u) {
						i32 src = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY) ? api->set_reference_array(view, field->index, NULL, 0u) :
																							api->set_subobject_list(view, field->index, NULL, 0u);
						if (src != 0) {
							api->discard(view);
							fail_rc = SK_RES_SER_FIELD;
							break;
						}
					}
				}
				continue;
			}

			/* Scalar / blob / type_id / buffer — apply immediately. */
			ctx.package_mode = 0;
			i32 arc = apply_field_value(&ctx, view, field, reader);
			if (arc != SK_RES_SER_OK) {
				api->discard(view);
				fail_rc = arc;
				break;
			}
		}
		if (fail_rc != SK_RES_SER_OK) {
			reader->end_map(reader->instance); /* fields (if still open) */
			reader->end_map(reader->instance); /* resource */
			break;
		}
		reader->end_map(reader->instance); /* fields */
		api->commit(view, scope);
		reader->end_map(reader->instance); /* resource */
	}
	reader->end_seq(reader->instance);

	/* Resolve pending references now that every UUID shell exists. */
	for (u32 i = 0u; fail_rc == SK_RES_SER_OK && i < pending.count; ++i) {
		sk_pending_ref_t* p = &pending.items[i];
		sk_rid_t target = api->find_by_uuid(repository, p->uuid);
		if (target.id == 0u) {
			fail_rc = SK_RES_SER_MISSING_REF;
			break;
		}
		sk_resource_object_t view = api->write(repository, p->owner);
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			fail_rc = SK_RES_SER_ERR;
			break;
		}
		i32 src = 0;
		if (p->kind == 0u) {
			src = api->set_reference(view, p->field_index, target);
		} else if (p->kind == 1u) {
			src = api->set_subobject(view, p->field_index, target);
		} else if (p->kind == 2u) {
			src = api->add_to_reference_array(view, p->field_index, target);
		} else {
			src = api->add_to_subobject_list(view, p->field_index, target);
		}
		if (src != 0) {
			api->discard(view);
			fail_rc = SK_RES_SER_FIELD;
			break;
		}
		api->commit(view, scope);
	}

	sk_array_free(&pending);

	if (fail_rc != SK_RES_SER_OK) {
		api->undo_redo_scope_undo(scope);
		api->undo_redo_scope_destroy(scope);
		*out_root = SK_RID_ZERO;
		return fail_rc;
	}

	sk_rid_t root = api->find_by_uuid(repository, root_uuid);
	if (root.id == 0u) {
		api->undo_redo_scope_undo(scope);
		api->undo_redo_scope_destroy(scope);
		return SK_RES_SER_MISSING_REF;
	}

	/* Keep published creates/commits; scope only held snapshots for rollback. */
	api->undo_redo_scope_destroy(scope);
	*out_root = root;
	return SK_RES_SER_OK;
}

/* ---- string convenience ---- */

static i32 emit_writer_to_alloc(sk_archive_writer_t* writer, const sk_allocator_t* allocator, char** out_json, u32* out_size) {
	sk_str_view_t emitted = sk_json_archive_writer_emit_as_string(writer);
	if (emitted.data == NULL || emitted.size == 0u) {
		*out_json = NULL;
		if (out_size != NULL) {
			*out_size = 0u;
		}
		return SK_RES_SER_ERR;
	}
	char* copy = (char*)allocator->alloc(allocator->instance, (size_t)emitted.size + 1u);
	if (copy == NULL) {
		*out_json = NULL;
		return SK_RES_SER_ERR;
	}
	memcpy(copy, emitted.data, (size_t)emitted.size);
	copy[emitted.size] = '\0';
	*out_json = copy;
	if (out_size != NULL) {
		*out_size = emitted.size;
	}
	return SK_RES_SER_OK;
}

i32 sk_resource_serialize_json_alloc(sk_repository_t* repository, sk_rid_t rid, const sk_allocator_t* allocator, char** out_json, u32* out_size) {
	*out_json = NULL;
	if (out_size != NULL) {
		*out_size = 0u;
	}
	sk_archive_writer_t writer;
	if (sk_json_archive_writer_init(&writer, allocator) != 0) {
		return SK_RES_SER_ERR;
	}
	i32 rc = sk_resource_serialize_json(repository, rid, &writer);
	if (rc == SK_RES_SER_OK) {
		rc = emit_writer_to_alloc(&writer, allocator, out_json, out_size);
	}
	sk_archive_writer_destroy(&writer);
	return rc;
}

i32 sk_resource_deserialize_json_string(sk_repository_t* repository, sk_str_view_t json, const sk_allocator_t* allocator, sk_rid_t* out_rid) {
	*out_rid = SK_RID_ZERO;
	sk_archive_reader_t reader;
	if (sk_json_archive_reader_init(&reader, json, allocator) != 0) {
		return SK_RES_SER_ERR;
	}
	i32 rc = sk_resource_deserialize_json(repository, &reader, out_rid);
	sk_archive_reader_destroy(&reader);
	return rc;
}

i32 sk_resource_serialize_package_json_alloc(sk_repository_t* repository, sk_rid_t root_rid, const sk_allocator_t* allocator, char** out_json, u32* out_size) {
	*out_json = NULL;
	if (out_size != NULL) {
		*out_size = 0u;
	}
	sk_archive_writer_t writer;
	if (sk_json_archive_writer_init(&writer, allocator) != 0) {
		return SK_RES_SER_ERR;
	}
	i32 rc = sk_resource_serialize_package_json(repository, root_rid, &writer);
	if (rc == SK_RES_SER_OK) {
		rc = emit_writer_to_alloc(&writer, allocator, out_json, out_size);
	}
	sk_archive_writer_destroy(&writer);
	return rc;
}

i32 sk_resource_deserialize_package_json_string(sk_repository_t* repository, sk_str_view_t json, const sk_allocator_t* allocator, sk_rid_t* out_root) {
	*out_root = SK_RID_ZERO;
	sk_archive_reader_t reader;
	if (sk_json_archive_reader_init(&reader, json, allocator) != 0) {
		return SK_RES_SER_ERR;
	}
	i32 rc = sk_resource_deserialize_package_json(repository, &reader, out_root);
	sk_archive_reader_destroy(&reader);
	return rc;
}

i32 sk_resource_serialize_package_json_to_file(sk_repository_t* repository, sk_rid_t root_rid, const_chr_t path) {
	if (path == NULL || path[0] == '\0') {
		return SK_RES_SER_ERR;
	}
	const sk_allocator_t* allocator = sk_allocator_default();
	char* json = NULL;
	u32 size = 0u;
	i32 rc = sk_resource_serialize_package_json_alloc(repository, root_rid, allocator, &json, &size);
	if (rc != SK_RES_SER_OK) {
		return rc;
	}

	const sk_filesystem_api_t* fs = sk_filesystem_api();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	if (file == NULL) {
		allocator->free(allocator->instance, json);
		return SK_RES_SER_ERR;
	}
	u64 written = fs->write_file(file, json, size);
	fs->close_file(file);
	allocator->free(allocator->instance, json);
	if (written != size) {
		return SK_RES_SER_ERR;
	}
	return SK_RES_SER_OK;
}

i32 sk_resource_deserialize_package_json_from_file(sk_repository_t* repository, const_chr_t path, sk_rid_t* out_root) {
	*out_root = SK_RID_ZERO;
	if (path == NULL || path[0] == '\0') {
		return SK_RES_SER_ERR;
	}

	const sk_filesystem_api_t* fs = sk_filesystem_api();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		return SK_RES_SER_ERR;
	}

	u64 file_size = fs->get_file_size(file);
	/* Cap to u32 so buffer sizes stay portable across LP64 and LLP64. */
	if (file_size == 0u || file_size > 0x7fffffffu) {
		fs->close_file(file);
		return SK_RES_SER_ERR;
	}
	u32 nbytes = (u32)file_size;

	const sk_allocator_t* allocator = sk_allocator_default();
	char* buffer = (char*)allocator->alloc(allocator->instance, nbytes + 1u);
	if (buffer == NULL) {
		fs->close_file(file);
		return SK_RES_SER_ERR;
	}

	u64 read_n = fs->read_file(file, buffer, nbytes);
	fs->close_file(file);
	if (read_n != nbytes) {
		allocator->free(allocator->instance, buffer);
		return SK_RES_SER_ERR;
	}
	buffer[nbytes] = '\0';

	i32 rc = sk_resource_deserialize_package_json_string(repository, sk_str_view_make(buffer, nbytes), allocator, out_root);
	allocator->free(allocator->instance, buffer);
	return rc;
}

/* ================================================================== */
/*  Tests                                                             */
/* ================================================================== */

#ifdef SK_TESTS

static sk_repository_t* ser_test_repo(void) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_repository_t* repo = api->create(sk_allocator_default());
	TEST_ASSERT_NOT_NULL(repo);
	TEST_ASSERT_EQUAL_INT(0, sk_resource_assets_register_types(repo));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_builtins_register_types(repo));
	return repo;
}

static sk_uuid_t ser_uuid(u64 lo, u64 hi) {
	sk_uuid_t u;
	u.lo = lo;
	u.hi = hi;
	return u;
}

static sk_rid_t ser_create(sk_repository_t* repo, const_chr_t type_name, sk_uuid_t uuid) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = api->find_type_by_name(repo, type_name);
	TEST_ASSERT_NOT_NULL(type);
	sk_rid_t rid = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	return rid;
}

/** Serialize -> destroy -> deserialize -> re-serialize; assert identical JSON. */
static void ser_assert_double_serialize_identity(sk_repository_t* repo, sk_rid_t rid, const sk_allocator_t* a) {
	const sk_repository_api_t* api = sk_repository_api();
	char* json1 = NULL;
	u32 size1 = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json1, &size1));
	TEST_ASSERT_NOT_NULL(json1);
	TEST_ASSERT_TRUE(size1 > 0u);

	sk_uuid_t uuid = api->resource_uuid(repo, rid);
	api->destroy_resource(repo, rid, NULL);

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_make(json1, size1), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	if (!SK_UUID_EQ(uuid, SK_UUID_ZERO)) {
		sk_uuid_t got = api->resource_uuid(repo, loaded);
		TEST_ASSERT_EQUAL_UINT64(uuid.lo, got.lo);
		TEST_ASSERT_EQUAL_UINT64(uuid.hi, got.hi);
	}

	char* json2 = NULL;
	u32 size2 = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, loaded, a, &json2, &size2));
	TEST_ASSERT_NOT_NULL(json2);
	TEST_ASSERT_EQUAL_UINT32(size1, size2);
	TEST_ASSERT_EQUAL_MEMORY(json1, json2, size1);

	a->free(a->instance, json1);
	a->free(a->instance, json2);
}

static void ser_roundtrip_named(const_chr_t type_name, u32 name_field_index, const_chr_t name_value) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t rid = ser_create(repo, type_name, ser_uuid(0x111u, 0x222u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, name_field_index, name_value));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_NOT_NULL(strstr(json, "\"format\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "sk.resource"));
	TEST_ASSERT_NOT_NULL(strstr(json, "format_version"));
	TEST_ASSERT_NOT_NULL(strstr(json, type_name));

	/* Double-serialize identity before destroying the first live rid. */
	{
		char* json2 = NULL;
		u32 s1 = 0u;
		u32 s2 = 0u;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json2, &s2));
		/* Re-emit the same live object twice — identical. */
		char* json1b = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json1b, &s1));
		TEST_ASSERT_EQUAL_UINT32(s1, s2);
		TEST_ASSERT_EQUAL_MEMORY(json1b, json2, s1);
		a->free(a->instance, json1b);
		a->free(a->instance, json2);
	}

	api->destroy_resource(repo, rid, NULL);

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING(name_value, api->get_string(r, name_field_index));
	sk_uuid_t u = api->resource_uuid(repo, loaded);
	TEST_ASSERT_EQUAL_UINT64(0x111u, u.lo);
	TEST_ASSERT_EQUAL_UINT64(0x222u, u.hi);

	/* serialize -> deserialize -> serialize produces identical JSON. */
	char* json_again = NULL;
	u32 size_again = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, loaded, a, &json_again, &size_again));
	TEST_ASSERT_EQUAL_STRING(json, json_again);

	a->free(a->instance, json_again);
	a->free(a->instance, json);
	api->destroy(repo);
}

/* Trivial in-test type: exercises shared plumbing without asset-type coupling. */
typedef struct sk_ser_trivial_t {
	sk_field_string_t label;
	i32 flag;
	u64 count;
	i64 signed_value;
	f64 ratio;
} sk_ser_trivial_t;

enum {
	SK_SER_TRIVIAL_FIELD_LABEL = 0u,
	SK_SER_TRIVIAL_FIELD_FLAG = 1u,
	SK_SER_TRIVIAL_FIELD_COUNT = 2u,
	SK_SER_TRIVIAL_FIELD_SIGNED = 3u,
	SK_SER_TRIVIAL_FIELD_RATIO = 4u,
};

static const sk_resource_field_t ser_trivial_fields[] = {
	{"Label", SK_SER_TRIVIAL_FIELD_LABEL, SK_RESOURCE_FIELD_TYPE_STRING, (u32)offsetof(sk_ser_trivial_t, label), (u32)sizeof(sk_field_string_t), {0ull, 0ull}},
	{"Flag", SK_SER_TRIVIAL_FIELD_FLAG, SK_RESOURCE_FIELD_TYPE_BOOL, (u32)offsetof(sk_ser_trivial_t, flag), (u32)sizeof(i32), {0ull, 0ull}},
	{"Count", SK_SER_TRIVIAL_FIELD_COUNT, SK_RESOURCE_FIELD_TYPE_UINT, (u32)offsetof(sk_ser_trivial_t, count), (u32)sizeof(u64), {0ull, 0ull}},
	{"SignedValue", SK_SER_TRIVIAL_FIELD_SIGNED, SK_RESOURCE_FIELD_TYPE_INT, (u32)offsetof(sk_ser_trivial_t, signed_value), (u32)sizeof(i64), {0ull, 0ull}},
	{"Ratio", SK_SER_TRIVIAL_FIELD_RATIO, SK_RESOURCE_FIELD_TYPE_FLOAT, (u32)offsetof(sk_ser_trivial_t, ratio), (u32)sizeof(f64), {0ull, 0ull}},
};

static const sk_resource_type_t* ser_register_trivial(sk_repository_t* repo) {
	const sk_repository_api_t* api = sk_repository_api();
	sk_type_id_t tid;
	tid.lo = 0xa11ce001ull;
	tid.hi = 0xb00b2ull;
	sk_resource_type_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.type_id = tid;
	desc.name = "SerTrivial";
	desc.fields = ser_trivial_fields;
	desc.field_count = (u32)(sizeof(ser_trivial_fields) / sizeof(ser_trivial_fields[0]));
	desc.instance_size = (u32)sizeof(sk_ser_trivial_t);
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	const sk_resource_type_t* type = api->find_type_by_name(repo, "SerTrivial");
	TEST_ASSERT_NOT_NULL(type);
	return type;
}

SK_TEST(resource_serialize_trivial_type_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	const sk_resource_type_t* type = ser_register_trivial(repo);

	sk_uuid_t uuid = ser_uuid(0xdeadu, 0xbeefu);
	sk_rid_t rid = api->create_resource(repo, type, uuid, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);

	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "hello"));
	TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_SER_TRIVIAL_FIELD_FLAG, 1));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_SER_TRIVIAL_FIELD_COUNT, 42u));
	TEST_ASSERT_EQUAL_INT(0, api->set_int(w, SK_SER_TRIVIAL_FIELD_SIGNED, -7));
	TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SER_TRIVIAL_FIELD_RATIO, 1.5));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_NOT_NULL(strstr(json, "\"format\""));
	TEST_ASSERT_NOT_NULL(strstr(json, SK_RESOURCE_JSON_FORMAT));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"format_version\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "SerTrivial"));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Label\"")); /* exact field descriptor names */
	TEST_ASSERT_NOT_NULL(strstr(json, "\"SignedValue\""));
	TEST_ASSERT_NULL(strstr(json, "signed_value")); /* no snake_case rename */

	api->destroy_resource(repo, rid, NULL);

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("hello", api->get_string(r, SK_SER_TRIVIAL_FIELD_LABEL));
	TEST_ASSERT_EQUAL_INT(1, api->get_bool(r, SK_SER_TRIVIAL_FIELD_FLAG));
	TEST_ASSERT_EQUAL_UINT64(42u, api->get_uint(r, SK_SER_TRIVIAL_FIELD_COUNT));
	TEST_ASSERT_EQUAL_INT64(-7, api->get_int(r, SK_SER_TRIVIAL_FIELD_SIGNED));
	TEST_ASSERT_EQUAL_DOUBLE(1.5, api->get_float(r, SK_SER_TRIVIAL_FIELD_RATIO));
	sk_uuid_t got = api->resource_uuid(repo, loaded);
	TEST_ASSERT_EQUAL_UINT64(0xdeadu, got.lo);
	TEST_ASSERT_EQUAL_UINT64(0xbeefu, got.hi);

	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_absent_and_unknown_fields) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	(void)ser_register_trivial(repo);

	/* Only Label present; other keys absent (soft default) + unknown key ignored. */
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"SerTrivial\",\n"
					   "  \"uuid\": \"00000000000000aa-00000000000000bb\",\n"
					   "  \"fields\": {\n"
					   "    \"Label\": \"partial\",\n"
					   "    \"FutureKey\": 123\n"
					   "  }\n"
					   "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
	TEST_ASSERT_TRUE(rid.id != 0u);
	sk_resource_object_t r = api->read(repo, rid);
	TEST_ASSERT_EQUAL_STRING("partial", api->get_string(r, SK_SER_TRIVIAL_FIELD_LABEL));
	TEST_ASSERT_EQUAL_INT(0, api->get_bool(r, SK_SER_TRIVIAL_FIELD_FLAG));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_uint(r, SK_SER_TRIVIAL_FIELD_COUNT));
	TEST_ASSERT_EQUAL_INT64(0, api->get_int(r, SK_SER_TRIVIAL_FIELD_SIGNED));
	api->destroy(repo);
}

SK_TEST(resource_serialize_resource_asset_package_roundtrip) {
	ser_roundtrip_named("ResourceAssetPackage", SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "MyPackage");
}

SK_TEST(resource_serialize_resource_asset_file_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceAssetFile", ser_uuid(0x10u, 0x20u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH, "/tmp/a.mesh"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH, "a.mesh"));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION, 7u));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK, 4096u));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME, 12345u));
	/* AssetRef left zero — single-doc omits null refs; field stays SK_RID_ZERO. */
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("/tmp/a.mesh", api->get_string(r, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH));
	TEST_ASSERT_EQUAL_STRING("a.mesh", api->get_string(r, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH));
	TEST_ASSERT_EQUAL_UINT64(7u, api->get_uint(r, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION));
	TEST_ASSERT_EQUAL_UINT64(4096u, api->get_uint(r, SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK));
	TEST_ASSERT_EQUAL_UINT64(12345u, api->get_uint(r, SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(r, SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF).id);
	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_resource_asset_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceAsset", ser_uuid(0x30u, 0x40u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "Hero"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".entity"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/Hero.entity"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_SOURCE_PATH, "/src/Hero.entity"));
	TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0));
	TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_READ_ONLY, 1));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(strstr(json, "Hero"));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("Hero", api->get_string(r, SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING(".entity", api->get_string(r, SK_RESOURCE_ASSET_FIELD_EXTENSION));
	TEST_ASSERT_EQUAL_STRING("Assets/Hero.entity", api->get_string(r, SK_RESOURCE_ASSET_FIELD_PATH_ID));
	TEST_ASSERT_EQUAL_STRING("/src/Hero.entity", api->get_string(r, SK_RESOURCE_ASSET_FIELD_SOURCE_PATH));
	TEST_ASSERT_EQUAL_INT(0, api->get_bool(r, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
	TEST_ASSERT_EQUAL_INT(1, api->get_bool(r, SK_RESOURCE_ASSET_FIELD_READ_ONLY));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_subobject(r, SK_RESOURCE_ASSET_FIELD_OBJECT).id);
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(r, SK_RESOURCE_ASSET_FIELD_PARENT).id);
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(r, SK_RESOURCE_ASSET_FIELD_ASSET_FILE).id);
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_subobject(r, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET).id);
	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_resource_asset_directory_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0x50u, 0x60u));
	/* empty lists / null subobject — just envelope + empty fields that were set */
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, NULL, 0u));
	TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, NULL, 0u));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_subobject(r, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET).id);
	u32 n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &n);
	TEST_ASSERT_EQUAL_UINT32(0u, n);
	n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &n);
	TEST_ASSERT_EQUAL_UINT32(0u, n);
	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_resource_imported_asset_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceImportedAsset", ser_uuid(0x70u, 0x80u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME, "tex.png"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION, ".png"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH, "abc"));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION, 3u));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE, 8192u));
	const u8 orig_data[] = {99u};
	TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA, orig_data, 1u));
	sk_type_id_t tid = SK_TEXTURE_RESOURCE_TYPE_ID;
	TEST_ASSERT_EQUAL_INT(0, api->set_type_id(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID, tid));
	/* Lists left empty; ImportSettings null — still durable on single-doc. */
	TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, NULL, 0u));
	TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, NULL, 0u));
	TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES, NULL, 0u));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("tex.png", api->get_string(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME));
	TEST_ASSERT_EQUAL_STRING(".png", api->get_string(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION));
	TEST_ASSERT_EQUAL_STRING("abc", api->get_string(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH));
	TEST_ASSERT_EQUAL_UINT64(3u, api->get_uint(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION));
	TEST_ASSERT_EQUAL_UINT64(8192u, api->get_uint(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE));
	{
		u32 size = 0u;
		const u8* data = api->get_buffer(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA, &size);
		TEST_ASSERT_EQUAL_UINT32(1u, size);
		TEST_ASSERT_NOT_NULL(data);
		TEST_ASSERT_EQUAL_UINT8(99u, data[0]);
	}
	sk_type_id_t got = api->get_type_id(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(got, tid));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_subobject(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORT_SETTINGS).id);
	u32 n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, &n);
	TEST_ASSERT_EQUAL_UINT32(0u, n);
	n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, &n);
	TEST_ASSERT_EQUAL_UINT32(0u, n);
	n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES, &n);
	TEST_ASSERT_EQUAL_UINT32(0u, n);
	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_sub_id_entry_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceSubIdEntry", ser_uuid(0x90u, 0xa0u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID, "mesh0"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID, "0000000000000001-0000000000000002"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME, "MeshResource"));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("mesh0", api->get_string(r, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID));
	TEST_ASSERT_EQUAL_STRING("0000000000000001-0000000000000002", api->get_string(r, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TARGET_UUID));
	TEST_ASSERT_EQUAL_STRING("MeshResource", api->get_string(r, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME));
	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_dependency_entry_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceDependencyEntry", ser_uuid(0xb0u, 0xc0u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH, "deps/a.png"));
	const u8 dep_data[] = {42u};
	TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA, dep_data, 1u));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE, 1024u));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("deps/a.png", api->get_string(r, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH));
	{
		u32 size = 0u;
		const u8* data = api->get_buffer(r, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA, &size);
		TEST_ASSERT_EQUAL_UINT32(1u, size);
		TEST_ASSERT_NOT_NULL(data);
		TEST_ASSERT_EQUAL_UINT8(42u, data[0]);
	}
	TEST_ASSERT_EQUAL_UINT64(1024u, api->get_uint(r, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE));
	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_extracted_entry_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = ser_create(repo, "ResourceExtractedEntry", ser_uuid(0xd0u, 0xe0u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_SOURCE_UUID, "000000000000000a-000000000000000b"));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_TARGET_UUID, "000000000000000c-000000000000000d"));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_KIND, 2u));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("000000000000000a-000000000000000b", api->get_string(r, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_SOURCE_UUID));
	TEST_ASSERT_EQUAL_STRING("000000000000000c-000000000000000d", api->get_string(r, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_TARGET_UUID));
	TEST_ASSERT_EQUAL_UINT64(2u, api->get_uint(r, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_KIND));
	a->free(a->instance, json);
	api->destroy(repo);
}

/* Builtin payload types: Name-only shells + Content/Blob variants. */
SK_TEST(resource_serialize_builtin_payloads_roundtrip) {
	static const_chr_t named_types[] = {
		"AnimationClipResource", "AnimationControllerResource", "CSharpScriptResource", "DCCAsset",			 "EntityResource",
		"FontResource",			 "MaterialGraphResource",		"MeshResource",			"SceneResource",	 "TextureResource",
		"TextureImportSettings", "FBXImportSettings",			"GLTFImportSettings",	"ObjImportSettings",
	};
	for (u32 i = 0u; i < (u32)(sizeof(named_types) / sizeof(named_types[0])); ++i) {
		ser_roundtrip_named(named_types[i], 0u, "payload");
	}

	/* Content-bearing types */
	static const_chr_t content_types[] = {"UIDocumentResource", "UIStyleResource", "ShaderResource"};
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	for (u32 i = 0u; i < (u32)(sizeof(content_types) / sizeof(content_types[0])); ++i) {
		sk_repository_t* repo = ser_test_repo();
		sk_rid_t rid = ser_create(repo, content_types[i], ser_uuid(0x1000u + i, 0x2000u + i));
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "doc"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 1u, "<rml/>"));
		api->commit(w, NULL);
		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
		api->destroy_resource(repo, rid, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		sk_resource_object_t r = api->read(repo, loaded);
		TEST_ASSERT_EQUAL_STRING("doc", api->get_string(r, 0u));
		TEST_ASSERT_EQUAL_STRING("<rml/>", api->get_string(r, 1u));
		a->free(a->instance, json);
		api->destroy(repo);
	}

	/* AudioResource with blob */
	{
		sk_repository_t* repo = ser_test_repo();
		sk_rid_t rid = ser_create(repo, "AudioResource", ser_uuid(0x3000u, 0x4000u));
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "sfx"));
		const u8 bytes[] = {1u, 2u, 3u, 4u};
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(w, 1u, bytes, 4u));
		api->commit(w, NULL);
		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
		api->destroy_resource(repo, rid, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		sk_resource_object_t r = api->read(repo, loaded);
		TEST_ASSERT_EQUAL_STRING("sfx", api->get_string(r, 0u));
		u32 size = 0u;
		const u8* got = api->get_blob(r, 1u, &size);
		TEST_ASSERT_EQUAL_UINT32(4u, size);
		TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, got, 4u);
		a->free(a->instance, json);
		api->destroy(repo);
	}
}

SK_TEST(resource_serialize_package_graph_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t package = ser_create(repo, "ResourceAssetPackage", ser_uuid(0xaaaa, 0xbbbb));
	sk_rid_t root_dir = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0xcccc, 0xdddd));
	sk_rid_t asset = ser_create(repo, "ResourceAsset", ser_uuid(0xeeee, 0xffff));
	sk_rid_t mesh = ser_create(repo, "MeshResource", ser_uuid(0x1111, 0x2222));

	{
		sk_resource_object_t w = api->write(repo, mesh);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "Cube"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "CubeAsset"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, mesh));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, root_dir);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &asset, 1u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "Pkg"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, root_dir));
		api->commit(w, NULL);
	}

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_package_json_alloc(repo, package, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(strstr(json, "sk.resource_package"));
	TEST_ASSERT_NOT_NULL(strstr(json, "resources"));
	TEST_ASSERT_NOT_NULL(strstr(json, "CubeAsset"));

	/* Destroy graph and reload into a fresh repository */
	api->destroy(repo);
	repo = ser_test_repo();

	sk_rid_t loaded_root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &loaded_root));
	TEST_ASSERT_TRUE(loaded_root.id != 0u);

	sk_resource_object_t pr = api->read(repo, loaded_root);
	TEST_ASSERT_EQUAL_STRING("Pkg", api->get_string(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	sk_rid_t loaded_dir = api->get_subobject(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
	TEST_ASSERT_TRUE(loaded_dir.id != 0u);
	sk_resource_object_t dr = api->read(repo, loaded_dir);
	u32 asset_count = 0u;
	const sk_rid_t* assets = api->get_subobject_list(dr, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &asset_count);
	TEST_ASSERT_EQUAL_UINT32(1u, asset_count);
	sk_resource_object_t ar = api->read(repo, assets[0]);
	TEST_ASSERT_EQUAL_STRING("CubeAsset", api->get_string(ar, SK_RESOURCE_ASSET_FIELD_NAME));
	sk_rid_t loaded_mesh = api->get_subobject(ar, SK_RESOURCE_ASSET_FIELD_OBJECT);
	TEST_ASSERT_TRUE(loaded_mesh.id != 0u);
	sk_resource_object_t mr = api->read(repo, loaded_mesh);
	TEST_ASSERT_EQUAL_STRING("Cube", api->get_string(mr, 0u));

	a->free(a->instance, json);
	api->destroy(repo);
}

/* ---- default-constructed, optional present/absent, edges ---- */

SK_TEST(resource_serialize_default_constructed_all_asset_types) {
	static const_chr_t types[] = {
		"ResourceAssetPackage",	   "ResourceAssetFile",
		"ResourceAsset",		   "ResourceAssetDirectory",
		"ResourceImportedAsset",   "ResourceSubIdEntry",
		"ResourceDependencyEntry", "ResourceExtractedEntry",
		"AnimationClipResource",   "AnimationControllerResource",
		"CSharpScriptResource",	   "DCCAsset",
		"EntityResource",		   "FontResource",
		"MaterialGraphResource",   "MeshResource",
		"SceneResource",		   "TextureResource",
		"TextureImportSettings",   "FBXImportSettings",
		"GLTFImportSettings",	   "ObjImportSettings",
		"UIDocumentResource",	   "UIStyleResource",
		"ShaderResource",		   "AudioResource",
	};
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	for (u32 i = 0u; i < (u32)(sizeof(types) / sizeof(types[0])); ++i) {
		sk_repository_t* repo = ser_test_repo();
		sk_rid_t rid = ser_create(repo, types[i], ser_uuid(0x5000u + i, 0x6000u + i));
		/* No field writes — pure defaults. */
		ser_assert_double_serialize_identity(repo, rid, a);
		api->destroy(repo);
	}
}

SK_TEST(resource_serialize_optional_fields_absent_defaults) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* fields present but empty object: all optional keys absent → soft defaults. */
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"ResourceAsset\",\n"
					   "  \"uuid\": \"0000000000000abc-0000000000000def\",\n"
					   "  \"fields\": {}\n"
					   "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
	TEST_ASSERT_TRUE(rid.id != 0u);
	sk_resource_object_t r = api->read(repo, rid);
	/* Unset strings may be NULL or empty; both mean soft default. */
	const_chr_t name = api->get_string(r, SK_RESOURCE_ASSET_FIELD_NAME);
	const_chr_t ext = api->get_string(r, SK_RESOURCE_ASSET_FIELD_EXTENSION);
	TEST_ASSERT_TRUE(name == NULL || name[0] == '\0');
	TEST_ASSERT_TRUE(ext == NULL || ext[0] == '\0');
	TEST_ASSERT_EQUAL_INT(0, api->get_bool(r, SK_RESOURCE_ASSET_FIELD_DIRECTORY));
	TEST_ASSERT_EQUAL_INT(0, api->get_bool(r, SK_RESOURCE_ASSET_FIELD_READ_ONLY));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_subobject(r, SK_RESOURCE_ASSET_FIELD_OBJECT).id);
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(r, SK_RESOURCE_ASSET_FIELD_PARENT).id);
	api->destroy(repo);
}

SK_TEST(resource_serialize_all_optional_fields_present_resource_asset) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t parent = ser_create(repo, "ResourceAsset", ser_uuid(0x7001u, 0x7002u));
	sk_rid_t file = ser_create(repo, "ResourceAssetFile", ser_uuid(0x7003u, 0x7004u));
	sk_rid_t mesh = ser_create(repo, "MeshResource", ser_uuid(0x7005u, 0x7006u));
	sk_rid_t imported = ser_create(repo, "ResourceImportedAsset", ser_uuid(0x7007u, 0x7008u));
	sk_rid_t asset = ser_create(repo, "ResourceAsset", ser_uuid(0x7009u, 0x700au));

	{
		sk_resource_object_t w = api->write(repo, mesh);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "Body"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, imported);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME, "body.fbx"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "Full"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, mesh));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, parent));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/Full.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, file));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_SOURCE_PATH, "/src/full.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_READ_ONLY, 1));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET, imported));
		api->commit(w, NULL);
	}

	/* Single-doc emit must include every present optional key. */
	char* single = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, asset, a, &single, NULL));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"Name\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"Extension\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"PathId\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"SourcePath\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"ReadOnly\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"Object\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"Parent\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"AssetFile\""));
	TEST_ASSERT_NOT_NULL(strstr(single, "\"ImportedAsset\""));
	a->free(a->instance, single);

	/* Package round-trip so UUID targets reload with the graph (destroying the
	 * asset alone may cascade owned subobjects and leave dangling single-doc refs). */
	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, asset, a, &json, NULL));
	api->destroy(repo);
	repo = ser_test_repo();

	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("Full", api->get_string(r, SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING(".mesh", api->get_string(r, SK_RESOURCE_ASSET_FIELD_EXTENSION));
	TEST_ASSERT_EQUAL_STRING("Assets/Full.mesh", api->get_string(r, SK_RESOURCE_ASSET_FIELD_PATH_ID));
	TEST_ASSERT_EQUAL_STRING("/src/full.mesh", api->get_string(r, SK_RESOURCE_ASSET_FIELD_SOURCE_PATH));
	TEST_ASSERT_EQUAL_INT(1, api->get_bool(r, SK_RESOURCE_ASSET_FIELD_READ_ONLY));

	sk_rid_t loaded_mesh = api->get_subobject(r, SK_RESOURCE_ASSET_FIELD_OBJECT);
	sk_rid_t loaded_parent = api->get_reference(r, SK_RESOURCE_ASSET_FIELD_PARENT);
	sk_rid_t loaded_file = api->get_reference(r, SK_RESOURCE_ASSET_FIELD_ASSET_FILE);
	sk_rid_t loaded_imp = api->get_subobject(r, SK_RESOURCE_ASSET_FIELD_IMPORTED_ASSET);
	TEST_ASSERT_TRUE(loaded_mesh.id != 0u);
	TEST_ASSERT_TRUE(loaded_parent.id != 0u);
	TEST_ASSERT_TRUE(loaded_file.id != 0u);
	TEST_ASSERT_TRUE(loaded_imp.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Body", api->get_string(api->read(repo, loaded_mesh), 0u));
	TEST_ASSERT_EQUAL_STRING("body.fbx", api->get_string(api->read(repo, loaded_imp), SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME));

	/* Package double-serialize identity */
	char* json2 = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, loaded, a, &json2, NULL));
	TEST_ASSERT_EQUAL_STRING(json, json2);

	a->free(a->instance, json);
	a->free(a->instance, json2);
	api->destroy(repo);
}

SK_TEST(resource_serialize_numeric_edge_values) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	const sk_resource_type_t* type = ser_register_trivial(repo);

	/* Zeroes */
	{
		sk_rid_t rid = api->create_resource(repo, type, ser_uuid(0x8001u, 1u), NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "z"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_SER_TRIVIAL_FIELD_FLAG, 0));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_SER_TRIVIAL_FIELD_COUNT, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(w, SK_SER_TRIVIAL_FIELD_SIGNED, 0));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SER_TRIVIAL_FIELD_RATIO, 0.0));
		api->commit(w, NULL);
		ser_assert_double_serialize_identity(repo, rid, a);
	}

	/* Negative int + negative float */
	{
		sk_rid_t rid = api->create_resource(repo, type, ser_uuid(0x8002u, 2u), NULL);
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "neg"));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(w, SK_SER_TRIVIAL_FIELD_SIGNED, (i64)(-9223372036854775807LL - 1LL))); /* INT64_MIN */
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SER_TRIVIAL_FIELD_RATIO, -1.25));
		api->commit(w, NULL);

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
		api->destroy_resource(repo, rid, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		sk_resource_object_t r = api->read(repo, loaded);
		TEST_ASSERT_EQUAL_INT64((i64)(-9223372036854775807LL - 1LL), api->get_int(r, SK_SER_TRIVIAL_FIELD_SIGNED));
		TEST_ASSERT_EQUAL_DOUBLE(-1.25, api->get_float(r, SK_SER_TRIVIAL_FIELD_RATIO));
		a->free(a->instance, json);
	}

	/* Very large uint (full 64-bit; JSON may lose precision above 2^53 — value still roundtrips via archive). */
	{
		sk_rid_t rid = api->create_resource(repo, type, ser_uuid(0x8003u, 3u), NULL);
		sk_resource_object_t w = api->write(repo, rid);
		const u64 big = 0xffffffffffffffffull;
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "big"));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_SER_TRIVIAL_FIELD_COUNT, big));
		TEST_ASSERT_EQUAL_INT(0, api->set_int(w, SK_SER_TRIVIAL_FIELD_SIGNED, 9223372036854775807LL)); /* INT64_MAX */
		api->commit(w, NULL);

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
		api->destroy_resource(repo, rid, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		sk_resource_object_t r = api->read(repo, loaded);
		/* yyjson preserves uint64 when written as integer. */
		TEST_ASSERT_EQUAL_UINT64(big, api->get_uint(r, SK_SER_TRIVIAL_FIELD_COUNT));
		TEST_ASSERT_EQUAL_INT64(9223372036854775807LL, api->get_int(r, SK_SER_TRIVIAL_FIELD_SIGNED));
		a->free(a->instance, json);
	}

	/* Asset-type uint edges on ResourceAssetFile */
	{
		sk_repository_t* arepo = ser_test_repo();
		sk_rid_t rid = ser_create(arepo, "ResourceAssetFile", ser_uuid(0x8004u, 4u));
		sk_resource_object_t w = api->write(arepo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK, 0xffffffffffffffffull));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME, 1u));
		api->commit(w, NULL);
		ser_assert_double_serialize_identity(arepo, rid, a);
		api->destroy(arepo);
	}

	api->destroy(repo);
}

SK_TEST(resource_serialize_float_nan_inf_behavior) {
	/*
	 * Standard JSON has no NaN/Infinity. yyjson emit without
	 * YYJSON_WRITE_ALLOW_INF_AND_NAN fails (empty emit) → serialize returns
	 * SK_RES_SER_ERR. Documented: non-finite floats are not portable through
	 * the JSON resource format.
	 */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	const sk_resource_type_t* type = ser_register_trivial(repo);

	sk_rid_t rid_nan = api->create_resource(repo, type, ser_uuid(0x8101u, 1u), NULL);
	{
		sk_resource_object_t w = api->write(repo, rid_nan);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "nan"));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SER_TRIVIAL_FIELD_RATIO, (f64)NAN));
		api->commit(w, NULL);
	}
	char* json_nan = NULL;
	i32 rc_nan = sk_resource_serialize_json_alloc(repo, rid_nan, a, &json_nan, NULL);
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, rc_nan);
	TEST_ASSERT_NULL(json_nan);

	sk_rid_t rid_inf = api->create_resource(repo, type, ser_uuid(0x8102u, 2u), NULL);
	{
		sk_resource_object_t w = api->write(repo, rid_inf);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "inf"));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SER_TRIVIAL_FIELD_RATIO, (f64)INFINITY));
		api->commit(w, NULL);
	}
	char* json_inf = NULL;
	i32 rc_inf = sk_resource_serialize_json_alloc(repo, rid_inf, a, &json_inf, NULL);
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, rc_inf);
	TEST_ASSERT_NULL(json_inf);

	/* Negative infinity same failure mode. */
	sk_rid_t rid_ninf = api->create_resource(repo, type, ser_uuid(0x8103u, 3u), NULL);
	{
		sk_resource_object_t w = api->write(repo, rid_ninf);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_SER_TRIVIAL_FIELD_LABEL, "ninf"));
		TEST_ASSERT_EQUAL_INT(0, api->set_float(w, SK_SER_TRIVIAL_FIELD_RATIO, (f64)(-INFINITY)));
		api->commit(w, NULL);
	}
	char* json_ninf = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_serialize_json_alloc(repo, rid_ninf, a, &json_ninf, NULL));
	TEST_ASSERT_NULL(json_ninf);

	api->destroy(repo);
}

SK_TEST(resource_serialize_empty_and_very_long_strings) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Empty string */
	{
		sk_rid_t rid = ser_create(repo, "ResourceAsset", ser_uuid(0x8201u, 1u));
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, ""));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, ""));
		api->commit(w, NULL);
		ser_assert_double_serialize_identity(repo, rid, a);
	}

	/* Very long string (> 4096) exercises heap path in string apply. */
	{
		enum { LONG_N = 8192 };
		char* long_s = (char*)a->alloc(a->instance, (size_t)LONG_N + 1u);
		TEST_ASSERT_NOT_NULL(long_s);
		for (u32 i = 0u; i < (u32)LONG_N; ++i) {
			long_s[i] = (char)('A' + (i % 26u));
		}
		long_s[LONG_N] = '\0';

		sk_rid_t rid = ser_create(repo, "ResourceAsset", ser_uuid(0x8202u, 2u));
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, long_s));
		api->commit(w, NULL);

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
		api->destroy_resource(repo, rid, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		TEST_ASSERT_EQUAL_STRING(long_s, api->get_string(api->read(repo, loaded), SK_RESOURCE_ASSET_FIELD_NAME));

		a->free(a->instance, json);
		a->free(a->instance, long_s);
	}

	api->destroy(repo);
}

SK_TEST(resource_serialize_unicode_and_json_escapes) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Quotes, backslash, control chars, unicode (CJK + emoji). */
	const_chr_t tricky = "quote\"backslash\\tab\there\nline\rcr / solidus \xC2\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80";
	sk_rid_t rid = ser_create(repo, "ResourceAsset", ser_uuid(0x8301u, 1u));
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, tricky));
	TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "path/with\"quotes\\and\nnewline"));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	/* Escaped forms must appear in emitted JSON (writer uses ESCAPE_UNICODE | pretty). */
	TEST_ASSERT_NOT_NULL(strstr(json, "\\\"")); /* escaped quote */
	TEST_ASSERT_NOT_NULL(strstr(json, "\\\\")); /* escaped backslash */
	TEST_ASSERT_NOT_NULL(strstr(json, "\\n"));	/* escaped newline */

	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_EQUAL_STRING(tricky, api->get_string(api->read(repo, loaded), SK_RESOURCE_ASSET_FIELD_NAME));

	/* Double-serialize identity after reload. */
	char* json2 = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, loaded, a, &json2, NULL));
	TEST_ASSERT_EQUAL_STRING(json, json2);

	a->free(a->instance, json);
	a->free(a->instance, json2);
	api->destroy(repo);
}

SK_TEST(resource_serialize_empty_and_nested_collections) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Empty SubObjectList */
	{
		sk_rid_t dir = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0x8401u, 1u));
		sk_resource_object_t w = api->write(repo, dir);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, NULL, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, NULL, 0u));
		api->commit(w, NULL);

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, dir, a, &json, NULL));
		TEST_ASSERT_NOT_NULL(strstr(json, "\"Directories\""));
		TEST_ASSERT_NOT_NULL(strstr(json, "\"Assets\""));
		api->destroy_resource(repo, dir, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		u32 dcount = 1u;
		u32 acount = 1u;
		(void)api->get_subobject_list(api->read(repo, loaded), SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &dcount);
		(void)api->get_subobject_list(api->read(repo, loaded), SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &acount);
		TEST_ASSERT_EQUAL_UINT32(0u, dcount);
		TEST_ASSERT_EQUAL_UINT32(0u, acount);
		a->free(a->instance, json);
	}

	/* Nested: package → root dir → child dir → assets list with two entries */
	{
		sk_rid_t package = ser_create(repo, "ResourceAssetPackage", ser_uuid(0x8410u, 0x10u));
		sk_rid_t root = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0x8411u, 0x11u));
		sk_rid_t child = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0x8412u, 0x12u));
		sk_rid_t a1 = ser_create(repo, "ResourceAsset", ser_uuid(0x8413u, 0x13u));
		sk_rid_t a2 = ser_create(repo, "ResourceAsset", ser_uuid(0x8414u, 0x14u));

		{
			sk_resource_object_t w = api->write(repo, a1);
			TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "A1"));
			api->commit(w, NULL);
		}
		{
			sk_resource_object_t w = api->write(repo, a2);
			TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "A2"));
			api->commit(w, NULL);
		}
		{
			sk_rid_t assets[2] = {a1, a2};
			sk_resource_object_t w = api->write(repo, child);
			TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, assets, 2u));
			api->commit(w, NULL);
		}
		{
			sk_resource_object_t w = api->write(repo, root);
			TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &child, 1u));
			api->commit(w, NULL);
		}
		{
			sk_resource_object_t w = api->write(repo, package);
			TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "NestedPkg"));
			TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, root));
			api->commit(w, NULL);
		}

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, package, a, &json, NULL));
		api->destroy(repo);
		repo = ser_test_repo();

		sk_rid_t loaded_root = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &loaded_root));
		sk_resource_object_t pr = api->read(repo, loaded_root);
		TEST_ASSERT_EQUAL_STRING("NestedPkg", api->get_string(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
		sk_rid_t loaded_dir = api->get_subobject(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
		u32 child_count = 0u;
		const sk_rid_t* children = api->get_subobject_list(api->read(repo, loaded_dir), SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &child_count);
		TEST_ASSERT_EQUAL_UINT32(1u, child_count);
		u32 asset_count = 0u;
		const sk_rid_t* assets = api->get_subobject_list(api->read(repo, children[0]), SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &asset_count);
		TEST_ASSERT_EQUAL_UINT32(2u, asset_count);
		TEST_ASSERT_EQUAL_STRING("A1", api->get_string(api->read(repo, assets[0]), SK_RESOURCE_ASSET_FIELD_NAME));
		TEST_ASSERT_EQUAL_STRING("A2", api->get_string(api->read(repo, assets[1]), SK_RESOURCE_ASSET_FIELD_NAME));

		/* Package double-serialize identity */
		char* json2 = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, loaded_root, a, &json2, NULL));
		TEST_ASSERT_EQUAL_STRING(json, json2);

		a->free(a->instance, json);
		a->free(a->instance, json2);
	}

	api->destroy(repo);
}

/* ---- Negative tests: assert specific documented failure codes ---- */
/*
 * SK_RES_SER_ERR (-1): parse failure / root not object / OOM
 * SK_RES_SER_INVALID (-2): bad/missing format, version, type, fields shape, bad UUID
 * SK_RES_SER_MISSING_REF (-3): package unresolved UUID target
 * SK_RES_SER_FIELD (-4): field Set failure
 * Unknown extra fields: ignored (success). Wrong scalar types: soft-default via archive.
 */

SK_TEST(resource_serialize_rejects_empty_input) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = (sk_rid_t){0xdeadu};
	/* Empty buffer → yyjson parse fail → SK_RES_SER_ERR; out_rid forced to zero. */
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_make("", 0u), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_whitespace_only_input) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = (sk_rid_t){1u};
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr("   \n\t  "), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_truncated_json) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = SK_RID_ZERO;
	const_chr_t truncated = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"ResourceAsset\"";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(truncated), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_array_root) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = SK_RID_ZERO;
	/* Syntactically valid JSON, wrong shape: array where object expected → reader init -1 → ERR. */
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr("[1,2,3]"), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	/* Bare null / string / number roots also rejected the same way. */
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr("null"), a, &rid));
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr("\"str\""), a, &rid));
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr("42"), a, &rid));
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_bad_format) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t bad = "{ \"format\": \"nope\", \"format_version\": 1, \"type\": \"ResourceAsset\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_missing_format) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t bad = "{ \"format_version\": 1, \"type\": \"ResourceAsset\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_unsupported_version) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t bad = "{ \"format\": \"sk.resource\", \"format_version\": 99, \"type\": \"ResourceAsset\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_missing_format_version) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	/* Absent format_version reads as 0 → INVALID (missing or zero). */
	const_chr_t bad = "{ \"format\": \"sk.resource\", \"type\": \"ResourceAsset\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	/* Explicit zero is the same failure. */
	const_chr_t zero = "{ \"format\": \"sk.resource\", \"format_version\": 0, \"type\": \"ResourceAsset\", \"fields\": {} }";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(zero), a, &rid));
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_unknown_type) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t bad = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"NotARealType\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_missing_type) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t bad = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_missing_fields) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	/* Required envelope key `fields` absent → SK_RES_SER_INVALID after create. */
	const_chr_t bad = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"ResourceAsset\" }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	/* out_rid may have been set then fail on fields; contract: non-zero status = fail-closed for callers. */
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_rejects_invalid_uuid_string) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	/* Envelope uuid with wrong shape → INVALID */
	const_chr_t bad_uuid = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"ResourceAsset\","
						   " \"uuid\": \"not-a-uuid\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad_uuid), a, &rid));

	/* Field reference with non-canonical UUID text → INVALID on parse */
	const_chr_t bad_ref = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"ResourceAsset\","
						  " \"uuid\": \"0000000000000001-0000000000000001\","
						  " \"fields\": { \"Parent\": \"zzzz\" } }";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad_ref), a, &rid));
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_wrong_field_types_soft_default) {
	/*
	 * Archive read_* soft-defaults wrong JSON types (not an error). Documented:
	 * string field fed a number → empty string; bool fed a string → false;
	 * uint fed a string → 0. Deserialize succeeds with soft defaults.
	 */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = api->create(a);
	TEST_ASSERT_NOT_NULL(repo);
	(void)ser_register_trivial(repo);

	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"SerTrivial\",\n"
					   "  \"uuid\": \"00000000000000c0-00000000000000de\",\n"
					   "  \"fields\": {\n"
					   "    \"Label\": 12345,\n"
					   "    \"Flag\": \"yes\",\n"
					   "    \"Count\": \"nope\",\n"
					   "    \"SignedValue\": true,\n"
					   "    \"Ratio\": \"x\"\n"
					   "  }\n"
					   "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
	TEST_ASSERT_TRUE(rid.id != 0u);
	sk_resource_object_t r = api->read(repo, rid);
	TEST_ASSERT_EQUAL_STRING("", api->get_string(r, SK_SER_TRIVIAL_FIELD_LABEL));
	TEST_ASSERT_EQUAL_INT(0, api->get_bool(r, SK_SER_TRIVIAL_FIELD_FLAG));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_uint(r, SK_SER_TRIVIAL_FIELD_COUNT));
	TEST_ASSERT_EQUAL_INT64(0, api->get_int(r, SK_SER_TRIVIAL_FIELD_SIGNED));
	TEST_ASSERT_EQUAL_DOUBLE(0.0, api->get_float(r, SK_SER_TRIVIAL_FIELD_RATIO));
	api->destroy(repo);
}

SK_TEST(resource_serialize_unknown_extra_fields_ignored) {
	/* Contract §3.5: unknown keys in fields are ignored (forward compatible). */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"ResourceAsset\",\n"
					   "  \"uuid\": \"00000000000000aa-00000000000000bb\",\n"
					   "  \"fields\": {\n"
					   "    \"Name\": \"ok\",\n"
					   "    \"TotallyUnknown\": { \"x\": 1 },\n"
					   "    \"AlsoUnknown\": [1, 2, 3]\n"
					   "  },\n"
					   "  \"extraEnvelope\": true\n"
					   "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
	TEST_ASSERT_EQUAL_STRING("ok", api->get_string(api->read(repo, rid), SK_RESOURCE_ASSET_FIELD_NAME));
	api->destroy(repo);
}

SK_TEST(resource_serialize_duplicate_keys_first_wins_in_fields) {
	/*
	 * Documented behavior with yyjson + named archive reads: apply_field_value
	 * re-looks up each field by name via yyjson_obj_get, which returns the
	 * *first* occurrence of a duplicate key. Deserializing duplicate "Name"
	 * keys therefore keeps "first", not "second". Must not fail the load.
	 */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"ResourceAsset\",\n"
					   "  \"uuid\": \"00000000000000d1-00000000000000d2\",\n"
					   "  \"fields\": {\n"
					   "    \"Name\": \"first\",\n"
					   "    \"Name\": \"second\"\n"
					   "  }\n"
					   "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
	TEST_ASSERT_EQUAL_STRING("first", api->get_string(api->read(repo, rid), SK_RESOURCE_ASSET_FIELD_NAME));
	api->destroy(repo);
}

SK_TEST(resource_serialize_rejects_blob_byte_out_of_range) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	/* Blob is array of 0..255; 300 is INVALID. */
	const_chr_t bad = "{\n"
					  "  \"format\": \"sk.resource\",\n"
					  "  \"format_version\": 1,\n"
					  "  \"type\": \"AudioResource\",\n"
					  "  \"uuid\": \"00000000000000b1-00000000000000b2\",\n"
					  "  \"fields\": { \"Name\": \"sfx\", \"Bytes\": [1, 300, 2] }\n"
					  "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad), a, &rid));
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_package_rejects_missing_uuid_target) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const u64 count_before = api->resource_count(repo);
	/* Package with a resource that references a UUID not in the document → MISSING_REF. */
	const_chr_t bad = "{\n"
					  "  \"format\": \"sk.resource_package\",\n"
					  "  \"format_version\": 1,\n"
					  "  \"root_uuid\": \"0000000000000001-0000000000000001\",\n"
					  "  \"resources\": [\n"
					  "    {\n"
					  "      \"format\": \"sk.resource\",\n"
					  "      \"format_version\": 1,\n"
					  "      \"type\": \"ResourceAsset\",\n"
					  "      \"uuid\": \"0000000000000001-0000000000000001\",\n"
					  "      \"fields\": { \"Object\": \"0000000000000099-0000000000000099\" }\n"
					  "    }\n"
					  "  ]\n"
					  "}";
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_MISSING_REF, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(bad), a, &root));
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);
	/* Failed package load must not leave partial shells (transactional undo). */
	TEST_ASSERT_EQUAL_UINT64(count_before, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(1u, 1u)).id == 0u);
	api->destroy(repo);
}

SK_TEST(resource_serialize_package_rejects_bad_format_and_version) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t root = SK_RID_ZERO;
	const_chr_t bad_fmt = "{ \"format\": \"nope\", \"format_version\": 1, \"root_uuid\": \"0000000000000001-0000000000000001\", \"resources\": [] }";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(bad_fmt), a, &root));
	const_chr_t bad_ver = "{ \"format\": \"sk.resource_package\", \"format_version\": 50, \"root_uuid\": \"0000000000000001-0000000000000001\", \"resources\": [] }";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(bad_ver), a, &root));
	const_chr_t empty_in = "";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(empty_in), a, &root));
	sk_repository_api()->destroy(repo);
}

SK_TEST(resource_serialize_imported_asset_all_fields_and_lists) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t sub = ser_create(repo, "ResourceSubIdEntry", ser_uuid(0x8501u, 1u));
	sk_rid_t dep = ser_create(repo, "ResourceDependencyEntry", ser_uuid(0x8502u, 2u));
	sk_rid_t ext = ser_create(repo, "ResourceExtractedEntry", ser_uuid(0x8503u, 3u));
	sk_rid_t settings = ser_create(repo, "TextureImportSettings", ser_uuid(0x8504u, 4u));
	sk_rid_t imp = ser_create(repo, "ResourceImportedAsset", ser_uuid(0x8505u, 5u));

	{
		sk_resource_object_t w = api->write(repo, sub);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_SUB_ID_ENTRY_FIELD_SUB_ID, "s0"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_SUB_ID_ENTRY_FIELD_TYPE_NAME, "MeshResource"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, dep);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH, "d.png"));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE, 0u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, ext);
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_EXTRACTED_ENTRY_FIELD_KIND, 0u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, settings);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "tex-settings"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, imp);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME, "a.png"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTENSION, ".png"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_CONTENT_HASH, ""));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORT_SETTINGS, settings));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA, NULL, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_SIZE, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, &sub, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, &dep, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES, &ext, 1u));
		sk_type_id_t tid = SK_TEXTURE_RESOURCE_TYPE_ID;
		TEST_ASSERT_EQUAL_INT(0, api->set_type_id(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID, tid));
		api->commit(w, NULL);
	}

	/* Use package graph so nested list UUID targets resolve. */
	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, imp, a, &json, NULL));
	api->destroy(repo);
	repo = ser_test_repo();
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("a.png", api->get_string(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME));
	u32 n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_SUB_RESOURCES, &n);
	TEST_ASSERT_EQUAL_UINT32(1u, n);
	n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_DEPENDENCIES, &n);
	TEST_ASSERT_EQUAL_UINT32(1u, n);
	n = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_EXTRACTED_RESOURCES, &n);
	TEST_ASSERT_EQUAL_UINT32(1u, n);

	char* json2 = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, loaded, a, &json2, NULL));
	TEST_ASSERT_EQUAL_STRING(json, json2);

	a->free(a->instance, json);
	a->free(a->instance, json2);
	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  Asset repository lifecycle via serialize (APX-189 integration)     */
/* ------------------------------------------------------------------ */

SK_TEST(resource_serialize_single_doc_missing_ref_is_zero) {
	/* Single-document policy: missing reference UUID → SK_RID_ZERO, success. */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"ResourceAsset\",\n"
					   "  \"uuid\": \"00000000000000aa-00000000000000bb\",\n"
					   "  \"fields\": {\n"
					   "    \"Name\": \"orphan-ref\",\n"
					   "    \"Parent\": \"0000000000000099-0000000000000099\"\n"
					   "  }\n"
					   "}";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
	TEST_ASSERT_TRUE(rid.id != 0u);
	sk_resource_object_t r = api->read(repo, rid);
	TEST_ASSERT_EQUAL_STRING("orphan-ref", api->get_string(r, SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(r, SK_RESOURCE_ASSET_FIELD_PARENT).id);
	api->destroy(repo);
}

SK_TEST(resource_serialize_self_reference_package) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	/* ResourceAsset.Parent is a soft REFERENCE; self-UUID is valid. */
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource_package\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"root_uuid\": \"00000000000000c1-00000000000000c1\",\n"
					   "  \"resources\": [\n"
					   "    {\n"
					   "      \"format\": \"sk.resource\",\n"
					   "      \"format_version\": 1,\n"
					   "      \"type\": \"ResourceAsset\",\n"
					   "      \"uuid\": \"00000000000000c1-00000000000000c1\",\n"
					   "      \"fields\": {\n"
					   "        \"Name\": \"self\",\n"
					   "        \"Parent\": \"00000000000000c1-00000000000000c1\"\n"
					   "      }\n"
					   "    }\n"
					   "  ]\n"
					   "}";
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &root));
	TEST_ASSERT_TRUE(root.id != 0u);
	sk_rid_t parent = api->get_reference(api->read(repo, root), SK_RESOURCE_ASSET_FIELD_PARENT);
	TEST_ASSERT_TRUE(SK_RID_EQ(parent, root));
	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  APX-194: encode + resolve handles / inter-asset references         */
/* ------------------------------------------------------------------ */

/**
 * Two assets that soft-reference each other (A.Parent=B, B.Parent=A).
 * Serialize as a package, destroy the repository, reload into a fresh instance.
 * UUIDs are stable; RIDs are session-local and must rematch via find_by_uuid.
 */
SK_TEST(resource_serialize_mutual_refs_fresh_repository) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* src = ser_test_repo();

	const sk_uuid_t uuid_a = ser_uuid(0xa1940001ull, 0xb1940001ull);
	const sk_uuid_t uuid_b = ser_uuid(0xa1940002ull, 0xb1940002ull);
	sk_rid_t a_rid = ser_create(src, "ResourceAsset", uuid_a);
	sk_rid_t b_rid = ser_create(src, "ResourceAsset", uuid_b);
	TEST_ASSERT_TRUE(a_rid.id != 0u);
	TEST_ASSERT_TRUE(b_rid.id != 0u);
	/* Capture session-local RIDs so we can prove they are not persisted. */
	const u64 src_a_id = a_rid.id;
	const u64 src_b_id = b_rid.id;

	{
		sk_resource_object_t w = api->write(src, a_rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "AssetA"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/A"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, b_rid));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, b_rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "AssetB"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/B"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, a_rid));
		api->commit(w, NULL);
	}

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, a_rid, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	/* On-disk form is UUID text, never the session RID integer. */
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a1940001-00000000b1940001"));
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a1940002-00000000b1940002"));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Parent\""));
	{
		char rid_needle[32];
		(void)snprintf(rid_needle, sizeof(rid_needle), "\"Parent\": %llu", (unsigned long long)src_a_id);
		TEST_ASSERT_NULL(strstr(json, rid_needle));
		(void)snprintf(rid_needle, sizeof(rid_needle), "\"Parent\": %llu", (unsigned long long)src_b_id);
		TEST_ASSERT_NULL(strstr(json, rid_needle));
	}

	api->destroy(src);

	/* Fresh repository instance: only the JSON snapshot is available. */
	sk_repository_t* dst = ser_test_repo();
	sk_rid_t loaded_root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_cstr(json), a, &loaded_root));
	TEST_ASSERT_TRUE(loaded_root.id != 0u);

	sk_rid_t loaded_a = api->find_by_uuid(dst, uuid_a);
	sk_rid_t loaded_b = api->find_by_uuid(dst, uuid_b);
	TEST_ASSERT_TRUE(loaded_a.id != 0u);
	TEST_ASSERT_TRUE(loaded_b.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(loaded_root, loaded_a));

	/* Mutual soft links re-resolve to live handles in the new session. */
	sk_rid_t a_parent = api->get_reference(api->read(dst, loaded_a), SK_RESOURCE_ASSET_FIELD_PARENT);
	sk_rid_t b_parent = api->get_reference(api->read(dst, loaded_b), SK_RESOURCE_ASSET_FIELD_PARENT);
	TEST_ASSERT_TRUE(SK_RID_EQ(a_parent, loaded_b));
	TEST_ASSERT_TRUE(SK_RID_EQ(b_parent, loaded_a));
	TEST_ASSERT_EQUAL_STRING("AssetA", api->get_string(api->read(dst, loaded_a), SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING("AssetB", api->get_string(api->read(dst, loaded_b), SK_RESOURCE_ASSET_FIELD_NAME));

	/* Re-serialize from the fresh repo; UUID graph identity is preserved. */
	char* json2 = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(dst, loaded_a, a, &json2, NULL));
	TEST_ASSERT_NOT_NULL(strstr(json2, "00000000a1940001-00000000b1940001"));
	TEST_ASSERT_NOT_NULL(strstr(json2, "00000000a1940002-00000000b1940002"));

	a->free(a->instance, json);
	a->free(a->instance, json2);
	api->destroy(dst);
}

/**
 * Hand-crafted package where resources[0] references resources[1] (forward
 * reference relative to load order). Two-pass create-then-resolve must succeed.
 */
SK_TEST(resource_serialize_forward_ref_later_in_load_order) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Root is listed first and points at a child that appears later in resources[]. */
	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource_package\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"root_uuid\": \"00000000a1941001-00000000b1941001\",\n"
					   "  \"resources\": [\n"
					   "    {\n"
					   "      \"format\": \"sk.resource\",\n"
					   "      \"format_version\": 1,\n"
					   "      \"type\": \"ResourceAsset\",\n"
					   "      \"uuid\": \"00000000a1941001-00000000b1941001\",\n"
					   "      \"fields\": {\n"
					   "        \"Name\": \"early\",\n"
					   "        \"Object\": \"00000000a1941002-00000000b1941002\",\n"
					   "        \"Parent\": \"00000000a1941003-00000000b1941003\"\n"
					   "      }\n"
					   "    },\n"
					   "    {\n"
					   "      \"format\": \"sk.resource\",\n"
					   "      \"format_version\": 1,\n"
					   "      \"type\": \"MeshResource\",\n"
					   "      \"uuid\": \"00000000a1941002-00000000b1941002\",\n"
					   "      \"fields\": { \"Name\": \"later-mesh\" }\n"
					   "    },\n"
					   "    {\n"
					   "      \"format\": \"sk.resource\",\n"
					   "      \"format_version\": 1,\n"
					   "      \"type\": \"ResourceAsset\",\n"
					   "      \"uuid\": \"00000000a1941003-00000000b1941003\",\n"
					   "      \"fields\": { \"Name\": \"later-parent\" }\n"
					   "    }\n"
					   "  ]\n"
					   "}";

	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &root));
	TEST_ASSERT_TRUE(root.id != 0u);

	sk_rid_t mesh = api->find_by_uuid(repo, ser_uuid(0xa1941002ull, 0xb1941002ull));
	sk_rid_t parent = api->find_by_uuid(repo, ser_uuid(0xa1941003ull, 0xb1941003ull));
	TEST_ASSERT_TRUE(mesh.id != 0u);
	TEST_ASSERT_TRUE(parent.id != 0u);

	sk_resource_object_t r = api->read(repo, root);
	TEST_ASSERT_EQUAL_STRING("early", api->get_string(r, SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(r, SK_RESOURCE_ASSET_FIELD_OBJECT), mesh));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(r, SK_RESOURCE_ASSET_FIELD_PARENT), parent));
	TEST_ASSERT_EQUAL_STRING("later-mesh", api->get_string(api->read(repo, mesh), 0u));
	TEST_ASSERT_EQUAL_STRING("later-parent", api->get_string(api->read(repo, parent), SK_RESOURCE_ASSET_FIELD_NAME));
	api->destroy(repo);
}

/**
 * Cross-type mutual soft links (ResourceAssetFile.AssetRef ↔ ResourceAsset.AssetFile)
 * plus Parent, saved and reloaded in a fresh repository — integration form of APX-194.
 */
SK_TEST(resource_serialize_cross_type_mutual_refs_fresh_repo) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* src = ser_test_repo();

	sk_rid_t asset = ser_create(src, "ResourceAsset", ser_uuid(0xa1942001ull, 0xb1942001ull));
	sk_rid_t file = ser_create(src, "ResourceAssetFile", ser_uuid(0xa1942002ull, 0xb1942002ull));
	sk_rid_t mesh = ser_create(src, "MeshResource", ser_uuid(0xa1942003ull, 0xb1942003ull));
	{
		sk_resource_object_t w = api->write(src, mesh);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "MutualMesh"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "MutualAsset"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, mesh));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, file));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, file);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF, asset));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH, "Assets/M.mesh"));
		api->commit(w, NULL);
	}

	char* json = NULL;
	/* Root at asset: collect_reachable must still pull in the soft-linked file. */
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, asset, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a1942001-00000000b1942001"));
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a1942002-00000000b1942002"));
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_cstr(json), a, &root));
	sk_rid_t loaded_asset = api->find_by_uuid(dst, ser_uuid(0xa1942001ull, 0xb1942001ull));
	sk_rid_t loaded_file = api->find_by_uuid(dst, ser_uuid(0xa1942002ull, 0xb1942002ull));
	sk_rid_t loaded_mesh = api->find_by_uuid(dst, ser_uuid(0xa1942003ull, 0xb1942003ull));
	TEST_ASSERT_TRUE(SK_RID_EQ(root, loaded_asset));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, loaded_asset), SK_RESOURCE_ASSET_FIELD_ASSET_FILE), loaded_file));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, loaded_file), SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF), loaded_asset));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(api->read(dst, loaded_asset), SK_RESOURCE_ASSET_FIELD_OBJECT), loaded_mesh));
	TEST_ASSERT_EQUAL_STRING("MutualMesh", api->get_string(api->read(dst, loaded_mesh), 0u));

	a->free(a->instance, json);
	api->destroy(dst);
}

/**
 * Explicit JSON null on a Reference field is SK_RID_ZERO (contract §3.5).
 * Package still hard-fails on a non-null UUID that does not resolve.
 */
SK_TEST(resource_serialize_null_ref_and_unresolvable_error_model) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();

	/* Single-doc: explicit null Parent → zero handle, success. */
	{
		sk_repository_t* repo = ser_test_repo();
		const_chr_t json = "{\n"
						   "  \"format\": \"sk.resource\",\n"
						   "  \"format_version\": 1,\n"
						   "  \"type\": \"ResourceAsset\",\n"
						   "  \"uuid\": \"00000000a1943001-00000000b1943001\",\n"
						   "  \"fields\": { \"Name\": \"null-parent\", \"Parent\": null }\n"
						   "}";
		sk_rid_t rid = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
		TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(api->read(repo, rid), SK_RESOURCE_ASSET_FIELD_PARENT).id);
		api->destroy(repo);
	}

	/* Package: unresolvable non-null UUID → MISSING_REF, no partial shells. */
	{
		sk_repository_t* repo = ser_test_repo();
		const u64 before = api->resource_count(repo);
		const_chr_t bad = "{\n"
						  "  \"format\": \"sk.resource_package\",\n"
						  "  \"format_version\": 1,\n"
						  "  \"root_uuid\": \"00000000a1943002-00000000b1943002\",\n"
						  "  \"resources\": [\n"
						  "    {\n"
						  "      \"format\": \"sk.resource\",\n"
						  "      \"format_version\": 1,\n"
						  "      \"type\": \"ResourceAsset\",\n"
						  "      \"uuid\": \"00000000a1943002-00000000b1943002\",\n"
						  "      \"fields\": {\n"
						  "        \"Name\": \"missing-target\",\n"
						  "        \"Parent\": \"00000000deadbeef-00000000deadbeef\"\n"
						  "      }\n"
						  "    }\n"
						  "  ]\n"
						  "}";
		sk_rid_t root = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_MISSING_REF, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(bad), a, &root));
		TEST_ASSERT_EQUAL_UINT64(0u, root.id);
		TEST_ASSERT_EQUAL_UINT64(before, api->resource_count(repo));
		TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(0xa1943002ull, 0xb1943002ull)).id == 0u);
		api->destroy(repo);
	}

	/* Single-doc dangling UUID → success with SK_RID_ZERO (not a silent partial type). */
	{
		sk_repository_t* repo = ser_test_repo();
		const_chr_t json = "{\n"
						   "  \"format\": \"sk.resource\",\n"
						   "  \"format_version\": 1,\n"
						   "  \"type\": \"ResourceAsset\",\n"
						   "  \"uuid\": \"00000000a1943003-00000000b1943003\",\n"
						   "  \"fields\": {\n"
						   "    \"Name\": \"dangling\",\n"
						   "    \"Parent\": \"00000000cafebabe-00000000cafebabe\"\n"
						   "  }\n"
						   "}";
		sk_rid_t rid = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &rid));
		TEST_ASSERT_TRUE(rid.id != 0u);
		TEST_ASSERT_EQUAL_UINT64(0u, api->get_reference(api->read(repo, rid), SK_RESOURCE_ASSET_FIELD_PARENT).id);
		api->destroy(repo);
	}
}

SK_TEST(resource_serialize_reload_same_uuid_reuses_rid) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t rid = ser_create(repo, "ResourceAsset", ser_uuid(0x6001u, 0x1u));
	{
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "before"));
		api->commit(w, NULL);
	}
	const u64 version_before = api->get_version(repo, rid);

	const_chr_t json = "{\n"
					   "  \"format\": \"sk.resource\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"type\": \"ResourceAsset\",\n"
					   "  \"uuid\": \"0000000000006001-0000000000000001\",\n"
					   "  \"fields\": { \"Name\": \"after-reload\" }\n"
					   "}";
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	/* UUID idempotent create: same live RID; holders observe new data. */
	TEST_ASSERT_TRUE(SK_RID_EQ(loaded, rid));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(repo));
	TEST_ASSERT_EQUAL_STRING("after-reload", api->get_string(api->read(repo, rid), SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_TRUE(api->get_version(repo, rid) > version_before);
	api->destroy(repo);
}

SK_TEST(resource_serialize_failed_package_does_not_partially_mutate) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Pre-existing resource must survive a failed package load that also
	 * creates shells before hitting MISSING_REF. */
	sk_rid_t keep = ser_create(repo, "ResourceAsset", ser_uuid(0x7001u, 0x1u));
	{
		sk_resource_object_t w = api->write(repo, keep);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "keep-me"));
		api->commit(w, NULL);
	}
	const u64 count_before = api->resource_count(repo);
	const u64 version_before = api->get_version(repo, keep);

	/* Two resources: first is valid scalars; second references missing UUID. */
	const_chr_t bad = "{\n"
					  "  \"format\": \"sk.resource_package\",\n"
					  "  \"format_version\": 1,\n"
					  "  \"root_uuid\": \"00000000000000f1-00000000000000f1\",\n"
					  "  \"resources\": [\n"
					  "    {\n"
					  "      \"format\": \"sk.resource\",\n"
					  "      \"format_version\": 1,\n"
					  "      \"type\": \"ResourceAsset\",\n"
					  "      \"uuid\": \"00000000000000f1-00000000000000f1\",\n"
					  "      \"fields\": { \"Name\": \"partial-a\" }\n"
					  "    },\n"
					  "    {\n"
					  "      \"format\": \"sk.resource\",\n"
					  "      \"format_version\": 1,\n"
					  "      \"type\": \"ResourceAsset\",\n"
					  "      \"uuid\": \"00000000000000f2-00000000000000f2\",\n"
					  "      \"fields\": {\n"
					  "        \"Name\": \"partial-b\",\n"
					  "        \"Parent\": \"00000000000000ff-00000000000000ff\"\n"
					  "      }\n"
					  "    }\n"
					  "  ]\n"
					  "}";
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_MISSING_REF, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(bad), a, &root));
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);
	TEST_ASSERT_EQUAL_UINT64(count_before, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(0xf1u, 0xf1u)).id == 0u);
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(0xf2u, 0xf2u)).id == 0u);
	TEST_ASSERT_TRUE(api->has_resource(repo, keep));
	TEST_ASSERT_EQUAL_STRING("keep-me", api->get_string(api->read(repo, keep), SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_UINT64(version_before, api->get_version(repo, keep));
	api->destroy(repo);
}

SK_TEST(resource_serialize_failed_single_doc_rolls_back_new_shell) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	const u64 count_before = api->resource_count(repo);

	/* Unknown type fails before create — still assert empty. */
	const_chr_t bad_type = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"NoSuchType\", \"uuid\": \"00000000000000e1-00000000000000e1\", \"fields\": {} }";
	sk_rid_t rid = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad_type), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	TEST_ASSERT_EQUAL_UINT64(count_before, api->resource_count(repo));

	/* Bad blob byte out of range fails after create → shell destroyed. */
	const_chr_t bad_blob = "{\n"
						   "  \"format\": \"sk.resource\",\n"
						   "  \"format_version\": 1,\n"
						   "  \"type\": \"AudioResource\",\n"
						   "  \"uuid\": \"00000000000000e2-00000000000000e2\",\n"
						   "  \"fields\": { \"Name\": \"x\", \"Bytes\": [999] }\n"
						   "}";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(bad_blob), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);
	TEST_ASSERT_EQUAL_UINT64(count_before, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(0xe2u, 0xe2u)).id == 0u);
	api->destroy(repo);
}

SK_TEST(resource_serialize_package_resources_order_bfs) {
	/* Contract: package resources[] is BFS from the root (root first). */
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t child = ser_create(repo, "ResourceAsset", ser_uuid(0x8102u, 2u));
	sk_rid_t parent = ser_create(repo, "ResourceAsset", ser_uuid(0x8101u, 1u));
	{
		sk_resource_object_t w = api->write(repo, child);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "child"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, parent);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "parent"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, child));
		api->commit(w, NULL);
	}

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, parent, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	/* Root appears before its reachable child in the flat resources array. */
	const char* p_parent = strstr(json, "0000000000008101-0000000000000001");
	const char* p_child = strstr(json, "0000000000008102-0000000000000002");
	TEST_ASSERT_NOT_NULL(p_parent);
	TEST_ASSERT_NOT_NULL(p_child);
	TEST_ASSERT_TRUE(p_parent < p_child);

	a->free(a->instance, json);
	api->destroy(repo);
}

/* ------------------------------------------------------------------ */
/*  APX-193: every field of every asset type + contract schema keys     */
/* ------------------------------------------------------------------ */

/** Assert @p json contains `"FieldName"` as a JSON object key (PascalCase). */
static void ser_assert_json_has_field_key(const char* json, const_chr_t field_name) {
	char needle[128];
	i32 n = snprintf(needle, sizeof(needle), "\"%s\"", field_name);
	TEST_ASSERT_TRUE(n > 0 && (u32)n < (u32)sizeof(needle));
	TEST_ASSERT_NOT_NULL(strstr(json, needle));
}

/**
 * Set every non-NONE field on @p rid to a non-default value so serialize emits
 * the full schema. Cross-resource edges point at @p peer when non-zero.
 */
static void ser_fill_all_fields(sk_repository_t* repo, sk_rid_t rid, sk_rid_t peer) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = api->resource_type(repo, rid);
	TEST_ASSERT_NOT_NULL(type);
	sk_resource_object_t w = api->write(repo, rid);
	TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(w));
	u32 field_count = api->type_field_count(type);
	for (u32 fi = 0u; fi < field_count; ++fi) {
		const sk_resource_field_t* field = api->type_field_at(type, fi);
		TEST_ASSERT_NOT_NULL(field);
		switch (field->type) {
		case SK_RESOURCE_FIELD_TYPE_NONE:
			break;
		case SK_RESOURCE_FIELD_TYPE_BOOL:
			TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, field->index, 1));
			break;
		case SK_RESOURCE_FIELD_TYPE_INT:
			TEST_ASSERT_EQUAL_INT(0, api->set_int(w, field->index, -42));
			break;
		case SK_RESOURCE_FIELD_TYPE_UINT:
			TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, field->index, 7u));
			break;
		case SK_RESOURCE_FIELD_TYPE_FLOAT:
			TEST_ASSERT_EQUAL_INT(0, api->set_float(w, field->index, 1.25));
			break;
		case SK_RESOURCE_FIELD_TYPE_STRING:
			TEST_ASSERT_EQUAL_INT(0, api->set_string(w, field->index, "filled"));
			break;
		case SK_RESOURCE_FIELD_TYPE_BLOB: {
			const u8 bytes[] = {9u, 8u, 7u};
			TEST_ASSERT_EQUAL_INT(0, api->set_blob(w, field->index, bytes, 3u));
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_BUFFER: {
			const u8 bytes[] = {55u, 54u, 53u};
			TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, field->index, bytes, 3u));
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID: {
			sk_type_id_t tid = SK_TEXTURE_RESOURCE_TYPE_ID;
			TEST_ASSERT_EQUAL_INT(0, api->set_type_id(w, field->index, tid));
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_REFERENCE:
			if (peer.id != 0u) {
				TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, field->index, peer));
			}
			break;
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT:
			if (peer.id != 0u) {
				TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, field->index, peer));
			}
			break;
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY:
			if (peer.id != 0u) {
				TEST_ASSERT_EQUAL_INT(0, api->set_reference_array(w, field->index, &peer, 1u));
			} else {
				TEST_ASSERT_EQUAL_INT(0, api->set_reference_array(w, field->index, NULL, 0u));
			}
			break;
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST:
			if (peer.id != 0u) {
				TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, field->index, &peer, 1u));
			} else {
				TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, field->index, NULL, 0u));
			}
			break;
		case SK_RESOURCE_FIELD_TYPE_VEC2:
		case SK_RESOURCE_FIELD_TYPE_VEC3:
		case SK_RESOURCE_FIELD_TYPE_VEC4:
		case SK_RESOURCE_FIELD_TYPE_QUAT:
		case SK_RESOURCE_FIELD_TYPE_MAT4:
		case SK_RESOURCE_FIELD_TYPE_COLOR:
		case SK_RESOURCE_FIELD_TYPE_ENUM:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			/* Not used by asset types under APX-186. */
			break;
		}
	}
	api->commit(w, NULL);
}

/**
 * Serialize @p rid and assert every non-NONE field with has_value appears as a
 * JSON key. Zero refs are omitted by contract.
 */
static void ser_assert_schema_keys(sk_repository_t* repo, sk_rid_t rid, const char* json) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type = api->resource_type(repo, rid);
	TEST_ASSERT_NOT_NULL(type);
	TEST_ASSERT_NOT_NULL(strstr(json, "\"format\""));
	TEST_ASSERT_NOT_NULL(strstr(json, SK_RESOURCE_JSON_FORMAT));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"format_version\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"fields\""));
	TEST_ASSERT_NOT_NULL(strstr(json, api->type_name(type)));

	sk_resource_object_t view = api->read(repo, rid);
	u32 field_count = api->type_field_count(type);
	for (u32 fi = 0u; fi < field_count; ++fi) {
		const sk_resource_field_t* field = api->type_field_at(type, fi);
		TEST_ASSERT_NOT_NULL(field);
		if (field->type == SK_RESOURCE_FIELD_TYPE_NONE) {
			continue;
		}
		if (!api->has_value_on_this_object(view, field->index)) {
			continue;
		}
		if ((field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE || field->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT)) {
			sk_rid_t child = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE) ? api->get_reference(view, field->index) : api->get_subobject(view, field->index);
			if (child.id == 0u) {
				continue;
			}
		}
		ser_assert_json_has_field_key(json, field->name);
	}
}

/**
 * For every APX-186 asset type: fill all settable fields, serialize, assert
 * contract schema keys, deserialize (package when refs present), re-serialize
 * identity. Buffer is a byte-array payload (v2); Type/NONE is omitted.
 */
SK_TEST(resource_serialize_every_asset_type_all_fields_schema) {
	static const_chr_t types[] = {
		"ResourceAssetPackage",	   "ResourceAssetFile",
		"ResourceAsset",		   "ResourceAssetDirectory",
		"ResourceImportedAsset",   "ResourceSubIdEntry",
		"ResourceDependencyEntry", "ResourceExtractedEntry",
		"AnimationClipResource",   "AnimationControllerResource",
		"CSharpScriptResource",	   "DCCAsset",
		"EntityResource",		   "FontResource",
		"MaterialGraphResource",   "MeshResource",
		"SceneResource",		   "TextureResource",
		"TextureImportSettings",   "FBXImportSettings",
		"GLTFImportSettings",	   "ObjImportSettings",
		"UIDocumentResource",	   "UIStyleResource",
		"ShaderResource",		   "AudioResource",
	};
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();

	for (u32 i = 0u; i < (u32)(sizeof(types) / sizeof(types[0])); ++i) {
		sk_repository_t* repo = ser_test_repo();
		sk_rid_t peer = ser_create(repo, "MeshResource", ser_uuid(0x9000u + i, 0x1u));
		{
			sk_resource_object_t w = api->write(repo, peer);
			TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "peer"));
			api->commit(w, NULL);
		}

		sk_rid_t rid = ser_create(repo, types[i], ser_uuid(0xa000u + i, 0x2u));
		const i32 needs_peer = (strcmp(types[i], "ResourceAssetPackage") == 0 || strcmp(types[i], "ResourceAssetFile") == 0 || strcmp(types[i], "ResourceAsset") == 0 ||
								strcmp(types[i], "ResourceAssetDirectory") == 0 || strcmp(types[i], "ResourceImportedAsset") == 0) ?
								   1 :
								   0;
		ser_fill_all_fields(repo, rid, needs_peer ? peer : SK_RID_ZERO);

		char* json = NULL;
		if (needs_peer) {
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, rid, a, &json, NULL));
			TEST_ASSERT_NOT_NULL(strstr(json, types[i]));
			ser_assert_schema_keys(repo, rid, json);
			api->destroy(repo);
			repo = ser_test_repo();
			sk_rid_t loaded = SK_RID_ZERO;
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &loaded));
			TEST_ASSERT_TRUE(loaded.id != 0u);
			char* json2 = NULL;
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, loaded, a, &json2, NULL));
			TEST_ASSERT_EQUAL_STRING(json, json2);
			a->free(a->instance, json2);
		} else {
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
			ser_assert_schema_keys(repo, rid, json);
			ser_assert_double_serialize_identity(repo, rid, a);
		}

		if (strcmp(types[i], "ResourceDependencyEntry") == 0) {
			/* Buffer fields serialize as byte arrays (v2 payload), not {"id":…}. */
			TEST_ASSERT_NOT_NULL(strstr(json, "\"Data\""));
			TEST_ASSERT_NOT_NULL(strstr(json, "55"));
		}
		if (strcmp(types[i], "ResourceImportedAsset") == 0) {
			TEST_ASSERT_NOT_NULL(strstr(json, "\"OriginalData\""));
			TEST_ASSERT_NOT_NULL(strstr(json, "55"));
		}
		if (strcmp(types[i], "AudioResource") == 0) {
			TEST_ASSERT_NOT_NULL(strstr(json, "\"Bytes\""));
			TEST_ASSERT_NOT_NULL(strstr(json, "9"));
		}

		a->free(a->instance, json);
		api->destroy(repo);
	}
}

/* ------------------------------------------------------------------ */
/*  APX-195: field-by-field round-trip for every asset type            */
/* ------------------------------------------------------------------ */

/** NULL and empty string are both soft defaults for absent String fields. */
static void ser_assert_string_soft_eq(const_chr_t a, const_chr_t b) {
	const i32 a_empty = (a == NULL || a[0] == '\0') ? 1 : 0;
	const i32 b_empty = (b == NULL || b[0] == '\0') ? 1 : 0;
	if (a_empty && b_empty) {
		return;
	}
	TEST_ASSERT_NOT_NULL(a);
	TEST_ASSERT_NOT_NULL(b);
	TEST_ASSERT_EQUAL_STRING(a, b);
}

/** UUID of a live rid, or SK_UUID_ZERO when rid is zero / missing. */
static sk_uuid_t ser_rid_uuid(sk_repository_t* repo, sk_rid_t rid) {
	if (rid.id == 0u) {
		return SK_UUID_ZERO;
	}
	return sk_repository_api()->resource_uuid(repo, rid);
}

/**
 * Field-by-field equality of two resources of the same type (APX-195).
 * Scalar / blob / buffer / type_id compared by value. Cross-resource edges
 * (Reference, SubObject, arrays, lists) compare target UUIDs, not session RIDs.
 * NONE fields are skipped (not represented in JSON).
 */
static void ser_assert_fields_equal(sk_repository_t* repo_a, sk_rid_t rid_a, sk_repository_t* repo_b, sk_rid_t rid_b) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_resource_type_t* type_a = api->resource_type(repo_a, rid_a);
	const sk_resource_type_t* type_b = api->resource_type(repo_b, rid_b);
	TEST_ASSERT_NOT_NULL(type_a);
	TEST_ASSERT_NOT_NULL(type_b);
	TEST_ASSERT_EQUAL_STRING(api->type_name(type_a), api->type_name(type_b));

	sk_uuid_t uuid_a = api->resource_uuid(repo_a, rid_a);
	sk_uuid_t uuid_b = api->resource_uuid(repo_b, rid_b);
	TEST_ASSERT_EQUAL_UINT64(uuid_a.lo, uuid_b.lo);
	TEST_ASSERT_EQUAL_UINT64(uuid_a.hi, uuid_b.hi);

	sk_resource_object_t va = api->read(repo_a, rid_a);
	sk_resource_object_t vb = api->read(repo_b, rid_b);
	u32 field_count = api->type_field_count(type_a);
	TEST_ASSERT_EQUAL_UINT32(field_count, api->type_field_count(type_b));

	for (u32 fi = 0u; fi < field_count; ++fi) {
		const sk_resource_field_t* field = api->type_field_at(type_a, fi);
		TEST_ASSERT_NOT_NULL(field);
		switch (field->type) {
		case SK_RESOURCE_FIELD_TYPE_NONE:
			break;
		case SK_RESOURCE_FIELD_TYPE_BOOL:
			TEST_ASSERT_EQUAL_INT(api->get_bool(va, field->index), api->get_bool(vb, field->index));
			break;
		case SK_RESOURCE_FIELD_TYPE_INT:
			TEST_ASSERT_EQUAL_INT64(api->get_int(va, field->index), api->get_int(vb, field->index));
			break;
		case SK_RESOURCE_FIELD_TYPE_UINT:
			TEST_ASSERT_EQUAL_UINT64(api->get_uint(va, field->index), api->get_uint(vb, field->index));
			break;
		case SK_RESOURCE_FIELD_TYPE_FLOAT:
			TEST_ASSERT_EQUAL_DOUBLE(api->get_float(va, field->index), api->get_float(vb, field->index));
			break;
		case SK_RESOURCE_FIELD_TYPE_STRING:
			ser_assert_string_soft_eq(api->get_string(va, field->index), api->get_string(vb, field->index));
			break;
		case SK_RESOURCE_FIELD_TYPE_BLOB: {
			u32 sa = 0u;
			u32 sb = 0u;
			const u8* ba = api->get_blob(va, field->index, &sa);
			const u8* bb = api->get_blob(vb, field->index, &sb);
			TEST_ASSERT_EQUAL_UINT32(sa, sb);
			if (sa > 0u) {
				TEST_ASSERT_NOT_NULL(ba);
				TEST_ASSERT_NOT_NULL(bb);
				TEST_ASSERT_EQUAL_UINT8_ARRAY(ba, bb, sa);
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_BUFFER: {
			u32 sa = 0u;
			u32 sb = 0u;
			const u8* ba = api->get_buffer(va, field->index, &sa);
			const u8* bb = api->get_buffer(vb, field->index, &sb);
			TEST_ASSERT_EQUAL_UINT32(sa, sb);
			if (sa > 0u) {
				TEST_ASSERT_NOT_NULL(ba);
				TEST_ASSERT_NOT_NULL(bb);
				TEST_ASSERT_EQUAL_UINT8_ARRAY(ba, bb, sa);
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_TYPE_ID: {
			sk_type_id_t ta = api->get_type_id(va, field->index);
			sk_type_id_t tb = api->get_type_id(vb, field->index);
			TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(ta, tb));
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_REFERENCE: {
			sk_uuid_t ua = ser_rid_uuid(repo_a, api->get_reference(va, field->index));
			sk_uuid_t ub = ser_rid_uuid(repo_b, api->get_reference(vb, field->index));
			TEST_ASSERT_TRUE(SK_UUID_EQ(ua, ub));
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT: {
			sk_uuid_t ua = ser_rid_uuid(repo_a, api->get_subobject(va, field->index));
			sk_uuid_t ub = ser_rid_uuid(repo_b, api->get_subobject(vb, field->index));
			TEST_ASSERT_TRUE(SK_UUID_EQ(ua, ub));
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY: {
			u32 ca = 0u;
			u32 cb = 0u;
			const sk_rid_t* ra = api->get_reference_array(va, field->index, &ca);
			const sk_rid_t* rb = api->get_reference_array(vb, field->index, &cb);
			TEST_ASSERT_EQUAL_UINT32(ca, cb);
			for (u32 i = 0u; i < ca; ++i) {
				sk_uuid_t ua = ser_rid_uuid(repo_a, ra != NULL ? ra[i] : SK_RID_ZERO);
				sk_uuid_t ub = ser_rid_uuid(repo_b, rb != NULL ? rb[i] : SK_RID_ZERO);
				TEST_ASSERT_TRUE(SK_UUID_EQ(ua, ub));
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST: {
			u32 ca = 0u;
			u32 cb = 0u;
			const sk_rid_t* ra = api->get_subobject_list(va, field->index, &ca);
			const sk_rid_t* rb = api->get_subobject_list(vb, field->index, &cb);
			TEST_ASSERT_EQUAL_UINT32(ca, cb);
			for (u32 i = 0u; i < ca; ++i) {
				sk_uuid_t ua = ser_rid_uuid(repo_a, ra != NULL ? ra[i] : SK_RID_ZERO);
				sk_uuid_t ub = ser_rid_uuid(repo_b, rb != NULL ? rb[i] : SK_RID_ZERO);
				TEST_ASSERT_TRUE(SK_UUID_EQ(ua, ub));
			}
			break;
		}
		case SK_RESOURCE_FIELD_TYPE_VEC2:
		case SK_RESOURCE_FIELD_TYPE_VEC3:
		case SK_RESOURCE_FIELD_TYPE_VEC4:
		case SK_RESOURCE_FIELD_TYPE_QUAT:
		case SK_RESOURCE_FIELD_TYPE_MAT4:
		case SK_RESOURCE_FIELD_TYPE_COLOR:
		case SK_RESOURCE_FIELD_TYPE_ENUM:
		case SK_RESOURCE_FIELD_TYPE_MAX:
			/* Not used by APX-186 asset types. */
			break;
		}
	}
}

/**
 * APX-195: for every registered asset type, populate all settable fields,
 * serialize, deserialize into a second repository, and assert field-by-field
 * equality (scalars by value; refs by UUID). Types with cross-resource edges
 * use package documents so targets resolve; pure scalar types use single-doc.
 *
 * Does not weaken assertions: if a type fails round-trip, the test fails.
 */
SK_TEST(resource_serialize_every_asset_type_field_by_field_roundtrip) {
	static const_chr_t types[] = {
		"ResourceAssetPackage",	   "ResourceAssetFile",
		"ResourceAsset",		   "ResourceAssetDirectory",
		"ResourceImportedAsset",   "ResourceSubIdEntry",
		"ResourceDependencyEntry", "ResourceExtractedEntry",
		"AnimationClipResource",   "AnimationControllerResource",
		"CSharpScriptResource",	   "DCCAsset",
		"EntityResource",		   "FontResource",
		"MaterialGraphResource",   "MeshResource",
		"SceneResource",		   "TextureResource",
		"TextureImportSettings",   "FBXImportSettings",
		"GLTFImportSettings",	   "ObjImportSettings",
		"UIDocumentResource",	   "UIStyleResource",
		"ShaderResource",		   "AudioResource",
	};
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();

	for (u32 i = 0u; i < (u32)(sizeof(types) / sizeof(types[0])); ++i) {
		sk_repository_t* repo_src = ser_test_repo();
		sk_rid_t peer = ser_create(repo_src, "MeshResource", ser_uuid(0xd000u + i, 0x1u));
		{
			sk_resource_object_t w = api->write(repo_src, peer);
			TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "peer"));
			api->commit(w, NULL);
		}

		sk_uuid_t subject_uuid = ser_uuid(0xe000u + i, 0x2u);
		sk_rid_t rid = ser_create(repo_src, types[i], subject_uuid);
		const i32 needs_peer = (strcmp(types[i], "ResourceAssetPackage") == 0 || strcmp(types[i], "ResourceAssetFile") == 0 || strcmp(types[i], "ResourceAsset") == 0 ||
								strcmp(types[i], "ResourceAssetDirectory") == 0 || strcmp(types[i], "ResourceImportedAsset") == 0) ?
								   1 :
								   0;
		ser_fill_all_fields(repo_src, rid, needs_peer ? peer : SK_RID_ZERO);

		char* json = NULL;
		if (needs_peer) {
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo_src, rid, a, &json, NULL));
		} else {
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo_src, rid, a, &json, NULL));
		}
		TEST_ASSERT_NOT_NULL(json);

		sk_repository_t* repo_dst = ser_test_repo();
		sk_rid_t loaded = SK_RID_ZERO;
		if (needs_peer) {
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo_dst, sk_str_view_cstr(json), a, &loaded));
		} else {
			TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo_dst, sk_str_view_cstr(json), a, &loaded));
		}
		TEST_ASSERT_TRUE(loaded.id != 0u);

		/* Package and single-doc loads both restore the subject UUID as the root/out rid. */
		sk_rid_t by_uuid = api->find_by_uuid(repo_dst, subject_uuid);
		TEST_ASSERT_TRUE(by_uuid.id != 0u);
		TEST_ASSERT_TRUE(SK_RID_EQ(by_uuid, loaded));

		ser_assert_fields_equal(repo_src, rid, repo_dst, by_uuid);

		/* Nested peer also round-trips when present in the package graph. */
		if (needs_peer) {
			sk_uuid_t peer_uuid = ser_uuid(0xd000u + i, 0x1u);
			sk_rid_t loaded_peer = api->find_by_uuid(repo_dst, peer_uuid);
			TEST_ASSERT_TRUE(loaded_peer.id != 0u);
			ser_assert_fields_equal(repo_src, peer, repo_dst, loaded_peer);
		}

		a->free(a->instance, json);
		api->destroy(repo_dst);
		api->destroy(repo_src);
	}
}

/**
 * APX-195: default-constructed (no field writes) round-trip field-by-field for
 * every asset type — complements the populated case above.
 */
SK_TEST(resource_serialize_every_asset_type_default_field_by_field) {
	static const_chr_t types[] = {
		"ResourceAssetPackage",	   "ResourceAssetFile",
		"ResourceAsset",		   "ResourceAssetDirectory",
		"ResourceImportedAsset",   "ResourceSubIdEntry",
		"ResourceDependencyEntry", "ResourceExtractedEntry",
		"AnimationClipResource",   "AnimationControllerResource",
		"CSharpScriptResource",	   "DCCAsset",
		"EntityResource",		   "FontResource",
		"MaterialGraphResource",   "MeshResource",
		"SceneResource",		   "TextureResource",
		"TextureImportSettings",   "FBXImportSettings",
		"GLTFImportSettings",	   "ObjImportSettings",
		"UIDocumentResource",	   "UIStyleResource",
		"ShaderResource",		   "AudioResource",
	};
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();

	for (u32 i = 0u; i < (u32)(sizeof(types) / sizeof(types[0])); ++i) {
		sk_repository_t* repo_src = ser_test_repo();
		sk_uuid_t uuid = ser_uuid(0xf000u + i, 0x3u);
		sk_rid_t rid = ser_create(repo_src, types[i], uuid);
		/* No field writes — pure defaults. */

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo_src, rid, a, &json, NULL));
		TEST_ASSERT_NOT_NULL(json);

		sk_repository_t* repo_dst = ser_test_repo();
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo_dst, sk_str_view_cstr(json), a, &loaded));
		TEST_ASSERT_TRUE(loaded.id != 0u);
		ser_assert_fields_equal(repo_src, rid, repo_dst, loaded);

		a->free(a->instance, json);
		api->destroy(repo_dst);
		api->destroy(repo_src);
	}
}

/**
 * APX-195 error-model anchors: malformed JSON and unsupported format_version
 * must return the contract codes (not success / partial load).
 */
SK_TEST(resource_serialize_apx195_malformed_and_wrong_version) {
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t rid = (sk_rid_t){0xbadu};

	/* Malformed / truncated JSON → SK_RES_SER_ERR, out_rid cleared. */
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_ERR, sk_resource_deserialize_json_string(repo, sk_str_view_cstr("{ \"format\": \"sk.resource\""), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);

	/* Wrong / unsupported major format_version → SK_RES_SER_INVALID. */
	rid = (sk_rid_t){0xbadu};
	const_chr_t wrong_ver = "{ \"format\": \"sk.resource\", \"format_version\": 99, \"type\": \"MeshResource\", \"fields\": {} }";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_INVALID, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(wrong_ver), a, &rid));
	TEST_ASSERT_EQUAL_UINT64(0u, rid.id);

	/* Optional/absent fields: empty fields object soft-defaults without error. */
	rid = SK_RID_ZERO;
	const_chr_t absent = "{ \"format\": \"sk.resource\", \"format_version\": 1, \"type\": \"MeshResource\","
						 "  \"uuid\": \"0000000000000f01-0000000000000004\", \"fields\": {} }";
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(absent), a, &rid));
	TEST_ASSERT_TRUE(rid.id != 0u);
	const_chr_t name = sk_repository_api()->get_string(sk_repository_api()->read(repo, rid), 0u);
	TEST_ASSERT_TRUE(name == NULL || name[0] == '\0');

	sk_repository_api()->destroy(repo);
}

/**
 * Full package graph covering ResourceAssetPackage + Directory + File + Asset
 * with every contract field set (including list edges).
 */
SK_TEST(resource_serialize_package_all_container_fields) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t mesh = ser_create(repo, "MeshResource", ser_uuid(0xb001u, 1u));
	sk_rid_t asset = ser_create(repo, "ResourceAsset", ser_uuid(0xb002u, 2u));
	sk_rid_t file = ser_create(repo, "ResourceAssetFile", ser_uuid(0xb003u, 3u));
	sk_rid_t dir_asset = ser_create(repo, "ResourceAsset", ser_uuid(0xb004u, 4u));
	sk_rid_t child_dir = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0xb005u, 5u));
	sk_rid_t root_dir = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0xb006u, 6u));
	sk_rid_t package = ser_create(repo, "ResourceAssetPackage", ser_uuid(0xb007u, 7u));

	{
		sk_resource_object_t w = api->write(repo, mesh);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "CubeMesh"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, dir_asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "AssetsDir"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 1));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "Cube"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, mesh));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/Cube.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 0));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, file));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_SOURCE_PATH, ""));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_READ_ONLY, 0));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, file);
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF, asset));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH, "/proj/Assets/Cube.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH, "Assets/Cube.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION, 2u));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK, 128u));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME, 99u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, child_dir);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, NULL, 0u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, NULL, 0u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, root_dir);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET, dir_asset));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &child_dir, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &asset, 1u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "DemoPkg"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH, "/proj"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &file, 1u));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, root_dir));
		api->commit(w, NULL);
	}

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, package, a, &json, NULL));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_NOT_NULL(strstr(json, "\"AbsolutePath\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Files\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Root\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"DirectoryAsset\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Directories\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Assets\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"AssetRef\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"PersistedVersion\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"TotalSizeInDisk\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"LastModifiedTime\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"Name\""));
	/* UUID reference encoding (APX-194): stable lo-hi form, never raw RID. */
	TEST_ASSERT_NOT_NULL(strstr(json, "000000000000b002-0000000000000002"));

	api->destroy(repo);
	repo = ser_test_repo();
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t pr = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("DemoPkg", api->get_string(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING("/proj", api->get_string(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH));
	u32 file_count = 0u;
	const sk_rid_t* files = api->get_subobject_list(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &file_count);
	TEST_ASSERT_EQUAL_UINT32(1u, file_count);
	sk_resource_object_t fr = api->read(repo, files[0]);
	TEST_ASSERT_EQUAL_UINT64(128u, api->get_uint(fr, SK_RESOURCE_ASSET_FILE_FIELD_TOTAL_SIZE_IN_DISK));
	TEST_ASSERT_EQUAL_UINT64(99u, api->get_uint(fr, SK_RESOURCE_ASSET_FILE_FIELD_LAST_MODIFIED_TIME));
	sk_rid_t loaded_root = api->get_subobject(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
	TEST_ASSERT_TRUE(loaded_root.id != 0u);
	u32 dir_count = 0u;
	(void)api->get_subobject_list(api->read(repo, loaded_root), SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &dir_count);
	TEST_ASSERT_EQUAL_UINT32(1u, dir_count);
	u32 asset_count = 0u;
	const sk_rid_t* assets = api->get_subobject_list(api->read(repo, loaded_root), SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &asset_count);
	TEST_ASSERT_EQUAL_UINT32(1u, asset_count);
	TEST_ASSERT_EQUAL_STRING("Cube", api->get_string(api->read(repo, assets[0]), SK_RESOURCE_ASSET_FIELD_NAME));

	char* json2 = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, loaded, a, &json2, NULL));
	TEST_ASSERT_EQUAL_STRING(json, json2);

	a->free(a->instance, json);
	a->free(a->instance, json2);
	api->destroy(repo);
}

/**
 * Explicit report: ResourceAsset.Type (NONE) is never written; Buffer payload
 * is emitted as a byte array (same wire form as Blob on v2).
 */
SK_TEST(resource_serialize_reports_unrepresentable_and_opaque_fields) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	sk_rid_t asset = ser_create(repo, "ResourceAsset", ser_uuid(0xc001u, 1u));
	{
		sk_resource_object_t w = api->write(repo, asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "typed"));
		api->commit(w, NULL);
	}
	char* asset_json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, asset, a, &asset_json, NULL));
	TEST_ASSERT_NOT_NULL(strstr(asset_json, "\"type\""));
	TEST_ASSERT_NOT_NULL(strstr(asset_json, "ResourceAsset"));
	const char* fields = strstr(asset_json, "\"fields\"");
	TEST_ASSERT_NOT_NULL(fields);
	TEST_ASSERT_NULL(strstr(fields, "\"Type\""));
	a->free(a->instance, asset_json);

	sk_rid_t dep = ser_create(repo, "ResourceDependencyEntry", ser_uuid(0xc002u, 2u));
	{
		sk_resource_object_t w = api->write(repo, dep);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH, "x.bin"));
		const u8 payload[] = {1u, 2u, 3u, 4u};
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA, payload, 4u));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE, 16u));
		api->commit(w, NULL);
	}
	char* dep_json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, dep, a, &dep_json, NULL));
	TEST_ASSERT_NOT_NULL(strstr(dep_json, "\"Data\""));
	/* Payload bytes appear as a JSON array (pretty or compact), not legacy {"id":…}. */
	TEST_ASSERT_NULL(strstr(dep_json, "\"id\""));
	TEST_ASSERT_NOT_NULL(strstr(dep_json, "1"));
	TEST_ASSERT_NOT_NULL(strstr(dep_json, "2"));
	TEST_ASSERT_NOT_NULL(strstr(dep_json, "3"));
	TEST_ASSERT_NOT_NULL(strstr(dep_json, "4"));
	a->free(a->instance, dep_json);
	api->destroy(repo);
}

/* ================================================================== */
/*  Disk integration: package save/load through real files (APX-190)  */
/* ================================================================== */

/**
 * Per-test temp directory fixture: unique under the OS temp folder (no CWD
 * dependence, no shared global paths). Caller cleans with ser_it_cleanup.
 */
static u32 ser_it_seq;

static void ser_it_make_temp_dir(char* dir, u32 dir_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char temp[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, fs->temp_folder(temp, (u32)sizeof(temp)));
	ser_it_seq += 1u;
	int n = snprintf(dir, dir_cap, "%s/skore_asset_it_%u", temp, ser_it_seq);
	TEST_ASSERT_TRUE(n > 0 && (u32)n < dir_cap);
	/* Best-effort remove leftovers from a prior crashed run with the same name. */
	(void)fs->remove(dir);
	TEST_ASSERT_EQUAL_INT(0, fs->create_directory(dir));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_DIRECTORY, fs->get_file_status(dir));
}

static void ser_it_join(const_chr_t dir, const_chr_t name, char* out, u32 out_cap) {
	TEST_ASSERT_TRUE(sk_path_join(sk_str_view_cstr(dir), sk_str_view_cstr(name), out, out_cap) >= 0);
}

static void ser_it_cleanup(const_chr_t dir, const_chr_t file_path) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	if (file_path != NULL && file_path[0] != '\0') {
		(void)fs->remove(file_path);
	}
	if (dir != NULL && dir[0] != '\0') {
		(void)fs->remove(dir);
	}
}

static void ser_it_write_raw(const_chr_t path, const void* data, size_t size) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	TEST_ASSERT_NOT_NULL(file);
	TEST_ASSERT_EQUAL_UINT64((u64)size, fs->write_file(file, data, size));
	fs->close_file(file);
}

/** Build an interlinked package graph: package → dir → assets → payloads + file refs. */
static sk_rid_t ser_it_build_interlinked_package(sk_repository_t* repo) {
	const sk_repository_api_t* api = sk_repository_api();

	sk_rid_t package = ser_create(repo, "ResourceAssetPackage", ser_uuid(0xa1900001ull, 0xb1900001ull));
	sk_rid_t root_dir = ser_create(repo, "ResourceAssetDirectory", ser_uuid(0xa1900002ull, 0xb1900002ull));
	sk_rid_t dir_asset = ser_create(repo, "ResourceAsset", ser_uuid(0xa1900003ull, 0xb1900003ull));
	sk_rid_t mesh_asset = ser_create(repo, "ResourceAsset", ser_uuid(0xa1900004ull, 0xb1900004ull));
	sk_rid_t mat_asset = ser_create(repo, "ResourceAsset", ser_uuid(0xa1900005ull, 0xb1900005ull));
	sk_rid_t mesh = ser_create(repo, "MeshResource", ser_uuid(0xa1900006ull, 0xb1900006ull));
	sk_rid_t material = ser_create(repo, "MaterialGraphResource", ser_uuid(0xa1900007ull, 0xb1900007ull));
	sk_rid_t file_meta = ser_create(repo, "ResourceAssetFile", ser_uuid(0xa1900008ull, 0xb1900008ull));

	{
		sk_resource_object_t w = api->write(repo, mesh);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "HeroMesh"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, material);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "HeroMat"));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, file_meta);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH, "/pkg/Assets/Hero.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH, "Assets/Hero.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION, 3u));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF, mesh_asset));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, mesh_asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "Hero"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/Hero.mesh"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, mesh));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_ASSET_FILE, file_meta));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, dir_asset));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, mat_asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "HeroMaterial"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_EXTENSION, ".material"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets/Hero.material"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, material));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, dir_asset));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, dir_asset);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "Assets"));
		TEST_ASSERT_EQUAL_INT(0, api->set_bool(w, SK_RESOURCE_ASSET_FIELD_DIRECTORY, 1));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_PATH_ID, "Assets"));
		api->commit(w, NULL);
	}
	{
		sk_rid_t assets[2];
		assets[0] = mesh_asset;
		assets[1] = mat_asset;
		sk_resource_object_t w = api->write(repo, root_dir);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET, dir_asset));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, assets, 2u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "HeroPkg"));
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH, "/pkg"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, root_dir));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &file_meta, 1u));
		api->commit(w, NULL);
	}
	return package;
}

/** Assert loaded graph matches the interlinked package built by ser_it_build_*. */
static void ser_it_assert_interlinked_equivalent(sk_repository_t* repo, sk_rid_t root) {
	const sk_repository_api_t* api = sk_repository_api();
	TEST_ASSERT_TRUE(root.id != 0u);
	TEST_ASSERT_TRUE(api->has_resource(repo, root));

	sk_uuid_t root_uuid = api->resource_uuid(repo, root);
	TEST_ASSERT_EQUAL_UINT64(0xa1900001ull, root_uuid.lo);
	TEST_ASSERT_EQUAL_UINT64(0xb1900001ull, root_uuid.hi);

	sk_resource_object_t pr = api->read(repo, root);
	TEST_ASSERT_EQUAL_STRING("HeroPkg", api->get_string(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING("/pkg", api->get_string(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_ABSOLUTE_PATH));

	sk_rid_t loaded_dir = api->get_subobject(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
	TEST_ASSERT_TRUE(loaded_dir.id != 0u);
	TEST_ASSERT_TRUE(SK_UUID_EQ(api->resource_uuid(repo, loaded_dir), ser_uuid(0xa1900002ull, 0xb1900002ull)));

	u32 file_count = 0u;
	const sk_rid_t* files = api->get_subobject_list(pr, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &file_count);
	TEST_ASSERT_EQUAL_UINT32(1u, file_count);
	TEST_ASSERT_NOT_NULL(files);
	sk_resource_object_t fr = api->read(repo, files[0]);
	TEST_ASSERT_EQUAL_STRING("Assets/Hero.mesh", api->get_string(fr, SK_RESOURCE_ASSET_FILE_FIELD_RELATIVE_PATH));
	TEST_ASSERT_EQUAL_UINT64(3u, api->get_uint(fr, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION));

	sk_resource_object_t dr = api->read(repo, loaded_dir);
	sk_rid_t dir_asset = api->get_subobject(dr, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORY_ASSET);
	TEST_ASSERT_TRUE(dir_asset.id != 0u);
	TEST_ASSERT_EQUAL_STRING("Assets", api->get_string(api->read(repo, dir_asset), SK_RESOURCE_ASSET_FIELD_NAME));

	u32 asset_count = 0u;
	const sk_rid_t* assets = api->get_subobject_list(dr, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &asset_count);
	TEST_ASSERT_EQUAL_UINT32(2u, asset_count);
	TEST_ASSERT_NOT_NULL(assets);

	/* Find mesh / material assets by stable UUID (order is BFS field walk). */
	sk_rid_t mesh_asset = api->find_by_uuid(repo, ser_uuid(0xa1900004ull, 0xb1900004ull));
	sk_rid_t mat_asset = api->find_by_uuid(repo, ser_uuid(0xa1900005ull, 0xb1900005ull));
	TEST_ASSERT_TRUE(mesh_asset.id != 0u);
	TEST_ASSERT_TRUE(mat_asset.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(assets[0], mesh_asset) || SK_RID_EQ(assets[1], mesh_asset));
	TEST_ASSERT_TRUE(SK_RID_EQ(assets[0], mat_asset) || SK_RID_EQ(assets[1], mat_asset));

	sk_resource_object_t mar = api->read(repo, mesh_asset);
	TEST_ASSERT_EQUAL_STRING("Hero", api->get_string(mar, SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING(".mesh", api->get_string(mar, SK_RESOURCE_ASSET_FIELD_EXTENSION));
	TEST_ASSERT_EQUAL_STRING("Assets/Hero.mesh", api->get_string(mar, SK_RESOURCE_ASSET_FIELD_PATH_ID));
	sk_rid_t mesh = api->get_subobject(mar, SK_RESOURCE_ASSET_FIELD_OBJECT);
	TEST_ASSERT_TRUE(mesh.id != 0u);
	TEST_ASSERT_EQUAL_STRING("HeroMesh", api->get_string(api->read(repo, mesh), 0u));
	/* Cross-reference: AssetFile → mesh_asset and mesh_asset → AssetFile resolved. */
	sk_rid_t back_file = api->get_reference(mar, SK_RESOURCE_ASSET_FIELD_ASSET_FILE);
	TEST_ASSERT_TRUE(SK_RID_EQ(back_file, files[0]));
	sk_rid_t file_ref = api->get_reference(fr, SK_RESOURCE_ASSET_FILE_FIELD_ASSET_REF);
	TEST_ASSERT_TRUE(SK_RID_EQ(file_ref, mesh_asset));
	sk_rid_t parent = api->get_reference(mar, SK_RESOURCE_ASSET_FIELD_PARENT);
	TEST_ASSERT_TRUE(SK_RID_EQ(parent, dir_asset));

	sk_resource_object_t matar = api->read(repo, mat_asset);
	TEST_ASSERT_EQUAL_STRING("HeroMaterial", api->get_string(matar, SK_RESOURCE_ASSET_FIELD_NAME));
	sk_rid_t material = api->get_subobject(matar, SK_RESOURCE_ASSET_FIELD_OBJECT);
	TEST_ASSERT_TRUE(material.id != 0u);
	TEST_ASSERT_EQUAL_STRING("HeroMat", api->get_string(api->read(repo, material), 0u));
}

SK_TEST(resource_serialize_it_interlinked_save_load_disk) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "package.json", path, (u32)sizeof(path));

	sk_repository_t* src = ser_test_repo();
	sk_rid_t package = ser_it_build_interlinked_package(src);
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(src, package, path));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(path));
	TEST_ASSERT_TRUE(sk_filesystem_api()->get_path_size(path) > 0ull);
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(dst, path, &loaded));
	ser_it_assert_interlinked_equivalent(dst, loaded);
	api->destroy(dst);

	ser_it_cleanup(dir, path);
}

SK_TEST(resource_serialize_it_empty_package_save_load_disk) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "empty_package.json", path, (u32)sizeof(path));

	sk_repository_t* src = ser_test_repo();
	/* Empty repository package: package root only, no Files / Root children. */
	sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa190e001ull, 0xb190e001ull));
	{
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "EmptyPkg"));
		api->commit(w, NULL);
	}
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(src, package, path));
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	const u64 before = api->resource_count(dst);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(dst, path, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	TEST_ASSERT_TRUE(api->resource_count(dst) > before);
	sk_resource_object_t r = api->read(dst, loaded);
	TEST_ASSERT_EQUAL_STRING("EmptyPkg", api->get_string(r, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	TEST_ASSERT_TRUE(api->get_subobject(r, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT).id == 0u);
	u32 files = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &files);
	TEST_ASSERT_EQUAL_UINT32(0u, files);
	api->destroy(dst);

	ser_it_cleanup(dir, path);
}

SK_TEST(resource_serialize_it_load_missing_directory) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	/* Path under a directory that was never created. */
	ser_it_join(dir, "no_such_subdir", path, (u32)sizeof(path));
	char missing[SK_FS_PATH_MAX];
	ser_it_join(path, "package.json", missing, (u32)sizeof(missing));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_NOT_FOUND, sk_filesystem_api()->get_file_status(path));

	sk_repository_t* repo = ser_test_repo();
	const u64 before = api->resource_count(repo);
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_NOT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(repo, missing, &root));
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);
	TEST_ASSERT_EQUAL_UINT64(before, api->resource_count(repo));
	/* Repository remains usable after the failed load. */
	sk_rid_t still_ok = ser_create(repo, "MeshResource", ser_uuid(0xa190f001ull, 0xb190f001ull));
	TEST_ASSERT_TRUE(still_ok.id != 0u);
	api->destroy(repo);

	ser_it_cleanup(dir, NULL);
}

SK_TEST(resource_serialize_it_load_no_read_permission) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "locked.json", path, (u32)sizeof(path));

	sk_repository_t* src = ser_test_repo();
	sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa190a101ull, 0xb190a101ull));
	{
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "Locked"));
		api->commit(w, NULL);
	}
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(src, package, path));
	api->destroy(src);

#if defined(_WIN32)
	/* ACL-based no-read is not portable in CI; skip cleanly. */
	(void)api;
	ser_it_cleanup(dir, path);
	TEST_IGNORE_MESSAGE("no-read permission cannot be simulated portably on Windows");
#else
	if (chmod(path, 0) != 0) {
		ser_it_cleanup(dir, path);
		TEST_IGNORE_MESSAGE("chmod failed; cannot simulate no-read permission on this platform");
	}

	sk_repository_t* repo = ser_test_repo();
	const u64 before = api->resource_count(repo);
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_NOT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(repo, path, &root));
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);
	TEST_ASSERT_EQUAL_UINT64(before, api->resource_count(repo));
	/* Remains usable. */
	sk_rid_t ok = ser_create(repo, "MeshResource", ser_uuid(0xa190a102ull, 0xb190a102ull));
	TEST_ASSERT_TRUE(ok.id != 0u);
	api->destroy(repo);

	/* Restore permissions so cleanup can remove the file. */
	(void)chmod(path, 0600);
	ser_it_cleanup(dir, path);
#endif
}

SK_TEST(resource_serialize_it_load_corrupted_truncated_file) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "truncated.json", path, (u32)sizeof(path));

	const char truncated[] = "{\"format\":\"sk.resource_package\",\"format_version\":1,\"root_uuid\":\"0000000000000001-0000000000000001\",\"resources\":[";
	ser_it_write_raw(path, truncated, sizeof(truncated) - 1u);

	sk_repository_t* repo = ser_test_repo();
	const u64 before = api->resource_count(repo);
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_NOT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(repo, path, &root));
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);
	TEST_ASSERT_EQUAL_UINT64(before, api->resource_count(repo));

	/* Repository reports the error and remains usable for subsequent work. */
	sk_rid_t package = ser_create(repo, "ResourceAssetPackage", ser_uuid(0xa190c101ull, 0xb190c101ull));
	{
		sk_resource_object_t w = api->write(repo, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "AfterCorrupt"));
		api->commit(w, NULL);
	}
	char path_ok[SK_FS_PATH_MAX];
	ser_it_join(dir, "ok.json", path_ok, (u32)sizeof(path_ok));
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(repo, package, path_ok));
	sk_rid_t reloaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(repo, path_ok, &reloaded));
	TEST_ASSERT_EQUAL_STRING("AfterCorrupt", api->get_string(api->read(repo, reloaded), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	api->destroy(repo);

	(void)sk_filesystem_api()->remove(path);
	(void)sk_filesystem_api()->remove(path_ok);
	(void)sk_filesystem_api()->remove(dir);
}

SK_TEST(resource_serialize_it_load_missing_referenced_asset) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "missing_ref.json", path, (u32)sizeof(path));

	/* Package whose Object field points at a UUID not present in resources[]. */
	const char bad[] = "{\n"
					   "  \"format\": \"sk.resource_package\",\n"
					   "  \"format_version\": 1,\n"
					   "  \"root_uuid\": \"000000000000a190-0000000000000001\",\n"
					   "  \"resources\": [\n"
					   "    {\n"
					   "      \"format\": \"sk.resource\",\n"
					   "      \"format_version\": 1,\n"
					   "      \"type\": \"ResourceAsset\",\n"
					   "      \"uuid\": \"000000000000a190-0000000000000001\",\n"
					   "      \"fields\": {\n"
					   "        \"Name\": \"Dangling\",\n"
					   "        \"Object\": \"000000000000dead-000000000000beef\"\n"
					   "      }\n"
					   "    }\n"
					   "  ]\n"
					   "}";
	ser_it_write_raw(path, bad, sizeof(bad) - 1u);

	sk_repository_t* repo = ser_test_repo();
	const u64 before = api->resource_count(repo);
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_MISSING_REF, sk_resource_deserialize_package_json_from_file(repo, path, &root));
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);
	TEST_ASSERT_EQUAL_UINT64(before, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(0xa190ull, 1u)).id == 0u);
	/* Usable after failure. */
	sk_rid_t mesh = ser_create(repo, "MeshResource", ser_uuid(0xa190d001ull, 0xb190d001ull));
	TEST_ASSERT_TRUE(mesh.id != 0u);
	api->destroy(repo);

	ser_it_cleanup(dir, path);
}

SK_TEST(resource_serialize_it_overwrite_existing_saved_package) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "overwrite.json", path, (u32)sizeof(path));

	/* First save. */
	{
		sk_repository_t* src = ser_test_repo();
		sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa190b001ull, 0xb190b001ull));
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "First"));
		api->commit(w, NULL);
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(src, package, path));
		api->destroy(src);
	}

	/* Overwrite same path with different content / UUID. */
	{
		sk_repository_t* src = ser_test_repo();
		sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa190b002ull, 0xb190b002ull));
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "Second"));
		api->commit(w, NULL);
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(src, package, path));
		api->destroy(src);
	}

	sk_repository_t* dst = ser_test_repo();
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(dst, path, &loaded));
	TEST_ASSERT_EQUAL_STRING("Second", api->get_string(api->read(dst, loaded), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	sk_uuid_t u = api->resource_uuid(dst, loaded);
	TEST_ASSERT_EQUAL_UINT64(0xa190b002ull, u.lo);
	TEST_ASSERT_EQUAL_UINT64(0xb190b002ull, u.hi);
	/* First UUID must not appear after overwrite. */
	TEST_ASSERT_TRUE(api->find_by_uuid(dst, ser_uuid(0xa190b001ull, 0xb190b001ull)).id == 0u);
	api->destroy(dst);

	ser_it_cleanup(dir, path);
}

/* ================================================================== */
/*  APX-191 edge-case hardening                                       */
/* ================================================================== */

/**
 * Three-node soft-reference cycle A→B→C→A via ResourceAsset.Parent.
 * Package serialize must terminate (BFS + visited set), emit each UUID once,
 * and reload with the cycle restored — no stack overflow.
 */
SK_TEST(resource_serialize_edge_cycle_three_node_package) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* src = ser_test_repo();

	const sk_uuid_t ua = ser_uuid(0xa191c001ull, 0xb191c001ull);
	const sk_uuid_t ub = ser_uuid(0xa191c002ull, 0xb191c002ull);
	const sk_uuid_t uc = ser_uuid(0xa191c003ull, 0xb191c003ull);
	sk_rid_t a_rid = ser_create(src, "ResourceAsset", ua);
	sk_rid_t b_rid = ser_create(src, "ResourceAsset", ub);
	sk_rid_t c_rid = ser_create(src, "ResourceAsset", uc);

	{
		sk_resource_object_t w = api->write(src, a_rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "A"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, b_rid));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, b_rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "B"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, c_rid));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, c_rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "C"));
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, a_rid));
		api->commit(w, NULL);
	}

	char* json = NULL;
	u32 size = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, a_rid, a, &json, &size));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_TRUE(size > 0u);
	/* Exactly one occurrence of each UUID (envelope uuid + Parent ref each share text; count resources entries). */
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a191c001-00000000b191c001"));
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a191c002-00000000b191c002"));
	TEST_ASSERT_NOT_NULL(strstr(json, "00000000a191c003-00000000b191c003"));
	/* Three resource objects only (format sk.resource appears once per entry). */
	{
		u32 n_res = 0u;
		const char* p = json;
		while ((p = strstr(p, "\"type\": \"ResourceAsset\"")) != NULL) {
			n_res += 1u;
			p += 1;
		}
		TEST_ASSERT_EQUAL_UINT32(3u, n_res);
	}
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_make(json, size), a, &root));
	sk_rid_t la = api->find_by_uuid(dst, ua);
	sk_rid_t lb = api->find_by_uuid(dst, ub);
	sk_rid_t lc = api->find_by_uuid(dst, uc);
	TEST_ASSERT_TRUE(la.id != 0u && lb.id != 0u && lc.id != 0u);
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, la), SK_RESOURCE_ASSET_FIELD_PARENT), lb));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, lb), SK_RESOURCE_ASSET_FIELD_PARENT), lc));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, lc), SK_RESOURCE_ASSET_FIELD_PARENT), la));

	/* Re-serialize cyclic graph a second time — still terminates and stays stable. */
	char* json2 = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(dst, la, a, &json2, NULL));
	TEST_ASSERT_EQUAL_STRING(json, json2);

	a->free(a->instance, json);
	a->free(a->instance, json2);
	api->destroy(dst);
}

/**
 * Deep SubObjectList ownership chain (package → root dir → … → leaf).
 * BFS walk and load must succeed without stack overflow at depth 64.
 */
SK_TEST(resource_serialize_edge_deeply_nested_directories) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* src = ser_test_repo();

	enum { DEPTH = 64 };
	sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa191d000ull, 0xb191d000ull));
	sk_rid_t dirs[DEPTH];
	for (u32 i = 0u; i < (u32)DEPTH; ++i) {
		dirs[i] = ser_create(src, "ResourceAssetDirectory", ser_uuid(0xa191d100ull + (u64)i, 0xb191d100ull + (u64)i));
	}
	/* Wire leaf → parent links: dirs[i] owns dirs[i+1] via Directories list. */
	for (u32 i = 0u; i + 1u < (u32)DEPTH; ++i) {
		sk_resource_object_t w = api->write(src, dirs[i]);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &dirs[i + 1u], 1u));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "DeepNest"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, dirs[0]));
		api->commit(w, NULL);
	}

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, package, a, &json, NULL));
	/* Package + DEPTH directories. */
	{
		u32 n_dir = 0u;
		const char* p = json;
		while ((p = strstr(p, "\"type\": \"ResourceAssetDirectory\"")) != NULL) {
			n_dir += 1u;
			p += 1;
		}
		TEST_ASSERT_EQUAL_UINT32((u32)DEPTH, n_dir);
	}
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	sk_rid_t root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_cstr(json), a, &root));
	TEST_ASSERT_EQUAL_STRING("DeepNest", api->get_string(api->read(dst, root), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));

	/* Walk the loaded chain DEPTH levels. */
	sk_rid_t cur = api->get_subobject(api->read(dst, root), SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
	TEST_ASSERT_TRUE(cur.id != 0u);
	for (u32 i = 0u; i + 1u < (u32)DEPTH; ++i) {
		u32 count = 0u;
		const sk_rid_t* kids = api->get_subobject_list(api->read(dst, cur), SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &count);
		TEST_ASSERT_EQUAL_UINT32(1u, count);
		TEST_ASSERT_NOT_NULL(kids);
		cur = kids[0];
	}
	/* Leaf has no further directories. */
	{
		u32 count = 1u;
		(void)api->get_subobject_list(api->read(dst, cur), SK_RESOURCE_ASSET_DIRECTORY_FIELD_DIRECTORIES, &count);
		TEST_ASSERT_EQUAL_UINT32(0u, count);
	}

	a->free(a->instance, json);
	api->destroy(dst);
}

/**
 * Wide package: root directory owns N ResourceAssets (flat SubObjectList).
 * Exercises large reachable sets and linear-time BFS visited membership.
 */
SK_TEST(resource_serialize_edge_large_asset_set) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* src = ser_test_repo();

	enum { N = 256 };
	sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa191e000ull, 0xb191e000ull));
	sk_rid_t root_dir = ser_create(src, "ResourceAssetDirectory", ser_uuid(0xa191e001ull, 0xb191e001ull));
	sk_rid_t* assets = (sk_rid_t*)a->alloc(a->instance, sizeof(sk_rid_t) * (size_t)N);
	TEST_ASSERT_NOT_NULL(assets);

	for (u32 i = 0u; i < (u32)N; ++i) {
		assets[i] = ser_create(src, "ResourceAsset", ser_uuid(0xa191e100ull + (u64)i, 0xb191e100ull + (u64)i));
		char name[32];
		(void)snprintf(name, sizeof(name), "Asset%u", i);
		sk_resource_object_t w = api->write(src, assets[i]);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, name));
		/* Soft self-ref cycle on each node — must not explode collect. */
		TEST_ASSERT_EQUAL_INT(0, api->set_reference(w, SK_RESOURCE_ASSET_FIELD_PARENT, assets[i]));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, root_dir);
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, assets, (u32)N));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "WidePkg"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT, root_dir));
		api->commit(w, NULL);
	}

	char* json = NULL;
	u32 size = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, package, a, &json, &size));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_TRUE(size > 1000u);
	{
		u32 n_asset = 0u;
		const char* p = json;
		while ((p = strstr(p, "\"type\": \"ResourceAsset\"")) != NULL) {
			n_asset += 1u;
			p += 1;
		}
		TEST_ASSERT_EQUAL_UINT32((u32)N, n_asset);
	}
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_make(json, size), a, &loaded));
	sk_rid_t dir = api->get_subobject(api->read(dst, loaded), SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT);
	u32 count = 0u;
	const sk_rid_t* got = api->get_subobject_list(api->read(dst, dir), SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, &count);
	TEST_ASSERT_EQUAL_UINT32((u32)N, count);
	TEST_ASSERT_NOT_NULL(got);
	/* Spot-check first / last names and self-Parent cycle. */
	TEST_ASSERT_EQUAL_STRING("Asset0", api->get_string(api->read(dst, got[0]), SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING("Asset255", api->get_string(api->read(dst, got[N - 1u]), SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, got[0]), SK_RESOURCE_ASSET_FIELD_PARENT), got[0]));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_reference(api->read(dst, got[N - 1u]), SK_RESOURCE_ASSET_FIELD_PARENT), got[N - 1u]));

	a->free(a->instance, assets);
	a->free(a->instance, json);
	api->destroy(dst);
}

/**
 * Repeated package save → load → save cycles produce byte-identical JSON
 * (format stability / deterministic BFS emission order).
 */
SK_TEST(resource_serialize_edge_multi_cycle_byte_identical) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
	sk_rid_t package = ser_it_build_interlinked_package(repo);

	char* baseline = NULL;
	u32 baseline_size = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(repo, package, a, &baseline, &baseline_size));
	TEST_ASSERT_NOT_NULL(baseline);
	TEST_ASSERT_TRUE(baseline_size > 0u);
	api->destroy(repo);

	enum { ROUNDS = 8 };
	char* prev = baseline;
	u32 prev_size = baseline_size;
	for (u32 round = 0u; round < (u32)ROUNDS; ++round) {
		sk_repository_t* next_repo = ser_test_repo();
		sk_rid_t root = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(next_repo, sk_str_view_make(prev, prev_size), a, &root));
		char* emitted = NULL;
		u32 emitted_size = 0u;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(next_repo, root, a, &emitted, &emitted_size));
		TEST_ASSERT_EQUAL_UINT32(baseline_size, emitted_size);
		TEST_ASSERT_EQUAL_MEMORY(baseline, emitted, baseline_size);
		api->destroy(next_repo);
		if (prev != baseline) {
			a->free(a->instance, prev);
		}
		prev = emitted;
		prev_size = emitted_size;
	}
	if (prev != baseline) {
		a->free(a->instance, prev);
	}
	a->free(a->instance, baseline);
}

/**
 * Blob fields carry arbitrary binary (including 0x00 and full 0..255 range).
 * Empty blob and full-range blob round-trip byte-for-byte.
 * Non-UTF-8 text in String fields is intentionally unsupported (use Blob).
 */
SK_TEST(resource_serialize_edge_binary_blob_full_range) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Empty blob (has_value set, zero length). */
	{
		sk_rid_t rid = ser_create(repo, "AudioResource", ser_uuid(0xa191b001ull, 0xb191b001ull));
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_NAMED_RESOURCE_FIELD_NAME, "empty"));
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(w, SK_NAMED_RESOURCE_FIELD_BYTES, NULL, 0u));
		api->commit(w, NULL);

		char* json = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
		TEST_ASSERT_NOT_NULL(strstr(json, "\"Bytes\""));
		api->destroy_resource(repo, rid, NULL);
		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
		u32 size = 1u;
		const u8* got = api->get_blob(api->read(repo, loaded), SK_NAMED_RESOURCE_FIELD_BYTES, &size);
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		(void)got;
		a->free(a->instance, json);
	}

	/* Full 0..255 byte range (includes NUL and high non-UTF8 bytes). */
	{
		u8 bytes[256];
		for (u32 i = 0u; i < 256u; ++i) {
			bytes[i] = (u8)i;
		}
		sk_rid_t rid = ser_create(repo, "AudioResource", ser_uuid(0xa191b002ull, 0xb191b002ull));
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_NAMED_RESOURCE_FIELD_NAME, "full"));
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(w, SK_NAMED_RESOURCE_FIELD_BYTES, bytes, 256u));
		api->commit(w, NULL);

		char* json = NULL;
		u32 jsize = 0u;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, &jsize));
		/* Byte array encoding: leading 0 and trailing 255 present as JSON numbers. */
		TEST_ASSERT_NOT_NULL(strstr(json, "0"));
		TEST_ASSERT_NOT_NULL(strstr(json, "255"));
		api->destroy_resource(repo, rid, NULL);

		sk_rid_t loaded = SK_RID_ZERO;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_make(json, jsize), a, &loaded));
		u32 size = 0u;
		const u8* got = api->get_blob(api->read(repo, loaded), SK_NAMED_RESOURCE_FIELD_BYTES, &size);
		TEST_ASSERT_EQUAL_UINT32(256u, size);
		TEST_ASSERT_NOT_NULL(got);
		TEST_ASSERT_EQUAL_MEMORY(bytes, got, 256u);

		/* Double-serialize identity for binary payload. */
		char* json2 = NULL;
		TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, loaded, a, &json2, NULL));
		TEST_ASSERT_EQUAL_STRING(json, json2);
		a->free(a->instance, json);
		a->free(a->instance, json2);
	}

	api->destroy(repo);
}

/**
 * Document single-thread requirement: resource_serialize is not a concurrent
 * public surface. This test records the contract in executable form (comment +
 * sequential multi-op smoke) rather than adding locks. Concurrent writers on
 * the same repository remain unsupported (repository write lock is exclusive;
 * serialize/deserialize are caller-serialized).
 */
SK_TEST(resource_serialize_edge_single_thread_contract) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();

	/* Sequential serialize then deserialize on one thread is the supported path. */
	sk_rid_t rid = ser_create(repo, "ResourceAsset", ser_uuid(0xa191f001ull, 0xb191f001ull));
	{
		sk_resource_object_t w = api->write(repo, rid);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "main-thread-only"));
		api->commit(w, NULL);
	}
	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_EQUAL_STRING("main-thread-only", api->get_string(api->read(repo, loaded), SK_RESOURCE_ASSET_FIELD_NAME));

	/*
	 * Unsupported (documented, not exercised under race):
	 * - concurrent sk_resource_serialize_* + sk_resource_deserialize_* on one repo
	 * - concurrent write views (repository allows only one exclusive write)
	 * No mutexes are added in this module; external serialization is required.
	 */
	TEST_ASSERT_TRUE(1);

	a->free(a->instance, json);
	api->destroy(repo);
}

SK_TEST(resource_serialize_it_save_modify_reload_disk_wins) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "reload_wins.json", path, (u32)sizeof(path));

	sk_repository_t* repo = ser_test_repo();
	sk_rid_t package = ser_it_build_interlinked_package(repo);
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(repo, package, path));

	/* Mutate live repository after save — disk still holds original. */
	{
		sk_resource_object_t w = api->write(repo, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "MutatedInMemory"));
		api->commit(w, NULL);
	}
	TEST_ASSERT_EQUAL_STRING("MutatedInMemory", api->get_string(api->read(repo, package), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));

	/* Reload from disk into a fresh repository: disk snapshot wins. */
	sk_repository_t* reloaded_repo = ser_test_repo();
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(reloaded_repo, path, &loaded));
	ser_it_assert_interlinked_equivalent(reloaded_repo, loaded);
	TEST_ASSERT_EQUAL_STRING("HeroPkg", api->get_string(api->read(reloaded_repo, loaded), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));

	/* Also reload into the mutated repo (UUID reuse): disk values overwrite live fields. */
	sk_rid_t same = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(repo, path, &same));
	TEST_ASSERT_TRUE(SK_RID_EQ(same, package));
	TEST_ASSERT_EQUAL_STRING("HeroPkg", api->get_string(api->read(repo, package), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));

	api->destroy(reloaded_repo);
	api->destroy(repo);
	ser_it_cleanup(dir, path);
}

/* ================================================================== */
/*  APX-196: asset repository save/load integration acceptance        */
/*                                                                    */
/*  Goal cases (in-source SK_TEST, same registration as unit tests):  */
/*   1. Multi-type assets + inter-asset refs → serialize whole graph  */
/*      → load into a fresh repository → full contents + refs survive */
/*   2. Empty repository / empty package round-trip                   */
/*   3. Corrupted/truncated payload fails via contract error model    */
/*      without leaving the repository half-populated                 */
/* ================================================================== */

/**
 * APX-196 case 1: populate a repository with multiple asset types and
 * inter-asset references, serialize the reachable package graph, load into a
 * brand-new repository instance, and assert contents + reference graph.
 */
SK_TEST(resource_serialize_apx196_multi_type_graph_fresh_repo) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();

	sk_repository_t* src = ser_test_repo();
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(src));
	sk_rid_t package = ser_it_build_interlinked_package(src);
	TEST_ASSERT_TRUE(package.id != 0u);
	/* Package + directory + dir asset + mesh asset + mat asset + mesh + material + file. */
	TEST_ASSERT_TRUE(api->resource_count(src) >= 8u);

	char* json = NULL;
	u32 size = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, package, a, &json, &size));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_TRUE(size > 0u);
	/* Distinct types in the emitted graph (never raw RID integers as handles). */
	TEST_ASSERT_NOT_NULL(strstr(json, "\"type\": \"ResourceAssetPackage\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"type\": \"ResourceAssetDirectory\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"type\": \"ResourceAsset\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"type\": \"ResourceAssetFile\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"type\": \"MeshResource\""));
	TEST_ASSERT_NOT_NULL(strstr(json, "\"type\": \"MaterialGraphResource\""));
	TEST_ASSERT_NULL(strstr(json, "\"AssetRef\": 1")); /* no integer RID encoding */
	api->destroy(src);

	/* Fresh repository: types registered, zero live assets before load. */
	sk_repository_t* dst = ser_test_repo();
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(dst));
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_make(json, size), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	TEST_ASSERT_TRUE(api->resource_count(dst) >= 8u);
	ser_it_assert_interlinked_equivalent(dst, loaded);

	/* Disk path of the same graph (real filesystem I/O). */
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "apx196_graph.json", path, (u32)sizeof(path));
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(dst, loaded, path));
	api->destroy(dst);

	sk_repository_t* disk_dst = ser_test_repo();
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(disk_dst));
	sk_rid_t disk_root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(disk_dst, path, &disk_root));
	ser_it_assert_interlinked_equivalent(disk_dst, disk_root);
	api->destroy(disk_dst);

	a->free(a->instance, json);
	ser_it_cleanup(dir, path);
}

/**
 * APX-196 case 2: empty-repository / empty-package save → load.
 * Source holds only a package shell (no Files, no Root children); destination
 * starts empty; after load only the empty package is present.
 */
SK_TEST(resource_serialize_apx196_empty_repository_roundtrip) {
	const sk_repository_api_t* api = sk_repository_api();
	const sk_allocator_t* a = sk_allocator_default();

	sk_repository_t* src = ser_test_repo();
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(src));
	sk_rid_t package = ser_create(src, "ResourceAssetPackage", ser_uuid(0xa196e001ull, 0xb196e001ull));
	{
		sk_resource_object_t w = api->write(src, package);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME, "EmptyRepoPkg"));
		/* Leave AbsolutePath / Files / Root at defaults (empty / zero). */
		api->commit(w, NULL);
	}
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(src));

	char* json = NULL;
	u32 size = 0u;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_alloc(src, package, a, &json, &size));
	TEST_ASSERT_NOT_NULL(json);
	TEST_ASSERT_NOT_NULL(strstr(json, "EmptyRepoPkg"));
	/* Single resource entry: only the package itself. */
	{
		u32 n_type = 0u;
		const char* p = json;
		while ((p = strstr(p, "\"type\": \"ResourceAssetPackage\"")) != NULL) {
			n_type += 1u;
			p += 1;
		}
		TEST_ASSERT_EQUAL_UINT32(1u, n_type);
	}
	api->destroy(src);

	sk_repository_t* dst = ser_test_repo();
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(dst));
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_string(dst, sk_str_view_make(json, size), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(dst));
	TEST_ASSERT_TRUE(SK_UUID_EQ(api->resource_uuid(dst, loaded), ser_uuid(0xa196e001ull, 0xb196e001ull)));
	sk_resource_object_t r = api->read(dst, loaded);
	TEST_ASSERT_EQUAL_STRING("EmptyRepoPkg", api->get_string(r, SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	TEST_ASSERT_TRUE(api->get_subobject(r, SK_RESOURCE_ASSET_PACKAGE_FIELD_ROOT).id == 0u);
	u32 files = 0u;
	(void)api->get_subobject_list(r, SK_RESOURCE_ASSET_PACKAGE_FIELD_FILES, &files);
	TEST_ASSERT_EQUAL_UINT32(0u, files);

	/* Disk empty-package path as well. */
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "apx196_empty.json", path, (u32)sizeof(path));
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_serialize_package_json_to_file(dst, loaded, path));
	api->destroy(dst);

	sk_repository_t* disk_dst = ser_test_repo();
	TEST_ASSERT_EQUAL_UINT64(0u, api->resource_count(disk_dst));
	sk_rid_t disk_root = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(SK_RES_SER_OK, sk_resource_deserialize_package_json_from_file(disk_dst, path, &disk_root));
	TEST_ASSERT_EQUAL_UINT64(1u, api->resource_count(disk_dst));
	TEST_ASSERT_EQUAL_STRING("EmptyRepoPkg", api->get_string(api->read(disk_dst, disk_root), SK_RESOURCE_ASSET_PACKAGE_FIELD_NAME));
	api->destroy(disk_dst);

	a->free(a->instance, json);
	ser_it_cleanup(dir, path);
}

/**
 * APX-196 case 3: corrupted / truncated payload fails through the contract
 * error model (non-zero i32, out_root = SK_RID_ZERO) and must not leave the
 * repository half-populated — pre-seeded assets stay intact and no shells
 * from the truncated document appear.
 */
SK_TEST(resource_serialize_apx196_truncated_does_not_half_populate) {
	const sk_repository_api_t* api = sk_repository_api();
	char dir[SK_FS_PATH_MAX];
	char path[SK_FS_PATH_MAX];
	ser_it_make_temp_dir(dir, (u32)sizeof(dir));
	ser_it_join(dir, "apx196_truncated.json", path, (u32)sizeof(path));

	/* Truncated mid-resources array (invalid JSON). */
	const char truncated[] = "{\"format\":\"sk.resource_package\",\"format_version\":1,"
							 "\"root_uuid\":\"00000000a196c001-00000000b196c001\","
							 "\"resources\":[{\"format\":\"sk.resource\",\"format_version\":1,"
							 "\"type\":\"ResourceAsset\",\"uuid\":\"00000000a196c001-00000000b196c001\","
							 "\"fields\":{\"Name\":\"partial";
	ser_it_write_raw(path, truncated, sizeof(truncated) - 1u);

	sk_repository_t* repo = ser_test_repo();
	/* Pre-existing assets that must survive the failed load. */
	sk_rid_t keep_a = ser_create(repo, "ResourceAsset", ser_uuid(0xa196a001ull, 0xb196a001ull));
	sk_rid_t keep_b = ser_create(repo, "MeshResource", ser_uuid(0xa196a002ull, 0xb196a002ull));
	{
		sk_resource_object_t w = api->write(repo, keep_a);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, SK_RESOURCE_ASSET_FIELD_NAME, "seeded-asset"));
		TEST_ASSERT_EQUAL_INT(0, api->set_subobject(w, SK_RESOURCE_ASSET_FIELD_OBJECT, keep_b));
		api->commit(w, NULL);
	}
	{
		sk_resource_object_t w = api->write(repo, keep_b);
		TEST_ASSERT_EQUAL_INT(0, api->set_string(w, 0u, "seeded-mesh"));
		api->commit(w, NULL);
	}
	const u64 count_before = api->resource_count(repo);
	const u64 version_a = api->get_version(repo, keep_a);
	const u64 version_b = api->get_version(repo, keep_b);
	TEST_ASSERT_TRUE(count_before >= 2u);

	sk_rid_t root = SK_RID_ZERO;
	const i32 rc = sk_resource_deserialize_package_json_from_file(repo, path, &root);
	/* Contract error model: non-zero status, zero out handle (not OK / not silent). */
	TEST_ASSERT_NOT_EQUAL_INT(SK_RES_SER_OK, rc);
	TEST_ASSERT_TRUE(rc == SK_RES_SER_ERR || rc == SK_RES_SER_INVALID);
	TEST_ASSERT_EQUAL_UINT64(0u, root.id);

	/* Not half-populated: count, pre-seeded content, and versions unchanged. */
	TEST_ASSERT_EQUAL_UINT64(count_before, api->resource_count(repo));
	TEST_ASSERT_TRUE(api->find_by_uuid(repo, ser_uuid(0xa196c001ull, 0xb196c001ull)).id == 0u);
	TEST_ASSERT_TRUE(api->has_resource(repo, keep_a));
	TEST_ASSERT_TRUE(api->has_resource(repo, keep_b));
	TEST_ASSERT_EQUAL_STRING("seeded-asset", api->get_string(api->read(repo, keep_a), SK_RESOURCE_ASSET_FIELD_NAME));
	TEST_ASSERT_EQUAL_STRING("seeded-mesh", api->get_string(api->read(repo, keep_b), 0u));
	TEST_ASSERT_TRUE(SK_RID_EQ(api->get_subobject(api->read(repo, keep_a), SK_RESOURCE_ASSET_FIELD_OBJECT), keep_b));
	TEST_ASSERT_EQUAL_UINT64(version_a, api->get_version(repo, keep_a));
	TEST_ASSERT_EQUAL_UINT64(version_b, api->get_version(repo, keep_b));

	/* Repository remains usable after the failed load. */
	sk_rid_t after = ser_create(repo, "MaterialGraphResource", ser_uuid(0xa196a003ull, 0xb196a003ull));
	TEST_ASSERT_TRUE(after.id != 0u);
	TEST_ASSERT_EQUAL_UINT64(count_before + 1u, api->resource_count(repo));

	api->destroy(repo);
	ser_it_cleanup(dir, path);
}

#endif /* SK_TESTS */
