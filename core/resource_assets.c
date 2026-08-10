/**
 * @file resource_assets.c
 * @brief Asset handler registry lookup and null-safe dispatch over add_impl.
 */

#include "resource_assets.h"

#include "allocator.h"

#include <string.h>

/* Default load order when get_load_order is NULL (main used INT32_MAX). */
#define SK_RESOURCE_ASSET_HANDLER_DEFAULT_LOAD_ORDER ((i32)0x7fffffff)

/* Stack capacity for handler enumeration before falling back to heap. */
#define SK_RESOURCE_ASSET_HANDLER_STACK_CAP 32u

/* ------------------------------------------------------------------ */
/*  Registry: count / get_all / find                                   */
/* ------------------------------------------------------------------ */

u32 sk_resource_asset_handler_count(sk_app_context_t* context, const sk_app_api_t* app_api) {
	return app_api->impl_count(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID);
}

u32 sk_resource_asset_handler_get_all(sk_app_context_t* context, const sk_app_api_t* app_api, const sk_resource_asset_handler_t** out, u32 out_cap) {
	/* get_all_impls stores opaque const_ptr_t; layout-compatible with handler*. */
	return app_api->get_all_impls(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, (const_ptr_t*)out, out_cap);
}

/**
 * Borrow registered handler pointers into @p stack_buf (capacity
 * SK_RESOURCE_ASSET_HANDLER_STACK_CAP) or a heap buffer when more are
 * registered. Caller must free *out_heap with the default allocator when
 * non-NULL.
 *
 * @return Total handler count; *out_items points at stack_buf or *out_heap.
 */
static u32 handler_snapshot(sk_app_context_t* context, const sk_app_api_t* app_api, const_ptr_t* stack_buf, const_ptr_t** out_items, const_ptr_t** out_heap) {
	u32 total = app_api->impl_count(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID);
	*out_heap = NULL;
	*out_items = stack_buf;

	if (total == 0u) {
		return 0u;
	}

	if (total <= SK_RESOURCE_ASSET_HANDLER_STACK_CAP) {
		(void)app_api->get_all_impls(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, stack_buf, SK_RESOURCE_ASSET_HANDLER_STACK_CAP);
		return total;
	}

	{
		const sk_allocator_t* alloc = sk_allocator_default();
		const_ptr_t* heap = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)total * sizeof(const_ptr_t));
		if (heap == NULL) {
			return 0u;
		}
		(void)app_api->get_all_impls(context, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, heap, total);
		*out_heap = heap;
		*out_items = heap;
		return total;
	}
}

static void handler_snapshot_free(const_ptr_t* heap) {
	if (heap != NULL) {
		const sk_allocator_t* alloc = sk_allocator_default();
		alloc->free(alloc->instance, heap);
	}
}

const sk_resource_asset_handler_t* sk_resource_asset_handler_find_by_extension(sk_app_context_t* context, const sk_app_api_t* app_api, const_chr_t extension) {
	const_ptr_t stack_buf[SK_RESOURCE_ASSET_HANDLER_STACK_CAP];
	const_ptr_t* items = NULL;
	const_ptr_t* heap = NULL;
	u32 total;
	u32 i;
	const sk_resource_asset_handler_t* found = NULL;

	total = handler_snapshot(context, app_api, stack_buf, &items, &heap);
	for (i = 0u; i < total; i++) {
		const sk_resource_asset_handler_t* handler = (const sk_resource_asset_handler_t*)items[i];
		const_chr_t claim;

		if (handler == NULL || handler->extension == NULL) {
			continue;
		}
		claim = handler->extension(handler->user_data);
		if (claim != NULL && strcmp(claim, extension) == 0) {
			found = handler;
			break;
		}
	}
	handler_snapshot_free(heap);
	return found;
}

const sk_resource_asset_handler_t* sk_resource_asset_handler_find_by_resource_type(sk_app_context_t* context, const sk_app_api_t* app_api, sk_type_id_t type_id) {
	const_ptr_t stack_buf[SK_RESOURCE_ASSET_HANDLER_STACK_CAP];
	const_ptr_t* items = NULL;
	const_ptr_t* heap = NULL;
	u32 total;
	u32 i;
	const sk_resource_asset_handler_t* found = NULL;

	if (SK_TYPE_ID_EQ(type_id, SK_TYPE_ID_ZERO)) {
		return NULL;
	}

	total = handler_snapshot(context, app_api, stack_buf, &items, &heap);
	for (i = 0u; i < total; i++) {
		const sk_resource_asset_handler_t* handler = (const sk_resource_asset_handler_t*)items[i];
		sk_type_id_t claimed;

		if (handler == NULL || handler->get_resource_type_id == NULL) {
			continue;
		}
		claimed = handler->get_resource_type_id(handler->user_data);
		if (SK_TYPE_ID_EQ(claimed, type_id)) {
			found = handler;
			break;
		}
	}
	handler_snapshot_free(heap);
	return found;
}

/* ------------------------------------------------------------------ */
/*  Null-safe dispatch                                                 */
/* ------------------------------------------------------------------ */

const_chr_t sk_resource_asset_handler_extension(const sk_resource_asset_handler_t* handler) {
	const_chr_t value;
	if (handler->extension == NULL) {
		return "";
	}
	value = handler->extension(handler->user_data);
	return (value != NULL) ? value : "";
}

void sk_resource_asset_handler_open_asset(const sk_resource_asset_handler_t* handler, sk_rid_t asset) {
	if (handler->open_asset != NULL) {
		handler->open_asset(handler->user_data, asset);
	}
}

sk_type_id_t sk_resource_asset_handler_get_resource_type_id(const sk_resource_asset_handler_t* handler) {
	if (handler->get_resource_type_id == NULL) {
		return SK_TYPE_ID_ZERO;
	}
	return handler->get_resource_type_id(handler->user_data);
}

const_chr_t sk_resource_asset_handler_get_desc(const sk_resource_asset_handler_t* handler) {
	const_chr_t value;
	if (handler->get_desc == NULL) {
		return "";
	}
	value = handler->get_desc(handler->user_data);
	return (value != NULL) ? value : "";
}

sk_rid_t sk_resource_asset_handler_load(const sk_resource_asset_handler_t* handler, sk_rid_t asset, const_chr_t absolute_path) {
	if (handler->load == NULL) {
		return SK_RID_ZERO;
	}
	return handler->load(handler->user_data, asset, absolute_path);
}

void sk_resource_asset_handler_save(const sk_resource_asset_handler_t* handler, sk_rid_t object, const_chr_t absolute_path) {
	if (handler->save != NULL) {
		handler->save(handler->user_data, object, absolute_path);
	}
}

sk_rid_t sk_resource_asset_handler_create(const sk_resource_asset_handler_t* handler, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	if (handler->create == NULL) {
		return SK_RID_ZERO;
	}
	return handler->create(handler->user_data, uuid, scope);
}

void sk_resource_asset_handler_reloaded(const sk_resource_asset_handler_t* handler, sk_rid_t asset, const_chr_t absolute_path) {
	if (handler->reloaded != NULL) {
		handler->reloaded(handler->user_data, asset, absolute_path);
	}
}

void sk_resource_asset_handler_after_move(const sk_resource_asset_handler_t* handler, sk_rid_t asset, const_chr_t old_absolute_path, const_chr_t new_absolute_path) {
	if (handler->after_move != NULL) {
		handler->after_move(handler->user_data, asset, old_absolute_path, new_absolute_path);
	}
}

void sk_resource_asset_handler_export_object(const sk_resource_asset_handler_t* handler, sk_rid_t object, sk_archive_writer_t* writer) {
	if (handler->export_object != NULL) {
		handler->export_object(handler->user_data, object, writer);
	}
}

const_chr_t sk_resource_asset_handler_get_icon(const sk_resource_asset_handler_t* handler) {
	const_chr_t value;
	if (handler->get_icon == NULL) {
		return "";
	}
	value = handler->get_icon(handler->user_data);
	return (value != NULL) ? value : "";
}

i32 sk_resource_asset_handler_get_load_order(const sk_resource_asset_handler_t* handler) {
	if (handler->get_load_order == NULL) {
		return SK_RESOURCE_ASSET_HANDLER_DEFAULT_LOAD_ORDER;
	}
	return handler->get_load_order(handler->user_data);
}

i32 sk_resource_asset_handler_get_asset_name(const sk_resource_asset_handler_t* handler, sk_rid_t rid, char* out_name, u32 out_cap) {
	if (handler->get_asset_name == NULL) {
		return 0;
	}
	return handler->get_asset_name(handler->user_data, rid, out_name, out_cap);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "test.h"
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

/* ---- Dummy handler call tracking for end-to-end registration tests ---- */

typedef struct dummy_handler_state_t {
	u32 extension_calls;
	u32 open_asset_calls;
	u32 get_resource_type_id_calls;
	u32 get_desc_calls;
	u32 load_calls;
	u32 save_calls;
	u32 create_calls;
	u32 reloaded_calls;
	u32 after_move_calls;
	u32 export_object_calls;
	u32 get_icon_calls;
	u32 get_load_order_calls;
	u32 get_asset_name_calls;
	sk_rid_t last_asset;
	sk_rid_t last_object;
	sk_uuid_t last_uuid;
	const_chr_t last_path;
	const_chr_t last_old_path;
	const_chr_t last_new_path;
	sk_archive_writer_t* last_writer;
	sk_undo_redo_scope_t* last_scope;
	char last_name_buf[64];
	u32 last_name_cap;
} dummy_handler_state_t;

/* Local type id for the dummy handler (not a file-scope compound literal). */
#define DUMMY_RESOURCE_TYPE_ID SK_TYPE_ID("test.dummy_resource", 0xaaaaaaaaaaaaaaaaULL, 0xbbbbbbbbbbbbbbbbULL)

static const_chr_t dummy_extension(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->extension_calls += 1u;
	return ".dummy";
}

static void dummy_open_asset(void_ptr_t user_data, sk_rid_t asset) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->open_asset_calls += 1u;
	s->last_asset = asset;
}

static sk_type_id_t dummy_get_resource_type_id(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_resource_type_id_calls += 1u;
	return DUMMY_RESOURCE_TYPE_ID;
}

static const_chr_t dummy_get_desc(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_desc_calls += 1u;
	return "Dummy Asset";
}

static sk_rid_t dummy_load(void_ptr_t user_data, sk_rid_t asset, const_chr_t absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->load_calls += 1u;
	s->last_asset = asset;
	s->last_path = absolute_path;
	return (sk_rid_t){42ull};
}

static void dummy_save(void_ptr_t user_data, sk_rid_t object, const_chr_t absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->save_calls += 1u;
	s->last_object = object;
	s->last_path = absolute_path;
}

static sk_rid_t dummy_create(void_ptr_t user_data, sk_uuid_t uuid, sk_undo_redo_scope_t* scope) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->create_calls += 1u;
	s->last_uuid = uuid;
	s->last_scope = scope;
	return (sk_rid_t){7ull};
}

static void dummy_reloaded(void_ptr_t user_data, sk_rid_t asset, const_chr_t absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->reloaded_calls += 1u;
	s->last_asset = asset;
	s->last_path = absolute_path;
}

static void dummy_after_move(void_ptr_t user_data, sk_rid_t asset, const_chr_t old_absolute_path, const_chr_t new_absolute_path) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->after_move_calls += 1u;
	s->last_asset = asset;
	s->last_old_path = old_absolute_path;
	s->last_new_path = new_absolute_path;
}

static void dummy_export_object(void_ptr_t user_data, sk_rid_t object, sk_archive_writer_t* writer) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->export_object_calls += 1u;
	s->last_object = object;
	s->last_writer = writer;
}

static const_chr_t dummy_get_icon(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_icon_calls += 1u;
	return "icon-dummy";
}

static i32 dummy_get_load_order(void_ptr_t user_data) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_load_order_calls += 1u;
	return 10;
}

static i32 dummy_get_asset_name(void_ptr_t user_data, sk_rid_t rid, char* out_name, u32 out_cap) {
	dummy_handler_state_t* s = (dummy_handler_state_t*)user_data;
	s->get_asset_name_calls += 1u;
	s->last_asset = rid;
	s->last_name_cap = out_cap;
	if (out_name != NULL && out_cap > 0u) {
		const char* name = "dummy-name";
		size_t len = strlen(name);
		if (len + 1u > (size_t)out_cap) {
			len = (size_t)out_cap - 1u;
		}
		memcpy(out_name, name, len);
		out_name[len] = '\0';
		memcpy(s->last_name_buf, out_name, len + 1u);
	}
	return 1;
}

/**
 * End-to-end: register a static dummy handler via add_impl, resolve by
 * extension, and assert every function pointer is invoked through null-safe
 * dispatch. Format-independent (no real asset I/O).
 */
SK_TEST(resource_asset_handler_add_impl_lookup_and_dispatch) {
	static dummy_handler_state_t state;
	static sk_resource_asset_handler_t handler;

	sk_app_context_t* ctx;
	const sk_app_api_t* api;
	const sk_resource_asset_handler_t* found;
	const sk_resource_asset_handler_t* found_by_type;
	const sk_resource_asset_handler_t* all[4];
	sk_rid_t load_rid;
	sk_rid_t create_rid;
	sk_rid_t probe = {99ull};
	sk_uuid_t uuid = {0x1111111111111111ULL, 0x2222222222222222ULL};
	sk_archive_writer_t writer;
	char name_buf[32];
	i32 name_ok;

	memset(&state, 0, sizeof(state));
	memset(&handler, 0, sizeof(handler));
	memset(&writer, 0, sizeof(writer));

	handler.user_data = &state;
	handler.extension = dummy_extension;
	handler.open_asset = dummy_open_asset;
	handler.get_resource_type_id = dummy_get_resource_type_id;
	handler.get_desc = dummy_get_desc;
	handler.load = dummy_load;
	handler.save = dummy_save;
	handler.create = dummy_create;
	handler.reloaded = dummy_reloaded;
	handler.after_move = dummy_after_move;
	handler.export_object = dummy_export_object;
	handler.get_icon = dummy_get_icon;
	handler.get_load_order = dummy_get_load_order;
	handler.get_asset_name = dummy_get_asset_name;

	ctx = sk_app_create();
	TEST_ASSERT_NOT_NULL(ctx);
	api = sk_app_api();
	TEST_ASSERT_NOT_NULL(api);

	TEST_ASSERT_EQUAL_UINT32(0u, sk_resource_asset_handler_count(ctx, api));
	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_extension(ctx, api, ".dummy"));

	api->add_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &handler);

	TEST_ASSERT_EQUAL_UINT32(1u, sk_resource_asset_handler_count(ctx, api));
	TEST_ASSERT_EQUAL_UINT32(1u, sk_resource_asset_handler_get_all(ctx, api, all, 4u));
	TEST_ASSERT_EQUAL_PTR(&handler, all[0]);

	found = sk_resource_asset_handler_find_by_extension(ctx, api, ".dummy");
	TEST_ASSERT_EQUAL_PTR(&handler, found);
	TEST_ASSERT_TRUE(state.extension_calls >= 1u);

	found_by_type = sk_resource_asset_handler_find_by_resource_type(ctx, api, DUMMY_RESOURCE_TYPE_ID);
	TEST_ASSERT_EQUAL_PTR(&handler, found_by_type);
	TEST_ASSERT_TRUE(state.get_resource_type_id_calls >= 1u);

	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_extension(ctx, api, ".missing"));
	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_resource_type(ctx, api, SK_TYPE_ID_ZERO));

	/* Dispatch every entry through the null-safe free functions. */
	TEST_ASSERT_EQUAL_STRING(".dummy", sk_resource_asset_handler_extension(found));
	TEST_ASSERT_EQUAL_STRING("Dummy Asset", sk_resource_asset_handler_get_desc(found));
	TEST_ASSERT_EQUAL_STRING("icon-dummy", sk_resource_asset_handler_get_icon(found));
	TEST_ASSERT_EQUAL_INT(10, sk_resource_asset_handler_get_load_order(found));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_asset_handler_get_resource_type_id(found), DUMMY_RESOURCE_TYPE_ID));

	sk_resource_asset_handler_open_asset(found, probe);
	TEST_ASSERT_EQUAL_UINT32(1u, state.open_asset_calls);
	TEST_ASSERT_TRUE(SK_RID_EQ(state.last_asset, probe));

	load_rid = sk_resource_asset_handler_load(found, probe, "/tmp/x.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.load_calls);
	TEST_ASSERT_EQUAL_UINT64(42ull, load_rid.id);
	TEST_ASSERT_EQUAL_STRING("/tmp/x.dummy", state.last_path);

	sk_resource_asset_handler_save(found, (sk_rid_t){3ull}, "/tmp/y.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.save_calls);
	TEST_ASSERT_EQUAL_UINT64(3ull, state.last_object.id);

	create_rid = sk_resource_asset_handler_create(found, uuid, NULL);
	TEST_ASSERT_EQUAL_UINT32(1u, state.create_calls);
	TEST_ASSERT_EQUAL_UINT64(7ull, create_rid.id);
	TEST_ASSERT_TRUE(SK_UUID_EQ(state.last_uuid, uuid));
	TEST_ASSERT_NULL(state.last_scope);

	sk_resource_asset_handler_reloaded(found, probe, "/tmp/z.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.reloaded_calls);

	sk_resource_asset_handler_after_move(found, probe, "/old.dummy", "/new.dummy");
	TEST_ASSERT_EQUAL_UINT32(1u, state.after_move_calls);
	TEST_ASSERT_EQUAL_STRING("/old.dummy", state.last_old_path);
	TEST_ASSERT_EQUAL_STRING("/new.dummy", state.last_new_path);

	sk_resource_asset_handler_export_object(found, (sk_rid_t){5ull}, &writer);
	TEST_ASSERT_EQUAL_UINT32(1u, state.export_object_calls);
	TEST_ASSERT_EQUAL_PTR(&writer, state.last_writer);

	name_ok = sk_resource_asset_handler_get_asset_name(found, probe, name_buf, (u32)sizeof(name_buf));
	TEST_ASSERT_EQUAL_INT(1, name_ok);
	TEST_ASSERT_EQUAL_STRING("dummy-name", name_buf);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_asset_name_calls);

	/* Every fp must have been invoked at least once (lookup + dispatch). */
	TEST_ASSERT_TRUE(state.extension_calls >= 1u);
	TEST_ASSERT_EQUAL_UINT32(1u, state.open_asset_calls);
	TEST_ASSERT_TRUE(state.get_resource_type_id_calls >= 1u);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_desc_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.load_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.save_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.create_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.reloaded_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.after_move_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.export_object_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_icon_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_load_order_calls);
	TEST_ASSERT_EQUAL_UINT32(1u, state.get_asset_name_calls);

	api->remove_impl(ctx, SK_RESOURCE_ASSET_HANDLER_TYPE_ID, &handler);
	TEST_ASSERT_EQUAL_UINT32(0u, sk_resource_asset_handler_count(ctx, api));
	TEST_ASSERT_NULL(sk_resource_asset_handler_find_by_extension(ctx, api, ".dummy"));

	sk_app_destroy(ctx);
}

/**
 * NULL function pointers must not crash: defaults / no-ops only.
 */
SK_TEST(resource_asset_handler_null_fp_dispatch_defaults) {
	static sk_resource_asset_handler_t empty = {
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
	sk_rid_t rid = {1ull};
	sk_uuid_t uuid = SK_UUID_ZERO;
	sk_archive_writer_t writer;
	char name[8];

	memset(&writer, 0, sizeof(writer));
	name[0] = 'x';

	TEST_ASSERT_EQUAL_STRING("", sk_resource_asset_handler_extension(&empty));
	TEST_ASSERT_EQUAL_STRING("", sk_resource_asset_handler_get_desc(&empty));
	TEST_ASSERT_EQUAL_STRING("", sk_resource_asset_handler_get_icon(&empty));
	TEST_ASSERT_EQUAL_INT(SK_RESOURCE_ASSET_HANDLER_DEFAULT_LOAD_ORDER, sk_resource_asset_handler_get_load_order(&empty));
	TEST_ASSERT_TRUE(SK_TYPE_ID_EQ(sk_resource_asset_handler_get_resource_type_id(&empty), SK_TYPE_ID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(sk_resource_asset_handler_load(&empty, rid, "/x"), SK_RID_ZERO));
	TEST_ASSERT_TRUE(SK_RID_EQ(sk_resource_asset_handler_create(&empty, uuid, NULL), SK_RID_ZERO));
	TEST_ASSERT_EQUAL_INT(0, sk_resource_asset_handler_get_asset_name(&empty, rid, name, (u32)sizeof(name)));

	/* No-ops — must not crash. */
	sk_resource_asset_handler_open_asset(&empty, rid);
	sk_resource_asset_handler_save(&empty, rid, "/x");
	sk_resource_asset_handler_reloaded(&empty, rid, "/x");
	sk_resource_asset_handler_after_move(&empty, rid, "/a", "/b");
	sk_resource_asset_handler_export_object(&empty, rid, &writer);
}

#endif /* SK_TESTS */
