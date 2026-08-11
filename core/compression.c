/**
 * @file compression.c
 * @brief Compression codec registry and built-in codecs.
 *
 * Implements the v2 compression interface (see docs/compression-design-v2.md).
 * Codecs are registered at build time in a static const table — no runtime
 * registration, no free-running constructors. The always-present identity
 * "none" codec plus the gated codecs (zstd, lz4, zlib/miniz) ship with the
 * registry lookup surface (docs/compression-codecs-evaluation.md §6–§7).
 */

#include "compression.h"

#include <limits.h> /* INT_MAX (LZ4 int-sized APIs) */
#include <string.h> /* memcpy */

/* Shared little-endian u64 helpers for the size-prefixed LZ4/zlib frames.
 * Only the LZ4 and miniz codecs use this framing, so the helpers are compiled
 * only when at least one of them is enabled — otherwise an all-codecs-off
 * build trips -Werror=unused-function. (The prefix macro below stays
 * unconditional: macros never trip unused warnings, and the size-prefixed
 * SK_TEST helper in this file reads it under the same guard.) */
#if defined(SK_COMPRESSION_HAS_LZ4) || defined(SK_COMPRESSION_HAS_MINIZ)
static void compression_write_u64_le(u8* dest, u64 value) {
	dest[0] = (u8)(value);
	dest[1] = (u8)(value >> 8u);
	dest[2] = (u8)(value >> 16u);
	dest[3] = (u8)(value >> 24u);
	dest[4] = (u8)(value >> 32u);
	dest[5] = (u8)(value >> 40u);
	dest[6] = (u8)(value >> 48u);
	dest[7] = (u8)(value >> 56u);
}

static u64 compression_read_u64_le(const u8* src) {
	return ((u64)src[0]) | ((u64)src[1] << 8u) | ((u64)src[2] << 16u) | ((u64)src[3] << 24u) | ((u64)src[4] << 32u) | ((u64)src[5] << 40u) | ((u64)src[6] << 48u) |
		   ((u64)src[7] << 56u);
}
#endif /* SK_COMPRESSION_HAS_LZ4 || SK_COMPRESSION_HAS_MINIZ */

/* Size of the original-size prefix shared by the LZ4 and zlib on-disk frames. */
#define COMPRESSION_SIZE_PREFIX_BYTES 8u

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

/* Resolve SK_COMPRESSION_LEVEL_DEFAULT to the zstd descriptor default (3),
 * then clamp to [ZSTD_minCLevel(), ZSTD_maxCLevel()] (design §3.2, §9). */
static i32 zstd_resolve_level(i32 level) {
	const i32 min_level = ZSTD_minCLevel();
	const i32 max_level = ZSTD_maxCLevel();
	/* Sentinel must map to level_default before clamp: -1 is a valid (fast)
	 * zstd level, so clamping alone would not select the codec default. */
	if (level == SK_COMPRESSION_LEVEL_DEFAULT) {
		level = 3; /* zstd_codec.level_default / main CompressionDefaultLevel */
	}
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
		const size_t param_rc = ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, zstd_resolve_level(level));
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

#ifdef SK_COMPRESSION_HAS_LZ4
/* ---- LZ4 codec (SK_COMPRESSION_CODEC_LZ4) -------------------------------- */

/*
 * On-disk frame: little-endian u64 original_size + raw LZ4 block
 * (LZ4_compress_fast / LZ4_decompress_safe). The size prefix makes
 * decompressed_size exact; the one-shot LZ4 block API is heap-free so the
 * injected allocator is unused (docs/compression-codecs-evaluation.md §7).
 */
#include "lz4.h"

/* Level is the LZ4 acceleration factor: 1 = default (best ratio among the
 * fast API), higher = faster / worse ratio. Clamp after resolving the sentinel. */
static i32 lz4_resolve_level(i32 level) {
	if (level == SK_COMPRESSION_LEVEL_DEFAULT) {
		level = 1;
	}
	if (level < 1) {
		return 1;
	}
	if (level > 16) {
		return 16;
	}
	return level;
}

static u64 lz4_compress_bound(u64 src_size) {
	/* LZ4 one-shot APIs take int and reject inputs above LZ4_MAX_INPUT_SIZE. */
	if (src_size > (u64)LZ4_MAX_INPUT_SIZE) {
		return SK_COMPRESSION_SIZE_UNKNOWN;
	}
	{
		const int block_bound = LZ4_compressBound((int)src_size);
		if (block_bound <= 0) {
			return SK_COMPRESSION_SIZE_UNKNOWN;
		}
		return COMPRESSION_SIZE_PREFIX_BYTES + (u64)block_bound;
	}
}

static i32 lz4_compress(const sk_allocator_t* allocator, i32 level, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	const u64 bound = lz4_compress_bound(src_size);
	(void)allocator;

	if (bound == SK_COMPRESSION_SIZE_UNKNOWN) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CODEC_FAILURE;
	}
	if (dest_cap < bound) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
	}

	compression_write_u64_le(dest, src_size);

	{
		const int acceleration = lz4_resolve_level(level);
		const int max_dst = (int)(dest_cap - COMPRESSION_SIZE_PREFIX_BYTES);
		/* LZ4_compress_fast accepts src == NULL when src_size == 0 and emits
		 * the canonical empty LZ4 block (one 0x00 byte), byte-identical to
		 * main's commented-out LZ4_compress_default path (APX-176 vectors). */
		const int rc = LZ4_compress_fast((const char*)src, (char*)(dest + COMPRESSION_SIZE_PREFIX_BYTES), (int)src_size, max_dst, acceleration);
		if (rc <= 0) {
			/* Sized with compress_bound, so failure is an internal error. */
			*out_written = 0u;
			return SK_COMPRESSION_ERR_CODEC_FAILURE;
		}
		*out_written = COMPRESSION_SIZE_PREFIX_BYTES + (u64)rc;
		return SK_COMPRESSION_OK;
	}
}

static i32 lz4_decompressed_size(const u8* src, u64 src_size, u64* out_size) {
	if (src_size < COMPRESSION_SIZE_PREFIX_BYTES) {
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	*out_size = compression_read_u64_le(src);
	return SK_COMPRESSION_OK;
}

static u64 lz4_decompress_bound(const u8* src, u64 src_size) {
	u64 declared = 0u;
	if (lz4_decompressed_size(src, src_size, &declared) != SK_COMPRESSION_OK) {
		return SK_COMPRESSION_SIZE_UNKNOWN;
	}
	return declared;
}

static i32 lz4_decompress(const sk_allocator_t* allocator, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	u64 original_size = 0u;
	(void)allocator;

	if (src_size < COMPRESSION_SIZE_PREFIX_BYTES) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	original_size = compression_read_u64_le(src);
	if (dest_cap < original_size) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
	}
	if (original_size == 0u) {
		/* Empty payload: accept the canonical empty block (prefix + 0x00)
		 * and the older prefix-only frame; either way nothing is written. */
		*out_written = 0u;
		return SK_COMPRESSION_OK;
	}
	if (original_size > (u64)LZ4_MAX_INPUT_SIZE) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	if (src_size == COMPRESSION_SIZE_PREFIX_BYTES) {
		/* Non-empty original size with no block bytes is corrupt. */
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	if (src_size - COMPRESSION_SIZE_PREFIX_BYTES > (u64)INT_MAX) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}

	{
		const int compressed_size = (int)(src_size - COMPRESSION_SIZE_PREFIX_BYTES);
		const int rc = LZ4_decompress_safe((const char*)(src + COMPRESSION_SIZE_PREFIX_BYTES), (char*)dest, compressed_size, (int)original_size);
		if (rc < 0 || (u64)rc != original_size) {
			*out_written = 0u;
			return SK_COMPRESSION_ERR_CORRUPT_DATA;
		}
		*out_written = original_size;
		return SK_COMPRESSION_OK;
	}
}

static const sk_compression_codec_t lz4_codec = {
	SK_COMPRESSION_CODEC_LZ4,
	"lz4",
	1,	/* level_min — acceleration floor */
	16, /* level_max — acceleration ceiling */
	1,	/* level_default — LZ4_compress_default equivalent */
	lz4_compress_bound,
	lz4_compress,
	lz4_decompressed_size,
	lz4_decompress_bound,
	lz4_decompress,
	NULL, /* stream_init — streaming deferred; one-shot only */
	NULL, /* stream_update */
	NULL, /* stream_finish */
	NULL, /* stream_destroy */
};
#endif /* SK_COMPRESSION_HAS_LZ4 */

#ifdef SK_COMPRESSION_HAS_MINIZ
/* ---- zlib/miniz codec (SK_COMPRESSION_CODEC_ZLIB) ------------------------ */

/*
 * On-disk frame: little-endian u64 original_size + RFC 1950 zlib stream
 * produced by miniz tdefl (TDEFL_WRITE_ZLIB_HEADER). The size prefix makes
 * decompressed_size exact. Compressor state is allocated through the injected
 * allocator; tinfl_decompress_mem_to_mem is heap-free
 * (docs/compression-codecs-evaluation.md §7).
 */
#include "miniz.h"

static i32 zlib_resolve_level(i32 level) {
	if (level == SK_COMPRESSION_LEVEL_DEFAULT) {
		level = MZ_DEFAULT_LEVEL; /* 6 — zlib default */
	}
	if (level < 0) {
		return 0;
	}
	if (level > 9) {
		return 9;
	}
	return level;
}

static u64 zlib_compress_bound(u64 src_size) {
	/* mz_compressBound formula is conservative; add the size prefix. */
	return COMPRESSION_SIZE_PREFIX_BYTES + (u64)mz_compressBound((mz_ulong)src_size);
}

static i32 zlib_compress(const sk_allocator_t* allocator, i32 level, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	const u64 bound = zlib_compress_bound(src_size);
	tdefl_compressor* comp = NULL;
	mz_uint flags = 0u;
	size_t in_size = 0u;
	size_t out_size = 0u;
	tdefl_status status;

	if (dest_cap < bound) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
	}

	compression_write_u64_le(dest, src_size);

	comp = (tdefl_compressor*)allocator->alloc(allocator->instance, sizeof(tdefl_compressor));
	if (comp == NULL) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_OUT_OF_MEMORY;
	}

	/* window_bits > 0 selects a zlib-wrapped stream (header + adler32). */
	flags = tdefl_create_comp_flags_from_zip_params(zlib_resolve_level(level), MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);
	if (tdefl_init(comp, NULL, NULL, (int)flags) != TDEFL_STATUS_OKAY) {
		allocator->free(allocator->instance, comp);
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CODEC_FAILURE;
	}

	/* No size_t/u64 casts: on LLP64 both are unsigned long long and
	 * (size_t)/(u64) trips readability-redundant-casting on MSVC CI.
	 * tdefl_compress accepts src == NULL when src_size == 0 and emits the
	 * canonical empty RFC 1950 stream (78 9C 03 00 00 00 00 01),
	 * byte-identical to mz_compress2 (APX-176 vectors). */
	in_size = src_size;
	out_size = dest_cap - COMPRESSION_SIZE_PREFIX_BYTES;
	status = tdefl_compress(comp, src, &in_size, dest + COMPRESSION_SIZE_PREFIX_BYTES, &out_size, TDEFL_FINISH);
	allocator->free(allocator->instance, comp);

	if (status != TDEFL_STATUS_DONE || in_size != src_size) {
		*out_written = 0u;
		/* Bound-sized destination should always succeed; map residual buffer
		 * exhaustion to INSUFFICIENT_OUTPUT for the contract, else failure. */
		if (status == TDEFL_STATUS_OKAY || status == TDEFL_STATUS_PUT_BUF_FAILED) {
			return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
		}
		return SK_COMPRESSION_ERR_CODEC_FAILURE;
	}

	*out_written = COMPRESSION_SIZE_PREFIX_BYTES + out_size;
	return SK_COMPRESSION_OK;
}

static i32 zlib_decompressed_size(const u8* src, u64 src_size, u64* out_size) {
	if (src_size < COMPRESSION_SIZE_PREFIX_BYTES) {
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	*out_size = compression_read_u64_le(src);
	return SK_COMPRESSION_OK;
}

static u64 zlib_decompress_bound(const u8* src, u64 src_size) {
	u64 declared = 0u;
	if (zlib_decompressed_size(src, src_size, &declared) != SK_COMPRESSION_OK) {
		return SK_COMPRESSION_SIZE_UNKNOWN;
	}
	return declared;
}

static i32 zlib_decompress(const sk_allocator_t* allocator, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	u64 original_size = 0u;
	size_t rc = 0u;
	(void)allocator;

	if (src_size < COMPRESSION_SIZE_PREFIX_BYTES) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	original_size = compression_read_u64_le(src);
	if (dest_cap < original_size) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT;
	}
	if (original_size == 0u) {
		/* Empty payload: accept the canonical empty stream (prefix + zlib
		 * empty frame) and the older prefix-only frame; either way nothing
		 * is written. */
		*out_written = 0u;
		return SK_COMPRESSION_OK;
	}
	if (src_size == COMPRESSION_SIZE_PREFIX_BYTES) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}

	/* No size_t/u64 casts: same LLP64 type as above; implicit conversion is fine. */
	rc = tinfl_decompress_mem_to_mem(dest, original_size, src + COMPRESSION_SIZE_PREFIX_BYTES, src_size - COMPRESSION_SIZE_PREFIX_BYTES,
									 TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
	if (rc == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || rc != original_size) {
		*out_written = 0u;
		return SK_COMPRESSION_ERR_CORRUPT_DATA;
	}
	*out_written = original_size;
	return SK_COMPRESSION_OK;
}

static const sk_compression_codec_t zlib_codec = {
	SK_COMPRESSION_CODEC_ZLIB,
	"zlib",
	0, /* level_min — store / no compression */
	9, /* level_max — best zlib-compatible compression */
	6, /* level_default — MZ_DEFAULT_LEVEL */
	zlib_compress_bound,
	zlib_compress,
	zlib_decompressed_size,
	zlib_decompress_bound,
	zlib_decompress,
	NULL, /* stream_init — streaming deferred; one-shot only */
	NULL, /* stream_update */
	NULL, /* stream_finish */
	NULL, /* stream_destroy */
};
#endif /* SK_COMPRESSION_HAS_MINIZ */

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
#ifdef SK_COMPRESSION_HAS_LZ4
	&lz4_codec,
#endif
#ifdef SK_COMPRESSION_HAS_MINIZ
	&zlib_codec,
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

/* ---------------------------------------------------------------------------
 * APX-164: call-site migration completed.
 *
 * The v1→v2 migration flag introduced by APX-174 was removed once the last
 * v1-shaped compression call site in the tree (the parity-harness zstd v1
 * adapter) was migrated to the v2 descriptor interface: the adapter is gone,
 * the corpus builder compresses through the registry, and the frozen APX-176
 * compatibility vectors are the wire-compat gate (docs/compression-design-v2.md
 * §9.3). The only raw-codec uses left in this file are deliberate:
 *   - the unknown-content-size test crafts a no-content-size zstd frame with
 *     the stable zstd API, which the v2 codec intentionally never emits;
 *   - scripts/gen-compression-vectors.c produces the frozen reference frames
 *     from the reference libraries by design.
 * ------------------------------------------------------------------------- */

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

SK_TEST(compression_registry_each_codec_resolves_by_id) {
	/* Registration contract (design §5, §3.2): every registered descriptor
	 * resolves from its stable id and no two descriptors share an id, so an
	 * on-disk id always maps to exactly one codec. */
	const u32 count = sk_compression_codec_count();

	TEST_ASSERT_TRUE(count >= 1u);
	for (u32 i = 0u; i < count; ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);

		TEST_ASSERT_NOT_NULL(codec);
		TEST_ASSERT_EQUAL_PTR(codec, sk_compression_codec(codec->id));
		for (u32 j = i + 1u; j < count; ++j) {
			TEST_ASSERT_TRUE(codec->id != sk_compression_codec_at(j)->id);
		}
	}
}

/* One-shot round-trip through any descriptor: compress into a bound-sized
 * buffer, decompress back, and require byte-identical recovery (design §4 —
 * the one-shot surface every enabled codec must implement). */
static void compression_assert_one_shot_roundtrip(const sk_compression_codec_t* codec, const u8* payload, u64 payload_size) {
	const sk_allocator_t* scratch = sk_allocator_default();
	const u64 bound = codec->compress_bound(payload_size);
	u8* compressed = scratch->alloc(scratch->instance, bound);
	u8* restored = scratch->alloc(scratch->instance, payload_size);
	u64 compressed_size = 0u;
	u64 restored_size = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_NOT_NULL(restored);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(scratch, SK_COMPRESSION_LEVEL_DEFAULT, payload, payload_size, compressed, bound, &compressed_size));
	TEST_ASSERT_TRUE(compressed_size <= bound);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(scratch, compressed, compressed_size, restored, payload_size, &restored_size));
	TEST_ASSERT_EQUAL_UINT64(payload_size, restored_size);
	if (payload_size > 0u) {
		TEST_ASSERT_EQUAL_MEMORY(payload, restored, payload_size);
	}

	scratch->free(scratch->instance, compressed);
	scratch->free(scratch->instance, restored);
}

SK_TEST(compression_roundtrip_all_registered_codecs) {
	/* Every codec in the registry (none + gated codecs) round-trips a fixed
	 * corpus byte-for-byte; a future codec is covered automatically. Covers
	 * empty, non-trivial compressible, and incompressible inputs. */
	const u8 small[] = "one-shot registry roundtrip payload payload payload payload payload";
	u8 large[8192];
	u8 incompressible[4096];
	u32 state = 0xA5A5F00Du;

	for (u32 i = 0u; i < sizeof(large); ++i) {
		large[i] = (u8)((i % 37u) + ((i % 11u == 0u) ? 0x80u : 0u));
	}
	for (u32 i = 0u; i < sizeof(incompressible); ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		incompressible[i] = (u8)(state >> 24u);
	}

	for (u32 i = 0u; i < sk_compression_codec_count(); ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);

		compression_assert_one_shot_roundtrip(codec, NULL, 0u);
		compression_assert_one_shot_roundtrip(codec, small, sizeof(small));
		compression_assert_one_shot_roundtrip(codec, large, sizeof(large));
		compression_assert_one_shot_roundtrip(codec, incompressible, sizeof(incompressible));
	}
}

/* ==========================================================================
 * APX-163: codec-generic edge-case suite
 *
 * One set of contract checks run against every registered descriptor
 * (sk_compression_codec_at), so a future codec inherits the full matrix:
 * empty / one-byte / highly-compressible / incompressible / large
 * multi-megabyte inputs; exact-bound and undersized output buffers;
 * truncated and corrupted compressed input; unknown-codec-id dispatch.
 * ========================================================================== */

/* Compress @p payload into exactly compress_bound() bytes and decompress into
 * exactly the declared original size; require byte-identical recovery and
 * exact size queries (design §4 buffer contract). */
static void compression_generic_assert_exact_bound(const sk_compression_codec_t* codec, const u8* payload, u64 payload_size) {
	const sk_allocator_t* a = sk_allocator_default();
	const u64 bound = codec->compress_bound(payload_size);
	u8* compressed = a->alloc(a->instance, bound > 0u ? bound : 1u);
	u8* restored = NULL;
	u64 compressed_size = 0u;
	u64 declared = 0u;
	u64 restored_size = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_TRUE(bound != SK_COMPRESSION_SIZE_UNKNOWN);

	/* Compressing into exactly the bound must succeed — the bound is the
	 * documented minimum destination size, not a "too small" case. */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, payload, payload_size, compressed, bound, &compressed_size));
	TEST_ASSERT_TRUE(compressed_size <= bound);

	/* Size queries must agree with the payload (design §4). */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(compressed, compressed_size, &declared));
	TEST_ASSERT_EQUAL_UINT64(payload_size, declared);
	TEST_ASSERT_TRUE(codec->decompress_bound(compressed, compressed_size) >= declared);

	restored = a->alloc(a->instance, declared > 0u ? declared : 1u);
	TEST_ASSERT_NOT_NULL(restored);
	/* Decompressing into exactly the declared size must succeed. */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(a, compressed, compressed_size, restored, declared, &restored_size));
	TEST_ASSERT_EQUAL_UINT64(declared, restored_size);
	if (declared > 0u) {
		TEST_ASSERT_EQUAL_MEMORY(payload, restored, declared);
	}

	a->free(a->instance, compressed);
	a->free(a->instance, restored);
}

SK_TEST(compression_generic_exact_bound_buffers) {
	const u8 payload[] = "exact-bound contract: size every destination with compress_bound / decompressed_size (design section 4)";

	for (u32 i = 0u; i < sk_compression_codec_count(); ++i) {
		compression_generic_assert_exact_bound(sk_compression_codec_at(i), payload, sizeof(payload));
	}
}

SK_TEST(compression_generic_roundtrip_edge_inputs) {
	/* Deterministic corpus: one byte, a 256 KiB run of one repeated byte (the
	 * most compressible case), 64 KiB of xorshift noise (incompressible), and
	 * 2 MiB of mixed data spanning many codec-internal blocks. */
	const u8 one_byte[] = {0x5Au};
	const u32 compressible_size = 256u * 1024u;
	const u32 random_size = 64u * 1024u;
	const u32 large_size = 2u * 1024u * 1024u;
	const sk_allocator_t* a = sk_allocator_default();
	u8* compressible = a->alloc(a->instance, compressible_size);
	u8* random_bytes = a->alloc(a->instance, random_size);
	u8* large = a->alloc(a->instance, large_size);
	u32 state = 0x1639A11Bu; /* deterministic xorshift seed */

	TEST_ASSERT_NOT_NULL(compressible);
	TEST_ASSERT_NOT_NULL(random_bytes);
	TEST_ASSERT_NOT_NULL(large);
	memset(compressible, 0x41u, compressible_size);
	for (u32 i = 0u; i < random_size; ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		random_bytes[i] = (u8)(state >> 24u);
	}
	for (u32 i = 0u; i < large_size; ++i) {
		large[i] = (u8)((i % 97u) + ((i % 13u == 0u) ? 0x80u : 0u));
	}

	for (u32 i = 0u; i < sk_compression_codec_count(); ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);

		compression_assert_one_shot_roundtrip(codec, NULL, 0u);
		compression_assert_one_shot_roundtrip(codec, one_byte, sizeof(one_byte));
		compression_assert_one_shot_roundtrip(codec, compressible, compressible_size);
		compression_assert_one_shot_roundtrip(codec, random_bytes, random_size);
		compression_assert_one_shot_roundtrip(codec, large, large_size);
	}

	a->free(a->instance, compressible);
	a->free(a->instance, random_bytes);
	a->free(a->instance, large);
}

SK_TEST(compression_generic_undersized_output_fails_cleanly) {
	/* dest_cap below the documented bound must fail with
	 * INSUFFICIENT_OUTPUT and claim zero bytes written — never overflow
	 * (design §7). Sized with the real codec frame, so every registered
	 * codec must honor the same contract. */
	const u8 payload[] = "undersized destination contract: report INSUFFICIENT_OUTPUT and write nothing (design section 7)";
	const u32 repeat_size = 4096u;
	const sk_allocator_t* a = sk_allocator_default();
	u8* repeat = a->alloc(a->instance, repeat_size);
	u8 sink[4];

	TEST_ASSERT_NOT_NULL(repeat);
	memset(repeat, 0x42u, repeat_size);

	for (u32 i = 0u; i < sk_compression_codec_count(); ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);
		const u64 bound = codec->compress_bound(sizeof(payload));
		u8* compressed = a->alloc(a->instance, bound > 0u ? bound : 1u);
		u8* restored = NULL;
		u64 compressed_size = 0u;
		u64 written = 0u;
		u64 declared = 0u;

		TEST_ASSERT_NOT_NULL(compressed);

		/* Compression into a 4-byte sink: no codec fits a 4096-byte repeated
		 * payload there (the smallest zstd frame header is larger). */
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, repeat, repeat_size, sink, sizeof(sink), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);

		/* Decompression one byte short of the declared size: same contract. */
		written = 0u;
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(compressed, compressed_size, &declared));
		TEST_ASSERT_TRUE(declared > 0u);
		restored = a->alloc(a->instance, declared);
		TEST_ASSERT_NOT_NULL(restored);
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT, codec->decompress(a, compressed, compressed_size, restored, declared - 1u, &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);

		a->free(a->instance, compressed);
		a->free(a->instance, restored);
	}

	a->free(a->instance, repeat);
}

SK_TEST(compression_generic_truncated_and_corrupt_input) {
	/* The identity codec has no framing: decompress is a raw copy, so it can
	 * neither truncate nor detect bit flips. Every real codec must reject
	 * both per design §7 (CORRUPT_DATA, zero bytes written). */
	const u8 payload[] = "codec-generic corruption contract: truncation and a flipped compressed byte must be rejected (design section 7)";
	const sk_allocator_t* a = sk_allocator_default();

	for (u32 i = 0u; i < sk_compression_codec_count(); ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);
		const u64 bound = codec->compress_bound(sizeof(payload));
		u8* compressed = a->alloc(a->instance, bound > 0u ? bound : 1u);
		u8* restored = a->alloc(a->instance, sizeof(payload));
		u64 compressed_size = 0u;
		u64 written = 0u;

		TEST_ASSERT_NOT_NULL(compressed);
		TEST_ASSERT_NOT_NULL(restored);
		if (codec->id == SK_COMPRESSION_CODEC_NONE) {
			a->free(a->instance, compressed);
			a->free(a->instance, restored);
			continue;
		}

		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));
		TEST_ASSERT_TRUE(compressed_size > 1u);

		/* Truncated frame: half the bytes can never be a complete frame. */
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(a, compressed, compressed_size / 2u, restored, sizeof(payload), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);

		/* One flipped byte. Built-ins: the zstd frame magic (byte 0) and the
		 * size-prefixed codecs' first block byte (past the u64 prefix) are
		 * always detected; future codecs inherit the same contract. */
		written = 0u;
		{
			u64 flip_index = compressed_size / 2u;
			if (codec->id == SK_COMPRESSION_CODEC_ZSTD) {
				flip_index = 0u;
			} else if (codec->id == SK_COMPRESSION_CODEC_LZ4 || codec->id == SK_COMPRESSION_CODEC_ZLIB) {
				flip_index = COMPRESSION_SIZE_PREFIX_BYTES;
			}
			TEST_ASSERT_TRUE(flip_index < compressed_size);
			compressed[flip_index] ^= 0xFFu;
		}
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(a, compressed, compressed_size, restored, sizeof(payload), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);

		a->free(a->instance, compressed);
		a->free(a->instance, restored);
	}
}

SK_TEST(compression_generic_unknown_id_dispatch) {
	/* Dispatch contract (design §5): every registered id resolves to exactly
	 * one descriptor; ids with no enabled descriptor (including unknown
	 * values) decode to NULL, which the caller maps to
	 * SK_COMPRESSION_ERR_UNSUPPORTED_CODEC. */
	const u32 count = sk_compression_codec_count();

	for (u32 i = 0u; i < count; ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);
		TEST_ASSERT_NOT_NULL(codec);
		TEST_ASSERT_EQUAL_PTR(codec, sk_compression_codec(codec->id));
	}
	{
		/* The full enum (0..3): exactly the registered ids resolve. */
		const sk_compression_codec_id_t known[] = {
			SK_COMPRESSION_CODEC_NONE,
			SK_COMPRESSION_CODEC_ZSTD,
			SK_COMPRESSION_CODEC_LZ4,
			SK_COMPRESSION_CODEC_ZLIB,
		};
		u32 resolved = 0u;
		for (u32 i = 0u; i < (u32)(sizeof(known) / sizeof(known[0])); ++i) {
			if (sk_compression_codec(known[i]) != NULL) {
				resolved += 1u;
			}
		}
		TEST_ASSERT_EQUAL_UINT32(count, resolved);
	}
	/* Arbitrary / out-of-enum ids never register. */
	TEST_ASSERT_NULL(sk_compression_codec((sk_compression_codec_id_t)0x7FFFu));
	TEST_ASSERT_NULL(sk_compression_codec_at(count));
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
	 * stable zstd API because the codec intentionally always writes the header
	 * (APX-164: the one deliberate raw-zstd use left in this file; see the
	 * migration-completion note above the test section). */
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

#endif /* SK_COMPRESSION_HAS_ZSTD */

#if defined(SK_COMPRESSION_HAS_LZ4) || defined(SK_COMPRESSION_HAS_MINIZ)
/* Shared round-trip helper for size-prefixed frames (LZ4 + zlib). */
static void size_prefixed_assert_roundtrip(const sk_compression_codec_t* codec, const u8* payload, u64 payload_size, i32 level) {
	const sk_allocator_t* scratch = sk_allocator_default();
	const u64 bound = codec->compress_bound(payload_size);
	u8* compressed = scratch->alloc(scratch->instance, bound);
	u8* restored = scratch->alloc(scratch->instance, payload_size > 0u ? payload_size : 1u);
	u64 compressed_size = 0u;
	u64 restored_size = 0u;
	u64 declared = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_NOT_NULL(restored);
	TEST_ASSERT_TRUE(bound != SK_COMPRESSION_SIZE_UNKNOWN);
	TEST_ASSERT_TRUE(bound >= COMPRESSION_SIZE_PREFIX_BYTES);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), level, payload, payload_size, compressed, bound, &compressed_size));
	TEST_ASSERT_TRUE(compressed_size >= COMPRESSION_SIZE_PREFIX_BYTES);
	TEST_ASSERT_TRUE(compressed_size <= bound);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(compressed, compressed_size, &declared));
	TEST_ASSERT_EQUAL_UINT64(payload_size, declared);
	TEST_ASSERT_EQUAL_UINT64(payload_size, codec->decompress_bound(compressed, compressed_size));

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(sk_allocator_default(), compressed, compressed_size, restored, payload_size, &restored_size));
	TEST_ASSERT_EQUAL_UINT64(payload_size, restored_size);
	if (payload_size > 0u) {
		TEST_ASSERT_EQUAL_MEMORY(payload, restored, payload_size);
	}

	scratch->free(scratch->instance, compressed);
	scratch->free(scratch->instance, restored);
}
#endif /* SK_COMPRESSION_HAS_LZ4 || SK_COMPRESSION_HAS_MINIZ */

#ifdef SK_COMPRESSION_HAS_LZ4
SK_TEST(compression_registry_lz4_lookup) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_LZ4);

	TEST_ASSERT_NOT_NULL(codec);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_CODEC_LZ4, codec->id);
	TEST_ASSERT_EQUAL_STRING("lz4", codec->name);
	TEST_ASSERT_EQUAL_INT(1, codec->level_default);
	TEST_ASSERT_EQUAL_INT(1, codec->level_min);
	TEST_ASSERT_EQUAL_INT(16, codec->level_max);
	TEST_ASSERT_NULL(codec->stream_init);
	TEST_ASSERT_NOT_NULL(codec->compress_bound);
	TEST_ASSERT_TRUE(codec->compress_bound(0u) >= COMPRESSION_SIZE_PREFIX_BYTES);
}

SK_TEST(compression_lz4_roundtrip_nontrivial) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_LZ4);
	u8 payload[16384];

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		payload[i] = (u8)((i % 101u) + ((i % 17u == 0u) ? 0xA0u : 0u));
	}
	size_prefixed_assert_roundtrip(codec, payload, sizeof(payload), SK_COMPRESSION_LEVEL_DEFAULT);
}

SK_TEST(compression_lz4_roundtrip_empty_and_incompressible) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_LZ4);
	u8 payload[8192];
	u32 state = 0x6D2B79F5u;
	const u8 one_byte[] = {0x42u};

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		payload[i] = (u8)(state >> 24u);
	}
	size_prefixed_assert_roundtrip(codec, NULL, 0u, SK_COMPRESSION_LEVEL_DEFAULT);
	size_prefixed_assert_roundtrip(codec, one_byte, sizeof(one_byte), SK_COMPRESSION_LEVEL_DEFAULT);
	size_prefixed_assert_roundtrip(codec, payload, sizeof(payload), 1);
	size_prefixed_assert_roundtrip(codec, payload, sizeof(payload), 16);
}

SK_TEST(compression_lz4_insufficient_output_and_corrupt) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_LZ4);
	const u8 payload[] = "lz4 insufficient and corrupt checks payload payload payload";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* compressed = sk_allocator_default()->alloc(NULL, bound);
	u8 sink[4];
	/* Full-size restore buffer for the corrupt path: dest_cap must be large
	 * enough to pass the insufficient-output gate so the codec actually
	 * attempts decode (and rejects the mangled block). Never pass
	 * sizeof(payload) as dest_cap with sink[4] — that overflows the stack. */
	u8 restored[128];
	u64 written = 0u;
	u64 compressed_size = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_TRUE(sizeof(restored) >= sizeof(payload));
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT,
						  codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), sink, sizeof(sink), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT, codec->decompress(sk_allocator_default(), compressed, compressed_size, sink, sizeof(sink), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	/* Flip a compressed-block byte (past the size prefix). */
	if (compressed_size > COMPRESSION_SIZE_PREFIX_BYTES) {
		compressed[COMPRESSION_SIZE_PREFIX_BYTES] ^= 0xFFu;
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(sk_allocator_default(), compressed, compressed_size, restored, sizeof(restored), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);
	}

	/* Truncated prefix. */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompressed_size(compressed, 3u, &written));

	sk_allocator_default()->free(NULL, compressed);
}
#endif /* SK_COMPRESSION_HAS_LZ4 */

#ifdef SK_COMPRESSION_HAS_MINIZ
SK_TEST(compression_registry_zlib_lookup) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZLIB);

	TEST_ASSERT_NOT_NULL(codec);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_CODEC_ZLIB, codec->id);
	TEST_ASSERT_EQUAL_STRING("zlib", codec->name);
	TEST_ASSERT_EQUAL_INT(6, codec->level_default);
	TEST_ASSERT_EQUAL_INT(0, codec->level_min);
	TEST_ASSERT_EQUAL_INT(9, codec->level_max);
	TEST_ASSERT_NULL(codec->stream_init);
	TEST_ASSERT_TRUE(codec->compress_bound(0u) >= COMPRESSION_SIZE_PREFIX_BYTES);
}

SK_TEST(compression_zlib_roundtrip_nontrivial) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZLIB);
	u8 payload[16384];

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		payload[i] = (u8)((i % 101u) + ((i % 17u == 0u) ? 0xA0u : 0u));
	}
	size_prefixed_assert_roundtrip(codec, payload, sizeof(payload), SK_COMPRESSION_LEVEL_DEFAULT);
}

SK_TEST(compression_zlib_roundtrip_empty_and_incompressible) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZLIB);
	u8 payload[8192];
	u32 state = 0xC0FFEEu;
	const u8 one_byte[] = {0x7Eu};

	for (u32 i = 0u; i < sizeof(payload); ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		payload[i] = (u8)(state >> 24u);
	}
	size_prefixed_assert_roundtrip(codec, NULL, 0u, SK_COMPRESSION_LEVEL_DEFAULT);
	size_prefixed_assert_roundtrip(codec, one_byte, sizeof(one_byte), SK_COMPRESSION_LEVEL_DEFAULT);
	size_prefixed_assert_roundtrip(codec, payload, sizeof(payload), 1);
	size_prefixed_assert_roundtrip(codec, payload, sizeof(payload), 9);
}

SK_TEST(compression_zlib_insufficient_output_and_corrupt) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZLIB);
	const u8 payload[] = "zlib insufficient and corrupt checks payload payload payload";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* compressed = sk_allocator_default()->alloc(NULL, bound);
	u8 sink[4];
	/* Full-size restore buffer for the corrupt path (see lz4 test). */
	u8 restored[128];
	u64 written = 0u;
	u64 compressed_size = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_TRUE(sizeof(restored) >= sizeof(payload));
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT,
						  codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), sink, sizeof(sink), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(sk_allocator_default(), SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT, codec->decompress(sk_allocator_default(), compressed, compressed_size, sink, sizeof(sink), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	if (compressed_size > COMPRESSION_SIZE_PREFIX_BYTES) {
		compressed[COMPRESSION_SIZE_PREFIX_BYTES] ^= 0xFFu;
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(sk_allocator_default(), compressed, compressed_size, restored, sizeof(restored), &written));
		TEST_ASSERT_EQUAL_UINT64(0u, written);
	}

	sk_allocator_default()->free(NULL, compressed);
}

/* Counting allocator: zlib one-shot must allocate compressor state through it. */
static u32 zlib_stub_alloc_calls = 0u;
static u32 zlib_stub_free_calls = 0u;

static void_ptr_t zlib_stub_alloc(void_ptr_t instance, size_t size) {
	(void)instance;
	zlib_stub_alloc_calls += 1u;
	return malloc(size);
}

static void zlib_stub_free(void_ptr_t instance, void_ptr_t ptr) {
	(void)instance;
	zlib_stub_free_calls += 1u;
	free(ptr);
}

static void_ptr_t zlib_stub_realloc(void_ptr_t instance, void_ptr_t ptr, size_t size) {
	(void)instance;
	return realloc(ptr, size);
}

SK_TEST(compression_zlib_allocator_injection) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZLIB);
	sk_allocator_t stub = {NULL, zlib_stub_alloc, zlib_stub_free, zlib_stub_realloc};
	const u8 payload[] = "zlib allocator injection payload payload payload payload";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* compressed = malloc(bound);
	u8 restored[256];
	u64 written = 0u;

	TEST_ASSERT_NOT_NULL(compressed);
	zlib_stub_alloc_calls = 0u;
	zlib_stub_free_calls = 0u;

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(&stub, SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &written));
	TEST_ASSERT_TRUE(zlib_stub_alloc_calls > 0u);
	TEST_ASSERT_EQUAL_UINT32(zlib_stub_alloc_calls, zlib_stub_free_calls);

	/* Decompress is heap-free (tinfl); allocator may go unused. */
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(&stub, compressed, written, restored, sizeof(restored), &written));

	free(compressed);
}
#endif /* SK_COMPRESSION_HAS_MINIZ */

#if defined(SK_COMPRESSION_HAS_ZSTD) || defined(SK_COMPRESSION_HAS_LZ4) || defined(SK_COMPRESSION_HAS_MINIZ)
/* BEGIN APX-176 COMPATIBILITY VECTORS */
/*
 * APX-176 cross-engine compatibility vectors (frozen).
 * Generated by scripts/gen-compression-vectors.c -- do not hand-edit.
 * Regenerate with scripts/gen-compression-vectors.sh and re-verify the
 * full suite; the frames below are the reference implementations'
 * output (see the generator header for exact provenance).
 */

#ifdef SK_COMPRESSION_HAS_ZSTD
/* zstd frames: main Compression.cpp `case ZSTD` -- ZSTD_compress, level 3. */
/* empty: 0 payload bytes -> 9 frame bytes */
static const u8 compat_zstd_empty_frame[] = {
	0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x00, 0x01, 0x00, 0x00,
};

/* one_byte: 1 payload bytes -> 10 frame bytes */
static const u8 compat_zstd_one_byte_frame[] = {
	0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x01, 0x09, 0x00, 0x00, 0x41,
};

/* fixture: 55 payload bytes -> 64 frame bytes */
static const u8 compat_zstd_fixture_frame[] = {
	0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x37, 0xB9, 0x01, 0x00, 0x53, 0x6B, 0x6F, 0x72, 0x65, 0x20, 0x76, 0x32, 0x20, 0x63, 0x6F, 0x6D, 0x70,
	0x72, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x6D, 0x61, 0x69, 0x6E, 0x2D, 0x62, 0x72, 0x61, 0x6E, 0x63, 0x68, 0x20, 0x63, 0x6F,
	0x6D, 0x70, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6C, 0x69, 0x74, 0x79, 0x20, 0x66, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x0A,
};

/* compressible: 768 payload bytes -> 136 frame bytes */
static const u8 compat_zstd_compressible_frame[] = {
	0x28, 0xB5, 0x2F, 0xFD, 0x60, 0x00, 0x02, 0xF5, 0x03, 0x00, 0x54, 0x07, 0x53, 0x6B, 0x6F, 0x72, 0x65, 0x20, 0x76, 0x32, 0x20, 0x63, 0x72, 0x6F, 0x73, 0x73, 0x2D, 0x65,
	0x6E, 0x67, 0x69, 0x6E, 0x65, 0x20, 0x63, 0x6F, 0x6D, 0x70, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6C, 0x69, 0x74, 0x79, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x20, 0x63,
	0x6F, 0x72, 0x70, 0x75, 0x73, 0x3A, 0x20, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
	0x56, 0x57, 0x58, 0x59, 0x5A, 0x20, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
	0x77, 0x78, 0x79, 0x7A, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2E, 0x0A, 0x01, 0x00, 0x35, 0x22, 0xBC, 0x0A, 0x51,
};

/* incompressible: 768 payload bytes -> 778 frame bytes */
static const u8 compat_zstd_incompressible_frame[] = {
	0x28, 0xB5, 0x2F, 0xFD, 0x60, 0x00, 0x02, 0x01, 0x18, 0x00, 0x40, 0x91, 0x9C, 0x65, 0x0F, 0x3D, 0xEF, 0x7C, 0xF9, 0x0D, 0x9F, 0x3F, 0x96, 0x48, 0xE6, 0x4F, 0xC1, 0x69, 0xEE,
	0x8A, 0x91, 0x23, 0xAB, 0x9B, 0xDA, 0x7B, 0x72, 0x62, 0x74, 0x81, 0xAA, 0x5A, 0xB4, 0x81, 0xFA, 0x21, 0x0E, 0x1C, 0xAE, 0xB8, 0xD8, 0xA2, 0x12, 0x7F, 0x0F, 0x40, 0xAD, 0x7D,
	0xA9, 0x09, 0x46, 0x22, 0xC9, 0xBA, 0x38, 0x04, 0x67, 0x34, 0x7D, 0x6B, 0xCF, 0x48, 0xEA, 0x7E, 0x76, 0xD0, 0x90, 0xEB, 0xED, 0x37, 0xEE, 0x59, 0xE5, 0x81, 0xB2, 0x93, 0x9F,
	0x5C, 0x44, 0x32, 0x40, 0x37, 0x6A, 0x11, 0x63, 0x5C, 0x4F, 0x39, 0x89, 0xC0, 0xA2, 0x90, 0x14, 0x2A, 0xA3, 0xDB, 0x1C, 0x42, 0xC9, 0x68, 0x1B, 0x0F, 0x3E, 0x39, 0x2C, 0xC6,
	0x7C, 0xFF, 0x97, 0x69, 0x04, 0x8A, 0x40, 0xE0, 0x56, 0x71, 0xA8, 0x8D, 0xC4, 0xC0, 0xD4, 0xBE, 0xB3, 0x43, 0x16, 0x6C, 0x72, 0xBA, 0x88, 0x0B, 0xEA, 0x7A, 0xCA, 0xF0, 0x5C,
	0xD5, 0xF8, 0xF6, 0xD6, 0x80, 0x9B, 0x28, 0xBF, 0x14, 0x53, 0x57, 0x8B, 0x73, 0x90, 0x5D, 0xDF, 0x4B, 0x37, 0x99, 0xD7, 0xE9, 0xDD, 0x75, 0x36, 0x49, 0xEB, 0xA5, 0xBC, 0xA5,
	0xAB, 0xC1, 0xC5, 0x61, 0x8B, 0xB5, 0x66, 0x35, 0x37, 0x3D, 0xD2, 0xC1, 0xBC, 0xDF, 0x70, 0x6B, 0x70, 0x70, 0x2D, 0x8B, 0xEC, 0x0D, 0x8B, 0x91, 0xBE, 0x17, 0x4F, 0xE4, 0x4C,
	0xE9, 0xAC, 0xC2, 0xDC, 0xF1, 0x43, 0xF8, 0x9E, 0x9F, 0xC3, 0x52, 0x34, 0xF6, 0xAF, 0x12, 0x9D, 0x77, 0xFC, 0x95, 0xC9, 0xC9, 0x32, 0xC6, 0x3D, 0xDF, 0x29, 0x1A, 0xD3, 0x5B,
	0x38, 0x98, 0x33, 0x06, 0xEB, 0x2B, 0x5F, 0x6F, 0x72, 0x6A, 0xD6, 0x09, 0x1B, 0x01, 0xFC, 0xE6, 0x45, 0x2F, 0x26, 0xB1, 0xED, 0x8A, 0xD3, 0x43, 0xE6, 0x8A, 0x12, 0x55, 0x9F,
	0x07, 0x56, 0xEF, 0xC8, 0xA1, 0x64, 0x6F, 0xDD, 0x6C, 0xAD, 0xC6, 0x64, 0x86, 0x3A, 0xBA, 0x27, 0x1D, 0x47, 0x64, 0xBE, 0x4E, 0x49, 0xDF, 0x16, 0x88, 0xB6, 0x08, 0x46, 0x5E,
	0xF4, 0x32, 0xD9, 0xC2, 0x75, 0xCD, 0x86, 0x39, 0xB5, 0xAF, 0x42, 0x0A, 0xA6, 0x5C, 0x50, 0x4D, 0xD2, 0x6D, 0xE5, 0x2D, 0x2A, 0xFA, 0x85, 0x0D, 0x9F, 0x36, 0xD7, 0x17, 0x14,
	0xA7, 0xCA, 0x77, 0xB5, 0x4B, 0xAC, 0x84, 0x78, 0x66, 0xD9, 0xB5, 0x45, 0x2C, 0x0D, 0x17, 0x9D, 0x18, 0x65, 0xEB, 0xA1, 0x9A, 0x65, 0xDF, 0x11, 0xD3, 0x3E, 0x4E, 0xC1, 0xE7,
	0x93, 0xAF, 0x69, 0xA0, 0xE8, 0xB9, 0x2C, 0xFF, 0xA8, 0xB9, 0x52, 0x1F, 0x39, 0x92, 0xD8, 0x30, 0x62, 0x5C, 0x35, 0x15, 0x88, 0x21, 0x8A, 0xB7, 0x87, 0x38, 0x6F, 0x5E, 0xE7,
	0x07, 0x53, 0x2A, 0x3B, 0x53, 0x61, 0x5A, 0xAB, 0x0E, 0xB5, 0xCD, 0xF8, 0x0E, 0x5D, 0x3E, 0x0A, 0xC4, 0x7E, 0x3A, 0x64, 0xBE, 0x03, 0xDD, 0x5B, 0x0E, 0xBA, 0xAF, 0x5C, 0x95,
	0x4C, 0x1B, 0xB1, 0x41, 0xC5, 0x39, 0xF1, 0xAC, 0xE0, 0xFF, 0x3E, 0x58, 0xE1, 0xF6, 0x1F, 0xA6, 0x3A, 0xFF, 0xB0, 0x48, 0x01, 0x90, 0x62, 0x0D, 0x97, 0xDB, 0xBF, 0x85, 0xDE,
	0x6A, 0xB9, 0x99, 0x6D, 0x1E, 0x7A, 0x79, 0x46, 0xAB, 0x5C, 0x3A, 0x3A, 0x44, 0x11, 0x3B, 0xC2, 0xAE, 0x4E, 0x44, 0x53, 0x0B, 0x23, 0x98, 0x64, 0x9A, 0xA6, 0x6B, 0x54, 0x30,
	0x88, 0xBD, 0xD9, 0xC7, 0x0E, 0x6F, 0x31, 0xC1, 0x88, 0xB0, 0x71, 0x03, 0x1E, 0x73, 0xA9, 0x39, 0x7C, 0xA2, 0x18, 0xED, 0xF8, 0xFA, 0x2F, 0xE2, 0x95, 0x8E, 0xED, 0x69, 0x5F,
	0xD6, 0xE4, 0xDE, 0xCC, 0x90, 0xAE, 0x89, 0xD5, 0xEB, 0x69, 0xDB, 0xCD, 0x0F, 0x21, 0x45, 0xC8, 0x4F, 0x9F, 0xCA, 0x6E, 0xCF, 0xBD, 0x43, 0x7F, 0x7A, 0xED, 0x14, 0xBF, 0x94,
	0xFD, 0xBE, 0xC5, 0xF2, 0xF8, 0x08, 0xCB, 0xD8, 0x38, 0x7F, 0xED, 0x22, 0x12, 0xC0, 0x84, 0x5F, 0x5E, 0x90, 0x5D, 0x41, 0x9F, 0xBC, 0xB7, 0x7F, 0xCD, 0x6B, 0x1C, 0xF9, 0x3A,
	0x55, 0xC0, 0xAD, 0x3D, 0x02, 0xBB, 0x74, 0x1B, 0x02, 0x99, 0x34, 0x05, 0x85, 0xAF, 0x65, 0x11, 0x1C, 0x56, 0x50, 0x3B, 0x2C, 0x96, 0x18, 0x36, 0x68, 0x47, 0xD7, 0xE0, 0xDD,
	0xBB, 0xFA, 0x44, 0x4B, 0x2B, 0x4F, 0xDB, 0x65, 0x1A, 0x74, 0xE3, 0x09, 0x2D, 0x04, 0x7C, 0x11, 0x20, 0xA9, 0x3F, 0x3D, 0x27, 0x8B, 0x24, 0x7D, 0x85, 0x41, 0xE5, 0x4A, 0x46,
	0xB8, 0x1D, 0x5C, 0x7E, 0xC7, 0x16, 0xF4, 0xD8, 0x58, 0x5D, 0xC3, 0x86, 0xEE, 0xCF, 0xE2, 0x76, 0xFF, 0xDF, 0x39, 0xA1, 0x2C, 0xA3, 0x73, 0x36, 0xC9, 0x38, 0x8D, 0x8B, 0x35,
	0x8A, 0xBA, 0xB1, 0x4D, 0xEB, 0xC9, 0x28, 0x44, 0xC6, 0x22, 0xFA, 0xA1, 0xEE, 0x53, 0x39, 0x55, 0xC7, 0xB1, 0x2F, 0x37, 0xD9, 0xAF, 0x75, 0x9B, 0x3A, 0xAA, 0x65, 0x56, 0x52,
	0x92, 0xA2, 0xE7, 0x33, 0x8D, 0x0F, 0xBD, 0x8B, 0xA4, 0xDD, 0xFB, 0xF9, 0xA8, 0x49, 0xB0, 0x34, 0xE1, 0x50, 0x77, 0x4D, 0xDB, 0x49, 0xBD, 0x4F, 0x53, 0xBC, 0xDF, 0xB7, 0x3C,
	0xF8, 0x0E, 0x50, 0x8E, 0x12, 0x26, 0x52, 0x51, 0x12, 0x7B, 0x6C, 0xA6, 0x20, 0x95, 0x05, 0xC4, 0xD6, 0x58, 0x61, 0x9B, 0x52, 0x09, 0x0B, 0x0B, 0xF7, 0xE8, 0x82, 0xFA, 0xDA,
	0xAB, 0xF1, 0xBE, 0xE9, 0x8B, 0xAA, 0x40, 0x07, 0xD1, 0x81, 0xAD, 0xAE, 0xDA, 0xFA, 0x11, 0xB6, 0xA2, 0x1B, 0xEF, 0xCD, 0x3B, 0xB4, 0xDC, 0x18, 0xE7, 0x8B, 0x30, 0x84, 0x1B,
	0xAE, 0x7B, 0xEA, 0x7B, 0x3E, 0xFE, 0xEA, 0xE2, 0x7B, 0x63, 0xF4, 0x95, 0x5D, 0x0E, 0x6A, 0x4F, 0x5E, 0x3C, 0x83, 0xD7, 0x4C, 0x2D, 0x06, 0x95,
};

#endif /* SK_COMPRESSION_HAS_ZSTD */

#ifdef SK_COMPRESSION_HAS_LZ4
/* lz4 frames: main's commented-out LZ4 branch -- LZ4_compress_default + size prefix. */
/* empty: 0 payload bytes -> 9 frame bytes */
static const u8 compat_lz4_empty_frame[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* one_byte: 1 payload bytes -> 10 frame bytes */
static const u8 compat_lz4_one_byte_frame[] = {
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x41,
};

/* fixture: 55 payload bytes -> 64 frame bytes */
static const u8 compat_lz4_fixture_frame[] = {
	0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF1, 0x11, 0x53, 0x6B, 0x6F, 0x72, 0x65, 0x20, 0x76, 0x32, 0x20, 0x63, 0x6F, 0x6D,
	0x70, 0x72, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x6D, 0x61, 0x69, 0x6E, 0x2D, 0x62, 0x72, 0x61, 0x6E, 0x63, 0x68, 0x18, 0x00,
	0xF0, 0x03, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6C, 0x69, 0x74, 0x79, 0x20, 0x66, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x0A,
};

/* compressible: 768 payload bytes -> 138 frame bytes */
static const u8 compat_lz4_compressible_frame[] = {
	0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x66, 0x53, 0x6B, 0x6F, 0x72, 0x65, 0x20, 0x76, 0x32, 0x20, 0x63, 0x72, 0x6F, 0x73, 0x73, 0x2D, 0x65, 0x6E, 0x67,
	0x69, 0x6E, 0x65, 0x20, 0x63, 0x6F, 0x6D, 0x70, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6C, 0x69, 0x74, 0x79, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6F, 0x72, 0x20, 0x63, 0x6F, 0x72,
	0x70, 0x75, 0x73, 0x3A, 0x20, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5A, 0x20, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7A, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2E, 0x0A, 0x75, 0x00, 0xFF, 0xFF, 0x75, 0x50, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
};

/* incompressible: 768 payload bytes -> 780 frame bytes */
static const u8 compat_lz4_incompressible_frame[] = {
	0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xF3, 0x40, 0x91, 0x9C, 0x65, 0x0F, 0x3D, 0xEF, 0x7C, 0xF9, 0x0D, 0x9F, 0x3F, 0x96, 0x48, 0xE6, 0x4F, 0xC1,
	0x69, 0xEE, 0x8A, 0x91, 0x23, 0xAB, 0x9B, 0xDA, 0x7B, 0x72, 0x62, 0x74, 0x81, 0xAA, 0x5A, 0xB4, 0x81, 0xFA, 0x21, 0x0E, 0x1C, 0xAE, 0xB8, 0xD8, 0xA2, 0x12, 0x7F, 0x0F, 0x40,
	0xAD, 0x7D, 0xA9, 0x09, 0x46, 0x22, 0xC9, 0xBA, 0x38, 0x04, 0x67, 0x34, 0x7D, 0x6B, 0xCF, 0x48, 0xEA, 0x7E, 0x76, 0xD0, 0x90, 0xEB, 0xED, 0x37, 0xEE, 0x59, 0xE5, 0x81, 0xB2,
	0x93, 0x9F, 0x5C, 0x44, 0x32, 0x40, 0x37, 0x6A, 0x11, 0x63, 0x5C, 0x4F, 0x39, 0x89, 0xC0, 0xA2, 0x90, 0x14, 0x2A, 0xA3, 0xDB, 0x1C, 0x42, 0xC9, 0x68, 0x1B, 0x0F, 0x3E, 0x39,
	0x2C, 0xC6, 0x7C, 0xFF, 0x97, 0x69, 0x04, 0x8A, 0x40, 0xE0, 0x56, 0x71, 0xA8, 0x8D, 0xC4, 0xC0, 0xD4, 0xBE, 0xB3, 0x43, 0x16, 0x6C, 0x72, 0xBA, 0x88, 0x0B, 0xEA, 0x7A, 0xCA,
	0xF0, 0x5C, 0xD5, 0xF8, 0xF6, 0xD6, 0x80, 0x9B, 0x28, 0xBF, 0x14, 0x53, 0x57, 0x8B, 0x73, 0x90, 0x5D, 0xDF, 0x4B, 0x37, 0x99, 0xD7, 0xE9, 0xDD, 0x75, 0x36, 0x49, 0xEB, 0xA5,
	0xBC, 0xA5, 0xAB, 0xC1, 0xC5, 0x61, 0x8B, 0xB5, 0x66, 0x35, 0x37, 0x3D, 0xD2, 0xC1, 0xBC, 0xDF, 0x70, 0x6B, 0x70, 0x70, 0x2D, 0x8B, 0xEC, 0x0D, 0x8B, 0x91, 0xBE, 0x17, 0x4F,
	0xE4, 0x4C, 0xE9, 0xAC, 0xC2, 0xDC, 0xF1, 0x43, 0xF8, 0x9E, 0x9F, 0xC3, 0x52, 0x34, 0xF6, 0xAF, 0x12, 0x9D, 0x77, 0xFC, 0x95, 0xC9, 0xC9, 0x32, 0xC6, 0x3D, 0xDF, 0x29, 0x1A,
	0xD3, 0x5B, 0x38, 0x98, 0x33, 0x06, 0xEB, 0x2B, 0x5F, 0x6F, 0x72, 0x6A, 0xD6, 0x09, 0x1B, 0x01, 0xFC, 0xE6, 0x45, 0x2F, 0x26, 0xB1, 0xED, 0x8A, 0xD3, 0x43, 0xE6, 0x8A, 0x12,
	0x55, 0x9F, 0x07, 0x56, 0xEF, 0xC8, 0xA1, 0x64, 0x6F, 0xDD, 0x6C, 0xAD, 0xC6, 0x64, 0x86, 0x3A, 0xBA, 0x27, 0x1D, 0x47, 0x64, 0xBE, 0x4E, 0x49, 0xDF, 0x16, 0x88, 0xB6, 0x08,
	0x46, 0x5E, 0xF4, 0x32, 0xD9, 0xC2, 0x75, 0xCD, 0x86, 0x39, 0xB5, 0xAF, 0x42, 0x0A, 0xA6, 0x5C, 0x50, 0x4D, 0xD2, 0x6D, 0xE5, 0x2D, 0x2A, 0xFA, 0x85, 0x0D, 0x9F, 0x36, 0xD7,
	0x17, 0x14, 0xA7, 0xCA, 0x77, 0xB5, 0x4B, 0xAC, 0x84, 0x78, 0x66, 0xD9, 0xB5, 0x45, 0x2C, 0x0D, 0x17, 0x9D, 0x18, 0x65, 0xEB, 0xA1, 0x9A, 0x65, 0xDF, 0x11, 0xD3, 0x3E, 0x4E,
	0xC1, 0xE7, 0x93, 0xAF, 0x69, 0xA0, 0xE8, 0xB9, 0x2C, 0xFF, 0xA8, 0xB9, 0x52, 0x1F, 0x39, 0x92, 0xD8, 0x30, 0x62, 0x5C, 0x35, 0x15, 0x88, 0x21, 0x8A, 0xB7, 0x87, 0x38, 0x6F,
	0x5E, 0xE7, 0x07, 0x53, 0x2A, 0x3B, 0x53, 0x61, 0x5A, 0xAB, 0x0E, 0xB5, 0xCD, 0xF8, 0x0E, 0x5D, 0x3E, 0x0A, 0xC4, 0x7E, 0x3A, 0x64, 0xBE, 0x03, 0xDD, 0x5B, 0x0E, 0xBA, 0xAF,
	0x5C, 0x95, 0x4C, 0x1B, 0xB1, 0x41, 0xC5, 0x39, 0xF1, 0xAC, 0xE0, 0xFF, 0x3E, 0x58, 0xE1, 0xF6, 0x1F, 0xA6, 0x3A, 0xFF, 0xB0, 0x48, 0x01, 0x90, 0x62, 0x0D, 0x97, 0xDB, 0xBF,
	0x85, 0xDE, 0x6A, 0xB9, 0x99, 0x6D, 0x1E, 0x7A, 0x79, 0x46, 0xAB, 0x5C, 0x3A, 0x3A, 0x44, 0x11, 0x3B, 0xC2, 0xAE, 0x4E, 0x44, 0x53, 0x0B, 0x23, 0x98, 0x64, 0x9A, 0xA6, 0x6B,
	0x54, 0x30, 0x88, 0xBD, 0xD9, 0xC7, 0x0E, 0x6F, 0x31, 0xC1, 0x88, 0xB0, 0x71, 0x03, 0x1E, 0x73, 0xA9, 0x39, 0x7C, 0xA2, 0x18, 0xED, 0xF8, 0xFA, 0x2F, 0xE2, 0x95, 0x8E, 0xED,
	0x69, 0x5F, 0xD6, 0xE4, 0xDE, 0xCC, 0x90, 0xAE, 0x89, 0xD5, 0xEB, 0x69, 0xDB, 0xCD, 0x0F, 0x21, 0x45, 0xC8, 0x4F, 0x9F, 0xCA, 0x6E, 0xCF, 0xBD, 0x43, 0x7F, 0x7A, 0xED, 0x14,
	0xBF, 0x94, 0xFD, 0xBE, 0xC5, 0xF2, 0xF8, 0x08, 0xCB, 0xD8, 0x38, 0x7F, 0xED, 0x22, 0x12, 0xC0, 0x84, 0x5F, 0x5E, 0x90, 0x5D, 0x41, 0x9F, 0xBC, 0xB7, 0x7F, 0xCD, 0x6B, 0x1C,
	0xF9, 0x3A, 0x55, 0xC0, 0xAD, 0x3D, 0x02, 0xBB, 0x74, 0x1B, 0x02, 0x99, 0x34, 0x05, 0x85, 0xAF, 0x65, 0x11, 0x1C, 0x56, 0x50, 0x3B, 0x2C, 0x96, 0x18, 0x36, 0x68, 0x47, 0xD7,
	0xE0, 0xDD, 0xBB, 0xFA, 0x44, 0x4B, 0x2B, 0x4F, 0xDB, 0x65, 0x1A, 0x74, 0xE3, 0x09, 0x2D, 0x04, 0x7C, 0x11, 0x20, 0xA9, 0x3F, 0x3D, 0x27, 0x8B, 0x24, 0x7D, 0x85, 0x41, 0xE5,
	0x4A, 0x46, 0xB8, 0x1D, 0x5C, 0x7E, 0xC7, 0x16, 0xF4, 0xD8, 0x58, 0x5D, 0xC3, 0x86, 0xEE, 0xCF, 0xE2, 0x76, 0xFF, 0xDF, 0x39, 0xA1, 0x2C, 0xA3, 0x73, 0x36, 0xC9, 0x38, 0x8D,
	0x8B, 0x35, 0x8A, 0xBA, 0xB1, 0x4D, 0xEB, 0xC9, 0x28, 0x44, 0xC6, 0x22, 0xFA, 0xA1, 0xEE, 0x53, 0x39, 0x55, 0xC7, 0xB1, 0x2F, 0x37, 0xD9, 0xAF, 0x75, 0x9B, 0x3A, 0xAA, 0x65,
	0x56, 0x52, 0x92, 0xA2, 0xE7, 0x33, 0x8D, 0x0F, 0xBD, 0x8B, 0xA4, 0xDD, 0xFB, 0xF9, 0xA8, 0x49, 0xB0, 0x34, 0xE1, 0x50, 0x77, 0x4D, 0xDB, 0x49, 0xBD, 0x4F, 0x53, 0xBC, 0xDF,
	0xB7, 0x3C, 0xF8, 0x0E, 0x50, 0x8E, 0x12, 0x26, 0x52, 0x51, 0x12, 0x7B, 0x6C, 0xA6, 0x20, 0x95, 0x05, 0xC4, 0xD6, 0x58, 0x61, 0x9B, 0x52, 0x09, 0x0B, 0x0B, 0xF7, 0xE8, 0x82,
	0xFA, 0xDA, 0xAB, 0xF1, 0xBE, 0xE9, 0x8B, 0xAA, 0x40, 0x07, 0xD1, 0x81, 0xAD, 0xAE, 0xDA, 0xFA, 0x11, 0xB6, 0xA2, 0x1B, 0xEF, 0xCD, 0x3B, 0xB4, 0xDC, 0x18, 0xE7, 0x8B, 0x30,
	0x84, 0x1B, 0xAE, 0x7B, 0xEA, 0x7B, 0x3E, 0xFE, 0xEA, 0xE2, 0x7B, 0x63, 0xF4, 0x95, 0x5D, 0x0E, 0x6A, 0x4F, 0x5E, 0x3C, 0x83, 0xD7, 0x4C, 0x2D, 0x06, 0x95,
};

#endif /* SK_COMPRESSION_HAS_LZ4 */

#ifdef SK_COMPRESSION_HAS_MINIZ
/* zlib frames: mz_compress2 level 6 (RFC 1950) + size prefix. */
/* empty: 0 payload bytes -> 16 frame bytes */
static const u8 compat_zlib_empty_frame[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01,
};

/* one_byte: 1 payload bytes -> 20 frame bytes */
static const u8 compat_zlib_one_byte_frame[] = {
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x9C, 0x01, 0x01, 0x00, 0xFE, 0xFF, 0x41, 0x00, 0x42, 0x00, 0x42,
};

/* fixture: 55 payload bytes -> 74 frame bytes */
static const u8 compat_zlib_fixture_frame[] = {
	0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x9C, 0x01, 0x37, 0x00, 0xC8, 0xFF, 0x53, 0x6B, 0x6F, 0x72, 0x65, 0x20, 0x76, 0x32, 0x20, 0x63,
	0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x6D, 0x61, 0x69, 0x6E, 0x2D, 0x62, 0x72, 0x61, 0x6E, 0x63, 0x68, 0x20, 0x63, 0x6F,
	0x6D, 0x70, 0x61, 0x74, 0x69, 0x62, 0x69, 0x6C, 0x69, 0x74, 0x79, 0x20, 0x66, 0x69, 0x78, 0x74, 0x75, 0x72, 0x65, 0x0A, 0x41, 0x1D, 0x14, 0xCA,
};

/* compressible: 768 payload bytes -> 142 frame bytes */
static const u8 compat_zlib_compressible_frame[] = {
	0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x9C, 0xED, 0xCD, 0x39, 0x16, 0x82, 0x30, 0x10, 0x00, 0xD0, 0xDE, 0x53, 0xCC, 0x05, 0xF4, 0x29, 0xEE, 0x76, 0xEE, 0xFB,
	0x1A, 0xF7, 0x0E, 0x62, 0xC4, 0x08, 0x24, 0x71, 0x12, 0xA2, 0x78, 0x7A, 0x38, 0x06, 0x05, 0xED, 0x6F, 0x3E, 0x09, 0x24, 0x32, 0xB0, 0x0E, 0x50, 0x94, 0x5A, 0x97, 0x99, 0xF0,
	0xB9, 0x60, 0x40, 0x65, 0xA4, 0x5C, 0xC3, 0x3D, 0x1E, 0x72, 0x93, 0x80, 0x65, 0xD4, 0x48, 0xCC, 0x10, 0x55, 0xAC, 0x7B, 0xD0, 0x1F, 0x0C, 0x47, 0xE3, 0xC9, 0x74, 0x36, 0x5F,
	0x2C, 0x57, 0xEB, 0xCD, 0x76, 0xB7, 0x3F, 0x90, 0xE3, 0xE9, 0x7C, 0xB9, 0xDE, 0xEE, 0xE0, 0x7A, 0xF4, 0xC1, 0x9E, 0xFE, 0x8B, 0xBF, 0x83, 0x30, 0x12, 0x52, 0x7D, 0x50, 0x9B,
	0xD8, 0x7E, 0x7F, 0xC9, 0x1F, 0xAA, 0x35, 0xA7, 0xDE, 0x68, 0xB6, 0xDA, 0x9D, 0x6E, 0xA5, 0x44, 0x8A, 0xB4, 0x48, 0xF3, 0x91, 0xA6, 0xBF, 0x6F, 0x0B, 0x1E,
};

/* incompressible: 768 payload bytes -> 787 frame bytes */
static const u8 compat_zlib_incompressible_frame[] = {
	0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x9C, 0x01, 0x00, 0x03, 0xFF, 0xFC, 0x40, 0x91, 0x9C, 0x65, 0x0F, 0x3D, 0xEF, 0x7C, 0xF9, 0x0D, 0x9F, 0x3F, 0x96, 0x48,
	0xE6, 0x4F, 0xC1, 0x69, 0xEE, 0x8A, 0x91, 0x23, 0xAB, 0x9B, 0xDA, 0x7B, 0x72, 0x62, 0x74, 0x81, 0xAA, 0x5A, 0xB4, 0x81, 0xFA, 0x21, 0x0E, 0x1C, 0xAE, 0xB8, 0xD8, 0xA2, 0x12,
	0x7F, 0x0F, 0x40, 0xAD, 0x7D, 0xA9, 0x09, 0x46, 0x22, 0xC9, 0xBA, 0x38, 0x04, 0x67, 0x34, 0x7D, 0x6B, 0xCF, 0x48, 0xEA, 0x7E, 0x76, 0xD0, 0x90, 0xEB, 0xED, 0x37, 0xEE, 0x59,
	0xE5, 0x81, 0xB2, 0x93, 0x9F, 0x5C, 0x44, 0x32, 0x40, 0x37, 0x6A, 0x11, 0x63, 0x5C, 0x4F, 0x39, 0x89, 0xC0, 0xA2, 0x90, 0x14, 0x2A, 0xA3, 0xDB, 0x1C, 0x42, 0xC9, 0x68, 0x1B,
	0x0F, 0x3E, 0x39, 0x2C, 0xC6, 0x7C, 0xFF, 0x97, 0x69, 0x04, 0x8A, 0x40, 0xE0, 0x56, 0x71, 0xA8, 0x8D, 0xC4, 0xC0, 0xD4, 0xBE, 0xB3, 0x43, 0x16, 0x6C, 0x72, 0xBA, 0x88, 0x0B,
	0xEA, 0x7A, 0xCA, 0xF0, 0x5C, 0xD5, 0xF8, 0xF6, 0xD6, 0x80, 0x9B, 0x28, 0xBF, 0x14, 0x53, 0x57, 0x8B, 0x73, 0x90, 0x5D, 0xDF, 0x4B, 0x37, 0x99, 0xD7, 0xE9, 0xDD, 0x75, 0x36,
	0x49, 0xEB, 0xA5, 0xBC, 0xA5, 0xAB, 0xC1, 0xC5, 0x61, 0x8B, 0xB5, 0x66, 0x35, 0x37, 0x3D, 0xD2, 0xC1, 0xBC, 0xDF, 0x70, 0x6B, 0x70, 0x70, 0x2D, 0x8B, 0xEC, 0x0D, 0x8B, 0x91,
	0xBE, 0x17, 0x4F, 0xE4, 0x4C, 0xE9, 0xAC, 0xC2, 0xDC, 0xF1, 0x43, 0xF8, 0x9E, 0x9F, 0xC3, 0x52, 0x34, 0xF6, 0xAF, 0x12, 0x9D, 0x77, 0xFC, 0x95, 0xC9, 0xC9, 0x32, 0xC6, 0x3D,
	0xDF, 0x29, 0x1A, 0xD3, 0x5B, 0x38, 0x98, 0x33, 0x06, 0xEB, 0x2B, 0x5F, 0x6F, 0x72, 0x6A, 0xD6, 0x09, 0x1B, 0x01, 0xFC, 0xE6, 0x45, 0x2F, 0x26, 0xB1, 0xED, 0x8A, 0xD3, 0x43,
	0xE6, 0x8A, 0x12, 0x55, 0x9F, 0x07, 0x56, 0xEF, 0xC8, 0xA1, 0x64, 0x6F, 0xDD, 0x6C, 0xAD, 0xC6, 0x64, 0x86, 0x3A, 0xBA, 0x27, 0x1D, 0x47, 0x64, 0xBE, 0x4E, 0x49, 0xDF, 0x16,
	0x88, 0xB6, 0x08, 0x46, 0x5E, 0xF4, 0x32, 0xD9, 0xC2, 0x75, 0xCD, 0x86, 0x39, 0xB5, 0xAF, 0x42, 0x0A, 0xA6, 0x5C, 0x50, 0x4D, 0xD2, 0x6D, 0xE5, 0x2D, 0x2A, 0xFA, 0x85, 0x0D,
	0x9F, 0x36, 0xD7, 0x17, 0x14, 0xA7, 0xCA, 0x77, 0xB5, 0x4B, 0xAC, 0x84, 0x78, 0x66, 0xD9, 0xB5, 0x45, 0x2C, 0x0D, 0x17, 0x9D, 0x18, 0x65, 0xEB, 0xA1, 0x9A, 0x65, 0xDF, 0x11,
	0xD3, 0x3E, 0x4E, 0xC1, 0xE7, 0x93, 0xAF, 0x69, 0xA0, 0xE8, 0xB9, 0x2C, 0xFF, 0xA8, 0xB9, 0x52, 0x1F, 0x39, 0x92, 0xD8, 0x30, 0x62, 0x5C, 0x35, 0x15, 0x88, 0x21, 0x8A, 0xB7,
	0x87, 0x38, 0x6F, 0x5E, 0xE7, 0x07, 0x53, 0x2A, 0x3B, 0x53, 0x61, 0x5A, 0xAB, 0x0E, 0xB5, 0xCD, 0xF8, 0x0E, 0x5D, 0x3E, 0x0A, 0xC4, 0x7E, 0x3A, 0x64, 0xBE, 0x03, 0xDD, 0x5B,
	0x0E, 0xBA, 0xAF, 0x5C, 0x95, 0x4C, 0x1B, 0xB1, 0x41, 0xC5, 0x39, 0xF1, 0xAC, 0xE0, 0xFF, 0x3E, 0x58, 0xE1, 0xF6, 0x1F, 0xA6, 0x3A, 0xFF, 0xB0, 0x48, 0x01, 0x90, 0x62, 0x0D,
	0x97, 0xDB, 0xBF, 0x85, 0xDE, 0x6A, 0xB9, 0x99, 0x6D, 0x1E, 0x7A, 0x79, 0x46, 0xAB, 0x5C, 0x3A, 0x3A, 0x44, 0x11, 0x3B, 0xC2, 0xAE, 0x4E, 0x44, 0x53, 0x0B, 0x23, 0x98, 0x64,
	0x9A, 0xA6, 0x6B, 0x54, 0x30, 0x88, 0xBD, 0xD9, 0xC7, 0x0E, 0x6F, 0x31, 0xC1, 0x88, 0xB0, 0x71, 0x03, 0x1E, 0x73, 0xA9, 0x39, 0x7C, 0xA2, 0x18, 0xED, 0xF8, 0xFA, 0x2F, 0xE2,
	0x95, 0x8E, 0xED, 0x69, 0x5F, 0xD6, 0xE4, 0xDE, 0xCC, 0x90, 0xAE, 0x89, 0xD5, 0xEB, 0x69, 0xDB, 0xCD, 0x0F, 0x21, 0x45, 0xC8, 0x4F, 0x9F, 0xCA, 0x6E, 0xCF, 0xBD, 0x43, 0x7F,
	0x7A, 0xED, 0x14, 0xBF, 0x94, 0xFD, 0xBE, 0xC5, 0xF2, 0xF8, 0x08, 0xCB, 0xD8, 0x38, 0x7F, 0xED, 0x22, 0x12, 0xC0, 0x84, 0x5F, 0x5E, 0x90, 0x5D, 0x41, 0x9F, 0xBC, 0xB7, 0x7F,
	0xCD, 0x6B, 0x1C, 0xF9, 0x3A, 0x55, 0xC0, 0xAD, 0x3D, 0x02, 0xBB, 0x74, 0x1B, 0x02, 0x99, 0x34, 0x05, 0x85, 0xAF, 0x65, 0x11, 0x1C, 0x56, 0x50, 0x3B, 0x2C, 0x96, 0x18, 0x36,
	0x68, 0x47, 0xD7, 0xE0, 0xDD, 0xBB, 0xFA, 0x44, 0x4B, 0x2B, 0x4F, 0xDB, 0x65, 0x1A, 0x74, 0xE3, 0x09, 0x2D, 0x04, 0x7C, 0x11, 0x20, 0xA9, 0x3F, 0x3D, 0x27, 0x8B, 0x24, 0x7D,
	0x85, 0x41, 0xE5, 0x4A, 0x46, 0xB8, 0x1D, 0x5C, 0x7E, 0xC7, 0x16, 0xF4, 0xD8, 0x58, 0x5D, 0xC3, 0x86, 0xEE, 0xCF, 0xE2, 0x76, 0xFF, 0xDF, 0x39, 0xA1, 0x2C, 0xA3, 0x73, 0x36,
	0xC9, 0x38, 0x8D, 0x8B, 0x35, 0x8A, 0xBA, 0xB1, 0x4D, 0xEB, 0xC9, 0x28, 0x44, 0xC6, 0x22, 0xFA, 0xA1, 0xEE, 0x53, 0x39, 0x55, 0xC7, 0xB1, 0x2F, 0x37, 0xD9, 0xAF, 0x75, 0x9B,
	0x3A, 0xAA, 0x65, 0x56, 0x52, 0x92, 0xA2, 0xE7, 0x33, 0x8D, 0x0F, 0xBD, 0x8B, 0xA4, 0xDD, 0xFB, 0xF9, 0xA8, 0x49, 0xB0, 0x34, 0xE1, 0x50, 0x77, 0x4D, 0xDB, 0x49, 0xBD, 0x4F,
	0x53, 0xBC, 0xDF, 0xB7, 0x3C, 0xF8, 0x0E, 0x50, 0x8E, 0x12, 0x26, 0x52, 0x51, 0x12, 0x7B, 0x6C, 0xA6, 0x20, 0x95, 0x05, 0xC4, 0xD6, 0x58, 0x61, 0x9B, 0x52, 0x09, 0x0B, 0x0B,
	0xF7, 0xE8, 0x82, 0xFA, 0xDA, 0xAB, 0xF1, 0xBE, 0xE9, 0x8B, 0xAA, 0x40, 0x07, 0xD1, 0x81, 0xAD, 0xAE, 0xDA, 0xFA, 0x11, 0xB6, 0xA2, 0x1B, 0xEF, 0xCD, 0x3B, 0xB4, 0xDC, 0x18,
	0xE7, 0x8B, 0x30, 0x84, 0x1B, 0xAE, 0x7B, 0xEA, 0x7B, 0x3E, 0xFE, 0xEA, 0xE2, 0x7B, 0x63, 0xF4, 0x95, 0x5D, 0x0E, 0x6A, 0x4F, 0x5E, 0x3C, 0x83, 0xD7, 0x4C, 0x2D, 0x06, 0x95,
	0xF6, 0xBB, 0x7E, 0x8C,
};

#endif /* SK_COMPRESSION_HAS_MINIZ */
/* END APX-176 COMPATIBILITY VECTORS */

/* ---- APX-176 test tables and assertions -------------------------------- */

/* Fixed corpus line used to build the compressible vector (must match
 * scripts/gen-compression-vectors.c's COMPAT_LINE_TEXT). */
#define COMPAT_LINE_TEXT "Skore v2 cross-engine compatibility vector corpus: ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789.\n"

static const u8 compat_one_byte_payload[] = {0x41u};
static const u8 compat_fixture_payload[] = "Skore v2 compression main-branch compatibility fixture\n";
static u8 compat_compressible_payload[768];
static u8 compat_incompressible_payload[768];

/* Build the two generated corpora once; identical construction to the
 * generator so the frozen frames stay auditable. */
static void compression_compat_build_corpora(void) {
	static i32 built = 0;
	u32 state = 0u;

	if (built != 0) {
		return;
	}
	built = 1;

	for (u32 i = 0u; i < sizeof(compat_compressible_payload); ++i) {
		compat_compressible_payload[i] = (u8)COMPAT_LINE_TEXT[i % (sizeof(COMPAT_LINE_TEXT) - 1u)];
	}
	state = 0x6D2B79F5u;
	for (u32 i = 0u; i < sizeof(compat_incompressible_payload); ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		compat_incompressible_payload[i] = (u8)(state >> 24u);
	}
}

typedef struct compression_compat_vector_t {
	const_chr_t name;
	const u8* payload;
	u64 payload_size;
	const u8* frame;
	u64 frame_size;
} compression_compat_vector_t;

#ifdef SK_COMPRESSION_HAS_ZSTD
static const compression_compat_vector_t compression_zstd_compat_vectors[] = {
	{"empty", NULL, 0u, compat_zstd_empty_frame, sizeof(compat_zstd_empty_frame)},
	{"one_byte", compat_one_byte_payload, sizeof(compat_one_byte_payload), compat_zstd_one_byte_frame, sizeof(compat_zstd_one_byte_frame)},
	{"fixture", compat_fixture_payload, sizeof(compat_fixture_payload) - 1u, compat_zstd_fixture_frame, sizeof(compat_zstd_fixture_frame)},
	{"compressible", compat_compressible_payload, sizeof(compat_compressible_payload), compat_zstd_compressible_frame, sizeof(compat_zstd_compressible_frame)},
	{"incompressible", compat_incompressible_payload, sizeof(compat_incompressible_payload), compat_zstd_incompressible_frame, sizeof(compat_zstd_incompressible_frame)},
};
#endif /* SK_COMPRESSION_HAS_ZSTD */

#ifdef SK_COMPRESSION_HAS_LZ4
static const compression_compat_vector_t compression_lz4_compat_vectors[] = {
	{"empty", NULL, 0u, compat_lz4_empty_frame, sizeof(compat_lz4_empty_frame)},
	{"one_byte", compat_one_byte_payload, sizeof(compat_one_byte_payload), compat_lz4_one_byte_frame, sizeof(compat_lz4_one_byte_frame)},
	{"fixture", compat_fixture_payload, sizeof(compat_fixture_payload) - 1u, compat_lz4_fixture_frame, sizeof(compat_lz4_fixture_frame)},
	{"compressible", compat_compressible_payload, sizeof(compat_compressible_payload), compat_lz4_compressible_frame, sizeof(compat_lz4_compressible_frame)},
	{"incompressible", compat_incompressible_payload, sizeof(compat_incompressible_payload), compat_lz4_incompressible_frame, sizeof(compat_lz4_incompressible_frame)},
};
#endif /* SK_COMPRESSION_HAS_LZ4 */

#ifdef SK_COMPRESSION_HAS_MINIZ
static const compression_compat_vector_t compression_zlib_compat_vectors[] = {
	{"empty", NULL, 0u, compat_zlib_empty_frame, sizeof(compat_zlib_empty_frame)},
	{"one_byte", compat_one_byte_payload, sizeof(compat_one_byte_payload), compat_zlib_one_byte_frame, sizeof(compat_zlib_one_byte_frame)},
	{"fixture", compat_fixture_payload, sizeof(compat_fixture_payload) - 1u, compat_zlib_fixture_frame, sizeof(compat_zlib_fixture_frame)},
	{"compressible", compat_compressible_payload, sizeof(compat_compressible_payload), compat_zlib_compressible_frame, sizeof(compat_zlib_compressible_frame)},
	{"incompressible", compat_incompressible_payload, sizeof(compat_incompressible_payload), compat_zlib_incompressible_frame, sizeof(compat_zlib_incompressible_frame)},
};
#endif /* SK_COMPRESSION_HAS_MINIZ */

typedef struct compression_compat_run_t {
	const sk_compression_codec_t* codec;
	const compression_compat_vector_t* vectors;
	u32 vector_count;
} compression_compat_run_t;

static compression_compat_run_t compression_compat_runs[4];
static u32 compression_compat_run_count = 0u;

/* Register every codec that has frozen vectors; idempotent per test. */
static void compression_compat_register_runs(void) {
	compression_compat_run_count = 0u;
#ifdef SK_COMPRESSION_HAS_ZSTD
	compression_compat_runs[compression_compat_run_count].codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
	compression_compat_runs[compression_compat_run_count].vectors = compression_zstd_compat_vectors;
	compression_compat_runs[compression_compat_run_count].vector_count = (u32)(sizeof(compression_zstd_compat_vectors) / sizeof(compression_zstd_compat_vectors[0]));
	compression_compat_run_count += 1u;
#endif
#ifdef SK_COMPRESSION_HAS_LZ4
	compression_compat_runs[compression_compat_run_count].codec = sk_compression_codec(SK_COMPRESSION_CODEC_LZ4);
	compression_compat_runs[compression_compat_run_count].vectors = compression_lz4_compat_vectors;
	compression_compat_runs[compression_compat_run_count].vector_count = (u32)(sizeof(compression_lz4_compat_vectors) / sizeof(compression_lz4_compat_vectors[0]));
	compression_compat_run_count += 1u;
#endif
#ifdef SK_COMPRESSION_HAS_MINIZ
	compression_compat_runs[compression_compat_run_count].codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZLIB);
	compression_compat_runs[compression_compat_run_count].vectors = compression_zlib_compat_vectors;
	compression_compat_runs[compression_compat_run_count].vector_count = (u32)(sizeof(compression_zlib_compat_vectors) / sizeof(compression_zlib_compat_vectors[0]));
	compression_compat_run_count += 1u;
#endif
	TEST_ASSERT_TRUE(compression_compat_run_count >= 1u);
	TEST_ASSERT_TRUE(compression_compat_run_count <= (u32)(sizeof(compression_compat_runs) / sizeof(compression_compat_runs[0])));
}

/* Largest frozen frame (zlib incompressible) plus guard room. */
#define COMPAT_VECTOR_MAX_FRAME 1024u

/* Decode direction: every frozen reference frame must decompress to its
 * corpus byte-for-byte (main C++ engine / reference output is readable). */
SK_TEST(compression_compat_vectors_decode) {
	const sk_allocator_t* scratch = sk_allocator_default();

	compression_compat_build_corpora();
	compression_compat_register_runs();

	for (u32 run = 0u; run < compression_compat_run_count; ++run) {
		const sk_compression_codec_t* codec = compression_compat_runs[run].codec;

		TEST_ASSERT_NOT_NULL(codec);
		for (u32 v = 0u; v < compression_compat_runs[run].vector_count; ++v) {
			const compression_compat_vector_t* vector = &compression_compat_runs[run].vectors[v];
			u8 restored[COMPAT_VECTOR_MAX_FRAME];
			u64 written = 0u;
			u64 declared = 0u;

			TEST_ASSERT_TRUE(vector->payload_size <= sizeof(restored));
			TEST_ASSERT_TRUE(vector->frame_size <= COMPAT_VECTOR_MAX_FRAME);
			TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompressed_size(vector->frame, vector->frame_size, &declared));
			TEST_ASSERT_EQUAL_UINT64(vector->payload_size, declared);
			TEST_ASSERT_TRUE(codec->decompress_bound(vector->frame, vector->frame_size) >= vector->payload_size);
			TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(scratch, vector->frame, vector->frame_size, restored, vector->payload_size, &written));
			TEST_ASSERT_EQUAL_UINT64(vector->payload_size, written);
			if (vector->payload_size > 0u) {
				TEST_ASSERT_EQUAL_MEMORY(vector->payload, restored, vector->payload_size);
			}
		}
	}
}

/* Encode direction: v2 at the default level must re-emit the frozen
 * reference frame byte-for-byte. Identical bytes mean the C++ engine (or the
 * reference decoder) recovers the corpus unchanged -- the checked-in
 * expected-output comparison. Fails loudly if a codec's format drifts; never
 * edit the vectors to match v2 output. */
SK_TEST(compression_compat_vectors_encode) {
	const sk_allocator_t* scratch = sk_allocator_default();

	compression_compat_build_corpora();
	compression_compat_register_runs();

	for (u32 run = 0u; run < compression_compat_run_count; ++run) {
		const sk_compression_codec_t* codec = compression_compat_runs[run].codec;

		for (u32 v = 0u; v < compression_compat_runs[run].vector_count; ++v) {
			const compression_compat_vector_t* vector = &compression_compat_runs[run].vectors[v];
			const u64 bound = codec->compress_bound(vector->payload_size);
			u8* encoded = scratch->alloc(scratch->instance, bound > 0u ? bound : 1u);
			u64 written = 0u;

			TEST_ASSERT_NOT_NULL(encoded);
			TEST_ASSERT_TRUE(bound != SK_COMPRESSION_SIZE_UNKNOWN);
			TEST_ASSERT_TRUE(vector->frame_size <= bound);
			TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(scratch, SK_COMPRESSION_LEVEL_DEFAULT, vector->payload, vector->payload_size, encoded, bound, &written));
			TEST_ASSERT_EQUAL_UINT64(vector->frame_size, written);
			TEST_ASSERT_EQUAL_MEMORY(vector->frame, encoded, vector->frame_size);

			scratch->free(scratch->instance, encoded);
		}
	}
}

/* Truncated / corrupted frames report the documented error code and write
 * nothing -- no crash, no out-of-bounds access (design §7). Truncation cuts
 * every frame at the prefix/header boundaries, mid-frame, and one byte short;
 * the corrupted cases flip a byte that every built-in codec must detect (the
 * zstd frame magic, or the first block/stream byte past the v2 size prefix). */
SK_TEST(compression_compat_vectors_truncated_and_corrupt) {
	const sk_allocator_t* scratch = sk_allocator_default();
	static const u64 cut_points[] = {0u, 1u, 4u, 7u, 8u, 512u};
	u8 mutated[COMPAT_VECTOR_MAX_FRAME];
	u8 guarded[COMPAT_VECTOR_MAX_FRAME + 16u];

	compression_compat_build_corpora();
	compression_compat_register_runs();

	for (u32 run = 0u; run < compression_compat_run_count; ++run) {
		const sk_compression_codec_t* codec = compression_compat_runs[run].codec;

		for (u32 v = 0u; v < compression_compat_runs[run].vector_count; ++v) {
			const compression_compat_vector_t* vector = &compression_compat_runs[run].vectors[v];
			u64 written = 0u;

			TEST_ASSERT_TRUE(vector->frame_size <= sizeof(mutated));
			memcpy(mutated, vector->frame, vector->frame_size);

			for (u64 c = 0u; c < sizeof(cut_points) / sizeof(cut_points[0]); ++c) {
				const u64 cut = cut_points[c];

				if (cut >= vector->frame_size) {
					continue;
				}
				/* zstd accepts a 0-byte input as the "empty frame" and
				 * returns OK with nothing written -- exactly what main's
				 * Compression::Decompress (ZSTD_decompress) does, so this is
				 * the compatible result, not a corruption failure. */
				if (codec->id == SK_COMPRESSION_CODEC_ZSTD && cut == 0u) {
					u64 ok_written = 0u;

					TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(scratch, mutated, 0u, guarded + 8u, vector->payload_size, &ok_written));
					TEST_ASSERT_EQUAL_UINT64(0u, ok_written);
					continue;
				}
				/* The 8-byte prefix-only empty frame is the older valid v2
				 * empty encoding (still accepted); not a truncation failure. */
				if (vector->payload_size == 0u && cut == 8u) {
					continue;
				}
				memset(guarded, 0xA5u, sizeof(guarded));
				written = 0u;
				TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(scratch, mutated, cut, guarded + 8u, vector->payload_size, &written));
				TEST_ASSERT_EQUAL_UINT64(0u, written);
				for (u32 g = 0u; g < 8u; ++g) {
					TEST_ASSERT_EQUAL_HEX8(0xA5u, guarded[g]);
					TEST_ASSERT_EQUAL_HEX8(0xA5u, guarded[8u + vector->payload_size + g]);
				}
			}

			/* Corrupt one byte that every built-in codec must detect. The
			 * empty frames are already covered by the truncation cuts. */
			if (vector->payload_size > 0u) {
				u64 flip_index = 0u;

				if (codec->id == SK_COMPRESSION_CODEC_ZSTD) {
					flip_index = 0u;
				} else if (codec->id == SK_COMPRESSION_CODEC_LZ4 || codec->id == SK_COMPRESSION_CODEC_ZLIB) {
					flip_index = 8u;
				}
				TEST_ASSERT_TRUE(flip_index < vector->frame_size);
				mutated[flip_index] ^= 0xFFu;
				memset(guarded, 0xA5u, sizeof(guarded));
				written = 0u;
				TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(scratch, mutated, vector->frame_size, guarded + 8u, vector->payload_size, &written));
				TEST_ASSERT_EQUAL_UINT64(0u, written);
				for (u32 g = 0u; g < 8u; ++g) {
					TEST_ASSERT_EQUAL_HEX8(0xA5u, guarded[g]);
					TEST_ASSERT_EQUAL_HEX8(0xA5u, guarded[8u + vector->payload_size + g]);
				}
			}
		}
	}
}

#endif /* SK_COMPRESSION_HAS_ZSTD || SK_COMPRESSION_HAS_LZ4 || SK_COMPRESSION_HAS_MINIZ */

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

/* ==========================================================================
 * APX-173: reusable v1-vs-v2 roundtrip + parity harness
 *
 * Parameterized by codec name. Every registered sk_compression_codec_t is
 * covered automatically for v2 round-trips (future codecs need no harness
 * changes). Optional v1 adapters, keyed by codec->name, add wire/backward-
 * compat checks against the main-branch path. Size and throughput deltas are
 * printed for review and never used as pass/fail gates.
 * ========================================================================== */

#include <stdio.h> /* printf (delta report only) */
#include <time.h>  /* clock (portable throughput) */

/* zstd default block size: size-boundary corpus entries sit around this. */
#define COMPRESSION_HARNESS_ZSTD_BLOCK (128u * 1024u)
/* Iterations for measurable clock() throughput on small payloads. */
#define COMPRESSION_HARNESS_TIMING_ITERS 64u

/** One fixed corpus entry: name + owned buffer (heap or static). */
typedef struct compression_harness_corpus_t {
	const_chr_t name;
	const u8* data;
	u64 size;
	u8* owned; /* non-NULL when data was heap-allocated by the harness */
} compression_harness_corpus_t;

/**
 * Optional legacy (v1 / main-branch) path for a codec, looked up by name.
 * When wire_compatible is non-zero, v2 must decompress v1-produced payloads.
 * When zero, the harness asserts the documented intentional incompatibility
 * instead of silently skipping (APX-173).
 */
typedef struct compression_harness_v1_adapter_t {
	const_chr_t name;
	u64 (*compress_bound)(u64 src_size);
	i32 (*compress)(const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written);
	i32 (*decompress)(const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written);
	i32 wire_compatible;
	const_chr_t wire_note; /* required when wire_compatible == 0 */
} compression_harness_v1_adapter_t;

/* ---- v1 adapters: main-branch shapes (design §9 / §11) ------------------- */

/* none: main stored raw bytes when mode==None (callers skipped Compress).
 * On-disk payload is identity; v2 identity codec is wire-compatible with that.
 * This is the only remaining v1 adapter: it models the identity semantics,
 * not a compression library, so it is not legacy compression code. The zstd
 * v1 adapter was migrated to the v2 descriptor and removed by APX-164; wire
 * compatibility with main's zstd output is frozen by the APX-176 vectors
 * (design §9.3, §13). */
static u64 harness_none_v1_bound(u64 src_size) {
	return src_size;
}

static i32 harness_none_v1_compress(const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	return none_copy(src, src_size, dest, dest_cap, out_written);
}

static i32 harness_none_v1_decompress(const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written) {
	return none_copy(src, src_size, dest, dest_cap, out_written);
}

/* Adapter table keyed by codec name. Only the identity codec has a v1-shaped
 * reference row; every real codec is covered by the v2 roundtrips below plus
 * the frozen APX-176 compat vectors (APX-164). */
static const compression_harness_v1_adapter_t harness_v1_adapters[] = {
	{"none", harness_none_v1_bound, harness_none_v1_compress, harness_none_v1_decompress, 1, NULL},
};

static const compression_harness_v1_adapter_t* harness_v1_for_name(const_chr_t name) {
	const u32 n = (u32)(sizeof(harness_v1_adapters) / sizeof(harness_v1_adapters[0]));
	for (u32 i = 0u; i < n; ++i) {
		if (strcmp(harness_v1_adapters[i].name, name) == 0) {
			return &harness_v1_adapters[i];
		}
	}
	return NULL;
}

static const sk_compression_codec_t* harness_codec_by_name(const_chr_t name) {
	const u32 count = sk_compression_codec_count();
	for (u32 i = 0u; i < count; ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);
		if (codec != NULL && strcmp(codec->name, name) == 0) {
			return codec;
		}
	}
	return NULL;
}

/* Portable wall/CPU timer for informational throughput only (not a gate). */
static f64 harness_now_s(void) {
	return (f64)clock() / (f64)CLOCKS_PER_SEC;
}

/* Fill @p dest with deterministic high-entropy bytes (already-compressed stand-in). */
static void harness_fill_entropy(u8* dest, u64 size, u32 seed) {
	u32 state = seed;
	for (u64 i = 0u; i < size; ++i) {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		dest[i] = (u8)(state >> 24u);
	}
}

/* Build the fixed corpus. Caller frees with harness_corpus_free. */
static u32 harness_corpus_build(compression_harness_corpus_t* out, u32 out_cap) {
	const sk_allocator_t* a = sk_allocator_default();
	u32 n = 0u;

	/* Static text payload. */
	static const u8 text_payload[] = "Skore compression parity corpus: the quick brown fox jumps over the lazy dog. "
									 "Repeated runs of natural language exercise dictionary matches and literal runs.";

	/* Structured binary (mixed low/high bytes). */
	static u8 binary_payload[512];
	static i32 binary_init = 0;
	if (!binary_init) {
		for (u32 i = 0u; i < sizeof(binary_payload); ++i) {
			binary_payload[i] = (u8)((i * 37u) ^ (i >> 3u) ^ 0xA5u);
		}
		binary_init = 1;
	}

	/* Empty. */
	if (n < out_cap) {
		out[n].name = "empty";
		out[n].data = NULL;
		out[n].size = 0u;
		out[n].owned = NULL;
		n += 1u;
	}

	/* Text. */
	if (n < out_cap) {
		out[n].name = "text";
		out[n].data = text_payload;
		out[n].size = sizeof(text_payload) - 1u;
		out[n].owned = NULL;
		n += 1u;
	}

	/* Binary. */
	if (n < out_cap) {
		out[n].name = "binary";
		out[n].data = binary_payload;
		out[n].size = sizeof(binary_payload);
		out[n].owned = NULL;
		n += 1u;
	}

	/* Already-compressed / high-entropy (incompressible). Prefer a real zstd
	 * frame as the payload when zstd is available so the outer compress sees
	 * pre-compressed bytes; otherwise use a PRNG stream. */
	if (n < out_cap) {
		const u64 raw_size = 4096u;
		u8* raw = a->alloc(a->instance, raw_size);
		u8* pre = NULL;
		u64 pre_size = 0u;
		TEST_ASSERT_NOT_NULL(raw);
		harness_fill_entropy(raw, raw_size, 0xC0FFEEu);
#ifdef SK_COMPRESSION_HAS_ZSTD
		{
			/* APX-164: the corpus builder compresses through the v2 zstd
			 * descriptor (registry lookup + explicit status), not raw
			 * ZSTD_compress; the emitted frame is byte-identical to main's
			 * level-3 output (APX-176 vectors). */
			const sk_compression_codec_t* zstd = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);
			const u64 bound = zstd->compress_bound(raw_size);
			u64 written = 0u;

			TEST_ASSERT_NOT_NULL(zstd);
			pre = a->alloc(a->instance, bound);
			TEST_ASSERT_NOT_NULL(pre);
			TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, zstd->compress(a, 3, raw, raw_size, pre, bound, &written));
			pre_size = written;
			a->free(a->instance, raw);
			out[n].name = "already_compressed";
			out[n].data = pre;
			out[n].size = pre_size;
			out[n].owned = pre;
			n += 1u;
		}
#else
		out[n].name = "already_compressed";
		out[n].data = raw;
		out[n].size = raw_size;
		out[n].owned = raw;
		n += 1u;
		(void)pre;
		(void)pre_size;
#endif
	}

	/* Pathological: all zeros. */
	if (n < out_cap) {
		const u64 zeros_size = 16u * 1024u;
		u8* zeros = a->alloc(a->instance, zeros_size);
		TEST_ASSERT_NOT_NULL(zeros);
		memset(zeros, 0, zeros_size);
		out[n].name = "all_zeros";
		out[n].data = zeros;
		out[n].size = zeros_size;
		out[n].owned = zeros;
		n += 1u;
	}

	/* Pathological: highly repetitive. */
	if (n < out_cap) {
		const u64 rep_size = 16u * 1024u;
		u8* rep = a->alloc(a->instance, rep_size);
		TEST_ASSERT_NOT_NULL(rep);
		memset(rep, 0xAAu, rep_size);
		out[n].name = "highly_repetitive";
		out[n].data = rep;
		out[n].size = rep_size;
		out[n].owned = rep;
		n += 1u;
	}

	/* Size boundaries around the internal zstd block size (128 KiB). */
	{
		const u64 boundaries[] = {
			COMPRESSION_HARNESS_ZSTD_BLOCK - 1u,
			COMPRESSION_HARNESS_ZSTD_BLOCK,
			COMPRESSION_HARNESS_ZSTD_BLOCK + 1u,
		};
		static const char* boundary_names[] = {
			"block_size_minus_1",
			"block_size",
			"block_size_plus_1",
		};
		for (u32 b = 0u; b < 3u && n < out_cap; ++b) {
			u8* buf = a->alloc(a->instance, boundaries[b]);
			TEST_ASSERT_NOT_NULL(buf);
			for (u64 i = 0u; i < boundaries[b]; ++i) {
				buf[i] = (u8)((i * 13u) + (i % 251u));
			}
			out[n].name = boundary_names[b];
			out[n].data = buf;
			out[n].size = boundaries[b];
			out[n].owned = buf;
			n += 1u;
		}
	}

	/* Tiny single byte. */
	if (n < out_cap) {
		static const u8 one[] = {0x7Eu};
		out[n].name = "one_byte";
		out[n].data = one;
		out[n].size = 1u;
		out[n].owned = NULL;
		n += 1u;
	}

	return n;
}

static void harness_corpus_free(compression_harness_corpus_t* corpus, u32 count) {
	const sk_allocator_t* a = sk_allocator_default();
	for (u32 i = 0u; i < count; ++i) {
		if (corpus[i].owned != NULL) {
			a->free(a->instance, corpus[i].owned);
			corpus[i].owned = NULL;
		}
	}
}

/**
 * Assert v2 decompress(v2 compress(x)) == x for one corpus entry.
 * Reports compressed size + throughput; never fails on size/throughput alone.
 */
static void harness_assert_v2_roundtrip(const sk_compression_codec_t* codec, const compression_harness_corpus_t* entry) {
	const sk_allocator_t* a = sk_allocator_default();
	const u64 bound = codec->compress_bound(entry->size);
	u8* compressed = a->alloc(a->instance, bound > 0u ? bound : 1u);
	u8* restored = a->alloc(a->instance, entry->size > 0u ? entry->size : 1u);
	u64 compressed_size = 0u;
	u64 restored_size = 0u;
	f64 t0 = 0.0;
	f64 t1 = 0.0;
	f64 compress_s = 0.0;
	f64 decompress_s = 0.0;
	const u32 iters = (entry->size < 4096u) ? COMPRESSION_HARNESS_TIMING_ITERS : 4u;

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_NOT_NULL(restored);

	/* Timed multi-iter compress (last iteration keeps the bytes). */
	t0 = harness_now_s();
	for (u32 i = 0u; i < iters; ++i) {
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, entry->data, entry->size, compressed, bound, &compressed_size));
	}
	t1 = harness_now_s();
	compress_s = (t1 - t0) / (f64)iters;

	TEST_ASSERT_TRUE(compressed_size <= bound);

	t0 = harness_now_s();
	for (u32 i = 0u; i < iters; ++i) {
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(a, compressed, compressed_size, restored, entry->size, &restored_size));
	}
	t1 = harness_now_s();
	decompress_s = (t1 - t0) / (f64)iters;

	TEST_ASSERT_EQUAL_UINT64(entry->size, restored_size);
	if (entry->size > 0u) {
		TEST_ASSERT_EQUAL_MEMORY(entry->data, restored, entry->size);
	}

	/* Informational only — not a pass/fail gate (APX-173). */
	{
		const f64 ratio = (entry->size > 0u) ? ((f64)compressed_size / (f64)entry->size) : 0.0;
		const f64 c_mbs = (compress_s > 0.0 && entry->size > 0u) ? ((f64)entry->size / (1024.0 * 1024.0)) / compress_s : 0.0;
		const f64 d_mbs = (decompress_s > 0.0 && entry->size > 0u) ? ((f64)entry->size / (1024.0 * 1024.0)) / decompress_s : 0.0;
		/* u64 is always unsigned long long (common.h); use %llu, no cast. */
		printf("[compression-parity] codec=%s corpus=%s v2: in=%llu out=%llu ratio=%.4f compress=%.3f MB/s decompress=%.3f MB/s\n", codec->name, entry->name, entry->size,
			   compressed_size, ratio, c_mbs, d_mbs);
	}

	a->free(a->instance, compressed);
	a->free(a->instance, restored);
}

/**
 * Wire / backward-compat path: v1 compress then v2 decompress, or assert the
 * intentional incompatibility documented on the adapter.
 */
static void harness_assert_v1_v2_wire(const sk_compression_codec_t* codec, const compression_harness_v1_adapter_t* v1, const compression_harness_corpus_t* entry) {
	const sk_allocator_t* a = sk_allocator_default();
	const u64 v1_bound = v1->compress_bound(entry->size);
	u8* v1_compressed = a->alloc(a->instance, v1_bound > 0u ? v1_bound : 1u);
	u8* restored = a->alloc(a->instance, entry->size > 0u ? entry->size : 1u);
	u64 v1_size = 0u;
	u64 restored_size = 0u;
	u64 v2_size = 0u;
	f64 t_v1 = 0.0;
	f64 t_v2 = 0.0;
	f64 t0 = 0.0;
	f64 t1 = 0.0;

	TEST_ASSERT_NOT_NULL(v1_compressed);
	TEST_ASSERT_NOT_NULL(restored);

	t0 = harness_now_s();
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, v1->compress(entry->data, entry->size, v1_compressed, v1_bound, &v1_size));
	t1 = harness_now_s();
	t_v1 = t1 - t0;

	if (v1->wire_compatible) {
		/* Design requires wire compat: v2 must recover the original payload. */
		TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->decompress(a, v1_compressed, v1_size, restored, entry->size, &restored_size));
		TEST_ASSERT_EQUAL_UINT64(entry->size, restored_size);
		if (entry->size > 0u) {
			TEST_ASSERT_EQUAL_MEMORY(entry->data, restored, entry->size);
		}

		/* Also measure v2 compress size for delta reporting (not a gate). */
		{
			const u64 v2_bound = codec->compress_bound(entry->size);
			u8* v2_compressed = a->alloc(a->instance, v2_bound > 0u ? v2_bound : 1u);
			TEST_ASSERT_NOT_NULL(v2_compressed);
			t0 = harness_now_s();
			TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, entry->data, entry->size, v2_compressed, v2_bound, &v2_size));
			t1 = harness_now_s();
			t_v2 = t1 - t0;

			{
				const i64 size_delta = (i64)v2_size - (i64)v1_size;
				const f64 size_pct = (v1_size > 0u) ? (100.0 * (f64)size_delta / (f64)v1_size) : 0.0;
				const f64 thr_delta = t_v1 - t_v2; /* positive => v2 faster */
				/* u64/i64 are always long long (common.h); use %llu/%lld. */
				printf("[compression-parity] codec=%s corpus=%s v1-vs-v2: v1_out=%llu v2_out=%llu size_delta=%lld (%.2f%%) "
					   "v1_s=%.6f v2_s=%.6f thr_delta_s=%.6f (positive => v2 faster)\n",
					   codec->name, entry->name, v1_size, v2_size, size_delta, size_pct, t_v1, t_v2, thr_delta);
			}
			a->free(a->instance, v2_compressed);
		}
	} else {
		/* Intentional incompatibility: must not silently skip. Assert v2 does
		 * not successfully recover a full equal payload from the v1 frame. */
		TEST_ASSERT_NOT_NULL(v1->wire_note);
		{
			const i32 st = codec->decompress(a, v1_compressed, v1_size, restored, entry->size, &restored_size);
			const i32 recovered_equal = (st == SK_COMPRESSION_OK && restored_size == entry->size && (entry->size == 0u || memcmp(entry->data, restored, entry->size) == 0));
			TEST_ASSERT_FALSE(recovered_equal);
			printf("[compression-parity] codec=%s corpus=%s intentional wire incompatibility confirmed: %s (status=%d)\n", codec->name, entry->name, v1->wire_note, st);
		}
	}

	a->free(a->instance, v1_compressed);
	a->free(a->instance, restored);
}

/**
 * Corruption sensitivity: flip one compressed byte and require failure.
 * The identity codec cannot detect flips (raw copy), so it is skipped.
 */
static void harness_assert_mutation_rejected(const sk_compression_codec_t* codec) {
	const sk_allocator_t* a = sk_allocator_default();
	const u8 payload[] = "mutation check payload: flip one compressed byte and expect CORRUPT_DATA";
	const u64 bound = codec->compress_bound(sizeof(payload));
	u8* compressed = a->alloc(a->instance, bound);
	u8 restored[256];
	u64 compressed_size = 0u;
	u64 written = 0u;

	if (codec->id == SK_COMPRESSION_CODEC_NONE) {
		return;
	}

	TEST_ASSERT_NOT_NULL(compressed);
	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_OK, codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT, payload, sizeof(payload), compressed, bound, &compressed_size));
	TEST_ASSERT_TRUE(compressed_size > 0u);

	/* Flip the frame magic/header byte. Mid-payload bit flips can still decode
	 * under zstd without a content checksum; a mangled magic is always rejected
	 * (matches compression_zstd_corrupt_frame_rejected). */
	compressed[0] ^= 0xFFu;

	TEST_ASSERT_EQUAL_INT(SK_COMPRESSION_ERR_CORRUPT_DATA, codec->decompress(a, compressed, compressed_size, restored, sizeof(restored), &written));
	TEST_ASSERT_EQUAL_UINT64(0u, written);

	a->free(a->instance, compressed);
}

/**
 * Run the full harness for one codec (by name) or every registered codec when
 * @p codec_name_filter is NULL. Future codecs with no v1 adapter still get
 * automatic v2 round-trip coverage.
 */
static void compression_run_parity_harness(const_chr_t codec_name_filter) {
	compression_harness_corpus_t corpus[16];
	const u32 corpus_count = harness_corpus_build(corpus, (u32)(sizeof(corpus) / sizeof(corpus[0])));
	const u32 codec_count = sk_compression_codec_count();

	TEST_ASSERT_TRUE(corpus_count >= 6u);
	TEST_ASSERT_TRUE(codec_count >= 1u);

	for (u32 ci = 0u; ci < codec_count; ++ci) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(ci);
		const compression_harness_v1_adapter_t* v1 = NULL;

		TEST_ASSERT_NOT_NULL(codec);
		if (codec_name_filter != NULL && strcmp(codec->name, codec_name_filter) != 0) {
			continue;
		}

		/* Resolve by name so the harness stays table-driven (APX-173). */
		TEST_ASSERT_EQUAL_PTR(codec, harness_codec_by_name(codec->name));
		v1 = harness_v1_for_name(codec->name);

		for (u32 ei = 0u; ei < corpus_count; ++ei) {
			harness_assert_v2_roundtrip(codec, &corpus[ei]);
			if (v1 != NULL) {
				harness_assert_v1_v2_wire(codec, v1, &corpus[ei]);
			} else {
				printf("[compression-parity] codec=%s corpus=%s: no v1 adapter; v2-only roundtrip\n", codec->name, corpus[ei].name);
			}
		}

		harness_assert_mutation_rejected(codec);
	}

	harness_corpus_free(corpus, corpus_count);
}

SK_TEST(compression_parity_harness_all_codecs) {
	/* NULL filter: every registered codec name. New codecs auto-join. */
	compression_run_parity_harness(NULL);
}

SK_TEST(compression_parity_harness_by_name_none) {
	/* Explicit name parameterization — same path future codecs will use. */
	compression_run_parity_harness("none");
}

#ifdef SK_COMPRESSION_HAS_ZSTD
SK_TEST(compression_parity_harness_by_name_zstd) {
	compression_run_parity_harness("zstd");
}

SK_TEST(compression_parity_harness_mutation_fails) {
	/* Standalone verification: a deliberately mutated zstd frame fails. */
	const sk_compression_codec_t* codec = harness_codec_by_name("zstd");
	TEST_ASSERT_NOT_NULL(codec);
	harness_assert_mutation_rejected(codec);
}
#endif /* SK_COMPRESSION_HAS_ZSTD */

/* ==========================================================================
 * APX-164: migration-completion tests
 *
 * The v1→v2 call-site migration is complete: the last v1-shaped compression
 * call site (the parity-harness zstd adapter) now routes through the v2
 * descriptor, the APX-174 feature flag was removed, and wire compatibility
 * with main's output is frozen by the APX-176 compat vectors
 * (docs/compression-design-v2.md §9.3). These tests pin the completed state
 * so a future codec cannot reintroduce a legacy path silently.
 * ========================================================================== */

SK_TEST(compression_migration_completed_no_legacy_adapters) {
	/* Only the identity codec keeps a v1-shaped reference row (raw-bytes
	 * semantics — not a compression library, so not legacy compression code).
	 * No real codec has a legacy adapter left. */
	TEST_ASSERT_NOT_NULL(harness_v1_for_name("none"));
	TEST_ASSERT_NULL(harness_v1_for_name("zstd"));
	TEST_ASSERT_NULL(harness_v1_for_name("lz4"));
	TEST_ASSERT_NULL(harness_v1_for_name("zlib"));

	/* Every registered codec still round-trips through the registry with no
	 * legacy side path; codecs without a v1 row get v2-only coverage. */
	compression_run_parity_harness(NULL);
}

#ifdef SK_COMPRESSION_HAS_ZSTD
SK_TEST(compression_migration_zstd_routes_through_registry) {
	/* The former v1 call site (the parity-harness zstd adapter) is now the v2
	 * descriptor itself: the harness resolves it exactly like any registry
	 * lookup, and the full zstd parity corpus runs on that single path. */
	const sk_compression_codec_t* codec = harness_codec_by_name("zstd");

	TEST_ASSERT_NOT_NULL(codec);
	TEST_ASSERT_EQUAL_PTR(codec, sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD));
	compression_run_parity_harness("zstd");
}
#endif /* SK_COMPRESSION_HAS_ZSTD */

#endif /* SK_TESTS */
