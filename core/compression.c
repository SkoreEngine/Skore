/**
 * @file compression.c
 * @brief Compression codec registry and built-in codecs.
 *
 * Implements the v2 compression interface (see docs/compression-design-v2.md).
 * Codecs are registered at build time in a static const table — no runtime
 * registration, no free-running constructors. The always-present identity
 * "none" codec plus the baseline zstd codec (gated behind
 * SK_COMPRESSION_HAS_ZSTD) ship with the registry lookup surface.
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

#ifdef SK_COMPRESSION_HAS_ZSTD
/* ---- zstd codec (SK_COMPRESSION_CODEC_ZSTD) ------------------------------ */

/* The advanced one-shot context constructors and ZSTD_decompressBound are
 * static-link APIs (ZSTD_STATIC_LINKING_ONLY, confined to this TU). */
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h" /* ZSTD_ErrorCode / ZSTD_getErrorCode (separate header) */

/* Bridge the injected sk_allocator_t into ZSTD_customMem. zstd 1.5.6 exposes
 * alloc/free slots only — no realloc — so the injected table's realloc entry
 * is never used (design §6). */
static void_ptr_t zstd_custom_alloc(void* opaque, size_t size) {
	const sk_allocator_t* allocator = (const sk_allocator_t*)opaque;
	return allocator->alloc(allocator->instance, size);
}

static void zstd_custom_free(void* opaque, void* address) {
	const sk_allocator_t* allocator = (const sk_allocator_t*)opaque;
	allocator->free(allocator->instance, address);
}

static ZSTD_customMem zstd_custom_mem(const sk_allocator_t* allocator) {
	ZSTD_customMem mem;
	mem.customAlloc = zstd_custom_alloc;
	mem.customFree = zstd_custom_free;
	/* zstd's customMem opaque is non-const; the table is only ever read
	 * through it, so route the const pointer through an integer handle. */
	mem.opaque = (void*)(uintptr_t)allocator;
	return mem;
}

/* Translate a ZSTD_isError() result to the status enum (design §7). */
static i32 zstd_status(size_t code) {
	const ZSTD_ErrorCode zcode = ZSTD_getErrorCode(code);
	if (zcode == ZSTD_error_dstSize_tooSmall) {
		return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
	}
	if (zcode == ZSTD_error_memory_allocation) {
		return SK_COMPRESSION_ERR_OUT_OF_MEMORY;
	}
	if (zcode == ZSTD_error_prefix_unknown || zcode == ZSTD_error_version_unsupported || zcode == ZSTD_error_frameParameter_unsupported ||
		zcode == ZSTD_error_frameParameter_windowTooLarge || zcode == ZSTD_error_corruption_detected || zcode == ZSTD_error_checksum_wrong ||
		zcode == ZSTD_error_literals_headerWrong || zcode == ZSTD_error_dictionary_corrupted || zcode == ZSTD_error_srcSize_wrong) {
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	return SK_COMPRESSION_ERR_CODEC_FAILURE;
}

/* Clamp to [ZSTD_minCLevel(), ZSTD_maxCLevel()] (design §3.2). */
static i32 zstd_clamp_level(i32 level) {
	const i32 min_level = ZSTD_minCLevel();
	const i32 max_level = ZSTD_maxCLevel();
	if (level < min_level) {
		return min_level;
	}
	if (level > max_level) {
		return max_level;
	}
	return level;
}

static u64 zstd_compress_bound(u64 src_size) {
	return ZSTD_compressBound(src_size);
}

static i32 zstd_compress(const sk_allocator_t* allocator, i32 level, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	ZSTD_CCtx* ctx = ZSTD_createCCtx_advanced(zstd_custom_mem(allocator));
	if (ctx == NULL) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_OUT_OF_MEMORY;
	}
	{
		const size_t param_rc = ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, zstd_clamp_level(level));
		if (ZSTD_isError(param_rc)) {
			ZSTD_freeCCtx(ctx);
			return zstd_status(param_rc);
		}
	}
	{
		const size_t rc = ZSTD_compress2(ctx, dest, dest_cap, src, src_size);
		ZSTD_freeCCtx(ctx);
		if (ZSTD_isError(rc)) {
			*out_written = 0u;
			return zstd_status(rc);
		}
		*out_written = rc;
		return SK_COMPRESSION_OK;
	}
}

static i32 zstd_decompressed_size(const u8* src, u64 src_size, u64* out_size) {
	const u64 declared = ZSTD_getFrameContentSize(src, src_size);
	if (declared == ZSTD_CONTENTSIZE_ERROR) {
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	if (declared == ZSTD_CONTENTSIZE_UNKNOWN) {
		*out_size = SK_COMPRESSION_SIZE_UNKNOWN;
		return SK_COMPRESSION_OK;
	}
	*out_size = declared;
	return SK_COMPRESSION_OK;
}

static u64 zstd_decompress_bound(const u8* src, u64 src_size) {
	const u64 bound = ZSTD_decompressBound(src, src_size);
	if (bound == ZSTD_CONTENTSIZE_ERROR) {
		return SK_COMPRESSION_SIZE_UNKNOWN;
	}
	return bound;
}

static i32 zstd_decompress(const sk_allocator_t* allocator, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	ZSTD_DCtx* ctx = ZSTD_createDCtx_advanced(zstd_custom_mem(allocator));
	if (ctx == NULL) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_OUT_OF_MEMORY;
	}
	{
		const size_t rc = ZSTD_decompressDCtx(ctx, dest, dest_cap, src, src_size);
		ZSTD_freeDCtx(ctx);
		if (ZSTD_isError(rc)) {
			*out_written = 0u;
			return zstd_status(rc);
		}
		*out_written = rc;
		return SK_COMPRESSION_OK;
	}
}

static const sk_compression_codec_t zstd_codec = {
	SK_COMPRESSION_CODEC_ZSTD,
	"zstd",
	/* Range of this single-threaded zstd 1.5.6 build: ZSTD_minCLevel() ==
	 * -131072, ZSTD_maxCLevel() == 22. compress() re-clamps at runtime, so
	 * these are descriptive; a test pins them to the library values. */
	-131072, /* level_min */
	22,		 /* level_max */
	3,		 /* level_default — matches main's CompressionDefaultLevel */
	zstd_compress_bound,
	zstd_compress,
	zstd_decompressed_size,
	zstd_decompress_bound,
	zstd_decompress,
	NULL, /* stream_init — streaming is deferred (design §4); one-shot only */
	NULL, /* stream_update */
	NULL, /* stream_finish */
	NULL, /* stream_destroy */
};
#endif /* SK_COMPRESSION_HAS_ZSTD */

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
#ifdef SK_COMPRESSION_HAS_ZSTD
	&zstd_codec,
#endif
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
/* Parse system headers before test.h: unity pulls in <stdnoreturn.h>, whose
 * `noreturn` macro breaks UCRT <stdlib.h>'s __declspec(noreturn) under the
 * Windows clang-tidy driver (same class of issue as core/atomics.h). */
#include <limits.h> /* INT_MIN / INT_MAX (level-clamp test) */
#include <stdlib.h> /* malloc / free (test scratch only) */
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
	/* Ids with no enabled descriptor decode to NULL (design §5). An arbitrary
	 * id is never registered. */
	const sk_compression_codec_t* unknown = sk_compression_codec((sk_compression_codec_id_t)0x7FFFu);
	TEST_ASSERT_NULL(unknown);

	/* Documented caller mapping for a NULL lookup (design §5, §7). */
	i32 status = (unknown != NULL) ? SK_COMPRESSION_OK : SK_COMPRESSION_ERR_UNSUPPORTED_CODEC;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_UNSUPPORTED_CODEC, status);
	TEST_ASSERT_TRUE(status != SK_COMPRESSION_OK);
}

#ifdef SK_COMPRESSION_HAS_ZSTD
SK_TEST(compression_registry_zstd_lookup) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);

	TEST_ASSERT_NOT_NULL(codec);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_CODEC_ZSTD, codec->id);
	TEST_ASSERT_EQUAL_STRING("zstd", codec->name);
	TEST_ASSERT_EQUAL_INT(3, codec->level_default); /* main's CompressionDefaultLevel */
	/* Descriptor range matches the single-threaded 1.5.6 library (design §3.2). */
	TEST_ASSERT_EQUAL_INT(ZSTD_minCLevel(), codec->level_min);
	TEST_ASSERT_EQUAL_INT(ZSTD_maxCLevel(), codec->level_max);
	/* Streaming is deferred for the baseline codec (design §4). */
	TEST_ASSERT_NULL(codec->stream_init);
	TEST_ASSERT_TRUE(sk_compression_codec_count() >= 2u);
}

/* Compress @p payload_size bytes with @p codec at @p level and require a
 * byte-for-byte decompress round-trip; scratch comes from the injected
 * allocator so sizes are never hard-coded. */
static void zstd_assert_roundtrip(const sk_compression_codec_t* codec, const u8* payload, u64 payload_size, i32 level) {
	const sk_allocator_t* scratch = sk_allocator_default();
	const u64 bound = codec->compress_bound(payload_size);
	u8* compressed = scratch->alloc(scratch->instance, bound);
	u8* restored = scratch->alloc(scratch->instance, payload_size);
	u64 compressed_size = 0u;
	u64 restored_size = 0u;
	u64 declared = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_NOT_NULL(restored);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), level, payload, payload_size, compressed, bound, &compressed_size));
	TEST_ASSERT_TRUE(compressed_size > 0u);
	TEST_ASSERT_TRUE(compressed_size <= bound);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(compressed, compressed_size, &declared));
	TEST_ASSERT_EQUAL_UINT64(payload_size, declared);
	TEST_ASSERT_TRUE(codec->decompress_bound(compressed, compressed_size) >= payload_size);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), compressed, compressed_size, restored, payload_size, &restored_size));
	TEST_ASSERT_EQUAL_UINT64(payload_size, restored_size);
	if (payload_size > 0u) {
		TEST_ASSERT_EQUAL_MEMORY(payload, restored, payload_size);
	}

	scratch->free(scratch->instance, compressed);
	scratch->free(scratch->instance, restored);
}

SK_TEST(compression_zstd_roundtrip_default_level) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	/* Deterministic mixed payload: repeated runs plus scattered non-zero bytes
	 * (compresses, but not trivially). */
	u8 payload[16384];

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		payload[i] = (u8)((i % 101u) + ((i % 17u == 0u) ? 0xA0u : 0u));
	}
	zstd_assert_roundtrip(codec, payload, sizeof(payload), SK_COMPRESSION_LEVEL_DEFAULT);
}

SK_TEST(compression_zstd_roundtrip_incompressible) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	/* Deterministic xorshift stream: incompressible, still round-trips. */
	u8 payload[8192];
	u32 state = 0x6D2B79F5u;

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		payload[i] = (u8)(state >> 24u);
	}
	zstd_assert_roundtrip(codec, payload, sizeof(payload), 3);
}

SK_TEST(compression_zstd_roundtrip_large) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	/* 256 KiB of repeating text spans multiple zstd 128 KiB blocks. */
	const u32 payload_size = 256u * 1024u;
	const sk_allocator_t* scratch = sk_allocator_default();
	u8* payload = scratch->alloc(scratch->instance, payload_size);

	TEST_ASSERT_NOT_NULL(payload);
	for (u32 i = 0u; i < payload_size; ++i) {
		payload[i] = (u8)("the quick brown fox jumps over the lazy dog."[i % 44u]);
	}
	zstd_assert_roundtrip(codec, payload, payload_size, 3);
	scratch->free(scratch->instance, payload);
}

SK_TEST(compression_zstd_roundtrip_highly_compressible) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	/* One repeated byte: the most compressible case (design §11 round-trip
	 * list) and the payload used for the insufficient-output contract. */
	const u32 payload_size = 64u * 1024u;
	const sk_allocator_t* scratch = sk_allocator_default();
	u8* payload = scratch->alloc(scratch->instance, payload_size);
	const u64 bound = codec->compress_bound(payload_size);
	u8* compressed = scratch->alloc(scratch->instance, bound);
	u8* restored = scratch->alloc(scratch->instance, payload_size);
	u64 compressed_size = 0u;
	u64 restored_size = 0u;

	TEST_ASSERT_NOT_NULL(payload);
	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_NOT_NULL(restored);
	memset(payload, 0x41, payload_size);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), 3, payload, payload_size, compressed, bound, &compressed_size));
	/* Repeating data must actually compress: far smaller than the input. */
	TEST_ASSERT_TRUE(compressed_size * 100u < payload_size);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), compressed, compressed_size, restored, payload_size, &restored_size));
	TEST_ASSERT_EQUAL_UINT64(payload_size, restored_size);
	TEST_ASSERT_EQUAL_MEMORY(payload, restored, payload_size);

	scratch->free(scratch->instance, payload);
	scratch->free(scratch->instance, compressed);
	scratch->free(scratch->instance, restored);
}

SK_TEST(compression_zstd_roundtrip_empty_and_tiny) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	const u8 one_byte[] = {0x42u};

	zstd_assert_roundtrip(codec, NULL, 0u, SK_COMPRESSION_LEVEL_DEFAULT);
	zstd_assert_roundtrip(codec, one_byte, sizeof(one_byte), SK_COMPRESSION_LEVEL_DEFAULT);
}

SK_TEST(compression_zstd_level_clamp) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	const u8 payload[] = "level clamp payload payload payload payload payload payload payload";

	/* Extremes clamp to [level_min, level_max]; every level round-trips. */
	zstd_assert_roundtrip(codec, payload, sizeof(payload), INT_MAX);
	zstd_assert_roundtrip(codec, payload, sizeof(payload), INT_MIN);
	zstd_assert_roundtrip(codec, payload, sizeof(payload), codec->level_min);
	zstd_assert_roundtrip(codec, payload, sizeof(payload), codec->level_max);
}

SK_TEST(compression_zstd_insufficient_output) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	/* Highly compressible payload: the compressed frame is tiny, so a 1-byte
	 * destination cannot fit it and the codec must report the error without
	 * claiming any output (design §7). */
	u8 payload[4096];
	u8 sink[4];
	u64 written = 0u;
	u64 compressed_size = 0u;

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		payload[i] = 0x41u;
	}
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT,
						  codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), sink, sizeof(sink), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	/* A valid frame decompressed into a too-small destination: same contract. */
	{
		const u64 bound = codec->compress_bound(sizeof(payload));
		u8* compressed = sk_allocator_default()->alloc(NULL, bound);

		TEST_ASSERT_NOT_NULL(compressed);
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK,
							  codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT, codec->decompress(sk_allocator_default(), compressed, compressed_size, sink, sizeof(sink), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);
		sk_allocator_default()->free(NULL, compressed);
	}
}

SK_TEST(compression_zstd_corrupt_frame_rejected) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	const u8 payload[] = "corruption check: this payload must never survive a mangled frame";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* compressed = sk_allocator_default()->alloc(NULL, bound);
	u8* truncated = sk_allocator_default()->alloc(NULL, bound);
	u64 compressed_size = 0u;
	u64 written = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_NOT_NULL(truncated);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));

	/* Mangled magic: corrupt data, not a partial success. */
	compressed[0] ^= 0xFFu;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(sk_allocator_default(), compressed, compressed_size, truncated, sizeof(payload), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompressed_size(compressed, compressed_size, &written));

	/* Truncated frame: also corrupt data. */
	written = 0u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(sk_allocator_default(), compressed, compressed_size / 2u, truncated, sizeof(payload), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	/* Garbage input never decodes. */
	{
		const u8 garbage[] = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u};
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(sk_allocator_default(), garbage, sizeof(garbage), truncated, sizeof(payload), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);
	}

	sk_allocator_default()->free(NULL, compressed);
	sk_allocator_default()->free(NULL, truncated);
}

/* Counting allocator stub that actually allocates: proves every zstd internal
 * allocation flows through the injected table (design §6) and is balanced. */
static u32 zstd_stub_alloc_calls = 0u;
static u32 zstd_stub_free_calls = 0u;

static void_ptr_t zstd_stub_alloc(void_ptr_t instance, size_t size) {
	(void)instance;
	zstd_stub_alloc_calls += 1u;
	return malloc(size);
}

static void zstd_stub_free(void_ptr_t instance, void_ptr_t ptr) {
	(void)instance;
	zstd_stub_free_calls += 1u;
	free(ptr);
}

static void_ptr_t zstd_stub_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	(void)instance;
	return realloc(ptr, size);
}

SK_TEST(compression_zstd_unknown_content_size) {
	/* A frame written without a content-size header (ZSTD_c_contentSizeFlag=0)
	 * exercises the v2 split bound queries (design §3.2): decompressed_size
	 * reports SK_COMPRESSION_SIZE_UNKNOWN while decompress_bound still gives a
	 * safe upper bound and the one-shot decompress succeeds. Crafted with the
	 * stable zstd API because the codec intentionally always writes the header. */
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	const u8 payload[] = "unknown content size frame payload payload payload payload payload";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* frame = sk_allocator_default()->alloc(NULL, bound);
	u8* restored = sk_allocator_default()->alloc(NULL, sizeof(payload));
	ZSTD_CCtx* ctx = ZSTD_createCCtx_advanced(zstd_custom_mem(sk_allocator_default()));
	size_t frame_size = 0;
	u64 declared = 0u;
	u64 written = 0u;

	TEST_ASSERT_NOT_NULL(frame);
	TEST_ASSERT_NOT_NULL(restored);
	TEST_ASSERT_NOT_NULL(ctx);

	TEST_ASSERT_FALSE(ZSTD_isError(ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, 3)));
	TEST_ASSERT_FALSE(ZSTD_isError(ZSTD_CCtx_setParameter(ctx, ZSTD_c_contentSizeFlag, 0)));
	frame_size = ZSTD_compress2(ctx, frame, bound, payload, sizeof(payload));
	TEST_ASSERT_FALSE(ZSTD_isError(frame_size));
	ZSTD_freeCCtx(ctx);

	/* Exact size is unknown: the frame header declares no content size. */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(frame, frame_size, &declared));
	TEST_ASSERT_EQUAL_UINT64(SK_COMPRESSION_SIZE_UNKNOWN, declared);

	/* The bound query still provides a usable upper bound, and the frame
	 * decompresses byte-for-byte when sized from it. */
	{
		const u64 dbound = codec->decompress_bound(frame, frame_size);
		TEST_ASSERT_TRUE(dbound != SK_COMPRESSION_SIZE_UNKNOWN);
		TEST_ASSERT_TRUE(dbound >= sizeof(payload));
	}

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), frame, frame_size, restored, sizeof(payload), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload), written);
	TEST_ASSERT_EQUAL_MEMORY(payload, restored, sizeof(payload));

	sk_allocator_default()->free(NULL, frame);
	sk_allocator_default()->free(NULL, restored);
}

SK_TEST(compression_zstd_allocator_injection) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	sk_allocator_t stub = {NULL, zstd_stub_alloc, zstd_stub_free, zstd_stub_realloc};
	const u8 payload[] = "allocator injection payload payload payload payload payload payload";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* compressed = malloc(bound);
	u8 restored[256];
	u64 written = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	zstd_stub_alloc_calls = 0u;
	zstd_stub_free_calls = 0u;

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(&stub, SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &written));
	/* The one-shot path created a context through the injected table and
	 * released it again before returning: allocations happened and balance. */
	TEST_ASSERT_TRUE(zstd_stub_alloc_calls > 0u);
	TEST_ASSERT_EQUAL_UINT32(zstd_stub_alloc_calls, zstd_stub_free_calls);

	zstd_stub_alloc_calls = 0u;
	zstd_stub_free_calls = 0u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(&stub, compressed, written, restored, sizeof(restored), &written));
	TEST_ASSERT_TRUE(zstd_stub_alloc_calls > 0u);
	TEST_ASSERT_EQUAL_UINT32(zstd_stub_alloc_calls, zstd_stub_free_calls);

	free(compressed);
}

SK_TEST(compression_zstd_main_frame_compat) {
	/* Frame produced by main's Compression.cpp (ZSTD_compress, level 3) for
	 * the fixture payload below — same zstd 1.5.6, same default parameters.
	 * Decoding it byte-for-byte proves on-disk interop with main. */
	const u8 fixture[] = {
		0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x37, 0xB9, 0x01, 0x00, 0x53, 0x6B, 0x6F, 0x72, 0x65, 0x20, 0x76, 0x32, 0x20, 0x63, 0x6F, 0x6D, 0x70,
		0x72, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x6D, 0x61, 0x69, 0x6E, 0x2D, 0x62, 0x72, 0x61, 0x6E, 0x63, 0x68, 0x20, 0x63, 0x6F,
		0x6D, 0x70, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6C, 0x69, 0x74, 0x79, 0x20, 0x66, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x0A,
	};
	const u8 payload[] = "Skore v2 compression main-branch compatibility fixture\n";
	u8 restored[256];
	u8 encoded[sizeof(fixture)];
	u64 written = 0u;
	u64 declared = 0u;
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);

	/* Our encoder at level 3 emits the same bytes as main's ZSTD_compress. */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), 3, payload, sizeof(payload) - 1u, encoded, sizeof(encoded), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(fixture), written);
	TEST_ASSERT_EQUAL_MEMORY(fixture, encoded, sizeof(fixture));

	/* And we decode a main-produced frame byte-for-byte. */
	written = 0u;
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(fixture, sizeof(fixture), &declared));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, declared);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), fixture, sizeof(fixture), restored, sizeof(restored), &written));
	TEST_ASSERT_EQUAL_UINT64(sizeof(payload) - 1u, written);
	TEST_ASSERT_EQUAL_MEMORY(payload, restored, sizeof(payload) - 1u);
}
#endif /* SK_COMPRESSION_HAS_ZSTD */

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
