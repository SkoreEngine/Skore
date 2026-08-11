#pragma once

/**
 * @file compression.h
 * @brief Compression codec abstraction (one-shot + optional streaming).
 *
 * C port of the main-branch C++ Compression.hpp abstraction. Codecs are
 * described by a multi-instance function-pointer table (sk_compression_codec_t,
 * same shape as sk_allocator_t — NOT a process-global sk_*_api_t): callers
 * look up a codec by its stable id from the build-time registry and call
 * through the descriptor. The authoritative design is
 * docs/compression-design-v2.md; a user-facing guide with worked examples is
 * docs/compression-api.md.
 *
 * Registry: sk_compression_codec(id) / _count / _at expose the enabled codecs.
 * The identity codec (SK_COMPRESSION_CODEC_NONE) is always present; the
 * optional codecs are compiled in per build flag (see "Build flags" below)
 * and decode to NULL when disabled (callers map NULL to
 * SK_COMPRESSION_ERR_UNSUPPORTED_CODEC). Codec ids are stable on-disk values:
 * never renumber or reuse (docs/compression-design-v2.md §5).
 *
 * One-shot (required surface, implemented by every codec):
 *   compress_bound -> compress -> decompressed_size/decompress_bound -> decompress.
 * Streaming (optional surface, deferred): stream_init / stream_update /
 * stream_finish / stream_destroy; a NULL stream_init means the codec has no
 * streaming implementation (docs/compression-design-v2.md §4).
 *
 * Buffers: all caller-owned — the module never allocates output. Size every
 * destination with compress_bound / decompressed_size / decompress_bound and
 * read the actual length from *out_written. Byte sizes are u64; src and dest
 * must not overlap. Status: 0 = success, non-zero = recoverable failure
 * (see sk_compression_status_t). Levels are clamped per codec;
 * SK_COMPRESSION_LEVEL_DEFAULT selects the codec default.
 *
 * Built-in codecs (docs/compression-codecs-evaluation.md):
 *   NONE  identity (copy in / copy out); always present.
 *   ZSTD  standard zstd frame (default, same bytes as main's level-3 output).
 *   LZ4   u64-LE original-size prefix + raw LZ4 block (fast decompress).
 *   ZLIB  u64-LE original-size prefix + RFC 1950 zlib stream (miniz).
 *
 * Build flags (root CMakeLists.txt): SK_COMPRESSION_ZSTD / SK_COMPRESSION_LZ4 /
 * SK_COMPRESSION_MINIZ, default ON. With a flag OFF the matching descriptor is
 * excluded from the registry and its id decodes to NULL.
 *
 * Threading: the one-shot entries are safe to call from worker threads
 * (zstd one-shot is thread-safe). A streaming session is owned by one thread
 * at a time (see stream_init).
 */

#include "allocator.h"
#include "common.h"

#include <stddef.h> /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/** Level sentinel: use the codec's default compression level. */
#define SK_COMPRESSION_LEVEL_DEFAULT (-1)

/**
 * Size sentinel: the codec could not determine a size or upper bound
 * (e.g. a zstd frame without a content-size header).
 */
#define SK_COMPRESSION_SIZE_UNKNOWN ((u64) - 1)

/**
 * Stable codec ids. Values are part of the on-disk resource format:
 * never renumber or reuse. Members are always defined so on-disk ids can be
 * read in any build; sk_compression_codec() returns NULL for ids whose
 * descriptor is not compiled in.
 */
typedef enum sk_compression_codec_id_t {
	SK_COMPRESSION_CODEC_NONE = 0,
	SK_COMPRESSION_CODEC_ZSTD = 1,
	/** Raw LZ4 block with a little-endian u64 original-size prefix. */
	SK_COMPRESSION_CODEC_LZ4 = 2,
	/** RFC 1950 zlib stream with a little-endian u64 original-size prefix. */
	SK_COMPRESSION_CODEC_ZLIB = 3,
} sk_compression_codec_id_t;

/**
 * Compression status codes. 0 = success, non-zero = failure (recoverable:
 * resize the output buffer, or handle corrupt/unsupported data).
 */
typedef enum sk_compression_status_t {
	SK_COMPRESSION_OK = 0,
	/** Output buffer was too small; nothing was written. Size with the
	 *  codec's bound query and retry. */
	SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT,
	/** Input is not a valid frame for this codec (corrupt/truncated data). */
	SK_COMPRESSION_ERR_CORRUPT_DATA,
	/** The codec id has no enabled descriptor in this build. */
	SK_COMPRESSION_ERR_UNSUPPORTED_CODEC,
	/** Allocation failed at a known boundary (see the allocator contract). */
	SK_COMPRESSION_ERR_OUT_OF_MEMORY,
	/** The underlying codec reported an error not covered above. */
	SK_COMPRESSION_ERR_CODEC_FAILURE,
} sk_compression_status_t;

/** Streaming direction (stream_init only). */
typedef enum sk_compression_mode_t {
	SK_COMPRESSION_MODE_COMPRESS = 0,
	SK_COMPRESSION_MODE_DECOMPRESS = 1,
} sk_compression_mode_t;

/**
 * Streaming session handle. Opaque codec state created by
 * sk_compression_codec_t::stream_init and released by
 * sk_compression_codec_t::stream_destroy. The codec descriptor must outlive
 * every stream created from it. One stream is owned by one thread at a time.
 */
typedef struct sk_compression_stream_t {
	void_ptr_t instance;
} sk_compression_stream_t;

/**
 * Codec descriptor (multi-instance strategy table; one per enabled codec).
 *
 * Every entry is implemented by the codec; pointers are never NULL except
 * stream_init (NULL when the codec has no streaming implementation, in which
 * case only the one-shot entries may be used).
 *
 * Allocator contract: entries that may allocate internally take
 * const sk_allocator_t* and route every internal allocation through it
 * (the zstd glue uses ZSTD_customMem). Pass sk_allocator_default() when you
 * have no reason to inject a specific allocator.
 */
typedef struct sk_compression_codec_t {
	/** Stable on-disk id (sk_compression_codec_id_t). */
	sk_compression_codec_id_t id;

	/** Short name, e.g. "none", "zstd" (logs / tooling only). */
	const_chr_t name;

	/** Compression level range for this codec; out-of-range levels are
	 *  clamped. Decompression ignores level. */
	i32 level_min;
	i32 level_max;
	/** Level selected by SK_COMPRESSION_LEVEL_DEFAULT. */
	i32 level_default;

	/**
	 * Upper bound for the compressed size of @p src_size input bytes.
	 * Guaranteed >= any compress() result for this codec at any level.
	 *
	 * @param src_size Input size in bytes.
	 * @return Bound, or SK_COMPRESSION_SIZE_UNKNOWN if the codec cannot
	 *         provide one (none of the built-in codecs hit this).
	 */
	u64 (*compress_bound)(u64 src_size);

	/**
	 * Compress @p src_size bytes of @p src into the caller-owned buffer
	 * @p dest. @p dest_cap must be >= compress_bound(src_size) for the
	 * call to succeed; a smaller capacity returns
	 * SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT without writing.
	 *
	 * @param allocator  Allocator for codec-internal state (must not be NULL).
	 * @param level      Compression level; SK_COMPRESSION_LEVEL_DEFAULT or
	 *                   any i32 (clamped to [level_min, level_max]).
	 * @param src        Input bytes (must not be NULL when src_size > 0).
	 * @param src_size   Input size in bytes.
	 * @param dest       Output buffer (must not be NULL when dest_cap > 0).
	 * @param dest_cap   Output capacity in bytes.
	 * @param out_written Receives the compressed size (valid on success only).
	 * @return SK_COMPRESSION_OK on success; INSUFFICIENT_OUTPUT when dest_cap
	 *         is too small; OUT_OF_MEMORY / CODEC_FAILURE otherwise.
	 */
	i32 (*compress)(const sk_allocator_t* allocator, i32 level, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written);

	/**
	 * Exact decompressed size declared by the frame header of @p src.
	 * Prefer this over decompress_bound when exact sizing is possible.
	 *
	 * @param src      A compressed frame (must not be NULL when src_size > 0).
	 * @param src_size Frame size in bytes.
	 * @param out_size Receives the declared size, or
	 *                 SK_COMPRESSION_SIZE_UNKNOWN when the frame does not
	 *                 declare one (valid frame; size the destination with
	 *                 decompress_bound or use streaming).
	 * @return SK_COMPRESSION_OK on success (valid frame);
	 *         SK_COMPRESSION_ERR_CORRUPT_DATA when @p src is not a valid
	 *         frame for this codec.
	 */
	i32 (*decompressed_size)(const u8* src, u64 src_size, u64* out_size);

	/**
	 * Upper bound for the decompressed size of @p src (independent of the
	 * frame header). Sizing a destination with this is always safe but may
	 * over-allocate for frames whose header declares a smaller size.
	 *
	 * @return Bound, or SK_COMPRESSION_SIZE_UNKNOWN when the codec cannot
	 *         determine one (fall back to streaming).
	 */
	u64 (*decompress_bound)(const u8* src, u64 src_size);

	/**
	 * Decompress one frame from @p src into the caller-owned buffer @p dest.
	 * @p dest_cap must be >= the frame's decompressed size (query
	 * decompressed_size or decompress_bound first); otherwise the call
	 * returns SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT without writing.
	 *
	 * @param allocator  Allocator for codec-internal state (must not be NULL).
	 * @param src        A compressed frame (must not be NULL when src_size > 0).
	 * @param src_size   Frame size in bytes.
	 * @param dest       Output buffer (must not be NULL when dest_cap > 0).
	 * @param dest_cap   Output capacity in bytes.
	 * @param out_written Receives the decompressed size (valid on success only).
	 * @return SK_COMPRESSION_OK on success; INSUFFICIENT_OUTPUT when dest_cap
	 *         is too small; CORRUPT_DATA on a bad/truncated frame;
	 *         OUT_OF_MEMORY / CODEC_FAILURE otherwise.
	 */
	i32 (*decompress)(const sk_allocator_t* allocator, const u8* src, u64 src_size, u8* dest, u64 dest_cap, u64* out_written);

	/**
	 * Optional streaming support. NULL when the codec has no streaming
	 * implementation (then only the one-shot entries may be used).
	 * The stream captures @p allocator for all subsequent allocations and
	 * is released with stream_destroy.
	 *
	 * @param out        Destination session handle (must not be NULL).
	 * @param mode       SK_COMPRESSION_MODE_COMPRESS or _DECOMPRESS.
	 * @param level      Compression level (ignored when mode == DECOMPRESS).
	 * @param allocator  Allocator for codec-internal stream state (must not be NULL).
	 * @return SK_COMPRESSION_OK on success; OUT_OF_MEMORY / CODEC_FAILURE otherwise.
	 */
	i32 (*stream_init)(sk_compression_stream_t* out, sk_compression_mode_t mode, i32 level, const sk_allocator_t* allocator);

	/**
	 * Feed input and/or drain output for a stream created by stream_init.
	 * Consumes up to @p src_size bytes and writes up to @p dest_cap bytes;
	 * unconsumed input is buffered inside the stream.
	 *
	 * Caller loop: advance @p src by *in_consumed and repeat until all input
	 * is consumed, then call with src_size == 0 until *out_written == 0
	 * (flush), then stream_finish. Never returns INSUFFICIENT_OUTPUT — output
	 * drains incrementally, so pass a fresh dest buffer each iteration.
	 *
	 * @param instance    Stream state (from stream_init).
	 * @param src         Input bytes (may be NULL when src_size == 0).
	 * @param src_size    Input bytes available this call.
	 * @param in_consumed Receives bytes consumed from @p src.
	 * @param dest        Output buffer (may be NULL when dest_cap == 0).
	 * @param dest_cap    Output capacity in bytes.
	 * @param out_written Receives bytes written to @p dest.
	 * @return SK_COMPRESSION_OK on success; CORRUPT_DATA on a bad frame;
	 *         OUT_OF_MEMORY / CODEC_FAILURE otherwise.
	 */
	i32 (*stream_update)(void_ptr_t instance, const u8* src, u64 src_size, u64* in_consumed, u8* dest, u64 dest_cap, u64* out_written);

	/**
	 * Finish a stream: finalize the frame (compress) or verify the end of
	 * the frame (decompress) and drain the tail.
	 * May be called repeatedly with fresh dest buffers; @p finished is set
	 * non-zero on the call that completes the stream with no output pending.
	 *
	 * @param instance    Stream state (from stream_init).
	 * @param dest        Output buffer (may be NULL when dest_cap == 0).
	 * @param dest_cap    Output capacity in bytes.
	 * @param out_written Receives bytes written to @p dest.
	 * @param finished    Receives 1 when the stream is complete, else 0.
	 * @return SK_COMPRESSION_OK on success; CORRUPT_DATA when the stream is
	 *         truncated (decompress); OUT_OF_MEMORY / CODEC_FAILURE otherwise.
	 *         The session stays valid until stream_destroy.
	 */
	i32 (*stream_finish)(void_ptr_t instance, u8* dest, u64 dest_cap, u64* out_written, i32* finished);

	/**
	 * Release a stream created by stream_init, freeing state with the
	 * allocator captured at init. NULL instance is a no-op.
	 * @param instance Stream state (from stream_init).
	 */
	void (*stream_destroy)(void_ptr_t instance);
} sk_compression_codec_t;

/**
 * Look up the enabled descriptor for @p id.
 * @param id Stable codec id (sk_compression_codec_id_t).
 * @return Non-NULL descriptor, or NULL when the codec is not compiled in
 *         (an on-disk id can decode to NULL if its codec was disabled).
 */
const sk_compression_codec_t* sk_compression_codec(sk_compression_codec_id_t id);

/**
 * Number of enabled codecs in the build-time registry (>= 1; None is always
 * present). Useful for tooling / iteration.
 */
u32 sk_compression_codec_count(void);

/**
 * Descriptor at registry index @p index (0 <= index < count).
 * @param index Registry index.
 * @return Non-NULL descriptor, or NULL when index >= count.
 */
const sk_compression_codec_t* sk_compression_codec_at(u32 index);

#ifdef __cplusplus
}
#endif
