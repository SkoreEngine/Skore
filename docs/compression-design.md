# Compression API design (v2 C engine)

Task: APX-159. Input: `docs/compression-inventory.md` (APX-158 audit of the
main-branch C++ `Compression.hpp` abstraction and the v2 port target). Scope:
**design only** — no implementation lands with this document. Every convention
below follows the v2 `AGENTS.md` house rules (fixed-width types, `sk_` public
prefix, opaque-by-default, allocator-explicit, `_t` vs `_api_t` naming,
in-source tests, vendored third-party).

## 1. Scope and goals

Port the main-branch compression abstraction to v2 as a C module while fixing
the audit gaps (inventory §5 and §10):

- invisible errors (`usize` returns, no `ZSTD_isError` translation) → explicit
  status codes;
- zstd internal allocations bypassing the engine allocator → `sk_allocator_t`
  injection through `ZSTD_customMem`;
- `CompressionMode::None` silently returning 0 → explicit identity codec;
- unsafe sizing paths (`(size_t)-1` sentinels, `u32` truncation, wrong bound
  usage in `TextureImporter`) → `u64` sizes, checked sentinels, bound queries.

Non-goals: dictionary/contextual compression, multi-threaded codecs (`ZSTDMT`),
a runtime plugin-registered codec registry, and the C++ reflection metadata
layer.

## 2. On-disk / on-wire format preservation

The only codec shipped by main is Zstandard. This design:

- keeps the zstd **frame format** (one-shot `ZSTD_compress` / `ZSTD_decompress`;
  the optional streaming path emits standard frames too), so `.resources` /
  `.cooked` archives, font blobs, dependency blobs, and per-mip texture data
  written by main decode identically on v2;
- maps the serialized `CompressionMode` enum 1:1 to
  `sk_compression_codec_id_t` (`None = 0`, `ZSTD = 1`) — id values are on-disk
  stable and never renumbered;
- keeps the zstd default level 3 as `level_default` on the zstd descriptor.

No escalation is required: every shipped on-disk/on-wire format is preserved.
Future codecs (LZ4, zlib) append new ids; existing ids never change meaning.

## 3. Design overview

- Module home: `core/compression.h` + `core/compression.c`. Pure engine
  utility (no host/app backing), so it implements in `sk-core` — not `sk-app`.
- **Codec descriptor** `sk_compression_codec_t`: a multi-instance
  function-pointer table, one per enabled codec, same shape as
  `sk_allocator_t`. Deliberately **not** `sk_compression_api_t`: `_api_t` is
  reserved for a single process-global module surface; a codec is a pluggable
  strategy (many instances) and callers hold a pointer to it.
- **Build-time registry**: maps the stable enum id to the enabled descriptor.
  No runtime registration, no free-running global constructors — the registry
  is a static const table populated at compile time.
- Callers look up a codec once, then call **through the descriptor**. No
  free-function mirrors of vtable entries (house rule). The only module-level
  free functions are registry lookups.
- All buffers are caller-owned; every operation reports
  `sk_compression_status_t` (0 = success).

## 4. Naming and type conventions

- Files: `core/compression.h` / `core/compression.c` (no `sk_` file prefix;
  `compression.h` does not shadow a C library header).
- Types: `sk_compression_codec_t` (descriptor), `sk_compression_codec_id_t`
  (stable ids), `sk_compression_status_t` (errors), `sk_compression_mode_t`
  (streaming direction), `sk_compression_stream_t` (opaque session handle).
- Constants: `SK_COMPRESSION_LEVEL_DEFAULT (-1)` (codec-default level sentinel)
  and `SK_COMPRESSION_SIZE_UNKNOWN ((u64)-1)` (unknown size / no bound).
- Byte sizes are `u64` throughout the public surface (LLP64-safe; no `size_t`,
  matching `sk_blob_view_t` in `core/serialization.h`).
- Enum values use the module prefix (`SK_COMPRESSION_OK`, ...) like
  `SK_FILE_STATUS_*`.

## 5. Public header sketch (complete)

```c
#pragma once

/**
 * @file compression.h
 * @brief Compression codec abstraction (one-shot + optional streaming).
 *
 * C port of the main-branch Compression.hpp abstraction. Codecs are described
 * by a multi-instance function-pointer table (sk_compression_codec_t, same
 * shape as sk_allocator_t — NOT a process-global sk_*_api_t): callers look up
 * a codec by its stable id from the build-time registry and call through the
 * descriptor.
 *
 * All buffers are caller-owned: the module never allocates output. Size every
 * destination with compress_bound / decompressed_size / decompress_bound and
 * read the actual length from *out_written. Byte sizes are u64; src and dest
 * must not overlap.
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
#define SK_COMPRESSION_SIZE_UNKNOWN ((u64)-1)

/**
 * Stable codec ids. Values are part of the on-disk resource format:
 * never renumber or reuse. Members are always defined so on-disk ids can be
 * read in any build; sk_compression_codec() returns NULL for ids whose
 * descriptor is not compiled in.
 */
typedef enum sk_compression_codec_id_t {
	SK_COMPRESSION_CODEC_NONE = 0,
	SK_COMPRESSION_CODEC_ZSTD = 1,
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
	 * call to succeed; a smaller capacity is a contract violation that
	 * returns SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT without writing.
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
```

## 6. Codec descriptor semantics

- `id` / `name` are metadata; the registry never contains two descriptors with
  the same id.
- `compress_bound` is level-independent (true for zstd and for the identity
  None codec); the doc contract requires it to be an upper bound for **any**
  level so callers can size once.
- `decompressed_size` is the *exact* declared frame size (zstd
  `ZSTD_getFrameContentSize`); `decompress_bound` is a true upper bound (zstd
  `ZSTD_decompressBound`). The C++ API exposed only the exact-size query and
  passed `(size_t)-1` through unguarded; v2 separates the two and makes the
  sentinel explicit and checkable.
- **None** (`SK_COMPRESSION_CODEC_NONE`): identity codec. `compress_bound(n) =
  n`, compress copies, decompress copies, `decompressed_size(n) = n`,
  `decompress_bound(n) = n`, `stream_init = NULL`. Levels are ignored
  (`level_min = level_max = level_default = 0`). This replaces the C++
  silent-0 no-op: call sites no longer branch on `mode == None` before every
  call, and a resource tagged None still stores the raw bytes unchanged.
- **Zstd** (`SK_COMPRESSION_CODEC_ZSTD`): `level_default = 3` (matches
  `CompressionDefaultLevel`), range `ZSTD_minCLevel()..ZSTD_maxCLevel()`
  (negative values select the fast levels). Entries map to
  `ZSTD_compress2` / `ZSTD_decompress` / `ZSTD_compressBound` /
  `ZSTD_getFrameContentSize` / `ZSTD_decompressBound`, with
  `ZSTD_isError` translated to the status enum. One-shot calls create and
  destroy a `ZSTD_CCtx`/`ZSTD_DCtx` via `ZSTD_createCCtx_advanced` /
  `ZSTD_createDCtx_advanced` with a `ZSTD_customMem` bridging to the injected
  `sk_allocator_t` — this is the audit gap (b) fix and is off the hot path.
  Streaming uses `ZSTD_compressStream2` / `ZSTD_decompressStream` on those
  contexts. zstd build defaults are kept (no `ZSTD_MULTITHREAD`), matching the
  frames main produces.

## 7. Registry

```c
/* compression.c — static const, built at compile time */
static const sk_compression_codec_t codecs[] = {
	&none_codec,
#ifdef SK_COMPRESSION_HAS_ZSTD
	&zstd_codec,
#endif
};
```

- `sk_compression_codec(id)` linear-scans the table (a handful of entries);
  returns NULL for disabled ids → callers treat NULL as
  `SK_COMPRESSION_ERR_UNSUPPORTED_CODEC`.
- `sk_compression_codec_count()` / `sk_compression_codec_at(i)` expose the same
  table for iteration (tooling, tests). Count always includes None.
- Enum members are **always** defined (even with zstd disabled) so on-disk ids
  compile and can be reported as unsupported; only the descriptor is gated.
- Extension path for a new codec: new enum id → descriptor → optional CMake
  option → registry entry. No runtime registration, no constructors.

## 8. Buffer ownership and sizing

- Input (`src` / `src_size`) is borrowed for the duration of the call (or, for
  streams, until consumed).
- Output is **caller-allocated** (`dest` / `dest_cap`). The module never
  allocates output buffers, so ownership and lifetimes stay with the caller
  (matches `Array<u8>` / staging-buffer call sites on main).
- Compress sizing: `compress_bound(src_size)` is sufficient for any level.
- Decompress sizing: prefer `decompressed_size` (exact); fall back to
  `decompress_bound` (upper bound); when both are unknown, stream.
- On `SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT` nothing is written and
  `*out_written` is 0 — callers grow and retry; no partial output is ever
  returned.
- `src` and `dest` must not overlap.
- Actual lengths always come back through `*out_written` (valid on success
  only), never through return values.

## 9. Compression level

- `i32 level` on compress (and stream_init in compress mode).
- `SK_COMPRESSION_LEVEL_DEFAULT (-1)` selects the codec's `level_default`
  (zstd: 3 — the main-branch default).
- Out-of-range levels are clamped to `[level_min, level_max]` (deterministic,
  mirrors zstd's clamp-on-use behavior; no error path needed).
- Decompression ignores level entirely.

## 10. Allocator injection

- Every entry that may allocate internally takes `const sk_allocator_t*`
  (one-shot) or captures it at `stream_init` (streaming), matching the v2
  convention used by `sk_array_init` and `sk_binary_archive_writer_init`.
- The zstd glue builds a `ZSTD_customMem` from the table
  (`alloc` / `free` / `realloc` + `instance`) so zstd's internal contexts and
  workspaces honor the engine allocator — fixing the audit finding that main's
  wrapper let zstd use libc `malloc` (inventory §4).
- Callers that do not care pass `sk_allocator_default()`.
- The None codec never allocates and ignores the allocator.

## 11. Streaming

- `stream_init` is optional per codec (NULL for None; zstd provides it).
- Contract: `update` consumes input / produces output incrementally (callers
  loop, advancing `src` by `*in_consumed`, then flush with `src_size == 0`);
  `finish` finalizes the frame and drains the tail (repeat until
  `*finished`); `destroy` frees the session with the init-time allocator.
- Streaming exists for large asset imports where buffering the whole input
  (main's one-shot-only shape) is undesirable. Main had **no** streaming call
  sites; nothing is lost by adding it.
- Threading: one-shot entries are worker-safe (zstd one-shot is thread-safe;
  no engine SIMD/alignment requirements — inventory §6). A stream session is
  single-threaded, like the v2 archive writer/reader.

## 12. Error handling

- Single status enum, `0 = success` (matches `i32` convention in
  `core/filesystem.h`); no exceptions, no magic ints.
- Real failure paths only: insufficient output (documented recovery: size +
  retry), corrupt/truncated frames, unsupported codec id in this build, OOM at
  the known allocation boundary, and translated zstd errors. Programmer errors
  (NULL codec, NULL allocator, overlapping buffers) are documented contracts —
  debug-build asserts at most, never soft `return -1` guards (house rule).
- `SK_COMPRESSION_ERR_UNSUPPORTED_CODEC` is a runtime status for on-disk ids
  whose codec was compiled out; lookup returns NULL and callers map to this
  code (or handle it how the resource loader prefers).

## 13. Build-time gating of optional codecs

Optional codecs are gated by a CMake option + vendored third-party lib, per
the v2 thirdparty rules (in-tree source + license, `add_subdirectory`, no
package managers / FetchContent / submodules):

```cmake
# root CMakeLists.txt — define BEFORE add_subdirectory(core)
option(SKORE_COMPRESSION_ZSTD "Enable the Zstandard compression codec" ON)

# thirdparty/CMakeLists.txt
if(SKORE_COMPRESSION_ZSTD)
    add_subdirectory(zstd)   # vendored zstd 1.5.6 (same version as main) -> zstd-static
endif()

# core/CMakeLists.txt
if(SKORE_COMPRESSION_ZSTD)
    target_compile_definitions(sk-core PRIVATE SK_COMPRESSION_HAS_ZSTD)
    target_link_libraries(sk-core PRIVATE zstd-static)
    # sk-core-tests gets the same PRIVATE define + link for in-source tests
endif()
```

- Vendoring: `thirdparty/zstd/` keeps the upstream **BSD-3-Clause LICENSE**
  (Meta Platforms), the `lib/` sources (common, compress, decompress, zstd.h,
  zstd_errors.h), and a project-owned CMakeLists that lists sources explicitly
  (same approach as `thirdparty/yyjson`). Same zstd **1.5.6** as main so frame
  output stays byte-identical. `huf_decompress_amd64.S` is excluded, matching
  main's plain-C build (inventory §6/§7).
- The public header stays free of third-party includes: `zstd.h` is included
  only from `compression.c` under `#ifdef SK_COMPRESSION_HAS_ZSTD`
  (`sk_check_header_isolation` stays green).
- With the option OFF the build still compiles (None-only); zstd-specific
  `SK_TEST`s are wrapped in the same define, and the registry test asserts
  `sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD) == NULL`.

## 14. C++ → C mapping table

| C++ element (main) | v2 C replacement | Notes |
|---|---|---|
| `enum class CompressionMode { None, ZSTD }` | `sk_compression_codec_id_t` (`SK_COMPRESSION_CODEC_NONE = 0`, `SK_COMPRESSION_CODEC_ZSTD = 1`) | Value-for-value: serialized resource fields decode to the same ids. Enum members always defined; descriptors are build-gated. |
| `constexpr i32 CompressionDefaultLevel = 3` | `SK_COMPRESSION_LEVEL_DEFAULT (-1)` sentinel; zstd descriptor `level_default = 3` | Sentinel keeps per-codec defaults local; zstd default unchanged. |
| `Compression::Compress(dest, descSize, src, srcSize, mode, level) -> usize` | `codec->compress(allocator, level, src, src_size, dest, dest_cap, &out_written) -> i32` | Length becomes an out param; failures explicit. `descSize` typo becomes `dest_cap`. |
| `Compression::GetMaxCompressedBufferSize(srcSize, mode) -> usize` | `codec->compress_bound(src_size) -> u64` | Same contract: sufficient for any level. |
| `Compression::Decompress(dest, descSize, src, srcSize, mode) -> usize` | `codec->decompress(allocator, src, src_size, dest, dest_cap, &out_written) -> i32` | `ZSTD_isError` translated to status codes (audit gap a). |
| `Compression::GetMaxDecompressedBufferSize(src, srcSize, mode) -> usize` | `codec->decompressed_size(src, src_size, &out)` (exact declared size) + `codec->decompress_bound(src, src_size)` (upper bound, new) | C++ returned `ZSTD_getFrameContentSize` (exact-or-`(size_t)-1`). Split fixes the unchecked sentinel; adds a true bound for frames without a content-size header. |
| (none) | `sk_compression_codec_t` descriptor + build-time registry | Replaces the compile-time `switch (mode)` with one strategy table + id lookup; keeps a single extension point. |
| (none) | `sk_compression_status_t` | New error codes (audit gap a). |
| (none) | `const sk_allocator_t*` on every allocating entry | zstd wired through `ZSTD_customMem` (audit gap b). |
| (none) | streaming `stream_init` / `stream_update` / `stream_finish` / `stream_destroy` | Optional per codec; main had no streaming call sites, v2 adds it for large imports. |
| (none) | `SK_COMPRESSION_CODEC_NONE` = identity copy | Replaces the silent-0 no-op (audit gap c); uniform call sites, stored bytes unchanged. |

## 15. Intentionally dropped

| C++ element | Why dropped |
|---|---|
| Reflection registration of `CompressionMode` (`RegisterIOTypes.cpp:251-253`; texture resource field `RegisterGraphicsTypes.cpp:1104`) | v2 has no reflection/metadata layer for this yet; the resource system stores the raw id (`u32`). Re-add with the reflection layer. |
| Commented-out LZ4 branches in `Compression.cpp` | Never shipped on main, no LZ4 dependency. If wanted later: new enum id + vendored lib + descriptor entry. |
| `usize` byte-count returns | Errors were invisible (inventory §5); replaced by status + `*out_written`. |
| Caller-side `mode == None` branching before every call | Unnecessary with the identity None codec. |
| `TextureImporter` sizing/truncation behavior | Bugs per inventory §8/§10: sized with `totalUncompressedSize` instead of the bound; `usize` stored into a `u32`. v2 uses `compress_bound` + `u64` throughout. |
| `ZSTD_MULTITHREAD` / `ZSTDMT` | Not used by main; one-shot is worker-safe. |

## 16. Test plan (implementation follow-up)

In-source `SK_TEST`s in `core/compression.c` (under `#ifdef SK_TESTS`), plus
host integration coverage, per the v2 testing rules:

- `compression_registry_none_always_present`, `compression_registry_zstd_lookup`,
  `compression_registry_unknown_id_null`;
- `compression_none_identity_roundtrip` (all sizes, incl. 0);
- `compression_zstd_roundtrip` (empty, 1 byte, large, incompressible payloads);
- `compression_compress_bound_sufficient` (any level);
- `compression_decompress_insufficient_output` (nothing written, status set);
- `compression_decompressed_size_exact_and_unknown` (with/without
  content-size header);
- `compression_decompress_bound_upper_bound`;
- `compression_level_clamp` and `compression_default_level_sentinel`;
- `compression_stream_roundtrip` (stream → one-shot and stream → stream);
- `compression_corrupt_frame_rejected`;
- allocator-injection test: a counting/stub `sk_allocator_t` verifies every
  internal allocation goes through the injected table (one-shot + streaming);
- host integration (once the resource pipeline exists): compress a serialized
  archive blob on one side, reload and decompress on the other — proves the
  module works across the plugin/host boundary.

## 17. Open questions

- Resource header metadata: main stored only the codec mode. v2 can also store
  the level once the resource pipeline lands; the API already accepts one.
- Frame checksums: keep zstd defaults (checksum off, content-size header as
  emitted by `ZSTD_compress`) so v2 output is byte-identical to main.
- Unknown-size frames: handled via `decompress_bound` (zstd) or streaming;
  no fallback heuristic needed for the built-in codecs.
