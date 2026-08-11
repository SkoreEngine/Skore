/**
 * @file resource_serialize.c
 * @brief JSON (de)serialization for repository resources.
 */

#include "resource_serialize.h"

#include "allocator.h"
#include "array.h"
#include "path.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS
#include "resource_asset_builtins.h"
#include "resource_assets_types.h"
#include "test.h"

#include <math.h>
#endif

/* Error codes (non-zero = failure; match core convention). */
enum {
	SK_RES_SER_OK = 0,
	SK_RES_SER_ERR = -1,		 /* OOM / generic / parse */
	SK_RES_SER_INVALID = -2,	 /* bad format / version / type / shape */
	SK_RES_SER_MISSING_REF = -3, /* package: unresolved UUID */
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

static i32 rid_list_contains(const sk_rid_list_t* list, sk_rid_t rid) {
	for (u32 i = 0u; i < list->count; ++i) {
		if (SK_RID_EQ(list->items[i], rid)) {
			return 1;
		}
	}
	return 0;
}

static i32 rid_list_push_unique(sk_rid_list_t* list, sk_rid_t rid) {
	if (rid.id == 0u || rid_list_contains(list, rid)) {
		return SK_RES_SER_OK;
	}
	if (sk_array_push(list, rid) != 0) {
		return SK_RES_SER_ERR;
	}
	return SK_RES_SER_OK;
}

/* BFS (non-recursive) collect of resources reachable via ref / subobject edges. */
static i32 collect_reachable(sk_repository_t* repository, sk_rid_t root, sk_rid_list_t* out) {
	const sk_repository_api_t* api = sk_repository_api();
	if (!api->has_resource(repository, root)) {
		return SK_RES_SER_INVALID;
	}
	if (rid_list_push_unique(out, root) != SK_RES_SER_OK) {
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
					if (rid_list_push_unique(out, child) != SK_RES_SER_OK) {
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
						if (rid_list_push_unique(out, items[i]) != SK_RES_SER_OK) {
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
						if (rid_list_push_unique(out, items[i]) != SK_RES_SER_OK) {
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
		u64 id = api->get_buffer(view, field->index);
		writer->begin_map_named(writer->instance, name);
		writer->write_uint(writer->instance, sk_str_view_cstr("id"), id);
		writer->end_map(writer->instance);
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
	sk_rid_t rid = api->find_by_uuid(ctx->repository, uuid);
	if (rid.id == 0u) {
		if (ctx->package_mode) {
			return SK_RES_SER_MISSING_REF;
		}
		return SK_RES_SER_OK; /* dangling allowed for single-document loads */
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
		if (!reader->begin_map_named(reader->instance, name)) {
			return SK_RES_SER_OK;
		}
		u64 id = reader->read_uint(reader->instance, sk_str_view_cstr("id"));
		reader->end_map(reader->instance);
		if (api->set_buffer(view, field->index, id) != 0) {
			return SK_RES_SER_FIELD;
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

	sk_rid_t rid = api->create_resource(ctx->repository, type, uuid, NULL);
	if (rid.id == 0u) {
		return SK_RES_SER_ERR;
	}
	*out_rid = rid;

	if (!apply_fields) {
		return SK_RES_SER_OK;
	}

	sk_resource_object_t view = api->write(ctx->repository, rid);
	if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
		return SK_RES_SER_ERR;
	}
	i32 arc = apply_fields_map(ctx, view, type, reader);
	if (arc != SK_RES_SER_OK) {
		api->discard(view);
		return arc;
	}
	api->commit(view, NULL);
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

	sk_res_ser_resolve_ctx_t ctx;
	ctx.repository = repository;
	ctx.package_mode = 1;

	/* Create shells + apply scalar fields; queue ref UUID patches for a second
	 * pass so forward references resolve after every resource exists. */
	sk_rid_list_t created;
	sk_array_init(&created, sk_allocator_default());

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

	while (reader->next_seq_entry(reader->instance)) {
		reader->begin_map(reader->instance);

		i32 env = validate_envelope(reader, SK_RESOURCE_JSON_FORMAT, NULL);
		if (env != SK_RES_SER_OK) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return env;
		}

		sk_str_view_t type_name = reader->read_string(reader->instance, sk_str_view_cstr("type"));
		if (type_name.size == 0u || type_name.size >= 256u) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_INVALID;
		}
		char type_buf[256];
		memcpy(type_buf, type_name.data, (size_t)type_name.size);
		type_buf[type_name.size] = '\0';
		const sk_resource_type_t* type = api->find_type_by_name(repository, type_buf);
		if (type == NULL) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_INVALID;
		}

		sk_uuid_t uuid = SK_UUID_ZERO;
		sk_str_view_t uuid_s = reader->read_string(reader->instance, sk_str_view_cstr("uuid"));
		if (uuid_s.size > 0u && uuid_parse(uuid_s, &uuid) != SK_RES_SER_OK) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_INVALID;
		}

		sk_rid_t rid = api->create_resource(repository, type, uuid, NULL);
		if (rid.id == 0u) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_ERR;
		}
		if (sk_array_push(&created, rid) != 0) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_ERR;
		}

		/* Apply non-ref fields now; queue refs. */
		if (!reader->begin_map_named(reader->instance, sk_str_view_cstr("fields"))) {
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_INVALID;
		}

		sk_resource_object_t view = api->write(repository, rid);
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			reader->end_map(reader->instance);
			reader->end_map(reader->instance);
			reader->end_seq(reader->instance);
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_ERR;
		}

		while (reader->next_map_entry(reader->instance)) {
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
					reader->end_map(reader->instance);
					reader->end_map(reader->instance);
					reader->end_seq(reader->instance);
					sk_array_free(&pending);
					sk_array_free(&created);
					return SK_RES_SER_INVALID;
				}
				sk_pending_ref_t p;
				memset(&p, 0, sizeof(p));
				p.owner = rid;
				p.field_index = field->index;
				p.kind = (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE) ? (u8)0u : (u8)1u;
				p.uuid = target;
				if (sk_array_push(&pending, p) != 0) {
					api->discard(view);
					reader->end_map(reader->instance);
					reader->end_map(reader->instance);
					reader->end_seq(reader->instance);
					sk_array_free(&pending);
					sk_array_free(&created);
					return SK_RES_SER_ERR;
				}
				continue;
			}

			if (field->type == SK_RESOURCE_FIELD_TYPE_REFERENCE_ARRAY || field->type == SK_RESOURCE_FIELD_TYPE_SUB_OBJECT_LIST) {
				if (!reader->begin_seq_named(reader->instance, sk_str_view_cstr(field->name))) {
					continue;
				}
				while (reader->next_seq_entry(reader->instance)) {
					sk_str_view_t us = reader->get_string(reader->instance);
					if (us.size == 0u) {
						continue;
					}
					sk_uuid_t target = SK_UUID_ZERO;
					if (uuid_parse(us, &target) != SK_RES_SER_OK) {
						api->discard(view);
						reader->end_seq(reader->instance);
						reader->end_map(reader->instance);
						reader->end_map(reader->instance);
						reader->end_seq(reader->instance);
						sk_array_free(&pending);
						sk_array_free(&created);
						return SK_RES_SER_INVALID;
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
						reader->end_map(reader->instance);
						reader->end_map(reader->instance);
						reader->end_seq(reader->instance);
						sk_array_free(&pending);
						sk_array_free(&created);
						return SK_RES_SER_ERR;
					}
				}
				reader->end_seq(reader->instance);
				continue;
			}

			/* Scalar / blob / type_id / buffer — apply immediately. */
			ctx.package_mode = 0;
			i32 arc = apply_field_value(&ctx, view, field, reader);
			if (arc != SK_RES_SER_OK) {
				api->discard(view);
				reader->end_map(reader->instance);
				reader->end_map(reader->instance);
				reader->end_seq(reader->instance);
				sk_array_free(&pending);
				sk_array_free(&created);
				return arc;
			}
		}
		reader->end_map(reader->instance); /* fields */
		api->commit(view, NULL);
		reader->end_map(reader->instance); /* resource */
	}
	reader->end_seq(reader->instance);

	/* Resolve pending references now that every UUID shell exists. */
	for (u32 i = 0u; i < pending.count; ++i) {
		sk_pending_ref_t* p = &pending.items[i];
		sk_rid_t target = api->find_by_uuid(repository, p->uuid);
		if (target.id == 0u) {
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_MISSING_REF;
		}
		sk_resource_object_t view = api->write(repository, p->owner);
		if (!SK_RESOURCE_OBJECT_IS_VALID(view)) {
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_ERR;
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
			sk_array_free(&pending);
			sk_array_free(&created);
			return SK_RES_SER_FIELD;
		}
		api->commit(view, NULL);
	}

	sk_array_free(&pending);
	sk_array_free(&created);

	sk_rid_t root = api->find_by_uuid(repository, root_uuid);
	if (root.id == 0u) {
		return SK_RES_SER_MISSING_REF;
	}
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
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("/tmp/a.mesh", api->get_string(r, SK_RESOURCE_ASSET_FILE_FIELD_ABSOLUTE_PATH));
	TEST_ASSERT_EQUAL_UINT64(7u, api->get_uint(r, SK_RESOURCE_ASSET_FILE_FIELD_PERSISTED_VERSION));
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
	TEST_ASSERT_EQUAL_INT(1, api->get_bool(r, SK_RESOURCE_ASSET_FIELD_READ_ONLY));
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
	TEST_ASSERT_EQUAL_INT(0, api->set_subobject_list(w, SK_RESOURCE_ASSET_DIRECTORY_FIELD_ASSETS, NULL, 0u));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	TEST_ASSERT_TRUE(loaded.id != 0u);
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
	TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA, 99u));
	sk_type_id_t tid = SK_TEXTURE_RESOURCE_TYPE_ID;
	TEST_ASSERT_EQUAL_INT(0, api->set_type_id(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID, tid));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("tex.png", api->get_string(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_FILE_NAME));
	TEST_ASSERT_EQUAL_UINT64(3u, api->get_uint(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_COOKER_VERSION));
	TEST_ASSERT_EQUAL_UINT64(99u, api->get_buffer(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA));
	sk_type_id_t got = api->get_type_id(r, SK_RESOURCE_IMPORTED_ASSET_FIELD_IMPORTER_ID);
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(got, tid));
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
	TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_DATA, 42u));
	TEST_ASSERT_EQUAL_INT(0, api->set_uint(w, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_SIZE, 1024u));
	api->commit(w, NULL);

	char* json = NULL;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_serialize_json_alloc(repo, rid, a, &json, NULL));
	api->destroy_resource(repo, rid, NULL);
	sk_rid_t loaded = SK_RID_ZERO;
	TEST_ASSERT_EQUAL_INT(0, sk_resource_deserialize_json_string(repo, sk_str_view_cstr(json), a, &loaded));
	sk_resource_object_t r = api->read(repo, loaded);
	TEST_ASSERT_EQUAL_STRING("deps/a.png", api->get_string(r, SK_RESOURCE_DEPENDENCY_ENTRY_FIELD_REL_PATH));
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
	const sk_allocator_t* a = sk_allocator_default();
	sk_repository_t* repo = ser_test_repo();
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
	sk_repository_api()->destroy(repo);
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
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(w, SK_RESOURCE_IMPORTED_ASSET_FIELD_ORIGINAL_DATA, 0u));
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

#endif /* SK_TESTS */
