/**
 * @file compression_mapping.c
 * @brief Executable conformance check for the APX-165 compression migration table.
 *
 * Encodes the main-branch -> v2 symbol mapping table from
 * docs/compression-design-v2.md §11 as a maintained artifact and verifies it
 * against the real v2 surface (foundation/compression.h + foundation/compression.c):
 *
 *  - every mapped main-branch symbol has a v2 replacement that exists and is
 *    callable with the documented signature: the typed-pointer assignments
 *    below turn a renamed, retyped, or dropped symbol into a hard build
 *    failure, and the runtime checks call each replacement through its exact
 *    documented parameter list;
 *  - symbols with no planned equivalent are declared as intentional gaps in
 *    the mapping table (with a reason), printed on every run, and checked to
 *    match the frozen expected-gap list exactly — a gap can never disappear
 *    silently, and an undeclared gap fails the run.
 *
 * The check runs as a CTest test (sk-compression-conformance) in Debug and
 * Release CI builds against the production sk-foundation surface.
 */

#include "compression.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Compile-time signature freeze.
 *
 * Assigning a function designator (or a codec descriptor member) to a typed
 * function pointer is a C11 constraint violation when the declared type
 * drifts, so each assignment below turns a renamed, retyped, or dropped
 * symbol into a build failure under the project's warnings-as-errors flags.
 * ------------------------------------------------------------------------- */

/* Free-function surface (design §3): registry lookups the mapped operations
 * depend on. */
static const sk_compression_codec_t* (*const check_lookup_fn)(sk_compression_codec_id_t) = sk_compression_codec;
static u32 (*const check_count_fn)(void) = sk_compression_codec_count;
static const sk_compression_codec_t* (*const check_at_fn)(u32) = sk_compression_codec_at;

/* Codec descriptor members: the one-shot entries every mapped operation maps
 * to (design §11 rows 5-8) plus the documented streaming entries. */
static void check_member_signatures(const sk_compression_codec_t* codec) {
	u64 (*member_compress_bound)(u64) = codec->compress_bound;
	i32 (*member_compress)(const sk_allocator_t*, i32, const u8*, u64, u8*, u64, u64*) = codec->compress;
	i32 (*member_decompressed_size)(const u8*, u64, u64*) = codec->decompressed_size;
	u64 (*member_decompress_bound)(const u8*, u64) = codec->decompress_bound;
	i32 (*member_decompress)(const sk_allocator_t*, const u8*, u64, u8*, u64, u64*) = codec->decompress;
	i32 (*member_stream_init)(sk_compression_stream_t*, sk_compression_mode_t, i32, const sk_allocator_t*) = codec->stream_init;
	i32 (*member_stream_update)(void_ptr_t, const u8*, u64, u64*, u8*, u64, u64*) = codec->stream_update;
	i32 (*member_stream_finish)(void_ptr_t, u8*, u64, u64*, i32*) = codec->stream_finish;
	void (*member_stream_destroy)(void_ptr_t) = codec->stream_destroy;

	(void)member_compress_bound;
	(void)member_compress;
	(void)member_decompressed_size;
	(void)member_decompress_bound;
	(void)member_decompress;
	(void)member_stream_init;
	(void)member_stream_update;
	(void)member_stream_finish;
	(void)member_stream_destroy;
}

/* ---------------------------------------------------------------------------
 * Mapping table (design §11) — the maintained artifact.
 * ------------------------------------------------------------------------- */

typedef enum check_kind_t {
	CHECK_MAPPED = 0,
	CHECK_INTENTIONAL_GAP = 1,
} check_kind_t;

typedef struct mapping_entry_t {
	const_chr_t main_symbol;	/* main-branch symbol, design §11 left column */
	check_kind_t kind;			/* mapped to a v2 replacement, or declared gap */
	const_chr_t v2_replacement; /* v2 replacement symbol, or gap reason */
} mapping_entry_t;

static const mapping_entry_t mapping_table[] = {
	/* §11 row 1: enum -> stable on-disk ids */
	{"enum class CompressionMode { None, ZSTD }", CHECK_MAPPED, "sk_compression_codec_id_t (SK_COMPRESSION_CODEC_NONE = 0, SK_COMPRESSION_CODEC_ZSTD = 1)"},
	/* §11 row 2: silent-0 None semantics -> identity codec */
	{"CompressionMode::None", CHECK_MAPPED, "SK_COMPRESSION_CODEC_NONE identity codec (copy in, copy out)"},
	/* §11 row 3: ZSTD mode -> descriptor */
	{"CompressionMode::ZSTD", CHECK_MAPPED, "SK_COMPRESSION_CODEC_ZSTD descriptor (same codec, same frames)"},
	/* §11 row 4: default level -> sentinel + per-codec default */
	{"CompressionDefaultLevel = 3", CHECK_MAPPED, "SK_COMPRESSION_LEVEL_DEFAULT (-1) sentinel; zstd descriptor level_default = 3"},
	/* §11 row 5: one-shot compress */
	{"Compression::Compress(dest, descSize, src, srcSize, mode, level = 3) -> usize", CHECK_MAPPED,
	 "codec->compress(allocator, level, src, src_size, dest, dest_cap, &out_written) -> i32"},
	/* §11 row 6: bound query */
	{"Compression::GetMaxCompressedBufferSize(srcSize, mode) -> usize", CHECK_MAPPED, "codec->compress_bound(src_size) -> u64"},
	/* §11 row 7: one-shot decompress */
	{"Compression::Decompress(dest, descSize, src, srcSize, mode) -> usize", CHECK_MAPPED, "codec->decompress(allocator, src, src_size, dest, dest_cap, &out_written) -> i32"},
	/* §11 row 8: size query split into exact + bound */
	{"Compression::GetMaxDecompressedBufferSize(src, srcSize, mode) -> usize", CHECK_MAPPED,
	 "codec->decompressed_size(src, src_size, &out) + codec->decompress_bound(src, src_size) -> u64"},
	/* §11 rows 9-11: no planned v2 equivalent — declared intentional gaps */
	{"Reflection value registration of CompressionMode (RegisterIOTypes.cpp:251-253)", CHECK_INTENTIONAL_GAP,
	 "No planned equivalent: v2 has no reflection/metadata layer; re-add with the reflection layer."},
	{"Texture resource field registration of CompressionMode (RegisterGraphicsTypes.cpp:1104)", CHECK_INTENTIONAL_GAP,
	 "No planned equivalent: v2 resource layer stores the raw id (u32); no metadata field yet."},
	{"Commented-out LZ4 branches in Compression.cpp", CHECK_INTENTIONAL_GAP, "No planned equivalent: never shipped on main, no LZ4 dependency."},
};

/* Frozen expected-gap list: the mapping table must declare exactly these
 * symbols as intentional gaps (no silent removal, no undeclared addition). */
static const_chr_t expected_gaps[] = {
	"Reflection value registration of CompressionMode (RegisterIOTypes.cpp:251-253)",
	"Texture resource field registration of CompressionMode (RegisterGraphicsTypes.cpp:1104)",
	"Commented-out LZ4 branches in Compression.cpp",
};

/* ---------------------------------------------------------------------------
 * Runtime harness.
 * ------------------------------------------------------------------------- */

static u32 check_failures = 0u;

static void check_true(i32 condition, const_chr_t what) {
	if (condition == 0) {
		fprintf(stderr, "FAIL: %s\n", what);
		check_failures += 1u;
	}
}

static size_t mapping_table_count(void) {
	return sizeof(mapping_table) / sizeof(mapping_table[0]);
}

static size_t expected_gap_count(void) {
	return sizeof(expected_gaps) / sizeof(expected_gaps[0]);
}

/* Stable on-disk ids and constants (design §3.1, §11 rows 1 and 4). */
static void check_stable_values(void) {
	check_true(SK_COMPRESSION_CODEC_NONE == 0, "SK_COMPRESSION_CODEC_NONE == 0 (on-disk id)");
	check_true(SK_COMPRESSION_CODEC_ZSTD == 1, "SK_COMPRESSION_CODEC_ZSTD == 1 (on-disk id)");
	check_true(SK_COMPRESSION_CODEC_LZ4 == 2, "SK_COMPRESSION_CODEC_LZ4 == 2 (on-disk id)");
	check_true(SK_COMPRESSION_CODEC_ZLIB == 3, "SK_COMPRESSION_CODEC_ZLIB == 3 (on-disk id)");
	check_true(SK_COMPRESSION_LEVEL_DEFAULT == -1, "SK_COMPRESSION_LEVEL_DEFAULT == -1 (sentinel)");
	check_true(SK_COMPRESSION_SIZE_UNKNOWN == (u64)-1, "SK_COMPRESSION_SIZE_UNKNOWN == (u64)-1");
}

/* Registry contract (design §5): lookup, iteration, unique ids. */
static void check_registry_surface(void) {
	const u32 count = sk_compression_codec_count();

	check_true(count >= 1u, "sk_compression_codec_count() >= 1 (none is always present)");
	for (u32 i = 0u; i < count; ++i) {
		const sk_compression_codec_t* codec = sk_compression_codec_at(i);

		check_true(codec != NULL, "sk_compression_codec_at(i) != NULL for every i < count");
		if (codec != NULL) {
			check_true(sk_compression_codec(codec->id) == codec, "descriptor resolves from its own stable id");
			for (u32 j = i + 1u; j < count; ++j) {
				check_true(codec->id != sk_compression_codec_at(j)->id, "no two descriptors share an id");
			}
		}
	}
	check_true(sk_compression_codec_at(count) == NULL, "sk_compression_codec_at(count) == NULL");
}

/* Identity codec (design §3.2, §11 row 2): always present, copies bytes,
 * ignores levels, has no streaming implementation. */
static void check_none_codec(const sk_compression_codec_t* codec) {
	check_true(codec->id == SK_COMPRESSION_CODEC_NONE, "none codec: id is SK_COMPRESSION_CODEC_NONE");
	check_true(strcmp(codec->name, "none") == 0, "none codec: name is \"none\"");
	check_true(codec->level_default == 0, "none codec: level_default == 0 (levels ignored)");
	check_true(codec->compress_bound != NULL, "none codec: compress_bound present");
	check_true(codec->compress != NULL, "none codec: compress present");
	check_true(codec->decompressed_size != NULL, "none codec: decompressed_size present");
	check_true(codec->decompress_bound != NULL, "none codec: decompress_bound present");
	check_true(codec->decompress != NULL, "none codec: decompress present");
	check_true(codec->stream_init == NULL, "none codec: stream_init is NULL (no streaming)");
}

/* One-shot surface (design §4, §11 rows 5-8): every mapped operation is
 * called through its exact documented signature and must round-trip a payload
 * byte-for-byte. Scratch buffers come from the injected allocator. */
static void check_one_shot_surface(const sk_compression_codec_t* codec) {
	const u8 payload[] = {0x00u, 0x01u, 0x7fu, 0x80u, 0xffu, 0x42u, 0x13u, 0x37u, 0xabu, 0x11u};
	const u64 payload_size = sizeof(payload);
	const sk_allocator_t* scratch = sk_allocator_default();
	const u64 bound = codec->compress_bound(payload_size);
	u8* compressed = scratch->alloc(scratch->instance, bound);
	u8* restored = scratch->alloc(scratch->instance, payload_size);
	u64 compressed_size = 0u;
	u64 restored_size = 0u;
	u64 declared = 0u;

	check_true(compressed != NULL, "scratch alloc for compressed buffer");
	check_true(restored != NULL, "scratch alloc for restored buffer");

	/* row 5: compress(dest, descSize -> dest_cap, src, srcSize -> src_size,
	 * mode -> codec lookup, level) with explicit status + out_written. */
	check_true(codec->compress(scratch, SK_COMPRESSION_LEVEL_DEFAULT, payload, payload_size, compressed, bound, &compressed_size) == SK_COMPRESSION_OK,
			   "codec->compress returns SK_COMPRESSION_OK");
	check_true(compressed_size <= bound, "compressed_size <= compress_bound(src_size)");

	/* row 8: decompressed_size (exact declared size) + decompress_bound. */
	check_true(codec->decompressed_size(compressed, compressed_size, &declared) == SK_COMPRESSION_OK, "codec->decompressed_size returns SK_COMPRESSION_OK");
	check_true(declared == payload_size, "decompressed_size reports the payload size");
	check_true(codec->decompress_bound(compressed, compressed_size) >= payload_size, "decompress_bound(src) >= declared size");

	/* row 7: decompress(dest, descSize -> dest_cap, src, srcSize -> src_size)
	 * with explicit status + out_written. */
	check_true(codec->decompress(scratch, compressed, compressed_size, restored, payload_size, &restored_size) == SK_COMPRESSION_OK, "codec->decompress returns SK_COMPRESSION_OK");
	check_true(restored_size == payload_size, "decompress writes the full payload size");
	check_true(memcmp(payload, restored, payload_size) == 0, "decompress restores the payload byte-for-byte");

	scratch->free(scratch->instance, compressed);
	scratch->free(scratch->instance, restored);
}

/* Zstd codec (design §3.2, §11 rows 3-4): present exactly when the codec is
 * compiled in; level_default matches main's CompressionDefaultLevel = 3. */
#ifdef SK_COMPRESSION_HAS_ZSTD
static void check_zstd_codec(void) {
	const sk_compression_codec_t* codec = sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD);

	check_true(codec != NULL, "zstd codec: enabled build -> sk_compression_codec(ZSTD) != NULL");
	if (codec != NULL) {
		check_true(codec->id == SK_COMPRESSION_CODEC_ZSTD, "zstd codec: id is SK_COMPRESSION_CODEC_ZSTD");
		check_true(strcmp(codec->name, "zstd") == 0, "zstd codec: name is \"zstd\"");
		check_true(codec->level_default == 3, "zstd codec: level_default == 3 (main's CompressionDefaultLevel)");
		check_true(codec->level_min <= codec->level_max, "zstd codec: level_min <= level_max");
		check_true(codec->compress_bound != NULL, "zstd codec: compress_bound present");
		check_true(codec->compress != NULL, "zstd codec: compress present");
		check_true(codec->decompressed_size != NULL, "zstd codec: decompressed_size present");
		check_true(codec->decompress_bound != NULL, "zstd codec: decompress_bound present");
		check_true(codec->decompress != NULL, "zstd codec: decompress present");
		check_one_shot_surface(codec);
	}
}
#else
static void check_zstd_codec(void) {
	check_true(sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD) == NULL, "zstd codec: disabled build -> lookup returns NULL (design §10)");
}
#endif

/* Intentional gaps: every frozen expected gap must be declared in the table
 * with a reason, and the table must not declare gaps beyond the frozen list. */
static void check_intentional_gaps(void) {
	const size_t table_count = mapping_table_count();
	const size_t gap_count = expected_gap_count();
	size_t declared_gaps = 0u;

	for (size_t i = 0u; i < table_count; ++i) {
		const mapping_entry_t* entry = &mapping_table[i];

		check_true(entry->main_symbol != NULL && entry->main_symbol[0] != '\0', "mapping entry has a main-branch symbol");
		check_true(entry->v2_replacement != NULL && entry->v2_replacement[0] != '\0', "mapping entry has a replacement or gap reason");
		if (entry->kind == CHECK_INTENTIONAL_GAP) {
			declared_gaps += 1u;
		}
	}

	for (size_t i = 0u; i < gap_count; ++i) {
		i32 found = 0;
		for (size_t j = 0u; j < table_count; ++j) {
			const mapping_entry_t* entry = &mapping_table[j];

			if (entry->kind == CHECK_INTENTIONAL_GAP && strcmp(entry->main_symbol, expected_gaps[i]) == 0) {
				found = 1;
				break;
			}
		}
		check_true(found, "every expected gap is declared in the mapping table");
	}

	check_true(declared_gaps == gap_count, "mapping table declares exactly the expected intentional gaps");
}

static void report_intentional_gaps(void) {
	const size_t table_count = mapping_table_count();

	printf("\nIntentional gaps in the APX-165 migration table (no planned v2 equivalent):\n");
	for (size_t i = 0u; i < table_count; ++i) {
		const mapping_entry_t* entry = &mapping_table[i];

		if (entry->kind == CHECK_INTENTIONAL_GAP) {
			printf("  - %s\n    reason: %s\n", entry->main_symbol, entry->v2_replacement);
		}
	}
}

int main(void) {
	const sk_compression_codec_t* none = sk_compression_codec(SK_COMPRESSION_CODEC_NONE);
	size_t mapped_count = 0u;
	size_t gap_count = 0u;
	size_t i;

	/* Reference the compile-time signature-check objects so the type checks
	 * above are always part of the build. */
	(void)check_lookup_fn;
	(void)check_count_fn;
	(void)check_at_fn;

	for (i = 0u; i < mapping_table_count(); ++i) {
		if (mapping_table[i].kind == CHECK_MAPPED) {
			mapped_count += 1u;
		} else {
			gap_count += 1u;
		}
	}

	check_stable_values();
	check_registry_surface();

	check_true(none != NULL, "sk_compression_codec(SK_COMPRESSION_CODEC_NONE) != NULL (always present)");
	if (none != NULL) {
		check_member_signatures(none);
		check_none_codec(none);
		check_one_shot_surface(none);
	}
	check_zstd_codec();
	check_intentional_gaps();
	report_intentional_gaps();

	if (check_failures != 0u) {
		fprintf(stderr, "\ncompression mapping conformance check FAILED: %u check(s) failed\n", check_failures);
		return 1;
	}
	printf("\ncompression mapping conformance check PASSED: %zu mapped, %zu declared intentional gaps\n", mapped_count, gap_count);
	return 0;
}
