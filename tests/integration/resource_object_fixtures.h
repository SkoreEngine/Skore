#pragma once

/**
 * @file resource_object_fixtures.h
 * @brief Locate and load on-disk ResourceObject buffer fixtures (APX-183).
 *
 * Fixtures live under tests/data/resource_object/ (see that directory's
 * README). This harness only resolves paths and loads bytes; buffer-field
 * assertions live in a separate integration-test task.
 *
 * Available only in SK_TESTS builds (linked into sk-integration-tests).
 */

#include "allocator.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SK_TESTS

/**
 * Which on-disk dummy resource to resolve.
 * Matches the four fixtures required by APX-183.
 */
typedef enum sk_resource_fixture_id_t {
	SK_RESOURCE_FIXTURE_BUFFER_SMALL = 0,  /* 32-byte payload present */
	SK_RESOURCE_FIXTURE_BUFFER_LARGE = 1,  /* >1 MiB payload present */
	SK_RESOURCE_FIXTURE_BUFFER_EMPTY = 2,  /* empty payload (size 0, present) */
	SK_RESOURCE_FIXTURE_BUFFER_ABSENT = 3, /* buffer field omitted */
	SK_RESOURCE_FIXTURE_COUNT = 4
} sk_resource_fixture_id_t;

/**
 * Buffer-field presence state encoded by a fixture.
 * Maps to repository semantics: present/empty set has-value; absent does not.
 */
typedef enum sk_resource_fixture_buffer_state_t {
	SK_RESOURCE_FIXTURE_BUFFER_STATE_PRESENT = 0,
	SK_RESOURCE_FIXTURE_BUFFER_STATE_EMPTY = 1,
	SK_RESOURCE_FIXTURE_BUFFER_STATE_ABSENT = 2
} sk_resource_fixture_buffer_state_t;

/**
 * Catalog entry for one fixture (paths relative to the fixture root unless
 * joined with sk_resource_fixture_dir).
 */
typedef struct sk_resource_fixture_desc_t {
	sk_resource_fixture_id_t id;
	const_chr_t fixture_id;	   /* stable string id, e.g. "buffer_small" */
	const_chr_t resource_file; /* JSON resource document name */
	const_chr_t payload_file;  /* binary payload name, or NULL if absent */
	sk_resource_fixture_buffer_state_t buffer_state;
	u32 expected_payload_size; /* 0 for empty and absent */
} sk_resource_fixture_desc_t;

/**
 * Loaded binary buffer payload. Ownership: data is allocated with the
 * allocator passed to sk_resource_fixture_load_payload; free with
 * sk_resource_fixture_free_payload.
 *
 * For EMPTY: data may be NULL, size 0, present == 1.
 * For ABSENT: data is NULL, size 0, present == 0.
 */
typedef struct sk_resource_fixture_payload_t {
	u8* data;
	u32 size;
	i32 present; /* 1 when the buffer field should be set (incl. empty) */
} sk_resource_fixture_payload_t;

/**
 * Resolve the absolute path of tests/data/resource_object/.
 * Works when the test binary runs from {build}/bin (CTest default) via
 * SK_TEST_DATA_DIR and relative fallbacks from app_folder / cwd.
 *
 * @param out     Destination path buffer (UTF-8, NUL-terminated on success).
 * @param out_cap Capacity of @p out in bytes (including NUL).
 * @return 0 on success, non-zero if no candidate directory exists.
 */
i32 sk_resource_fixture_dir(char* out, u32 out_cap);

/**
 * Shared fixture-root resolution for every tests/data/<subdir> fixture set
 * (resource_object, entities, ...): locate the directory that contains
 * @p subdir and a manifest.json, using the same SK_TEST_DATA_DIR / app_folder
 * / cwd fallbacks as sk_resource_fixture_dir.
 *
 * @param subdir  Fixture subdirectory name under tests/data/ (e.g. "entities").
 * @param out     Destination path buffer (UTF-8, NUL-terminated on success).
 * @param out_cap Capacity of @p out in bytes (including NUL).
 * @return 0 on success, non-zero if no candidate directory exists.
 */
i32 sk_resource_fixture_dir_for_subdir(const_chr_t subdir, char* out, u32 out_cap);

/**
 * Return the static catalog entry for @p id.
 * @param id Fixture id; must be in range [0, SK_RESOURCE_FIXTURE_COUNT).
 * @return Non-NULL descriptor.
 */
const sk_resource_fixture_desc_t* sk_resource_fixture_desc(sk_resource_fixture_id_t id);

/**
 * Build an absolute path to a file under the fixture root.
 *
 * @param relative_name File name relative to the fixture root (e.g.
 *                      "buffer_small.bin" or "manifest.json").
 * @param out           Destination path buffer.
 * @param out_cap       Capacity of @p out.
 * @return 0 on success, non-zero on path/join or fixture-dir failure.
 */
i32 sk_resource_fixture_path(const_chr_t relative_name, char* out, u32 out_cap);

/**
 * Absolute path to the JSON resource document for @p id.
 * @return 0 on success, non-zero on failure.
 */
i32 sk_resource_fixture_resource_path(sk_resource_fixture_id_t id, char* out, u32 out_cap);

/**
 * Absolute path to the binary payload for @p id.
 * For ABSENT fixtures returns non-zero (no payload file).
 * @return 0 on success, -1 on path failure, -2 when the fixture has no payload.
 */
i32 sk_resource_fixture_payload_path(sk_resource_fixture_id_t id, char* out, u32 out_cap);

/**
 * Load the binary buffer payload for @p id into @p out.
 * Allocates with @p allocator when size > 0. For EMPTY and ABSENT, out->data
 * is NULL and out->size is 0; out->present distinguishes them.
 *
 * @param id        Fixture id.
 * @param allocator Allocator for the payload (must not be NULL when size > 0).
 * @param out       Destination; must not be NULL. Zeroed on entry failure.
 * @return 0 on success, non-zero on path/IO/OOM failure.
 */
i32 sk_resource_fixture_load_payload(sk_resource_fixture_id_t id, const sk_allocator_t* allocator, sk_resource_fixture_payload_t* out);

/**
 * Free a payload previously loaded with sk_resource_fixture_load_payload.
 * Safe on a zeroed or already-freed payload (no-op when data is NULL).
 */
void sk_resource_fixture_free_payload(sk_resource_fixture_payload_t* payload, const sk_allocator_t* allocator);

/**
 * Expected size of the large fixture (>1 MiB). Public so tests can assert
 * without hard-coding the magic number in multiple places.
 */
enum { SK_RESOURCE_FIXTURE_LARGE_SIZE = 1048577u }; /* 1 MiB + 1 */

/**
 * Expected size of the small fixture payload.
 */
enum { SK_RESOURCE_FIXTURE_SMALL_SIZE = 32u };

/**
 * Byte value at index @p i in the large fixture pattern: (u8)(i & 0xFF).
 */
SK_FINLINE u8 sk_resource_fixture_large_byte_at(u32 i) {
	return (u8)(i & 0xFFu);
}

#endif /* SK_TESTS */

#ifdef __cplusplus
}
#endif
