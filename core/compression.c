/**
 * @file compression.c
 * @brief Compression codec registry and built-in codecs.
 *
 * Implements the v2 compression interface (see docs/compression-design-v2.md).
 * Codecs are registered at build time in a static const table — no runtime
 * registration, no free-running constructors. This task (APX-166) ships the
 * interface skeleton: the always-present identity "none" codec plus the
 * registry lookup surface. Real codecs (zstd, ...) are added as descriptors
 * gated behind SK_COMPRESSION_HAS_* compile definitions by the codec task
 * (APX-167).
 */

#include "compression.h"

#include <string.h> /* memcpy */

/* ---- identity codec (SK_COMPRESSION_CODEC_NONE) ------------------------- */

/* Copy @p src_size bytes of @p src into @p dest; shared by compress/decompress. */
static i32 none_copy(const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	if (dest_cap < src_size) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
	}
	if (src_size > 0u) {
		memcpy(dest, src, src_size);
	}
	*out_written = src_size;
	return SK_COMPRESSION_OK;
}

static u64 none_compress_bound(u64 src_size) {
	return src_size;
}

static i32 none_compress(const sk_allocator_t* allocator, i32 level, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	(void)allocator;
	(void)level;
	return none_copy(src, src_size, dest, dest_cap, out_written);
}

static i32 none_decompressed_size(const u8* src, u64 src_size, u64* out_size) {
	(void)src;
	*out_size = src_size;
	return SK_COMPRESSION_OK;
}

static u64 none_decompress_bound(const u8* src, u64 src_size) {
	(void)src;
	return src_size;
}

static i32 none_decompress(const sk_allocator_t* allocator, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	(void)allocator;
	return none_copy(src, src_size, dest, dest_cap, out_written);
}

static const sk_compression_codec_t none_codec = {
	SK_COMPRESSION_CODEC_NONE,
	"none",
	0, /* level_min */
	0, /* level_max */
	0, /* level_default */
	none_compress_bound,
	none_compress,
	none_decompressed_size,
	none_decompress_bound,
	none_decompress,
	NULL, /* stream_init — no streaming implementation */
	NULL, /* stream_update */
	NULL, /* stream_finish */
	NULL, /* stream_destroy */
};

/* ---- build-time registry -------------------------------------------------- */

/*
 * Static const table, built at compile time (design §5). The identity codec
 * is always present. Extension path: add a descriptor plus one table entry
 * per new codec, gated behind a SK_COMPRESSION_HAS_* define so on-disk ids
 * stay readable in any build — sk_compression_codec() returns NULL for ids
 * whose descriptor is not compiled in.
 */
static const sk_compression_codec_t* codecs[] = {
	&none_codec,
};

static u32 codec_count(void) {
	return (u32)(sizeof(codecs) / sizeof(codecs[0]));
}

const sk_compression_codec_t* sk_compression_codec(sk_compression_codec_id_t id) {
	for (u32 i = 0u; i < codec_count(); ++i) {
		if (codecs[i]->id == id) {
			return codecs[i];
		}
	}
	return NULL;
}

u32 sk_compression_codec_count(void) {
	return codec_count();
}

const sk_compression_codec_t* sk_compression_codec_at(u32 index) {
	if (index >= codec_count()) {
		return NULL;
	}
	return codecs[index];
}

#ifdef SK_TESTS
#include "test.h"

/* Counting allocator stub: records internal allocation attempts. The identity
 * codec never allocates, so zero calls prove no allocation bypasses the
 * injected table (design §6). */
static u32 stub_alloc_calls = 0u;

static void_ptr_t stub_alloc(void_ptr_t instance, size_t size) {
	(void)instance;
	(void)size;
	stub_alloc_calls += 1u;
	return NULL;
}

static void stub_free(void_ptr_t instance, void_ptr_t ptr) {
	(void)instance;
	(void)ptr;
}

static void_ptr_t stub_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	(void)instance;
	(void)ptr;
	(void)size;
	stub_alloc_calls += 1u;
	return NULL;
}

SK_TEST(compression_registry_none_always_present) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);

	TEST_ASSERT_NOT_NULL(codec);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_CODEC_NONE, codec->id);
	TEST_ASSERT_EQUAL_STRING("none", codec->name);
	TEST_ASSERT_TRUE(sk_compression_codec_count() >= 1u);
	/* The identity codec is the first (index 0) registry entry. */
	TEST_ASSERT_EQUAL_PTR(codec, sk_compression_codec_at(0u));
}

SK_TEST(compression_registry_iteration) {
	u32 found_none = 0u;
	u32 count = sk_compression_codec_count();

	TEST_ASSERT_TRUE(count >= 1u);
	for (u32 i = 0u; i < count; ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);
		TEST_ASSERT_NOT_NULL(codec);
		if (codec->id == SK_COMPRESSION_CODEC_NONE) {
			found_none += 1u;
		}
	}
	TEST_ASSERT_EQUAL_UINT32(1u, found_none);
	TEST_ASSERT_NULL(sk_compression_codec_at(count));
}

SK_TEST(compression_registry_unknown_id_unsupported) {
	/* Ids with no enabled descriptor decode to NULL (design §5). ZSTD is not
	 * compiled in yet (APX-167); an arbitrary id is never registered. */
	const sk_compression_codec_t* unknown = sk_compression_codec((sk_compression_codec_id_t)0x7FFFu);
	TEST_ASSERT_NULL(unknown);
	TEST_ASSERT_NULL(sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD));

	/* Documented caller mapping for a NULL lookup (design §5, §7). */
	i32 status = (unknown != NULL) ? SK_COMPRESSION_OK : SK_COMPRESSION_ERR_UNSUPPORTED_CODEC;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_UNSUPPORTED_CODEC, status);
	TEST_ASSERT_TRUE(status != SK_COMPRESSION_OK);
}

SK_TEST(compression_status_codes_contract) {
	/* 0 = success, non-zero = failure; error codes are distinct (design §7). */
	TEST_ASSERT_EQUAL_INT(0, SK_COMPRESSION_OK);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT != 0);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_CORRUPT_DATA != 0);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_UNSUPPORTED_CODEC != 0);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_OUT_OF_MEMORY != 0);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_CODEC_FAILURE != 0);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT != SK_COMPRESSION_ERR_CORRUPT_DATA);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_CORRUPT_DATA != SK_COMPRESSION_ERR_UNSUPPORTED_CODEC);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_UNSUPPORTED_CODEC != SK_COMPRESSION_ERR_OUT_OF_MEMORY);
	TEST_ASSERT_TRUE(SK_COMPRESSION_ERR_OUT_OF_MEMORY != SK_COMPRESSION_ERR_CODEC_FAILURE);

	/* Sentinel values from the design header sketch. */
	TEST_ASSERT_EQUAL_INT(-1, SK_COMPRESSION_LEVEL_DEFAULT);
	TEST_ASSERT_EQUAL_UINT64((u64)-1, SK_COMPRESSION_SIZE_UNKNOWN);
}

SK_TEST(compression_none_descriptor_surface) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);

	/* One-shot entries always present; streaming is optional (NULL for none). */
	TEST_ASSERT_NOT_NULL(codec->compress_bound);
	TEST_ASSERT_NOT_NULL(codec->compress);
	TEST_ASSERT_NOT_NULL(codec->decompressed_size);
	TEST_ASSERT_NOT_NULL(codec->decompress_bound);
	TEST_ASSERT_NOT_NULL(codec->decompress);
	TEST_ASSERT_NULL(codec->stream_init);
	TEST_ASSERT_NULL(codec->stream_update);
	TEST_ASSERT_NULL(codec->stream_finish);
	TEST_ASSERT_NULL(codec->stream_destroy);

	/* Levels are ignored by the identity codec (design §3.2). */
	TEST_ASSERT_EQUAL_INT(0, codec->level_min);
	TEST_ASSERT_EQUAL_INT(0, codec->level_max);
	TEST_ASSERT_EQUAL_INT(0, codec->level_default);
}

SK_TEST(compression_none_identity_roundtrip) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);
	const u8 payload[] = {0x00u, 0x01u, 0x7fu, 0x80u, 0xffu, 0x42u, 0x13u, 0x37u};
	u8 compressed[8];
	u8 restored[8];
	u64 written = 0u;

	/* None stores bytes unchanged (identity codec, design §3.2). */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK,
						  codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, sizeof(compressed), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), written);
	TEST_ASSERT_EQUAL_MEMORY(payload, compressed, sizeof(payload));

	written = 0u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), compressed, sizeof(compressed), restored, sizeof(restored), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), written);
	TEST_ASSERT_EQUAL_MEMORY(payload, restored, sizeof(payload));
}

SK_TEST(compression_none_identity_empty) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);
	u64 written = 1u;
	u64 size = 1u;

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, NULL, 0u, NULL, 0u, &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	written = 1u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), NULL, 0u, NULL, 0u, &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(NULL, 0u, &size));
	TEST_ASSERT_EQUAL_UINT64(0u, size);
	TEST_ASSERT_EQUAL_UINT64(0u, codec->compress_bound(0u));
	TEST_ASSERT_EQUAL_UINT64(0u, codec->decompress_bound(NULL, 0u));
}

SK_TEST(compression_none_insufficient_output) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);
	const u8 payload[] = {0x01u, 0x02u, 0x03u, 0x04u};
	u8 sink[4];
	u64 written = 123u;

	/* dest_cap < src_size: nothing written, error status set (design §7). */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT,
						  codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), sink, 3u, &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	written = 123u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT, codec->decompress(sk_allocator_default(), payload, sizeof(payload), sink, 0u, &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);
}

SK_TEST(compression_none_bounds) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);
	const u8 payload[] = {0xdeu, 0xadu, 0xbeu, 0xefu};
	u64 size = 0u;

	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), codec->compress_bound(sizeof(payload)));
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(payload, sizeof(payload), &size));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), size);
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), codec->decompress_bound(payload, sizeof(payload)));
}

SK_TEST(compression_allocator_injection) {
	/* The injected allocator flows through the one-shot entry points; the
	 * identity codec must not allocate, and never outside that table. */
	sk_allocator_t stub = {NULL, stub_alloc, stub_free, stub_realloc};
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);
	const u8 payload[] = {0x11u, 0x22u, 0x33u};
	u8 compressed[3];
	u8 restored[3];
	u64 written = 0u;

	stub_alloc_calls = 0u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(&stub, 3, payload, sizeof(payload), compressed, sizeof(compressed), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), written);
	TEST_ASSERT_EQUAL_MEMORY(payload, compressed, sizeof(payload));
	TEST_ASSERT_EQUAL_UINT32(0u, stub_alloc_calls);

	written = 0u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(&stub, compressed, sizeof(compressed), restored, sizeof(restored), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), written);
	TEST_ASSERT_EQUAL_MEMORY(payload, restored, sizeof(payload));
	TEST_ASSERT_EQUAL_UINT32(0u, stub_alloc_calls);
}

#endif /* SK_TESTS */
