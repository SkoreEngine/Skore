/*
 * resource_object_fixtures.c — path resolution + load for ResourceObject
 * buffer fixtures (APX-183).
 *
 * Locates tests/data/resource_object/ from the build/bin test binary and
 * loads binary payloads. Buffer-field assertions are intentionally absent
 * (follow-on integration test task).
 */

#include "resource_object_fixtures.h"

#include "filesystem.h"
#include "path.h"
#include "test.h"

#include <string.h>

#ifdef SK_TESTS

#ifndef SK_TEST_DATA_DIR
#define SK_TEST_DATA_DIR ""
#endif

/* Validate a candidate fixture root: it must exist and contain a manifest so
 * a wrong folder (or a parent dir that merely exists) does not win. */
static i32 fixture_dir_is_valid(const_chr_t dir) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char probe[SK_FS_PATH_MAX];
	if (dir == NULL || dir[0] == '\0') {
		return 0;
	}
	if (fs->get_file_status(dir) != SK_FILE_STATUS_DIRECTORY) {
		return 0;
	}
	/* Require the manifest so a wrong "resource_object" folder does not win. */
	if (sk_path_join(sk_str_view_cstr(dir), sk_str_view_cstr("manifest.json"), probe, (u32)sizeof(probe)) < 0) {
		return 0;
	}
	return fs->get_file_status(probe) == SK_FILE_STATUS_FILE ? 1 : 0;
}

static i32 try_join_fixture_root(const_chr_t base, const_chr_t mid, const_chr_t subdir, char* out, u32 out_cap) {
	char step[SK_FS_PATH_MAX];
	if (base == NULL || base[0] == '\0') {
		return -1;
	}
	if (mid != NULL && mid[0] != '\0') {
		if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr(mid), step, (u32)sizeof(step)) < 0) {
			return -1;
		}
		if (sk_path_join(sk_str_view_cstr(step), sk_str_view_cstr(subdir), out, out_cap) < 0) {
			return -1;
		}
	} else {
		if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr(subdir), out, out_cap) < 0) {
			return -1;
		}
	}
	return fixture_dir_is_valid(out) ? 0 : -1;
}

/* Shared fixture-root resolution for every tests/data/<subdir> fixture set
 * (resource_object, entities, ...). Works when the test binary runs from
 * {build}/bin (CTest default) via SK_TEST_DATA_DIR and relative fallbacks
 * from app_folder / cwd. The candidate must contain a manifest.json. */
i32 sk_resource_fixture_dir_for_subdir(const_chr_t subdir, char* out, u32 out_cap) {
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char base[SK_FS_PATH_MAX];

	/* 1) Compile-time source-tree path: ${CMAKE_SOURCE_DIR}/tests/data */
	if (SK_TEST_DATA_DIR[0] != '\0') {
		if (try_join_fixture_root(SK_TEST_DATA_DIR, NULL, subdir, out, out_cap) == 0) {
			return 0;
		}
		/* SK_TEST_DATA_DIR may already point at tests/data/<subdir> */
		if (fixture_dir_is_valid(SK_TEST_DATA_DIR)) {
			size_t n = strlen(SK_TEST_DATA_DIR);
			if (n + 1u > (size_t)out_cap) {
				return -1;
			}
			memcpy(out, SK_TEST_DATA_DIR, n + 1u);
			return 0;
		}
	}

	/* 2) Relative to the running executable (…/build/bin → source tree) */
	if (fs->app_folder(base, (u32)sizeof(base)) == 0 && base[0] != '\0') {
		if (try_join_fixture_root(base, "tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
		if (try_join_fixture_root(base, "../tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
		if (try_join_fixture_root(base, "../../tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
	}

	/* 3) Relative to cwd (ctest WORKING_DIRECTORY is usually {build}/bin) */
	if (fs->current_dir(base, (u32)sizeof(base)) == 0 && base[0] != '\0') {
		if (try_join_fixture_root(base, "tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
		if (try_join_fixture_root(base, "../tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
		if (try_join_fixture_root(base, "../../tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
		if (try_join_fixture_root(base, "../../../tests/data", subdir, out, out_cap) == 0) {
			return 0;
		}
	}

	return -1;
}

/* Relative root under tests/data/ */
static const char fixture_subdir[] = "resource_object";

static const sk_resource_fixture_desc_t fixture_catalog[SK_RESOURCE_FIXTURE_COUNT] = {
	{SK_RESOURCE_FIXTURE_BUFFER_SMALL, "buffer_small", "buffer_small.json", "buffer_small.bin", SK_RESOURCE_FIXTURE_BUFFER_STATE_PRESENT, SK_RESOURCE_FIXTURE_SMALL_SIZE},
	{SK_RESOURCE_FIXTURE_BUFFER_LARGE, "buffer_large", "buffer_large.json", "buffer_large.bin", SK_RESOURCE_FIXTURE_BUFFER_STATE_PRESENT, SK_RESOURCE_FIXTURE_LARGE_SIZE},
	{SK_RESOURCE_FIXTURE_BUFFER_EMPTY, "buffer_empty", "buffer_empty.json", "buffer_empty.bin", SK_RESOURCE_FIXTURE_BUFFER_STATE_EMPTY, 0u},
	{SK_RESOURCE_FIXTURE_BUFFER_ABSENT, "buffer_absent", "buffer_absent.json", NULL, SK_RESOURCE_FIXTURE_BUFFER_STATE_ABSENT, 0u},
};

const sk_resource_fixture_desc_t* sk_resource_fixture_desc(sk_resource_fixture_id_t id) {
	return &fixture_catalog[(u32)id < (u32)SK_RESOURCE_FIXTURE_COUNT ? (u32)id : 0u];
}

i32 sk_resource_fixture_dir(char* out, u32 out_cap) {
	return sk_resource_fixture_dir_for_subdir(fixture_subdir, out, out_cap);
}

i32 sk_resource_fixture_path(const_chr_t relative_name, char* out, u32 out_cap) {
	char root[SK_FS_PATH_MAX];
	if (relative_name == NULL || relative_name[0] == '\0') {
		return -1;
	}
	if (sk_resource_fixture_dir(root, (u32)sizeof(root)) != 0) {
		return -1;
	}
	return sk_path_join(sk_str_view_cstr(root), sk_str_view_cstr(relative_name), out, out_cap) < 0 ? -1 : 0;
}

i32 sk_resource_fixture_resource_path(sk_resource_fixture_id_t id, char* out, u32 out_cap) {
	const sk_resource_fixture_desc_t* desc = sk_resource_fixture_desc(id);
	return sk_resource_fixture_path(desc->resource_file, out, out_cap);
}

i32 sk_resource_fixture_payload_path(sk_resource_fixture_id_t id, char* out, u32 out_cap) {
	const sk_resource_fixture_desc_t* desc = sk_resource_fixture_desc(id);
	if (desc->payload_file == NULL) {
		return -2;
	}
	return sk_resource_fixture_path(desc->payload_file, out, out_cap);
}

i32 sk_resource_fixture_load_payload(sk_resource_fixture_id_t id, const sk_allocator_t* allocator, sk_resource_fixture_payload_t* out) {
	const sk_resource_fixture_desc_t* desc = sk_resource_fixture_desc(id);
	const sk_filesystem_api_t* fs = sk_filesystem_api();
	char path[SK_FS_PATH_MAX];

	if (out == NULL) {
		return -1;
	}
	out->data = NULL;
	out->size = 0u;
	out->present = 0;

	if (desc->buffer_state == SK_RESOURCE_FIXTURE_BUFFER_STATE_ABSENT) {
		out->present = 0;
		return 0;
	}

	/* Empty: field is present with size 0; still verify the on-disk 0-byte file. */
	out->present = 1;
	if (sk_resource_fixture_payload_path(id, path, (u32)sizeof(path)) != 0) {
		return -1;
	}
	if (fs->get_file_status(path) != SK_FILE_STATUS_FILE) {
		return -1;
	}

	u64 file_size = fs->get_path_size(path);
	if (file_size != (u64)desc->expected_payload_size) {
		return -1;
	}
	if (file_size == 0u) {
		out->size = 0u;
		out->data = NULL;
		return 0;
	}
	if (file_size > 0xFFFFFFFFull) {
		return -1;
	}
	if (allocator == NULL || allocator->alloc == NULL) {
		return -1;
	}

	u32 size = (u32)file_size;
	u8* data = (u8*)allocator->alloc(allocator->instance, (size_t)size);
	if (data == NULL) {
		return -1;
	}

	sk_file_handle_t file = fs->open_file(path, SK_FILE_ACCESS_READ);
	if (file == NULL) {
		allocator->free(allocator->instance, data);
		return -1;
	}
	u64 got = fs->read_file(file, data, (size_t)size);
	fs->close_file(file);
	if (got != (u64)size) {
		allocator->free(allocator->instance, data);
		return -1;
	}

	out->data = data;
	out->size = size;
	out->present = 1;
	return 0;
}

void sk_resource_fixture_free_payload(sk_resource_fixture_payload_t* payload, const sk_allocator_t* allocator) {
	if (payload == NULL) {
		return;
	}
	if (payload->data != NULL && allocator != NULL && allocator->free != NULL) {
		allocator->free(allocator->instance, payload->data);
	}
	payload->data = NULL;
	payload->size = 0u;
	payload->present = 0;
}

/* ------------------------------------------------------------------ */
/* Harness smoke tests — locate fixtures and load payloads only.      */
/* Buffer get/set assertions belong to the follow-on integration task. */
/* ------------------------------------------------------------------ */

SK_TEST(resource_object_fixture_dir_resolves) {
	char dir[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_dir(dir, (u32)sizeof(dir)));
	TEST_ASSERT_TRUE(dir[0] != '\0');
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_DIRECTORY, sk_filesystem_api()->get_file_status(dir));

	char manifest[SK_FS_PATH_MAX];
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_path("manifest.json", manifest, (u32)sizeof(manifest)));
	TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(manifest));
}

SK_TEST(resource_object_fixture_catalog_and_paths) {
	for (u32 i = 0u; i < (u32)SK_RESOURCE_FIXTURE_COUNT; ++i) {
		const sk_resource_fixture_desc_t* desc = sk_resource_fixture_desc((sk_resource_fixture_id_t)i);
		TEST_ASSERT_NOT_NULL(desc);
		TEST_ASSERT_EQUAL_INT((int)i, (int)desc->id);
		TEST_ASSERT_NOT_NULL(desc->fixture_id);
		TEST_ASSERT_NOT_NULL(desc->resource_file);

		char resource_path[SK_FS_PATH_MAX];
		TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_resource_path((sk_resource_fixture_id_t)i, resource_path, (u32)sizeof(resource_path)));
		TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(resource_path));

		char payload_path[SK_FS_PATH_MAX];
		i32 payload_rc = sk_resource_fixture_payload_path((sk_resource_fixture_id_t)i, payload_path, (u32)sizeof(payload_path));
		if (desc->buffer_state == SK_RESOURCE_FIXTURE_BUFFER_STATE_ABSENT) {
			TEST_ASSERT_EQUAL_INT(-2, payload_rc);
			TEST_ASSERT_NULL(desc->payload_file);
		} else {
			TEST_ASSERT_EQUAL_INT(0, payload_rc);
			TEST_ASSERT_EQUAL_INT(SK_FILE_STATUS_FILE, sk_filesystem_api()->get_file_status(payload_path));
			TEST_ASSERT_EQUAL_UINT64((u64)desc->expected_payload_size, sk_filesystem_api()->get_path_size(payload_path));
		}
	}
}

SK_TEST(resource_object_fixture_load_payloads) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_resource_fixture_payload_t payload;

	/* Small: 32 bytes, first byte 0x10 */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_SMALL, alloc, &payload));
	TEST_ASSERT_EQUAL_INT(1, payload.present);
	TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_SMALL_SIZE, payload.size);
	TEST_ASSERT_NOT_NULL(payload.data);
	TEST_ASSERT_EQUAL_UINT8(0x10u, payload.data[0]);
	TEST_ASSERT_EQUAL_UINT8(0x2Fu, payload.data[31]);
	sk_resource_fixture_free_payload(&payload, alloc);

	/* Large: >1 MiB, pattern byte at index i is i & 0xFF */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_LARGE, alloc, &payload));
	TEST_ASSERT_EQUAL_INT(1, payload.present);
	TEST_ASSERT_EQUAL_UINT32(SK_RESOURCE_FIXTURE_LARGE_SIZE, payload.size);
	TEST_ASSERT_TRUE(payload.size > 1024u * 1024u);
	TEST_ASSERT_NOT_NULL(payload.data);
	TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(0u), payload.data[0]);
	TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(255u), payload.data[255]);
	TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(256u), payload.data[256]);
	TEST_ASSERT_EQUAL_UINT8(sk_resource_fixture_large_byte_at(payload.size - 1u), payload.data[payload.size - 1u]);
	sk_resource_fixture_free_payload(&payload, alloc);

	/* Empty: present, size 0, data NULL */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_EMPTY, alloc, &payload));
	TEST_ASSERT_EQUAL_INT(1, payload.present);
	TEST_ASSERT_EQUAL_UINT32(0u, payload.size);
	TEST_ASSERT_NULL(payload.data);
	sk_resource_fixture_free_payload(&payload, alloc);

	/* Absent: not present */
	TEST_ASSERT_EQUAL_INT(0, sk_resource_fixture_load_payload(SK_RESOURCE_FIXTURE_BUFFER_ABSENT, alloc, &payload));
	TEST_ASSERT_EQUAL_INT(0, payload.present);
	TEST_ASSERT_EQUAL_UINT32(0u, payload.size);
	TEST_ASSERT_NULL(payload.data);
	sk_resource_fixture_free_payload(&payload, alloc);
}

#endif /* SK_TESTS */
