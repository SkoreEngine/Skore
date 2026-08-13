/*
 * resource_object_buffer.c — end-to-end SK_RESOURCE_FIELD_TYPE_BUFFER
 * integration tests (APX-184).
 *
 * Drives sk_resource_object_t's Buffer field storage against the on-disk
 * dummy resource fixtures in tests/data/resource_object/ (APX-183):
 *
 *   - load a fixture payload from disk and verify the buffer read back from a
 *     resource is byte-for-byte identical, size included;
 *   - save the buffer to a temp file, reload it, and verify the round-trip
 *     (including after an unrelated overwrite in between);
 *   - overwrite an existing buffer and prove the old payload is released
 *     (counting allocator: live allocation count returns to baseline after
 *     garbage_collect, and destroying the repository frees everything) and
 *     that reads observe only the new bytes;
 *   - empty-buffer and missing-field cases return the documented sentinel
 *     (NULL pointer, size 0) instead of crashing;
 *   - the >1 MiB large fixture round-trips intact through
 *     set → save → reload → set.
 *
 * Temp files are created under the OS temp folder with unique names
 * (per-process counter + monotonic clock, so parallel ctest runs never
 * collide). All file IO for one round-trip is completed and the temp file
 * removed before any assertion that could fail, so no temp file survives a
 * failing test.
 */

#include "resource_object_fixtures.h"

#include "allocator.h"
#include "app.h"
#include "common.h"
#include "filesystem.h"
#include "path.h"
#include "platform.h"
#include "repository.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

#ifdef SK_TESTS

/* ---- test resource type: one Buffer field + one Blob field ---- */

typedef struct it_buffer_object_t {
	sk_field_buffer_t buffer;
	sk_field_blob_t blob;
} it_buffer_object_t;

enum { IT_BUF_FIELD_BUFFER = 0u, IT_BUF_FIELD_BLOB = 1u, IT_BUF_FIELD_COUNT = 2u };

static const sk_resource_field_t it_buffer_fields[IT_BUF_FIELD_COUNT] = {
	{"Data", IT_BUF_FIELD_BUFFER, SK_RESOURCE_FIELD_TYPE_BUFFER, (u32)offsetof(it_buffer_object_t, buffer), (u32)sizeof(sk_field_buffer_t), {0ull, 0ull}},
	{"Blob", IT_BUF_FIELD_BLOB, SK_RESOURCE_FIELD_TYPE_BLOB, (u32)offsetof(it_buffer_object_t, blob), (u32)sizeof(sk_field_blob_t), {0ull, 0ull}},
};

/* Distinct per-tag type ids; each test uses its own repository, so values may
 * repeat across tests. */
static sk_type_id_t it_buffer_type_id(u64 tag) {
	return (sk_type_id_t){tag * 0x9e3779b97f4a7c15ull + 1ull, tag};
}

static sk_repository_t* it_buffer_repo(const sk_resource_type_t** out_type, const sk_allocator_t* allocator, u64 tag) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	sk_repository_t* repo = api->create(allocator);
	TEST_ASSERT_NOT_NULL(repo);
	sk_resource_type_desc_t desc;
	desc.type_id = it_buffer_type_id(tag);
	desc.name = "TestBufferResource";
	desc.instance_size = (u32)sizeof(it_buffer_object_t);
	desc.fields = it_buffer_fields;
	desc.field_count = IT_BUF_FIELD_COUNT;
	desc.defaults = NULL;
	TEST_ASSERT_EQUAL_INT(0, api->register_type(repo, &desc));
	*out_type = api->find_type_by_name(repo, "TestBufferResource");
	TEST_ASSERT_NOT_NULL(*out_type);
	return repo;
}

/* ---- counting allocator: proves overwrites / destroys release payloads ---- */

typedef struct it_counting_alloc_t {
	const sk_allocator_t* base;
	u64 live;
} it_counting_alloc_t;

static void_ptr_t it_counting_alloc(void_ptr_t instance, size_t size) {
	it_counting_alloc_t* state = (it_counting_alloc_t*)instance;
	void_ptr_t p = state->base->alloc(state->base->instance, size);
	if (p != NULL) {
		state->live += 1u;
	}
	return p;
}

static void it_counting_free(void_ptr_t instance, void_ptr_t ptr) {
	it_counting_alloc_t* state = (it_counting_alloc_t*)instance;
	if (ptr != NULL) {
		state->live -= 1u;
	}
	state->base->free(state->base->instance, ptr);
}

static void_ptr_t it_counting_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	it_counting_alloc_t* state = (it_counting_alloc_t*)instance;
	void_ptr_t p = state->base->realloc(state->base->instance, ptr, size);
	if (p != NULL && ptr == NULL) {
		state->live += 1u;
	}
	return p;
}

/* ---- temp-file helpers (unique names; removed on every path) ---- */

static i32 it_temp_path(const_chr_t tag, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	static u64 counter; /* process-lifetime; integration tests run single-threaded */
	char dir[SK_FS_PATH_MAX];
	char name[128];

	if (fs->temp_folder(dir, (u32)sizeof(dir)) != 0 || dir[0] == '\0') {
		return -1;
	}
	counter += 1u;
	/* Monotonic clock (not wall time) uniquifies across ctest runs; the
	 * counter uniquifies within one run. Never asserted on, so determinism
	 * is unaffected. */
	u64 mono_ms = (u64)(sk_test_platform_table()->monotonic_seconds() * 1000.0);
	snprintf(name, sizeof(name), "sk_buffer_it_%s_%llu_%llu.bin", tag, mono_ms, counter);
	return sk_path_join(sk_str_view_cstr(dir), sk_str_view_cstr(name), out, out_cap) < 0 ? -1 : 0;
}

/* Write @p size bytes to @p path (create/truncate). Removes the file on
 * failure so a failed round-trip never leaves temp junk behind. */
static i32 it_file_write_all(const_chr_t path, const void* data, u64 size) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_WRITE);
	if (file == NULL) {
		fs->remove(path);
		return -1;
	}
	/* size_t is unsigned long long on Windows (LLP64), so an explicit
	 * (size_t) cast would be redundant there; the implicit conversion is
	 * value-preserving on every supported platform (matches
	 * resource_asset_builtins.c). */
	u64 written = fs->write_file(file, data, size);
	fs->close_file(file);
	if (written != size) {
		fs->remove(path);
		return -1;
	}
	return 0;
}

/* Read exactly @p size bytes from @p path into @p data. */
static i32 it_file_read_all(const_chr_t path, void* data, u64 size) {
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		return -1;
	}
	u64 got = fs->read_file(file, data, size);
	fs->close_file(file);
	return got == size ? 0 : -1;
}

/* ---- tests ---- */

/* Load each present fixture from disk, set it on a resource, and verify the
 * buffer read back matches the on-disk bytes and size byte-for-byte. */
SK_TEST(resource_object_buffer_fixture_load_byte_for_byte) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = it_buffer_repo(&type, alloc, 184u);
	sk_resource_fixture_payload_t fixture;

	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_SMALL, alloc, &fixture));
	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, fixture.data, fixture.size));
		api->commit(view, NULL);
	}
	{
		char payload_path[SK_FS_PATH_MAX];
		TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_payload_path(SK_RESOURCE_FIXTURE_BUFFER_SMALL, payload_path, (u32)sizeof(payload_path)));
		/* On-disk file size == fixture size == stored buffer size. */
		TEST_ASSERT_EQUAL_UINT64((u64)fixture.size, fs->get_path_size(payload_path));

		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_NOT_NULL(data);
		TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_SMALL_SIZE, size);
		TEST_ASSERT_EQUAL_UINT32(fixture.size, size);
		TEST_ASSERT_EQUAL_MEMORY(fixture.data, data, fixture.size);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(read, IT_BUF_FIELD_BUFFER));
	}
	sk_resource_fixture_free_payload(&fixture, alloc);

	/* Same exercise for the >1 MiB fixture, spot-checking the pattern. */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_LARGE, alloc, &fixture));
	TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_LARGE_SIZE, fixture.size);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, fixture.data, fixture.size));
		api->commit(view, NULL);
	}
	{
		char payload_path[SK_FS_PATH_MAX];
		TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_payload_path(SK_RESOURCE_FIXTURE_BUFFER_LARGE, payload_path, (u32)sizeof(payload_path)));
		TEST_ASSERT_EQUAL_UINT64((u64)fixture.size, fs->get_path_size(payload_path));

		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_NOT_NULL(data);
		TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_LARGE_SIZE, size);
		TEST_ASSERT_EQUAL_MEMORY(fixture.data, data, fixture.size);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(0u), data[0]);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(255u), data[255]);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(65536u), data[65536]);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(size - 1u), data[size - 1u]);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(read, IT_BUF_FIELD_BUFFER));
	}
	sk_resource_fixture_free_payload(&fixture, alloc);

	api->destroy(repo);
}

/* Set a buffer from the on-disk fixture, save it to a temp file, overwrite the
 * resource with different bytes, reload the temp file, and verify the buffer
 * round-trips back to the original bytes. */
SK_TEST(resource_object_buffer_save_reload_roundtrip) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = it_buffer_repo(&type, alloc, 185u);
	sk_resource_fixture_payload_t fixture;
	char path[SK_FS_PATH_MAX];
	static const u8 other[3] = {9u, 8u, 7u};

	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_SMALL, alloc, &fixture));
	TEST_ASSERT_EQUAL_INT(0, it_temp_path("roundtrip", path, (u32)sizeof(path)));

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, fixture.data, fixture.size));
		api->commit(view, NULL);
	}

	/* Phase 1: save the stored buffer to a temp file and reload it. All IO is
	 * finished (and the file removed) before the first assertion. */
	u32 saved_size = 0u;
	const u8* saved_data = NULL;
	{
		sk_resource_object_t read = api->read(repo, rid);
		saved_data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &saved_size);
		TEST_ASSERT_NOT_NULL(saved_data);
		TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_SMALL_SIZE, saved_size);
	}
	i32 rc_write = it_file_write_all(path, saved_data, (u64)saved_size);
	u64 file_size = fs->get_path_size(path);
	u8* reloaded = (u8*)alloc->alloc(alloc->instance, (size_t)saved_size);
	TEST_ASSERT_NOT_NULL(reloaded);
	i32 rc_read = it_file_read_all(path, reloaded, (u64)saved_size);
	i32 rc_remove = fs->remove(path);

	TEST_ASSERT_EQUAL_INT(0, rc_write);
	TEST_ASSERT_EQUAL_UINT64((u64)saved_size, file_size);
	TEST_ASSERT_EQUAL_INT(0, rc_read);
	TEST_ASSERT_EQUAL_INT(0, rc_remove);
	TEST_ASSERT_EQUAL_MEMORY(fixture.data, reloaded, fixture.size);

	/* Phase 2: overwrite the resource, then reload the file back onto it. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, other, (u32)sizeof(other)));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(3u, size);
		TEST_ASSERT_EQUAL_MEMORY(other, data, sizeof(other));
	}
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, reloaded, saved_size));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_NOT_NULL(data);
		TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_SMALL_SIZE, size);
		TEST_ASSERT_EQUAL_MEMORY(fixture.data, data, fixture.size);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(read, IT_BUF_FIELD_BUFFER));
	}

	alloc->free(alloc->instance, reloaded);
	sk_resource_fixture_free_payload(&fixture, alloc);
	api->destroy(repo);
}

/* Overwrite a buffer many times with varying sizes; each overwrite must
 * release the previous payload (live allocation count returns to baseline
 * after garbage_collect) and reads must observe only the new bytes. */
SK_TEST(resource_object_buffer_overwrite_releases_old_payload) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	it_counting_alloc_t state = {sk_allocator_default(), 0u};
	sk_allocator_t counting_allocator = {&state, it_counting_alloc, it_counting_free, it_counting_realloc};
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = it_buffer_repo(&type, &counting_allocator, 186u);
	sk_resource_fixture_payload_t fixture;

	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_SMALL, &counting_allocator, &fixture));
	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);

	/* First set + commit + gc establishes the per-buffer baseline. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, fixture.data, fixture.size));
		api->commit(view, NULL);
	}
	api->garbage_collect(repo);
	u64 baseline = state.live;
	TEST_ASSERT_TRUE(baseline > 0u);

	/* Overwrite repeatedly; after each commit + gc the live allocation count
	 * must return to the baseline (the superseded payload was released, and
	 * the commit's retired instance was reclaimed). */
	for (u32 i = 0u; i < 64u; ++i) {
		u8 payload[64];
		u32 size = (i % 2u == 0u) ? 3u : 64u;
		memset(payload, (int)(0x10u + (i & 0x0Fu)), sizeof(payload));
		{
			sk_resource_object_t view = api->write(repo, rid);
			TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
			TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, payload, size));
			api->commit(view, NULL);
		}
		api->garbage_collect(repo);
		TEST_ASSERT_EQUAL_UINT64(baseline, state.live);
		{
			sk_resource_object_t read = api->read(repo, rid);
			u32 got_size = 0u;
			const u8* got = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &got_size);
			TEST_ASSERT_NOT_NULL(got);
			TEST_ASSERT_EQUAL_UINT32(size, got_size);
			TEST_ASSERT_EQUAL_MEMORY(payload, got, size);
		}
	}

	/* Destroying the repository releases every remaining allocation. */
	sk_resource_fixture_free_payload(&fixture, &counting_allocator);
	api->destroy(repo);
	TEST_ASSERT_EQUAL_UINT64(0u, state.live);
}

/* Unset, empty-buffer, unknown-index, and type-mismatch reads return the
 * documented sentinel (NULL pointer, size 0) instead of crashing. */
SK_TEST(resource_object_buffer_sentinels_empty_and_missing) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = it_buffer_repo(&type, alloc, 187u);
	sk_resource_fixture_payload_t fixture;

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);

	/* Missing value (field never set): NULL + size 0, no has-value bit. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_FALSE(api->has_value_on_this_object(read, IT_BUF_FIELD_BUFFER));
	}

	/* Missing field index: same sentinel, no crash. */
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(read, 999u, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
	}
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(view, 999u, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_EQUAL_INT(-1, api->set_buffer(view, 999u, "x", 1u));
		api->discard(view);
	}

	/* Field-type mismatch: get_buffer on the Blob field returns the sentinel
	 * (and set_buffer on the wrong-typed field fails with -2). */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		static const u8 payload[4] = {1u, 2u, 3u, 4u};
		TEST_ASSERT_EQUAL_INT(0, api->set_blob(view, IT_BUF_FIELD_BLOB, payload, sizeof(payload)));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(view, IT_BUF_FIELD_BLOB, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_EQUAL_INT(-2, api->set_buffer(view, IT_BUF_FIELD_BLOB, payload, sizeof(payload)));
		api->commit(view, NULL);
	}

	/* Empty fixture (field present with size 0): set_buffer(NULL, 0) is the
	 * documented "empty buffer" — reads stay NULL / 0 but the has-value bit
	 * is set, distinct from unset. */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_EMPTY, alloc, &fixture));
	TEST_ASSERT_EQUAL_INT(1, fixture.present);
	TEST_ASSERT_EQUAL_UINT32(0u, fixture.size);
	TEST_ASSERT_NULL(fixture.data);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, NULL, 0u));
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(view, IT_BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(view, IT_BUF_FIELD_BUFFER));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_TRUE(api->has_value_on_this_object(read, IT_BUF_FIELD_BUFFER));
	}
	sk_resource_fixture_free_payload(&fixture, alloc);

	/* Absent fixture (field omitted from the document): never set here; the
	 * read sentinel must match the documented unset behavior. */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_ABSENT, alloc, &fixture));
	TEST_ASSERT_EQUAL_INT(0, fixture.present);
	sk_rid_t other_rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(other_rid.id != 0u);
	{
		sk_resource_object_t read = api->read(repo, other_rid);
		u32 size = 123u;
		TEST_ASSERT_NULL(api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size));
		TEST_ASSERT_EQUAL_UINT32(0u, size);
		TEST_ASSERT_FALSE(api->has_value_on_this_object(read, IT_BUF_FIELD_BUFFER));
	}
	sk_resource_fixture_free_payload(&fixture, alloc);

	api->destroy(repo);
}

/* The >1 MiB fixture round-trips intact: load → set → save → reload → set,
 * with the on-disk file size and every byte verified at each stage. */
SK_TEST(resource_object_buffer_large_fixture_roundtrip) {
	sk_app_boot_t boot = sk_app_create();
	const sk_repository_api_t* api = boot.api->repository_api(boot.context);
	sk_app_shutdown(boot.context);
	const sk_allocator_t* alloc = sk_allocator_default();
	const sk_filesystem_api_t* fs = sk_test_filesystem_table();
	const sk_resource_type_t* type = NULL;
	sk_repository_t* repo = it_buffer_repo(&type, alloc, 188u);
	sk_resource_fixture_payload_t fixture;
	char path[SK_FS_PATH_MAX];
	static const u8 other[3] = {1u, 2u, 3u};

	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_LARGE, alloc, &fixture));
	TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_LARGE_SIZE, fixture.size);
	TEST_ASSERT_TRUE(fixture.size > 1024u * 1024u);
	TEST_ASSERT_NOT_NULL(fixture.data);
	TEST_ASSERT_EQUAL_INT(0, it_temp_path("large", path, (u32)sizeof(path)));

	sk_rid_t rid = api->create_resource(repo, type, SK_UUID_ZERO, NULL);
	TEST_ASSERT_TRUE(rid.id != 0u);
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, fixture.data, fixture.size));
		api->commit(view, NULL);
	}

	/* Stage 1: buffer read back is byte-for-byte the on-disk payload. */
	u32 stored_size = 0u;
	const u8* stored_data = NULL;
	{
		sk_resource_object_t read = api->read(repo, rid);
		stored_data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &stored_size);
		TEST_ASSERT_NOT_NULL(stored_data);
		TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_LARGE_SIZE, stored_size);
	}
	TEST_ASSERT_EQUAL_MEMORY(fixture.data, stored_data, fixture.size);

	/* Stage 2: save to a temp file; the file must be exactly the fixture size
	 * and reload byte-for-byte. All IO completes (and the file is removed)
	 * before the first assertion. */
	i32 rc_write = it_file_write_all(path, stored_data, (u64)stored_size);
	u64 file_size = fs->get_path_size(path);
	u8* reloaded = (u8*)alloc->alloc(alloc->instance, (size_t)stored_size);
	TEST_ASSERT_NOT_NULL(reloaded);
	i32 rc_read = it_file_read_all(path, reloaded, (u64)stored_size);
	i32 rc_remove = fs->remove(path);

	TEST_ASSERT_EQUAL_INT(0, rc_write);
	TEST_ASSERT_EQUAL_UINT64((u64)SK_RESOURCE_FIXTURE_LARGE_SIZE, file_size);
	TEST_ASSERT_EQUAL_INT(0, rc_read);
	TEST_ASSERT_EQUAL_INT(0, rc_remove);
	TEST_ASSERT_EQUAL_MEMORY(fixture.data, reloaded, fixture.size);

	/* Stage 3: overwrite with a tiny buffer, then reload the large file back
	 * onto the resource; the round-trip must be intact. */
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, other, (u32)sizeof(other)));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_EQUAL_UINT32(3u, size);
		TEST_ASSERT_EQUAL_MEMORY(other, data, sizeof(other));
	}
	{
		sk_resource_object_t view = api->write(repo, rid);
		TEST_ASSERT_TRUE(SK_RESOURCE_OBJECT_IS_VALID(view));
		TEST_ASSERT_EQUAL_INT(0, api->set_buffer(view, IT_BUF_FIELD_BUFFER, reloaded, stored_size));
		api->commit(view, NULL);
	}
	{
		sk_resource_object_t read = api->read(repo, rid);
		u32 size = 0u;
		const u8* data = api->get_buffer(read, IT_BUF_FIELD_BUFFER, &size);
		TEST_ASSERT_NOT_NULL(data);
		TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_LARGE_SIZE, size);
		TEST_ASSERT_EQUAL_MEMORY(fixture.data, data, fixture.size);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(0u), data[0]);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(1048576u), data[1048576u]);
		TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(size - 1u), data[size - 1u]);
	}

	alloc->free(alloc->instance, reloaded);
	sk_resource_fixture_free_payload(&fixture, alloc);
	api->destroy(repo);
}

#endif /* SK_TESTS */
