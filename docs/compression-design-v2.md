# Compression API design (v2 C engine) — APX-165

Task: APX-165. Input: `docs/compression-inventory.md` (APX-158 audit of the
main-branch C++ `Compression.hpp` abstraction and the v2 port target).
Status: **design only** — no engine code changes land with this document.

This is the authoritative design for the implementation tasks that follow
(APX-166 interface skeleton, APX-167 codecs, APX-168 call-site migration). It
supersedes and consolidates the earlier draft `docs/compression-design.md`
(APX-159); where the two disagree, this document wins.

## 1. Scope and goals

Port the main-branch compression abstraction (inventory §1) to v2 as a C
module in `core/`, preserving behavior at every existing call site (inventory
§8) and every shipped on-disk format (zstd frames, inventory §7), while fixing
the audit gaps (inventory §5, §10):

- invisible errors (`usize` returns, unchecked `ZSTD_isError`, unguarded
  `ZSTD_CONTENTSIZE_*` sentinels) → explicit `sk_compression_status_t`;
- zstd internal allocations bypassing the engine allocator (libc `malloc`) →
  `sk_allocator_t` injection through `ZSTD_customMem`;
- `CompressionMode::None` silently returning `0` → identity codec with an
  explicit contract;
- unsafe sizing paths (`(size_t)-1` sentinels, `u32` truncation, wrong bound
  usage in `TextureImporter`) → `u64` sizes and split bound queries.

Non-goals (deferred — see §2): a runtime plugin-registered codec registry,
multi-threaded codecs (`ZSTDMT`), additional codecs beyond zstd (evaluated by
APX-161, implemented by APX-162), and the C++ reflection metadata layer.

## 2. Kept / dropped / deferred — main-branch capabilities

The decision for every main-branch capability, stated explicitly:

| Main-branch capability | v2 decision | Why |
|---|---|---|
| One-shot compress / decompress | **Kept** | Every main call site (inventory §8) is one-shot over fully-buffered data; the operation shape carries over 1:1. |
| `GetMaxCompressedBufferSize` (bound) | **Kept** → `codec->compress_bound()` | Required for caller-owned output sizing; level-independent. |
| `GetMaxDecompressedBufferSize` (declared size) | **Kept**, split into exact-size + true-bound queries | `ZSTD_getFrameContentSize` sentinels were passed through unguarded (bug); v2 separates “exact declared size” from “upper bound”. |
| `CompressionMode` enum → codec selection | **Kept** as stable ids + build-time descriptor table | Ids are on-disk stable (serialized resource fields); the compile-time `switch` becomes a descriptor lookup (§5). |
| `CompressionDefaultLevel = 3` | **Kept** as the zstd descriptor default; API surface uses a `-1` sentinel | Per-codec default stays local to the descriptor; the zstd default is unchanged so output matches main. |
| `CompressionMode::None` (silent `return 0`) | **Dropped** — replaced by an identity codec | The silent 0 hid misuse and forced caller-side `mode == None` branching; the identity codec preserves stored bytes and makes the contract explicit. |
| `usize` byte-count returns | **Dropped** — status + `*out_written` | Byte-count returns made errors invisible (inventory §5); v2 reports failures explicitly. |
| Caller-owned buffers (`dest`/`src` spans) | **Kept** | Matches v2 house style and every main call site (`Array<u8>`, GPU staging buffers). |
| Streaming (`ZSTD_CStream`/`ZSTD_DStream`) | **Deferred** — optional per-codec entries, no main call sites | Main is one-shot only; v2 adds optional streaming entries so large future imports can avoid full buffering, but no migration path needs them. |
| Multi-threaded compression (`ZSTDMT`) | **Deferred** | Main does not use it; the one-shot API is already worker-safe (§8). Add only behind a real job system. |
| Reflection registration of `CompressionMode` | **Dropped for now** | v2 has no reflection/metadata layer yet; the resource layer stores the raw id. Re-add when the reflection layer lands. |
| Commented-out LZ4 branches in `Compression.cpp` | **Dropped** | Never shipped on main; no LZ4 dependency. Additional codecs are a separate evaluation (APX-161). |
| Worker-thread decompression | **Kept** | One-shot entries are worker-safe; main already decompresses on `ResourceWorker` threads and editor task threads (§8). |

## 3. Public types and functions

Module home: `core/compression.h` + `core/compression.c`. This is a pure
engine utility (like `math3d`, `serialization`), so it implements in
`sk-core` — not `sk-app` (AGENTS.md: “host / module API implementations live
in `app`”; this is neither).

Public surface:

| Kind | Symbol | Notes |
|---|---|---|
| constant | `SK_COMPRESSION_LEVEL_DEFAULT` | `-1`; select the codec’s `level_default`. |
| constant | `SK_COMPRESSION_SIZE_UNKNOWN` | `(u64)-1`; size/bound could not be determined. |
| enum | `sk_compression_codec_id_t` | Stable on-disk ids; members always defined. |
| enum | `sk_compression_status_t` | `0` = success, non-zero = failure. |
| enum | `sk_compression_mode_t` | Streaming direction (`COMPRESS`/`DECOMPRESS`). |
| struct | `sk_compression_stream_t` | Opaque streaming session handle. |
| struct | `sk_compression_codec_t` | Codec descriptor: multi-instance function-pointer table (one per enabled codec). |
| function | `sk_compression_codec(id)` | Registry lookup → descriptor or `NULL`. |
| function | `sk_compression_codec_count()` | Number of enabled codecs (≥ 1). |
| function | `sk_compression_codec_at(index)` | Descriptor at registry index. |

Naming follows the v2 table rules: `sk_compression_codec_t` is a
multi-instance strategy table in the shape of `sk_allocator_t` (AGENTS.md
“`sk_*_api_t` vs other function-pointer structs”) — deliberately **not**
`sk_compression_api_t`, which is reserved for one process-global module
surface. Callers hold a descriptor pointer and call through it; the only
module-level free functions are registry lookups (no free-function mirrors of
table entries).

Byte sizes are `u64` throughout (LLP64-safe, matching `sk_blob_view_t` in
`core/serialization.h`). The header is C with `extern "C"` for C++.

### 3.1 Header sketch (complete)

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
	i32 (*compress)(const sk_allocator_t* allocator, i32 level,
	                const u8* src, u64 src_size,
	                u8* dest, u64 dest_cap, u64* out_written);

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
	i32 (*decompress)(const sk_allocator_t* allocator,
	                  const u8* src, u64 src_size,
	                  u8* dest, u64 dest_cap, u64* out_written);

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
	i32 (*stream_init)(sk_compression_stream_t* out, sk_compression_mode_t mode,
	                   i32 level, const sk_allocator_t* allocator);

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
	i32 (*stream_update)(void_ptr_t instance,
	                     const u8* src, u64 src_size, u64* in_consumed,
	                     u8* dest, u64 dest_cap, u64* out_written);

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
	i32 (*stream_finish)(void_ptr_t instance,
	                     u8* dest, u64 dest_cap, u64* out_written, i32* finished);

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

### 3.2 Descriptor semantics

- `id` / `name` are metadata; the registry never contains two descriptors with
  the same id.
- `compress_bound` is level-independent (true for zstd and for the identity
  None codec); the contract requires it to be an upper bound for **any** level
  so callers can size once.
- `decompressed_size` is the *exact* declared frame size (zstd
  `ZSTD_getFrameContentSize`); `decompress_bound` is a true upper bound (zstd
  `ZSTD_decompressBound`). The C++ API exposed only the exact-size query and
  passed its sentinels through unguarded; v2 separates the two and makes the
  sentinel explicit and checkable (`SK_COMPRESSION_SIZE_UNKNOWN`).
- **None** (`SK_COMPRESSION_CODEC_NONE`): identity codec. `compress_bound(n) =
  n`, compress copies, decompress copies, `decompressed_size(n) = n`,
  `decompress_bound(n) = n`, `stream_init = NULL`. Levels are ignored
  (`level_min = level_max = level_default = 0`). This replaces the C++
  silent-0 no-op: call sites no longer branch on `mode == None` before every
  call, and a resource tagged None still stores the raw bytes unchanged.
- **Zstd** (`SK_COMPRESSION_CODEC_ZSTD`): `level_default = 3` (matches
  `CompressionDefaultLevel`), range `ZSTD_minCLevel()..ZSTD_maxCLevel()`
  (negative values select the fast levels). Entries map to `ZSTD_compress2` /
  `ZSTD_decompressDCtx` / `ZSTD_compressBound` /
  `ZSTD_getFrameContentSize` / `ZSTD_decompressBound`, with `ZSTD_isError`
  translated to the status enum (§7). One-shot calls create and destroy a
  `ZSTD_CCtx` / `ZSTD_DCtx` via `ZSTD_createCCtx_advanced` /
  `ZSTD_createDCtx_advanced` with a `ZSTD_customMem` bridging to the injected
  `sk_allocator_t` — this is the audit gap (b) fix and is off the hot path
  (§6). Streaming uses `ZSTD_compressStream2` / `ZSTD_decompressStream` on
  those contexts.

## 4. One-shot vs streaming surface

**One-shot is the required, primary surface** — the exact operations every
main-branch call site uses (inventory §8): `compress`, `decompress`,
`compress_bound`, `decompressed_size` (+ the new `decompress_bound`). Every
enabled codec implements them. This preserves the main one-shot-only behavior
with no call-site rewrite beyond the mechanical shape change.

**Streaming is optional, per codec** (`stream_init` / `stream_update` /
`stream_finish` / `stream_destroy`; descriptor entry NULL when unsupported).
Main had no streaming call sites and no streaming API, so nothing that ships
needs it; v2 adds it as a *deferred* capability for large asset imports that
should not buffer the whole input (e.g. multi-GB imports, incremental writes).
None has no streaming; zstd implements it. Because it is optional and
descriptor-gated, it cannot break migration: a caller that never touches
`stream_*` sees identical behavior to main.

Summary:

| Capability | Main | v2 | Notes |
|---|---|---|---|
| One-shot compress / decompress | required | required | Same operations, explicit errors. |
| Bound / size queries | required (2) | required (3: bound + exact + bound) | Split fixes the unguarded sentinel. |
| Streaming | absent | optional per codec (deferred) | No main call site uses it; added for future large imports. |
| Level parameter | `i32 level` default 3 | `i32 level` + `-1` sentinel | Same zstd behavior; per-codec default. |

## 5. Codec registration mechanism

Registration is **build-time only** — matching main’s compile-time `switch`
semantics and the v2 house rule against free-running global constructors:

```c
/* compression.c — static const table, built at compile time */
static const sk_compression_codec_t codecs[] = {
	&none_codec,
#ifdef SK_COMPRESSION_HAS_ZSTD
	&zstd_codec,
#endif
};
```

- `sk_compression_codec(id)` linear-scans the table (a handful of entries);
  returns NULL for disabled ids → callers map NULL to
  `SK_COMPRESSION_ERR_UNSUPPORTED_CODEC` (or handle it however the resource
  loader prefers).
- `sk_compression_codec_count()` / `sk_compression_codec_at(i)` expose the same
  table for iteration (tooling, tests). Count always includes None.
- Enum members are **always** defined (even with zstd disabled) so on-disk ids
  compile and can be reported as unsupported; only the descriptor is gated.
- Extension path for a new codec: new enum id → descriptor → optional CMake
  option → registry entry. No runtime registration, no `sk_*_get_api`
  accessors, no plugin involvement — codecs are core modules, not plugins.

## 6. Allocator API (v2 house style)

Follows the v2 allocator convention exactly (AGENTS.md, `core/allocator.h`,
`core/array.h`):

- Every entry that may allocate internally takes `const sk_allocator_t*`
  (one-shot) or captures it at `stream_init` (streaming) — the same shape as
  `sk_array_init(&arr, allocator)` and
  `sk_binary_archive_writer_init(&w, allocator)`.
- Callers that do not care pass `sk_allocator_default()`.
- The None codec never allocates and ignores the allocator.
- The zstd glue builds a `ZSTD_customMem` from the table:
  `customAlloc = allocator->alloc` with `opaque = allocator->instance`,
  `customFree = allocator->free`. Note `ZSTD_customMem` has **no realloc
  slot** (zstd 1.5.6: `{ZSTD_allocFunction customAlloc; ZSTD_freeFunction
  customFree; void* opaque;}`), so the allocator’s `realloc` is simply not
  used by the zstd backend — this is expected and not a gap.
- To use the advanced context constructors the glue defines
  `ZSTD_STATIC_LINKING_ONLY` before including `zstd.h` (main did not need it;
  v2 does, and it stays confined to `compression.c`).
- One-shot compress/decompress create and destroy a `ZSTD_CCtx`/`ZSTD_DCtx`
  per call (`ZSTD_createCCtx_advanced` / `ZSTD_createDCtx_advanced` +
  `ZSTD_compress2` / `ZSTD_decompressDCtx`, level set via
  `ZSTD_CCtx_setParameter`). This is an allocation at a known call boundary,
  off the hot path, and fixes the audit finding that main let zstd use libc
  `malloc` internally (inventory §4).

## 7. Error-code conventions

Matches v2: `i32`-style status codes with `0` = success, non-zero = failure,
domain enum like `sk_file_status_t` (`core/filesystem.h`). **No exceptions** —
v2 is C and uses exceptions nowhere; this module keeps that. No magic ints, no
errno leakage, no defensive null-police guards.

`sk_compression_status_t` covers only failures that can actually occur at
runtime:

| Status | When |
|---|---|
| `SK_COMPRESSION_OK` | Success. `*out_written` is valid. |
| `SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT` | `dest_cap` too small; nothing written, `*out_written` = 0. Documented recovery: size with the bound query and retry. |
| `SK_COMPRESSION_ERR_CORRUPT_DATA` | Bad/truncated/unsupported frame. |
| `SK_COMPRESSION_ERR_UNSUPPORTED_CODEC` | On-disk id whose codec was compiled out. |
| `SK_COMPRESSION_ERR_OUT_OF_MEMORY` | Allocation failed at the known context-creation boundary. |
| `SK_COMPRESSION_ERR_CODEC_FAILURE` | Underlying codec error not covered above. |

zstd error translation (`ZSTD_getErrorCode` + `ZSTD_getErrorName` for logs):

| zstd error class | v2 status |
|---|---|
| `ZSTD_error_dstSize_tooSmall` | `SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT` |
| `ZSTD_error_memory_allocation` | `SK_COMPRESSION_ERR_OUT_OF_MEMORY` |
| `ZSTD_error_prefix_unknown`, `ZSTD_error_frameParameter_*`, `ZSTD_error_corruption_detected`, `ZSTD_error_checksum_wrong`, truncated input | `SK_COMPRESSION_ERR_CORRUPT_DATA` |
| anything else | `SK_COMPRESSION_ERR_CODEC_FAILURE` |

Programmer errors (NULL codec/allocator, overlapping buffers, using a NULL
stream entry) are documented contracts — debug-build `assert` at most, never a
soft `return -1` guard (house rule).

## 8. Threading / SIMD assumptions carried over

Carried over from the inventory (§6) without change:

- **One-shot entries are worker-safe.** zstd one-shot is thread-safe per call
  (entropy tables use thread-local state), and main relies on that:
  decompression on `ResourceWorker` threads during texture upload, editor
  background-task threads for imports/thumbnails, and the `.resources`
  archive load path. The v2 module keeps this contract: the one-shot
  descriptor entries make no engine-thread assumptions and touch no globals.
- **A streaming session is owned by one thread at a time** — same rule as
  `sk_archive_writer_t`/`sk_archive_reader_t` (`core/serialization.h`).
- **No engine SIMD / alignment requirements.** Buffers are plain `u8` arrays
  with no special alignment contract, matching main. zstd internally uses
  runtime CPU dispatch; the vendored build compiles as plain C and the
  `huf_decompress_amd64.S` assembly file is excluded, exactly like main’s
  build, so frames and behavior stay identical.
- **No `ZSTD_MULTITHREAD` / `ZSTDMT`** — single-threaded zstd build, deferred
  (see §2).

## 9. Format preservation and call-site behavior (escalation check)

The design **preserves behavior for every existing call site without touching
serialization or the asset pipeline** — no escalation is required:

- **Frame format:** the only shipped codec is zstd; v2 keeps the zstd frame
  format. One-shot entries emit/consume standard frames with the same default
  parameters as main’s `ZSTD_compress` at the same level (content-size header
  written, checksum off, same level → same bytes). `.resources`/`.cooked`
  archives, font blobs, dependency blobs, and per-mip texture data written by
  main decode identically on v2.
- **On-disk ids:** `None = 0`, `ZSTD = 1` map 1:1 to
  `sk_compression_codec_id_t`; values are stable and never renumbered.
- **Call sites (inventory §8):** every consumer uses only the four one-shot
  operations (`Compress`, `Decompress`, `GetMaxCompressedBufferSize`,
  `GetMaxDecompressedBufferSize`); the v2 surface covers all four, so the
  migration (APX-168) is a mechanical shape change (status + `*out_written`,
  `u64` sizes) with no serialization or pipeline change.
- The only behavioral differences are the audit-gap fixes: failures are
  reported instead of silently producing garbage sizes/corrupt blobs, and
  `None` is explicit. No main call site depends on the silent behavior (today
  a zstd failure silently corrupts the stored blob), so this cannot break
  shipped behavior.

### 9.1 Call-site migration status (APX-168)

APX-168 checked the migration surface on this branch and found **no v2
compression call sites to migrate and no interim shim to remove**:

- Every consumer in the inventory (§8) lives in the C++ engine on `main`
  (`.resources`/`.cooked` archive load/save, font blobs, per-mip texture
  decompress, thumbnails). None of those modules exist on this branch: the v2
  resource/asset pipeline (`core/resource_assets*`, landed on `origin/v2` as
  `repository_assets`, commit `1f334e5`) is not part of this feature branch
  and, as of that commit, contains zero compression calls.
- A full-tree grep for `sk_compression_*`/`sk_compress*`/`sk_decompress*`
  matches only the interface (`core/compression.{h,c}`) and its
  tests/conformance checks; the remaining "compress" hits are GPU
  acceleration-structure compaction flags and texture-format comments,
  unrelated to this module.
- Because migrating the inventory consumers here would require building the v2
  asset pipeline first (a serialization/pipeline change), APX-168 leaves them
  on the old path per the task's escalation rule. When that pipeline lands,
  the migration is the mechanical registry-based shape change described above;
  the byte-compatibility and error-contract guarantees still hold.

Verification on this branch (Debug, gcc, Ninja): full build clean; CTest 3/3
(`sk-compression-conformance`, `sk-tests`, `sk-integration-tests`);
`sk-tests` 457/457; `sk-integration-tests` 22/22. No code change was needed,
so the before/after suite results are identical.

### 9.2 First call-site migration (APX-174) — superseded by §9.3

*Historical record.* With no engine call sites on this branch (see §9.1),
APX-174 migrated the one remaining v1-shaped compression call site in the tree
— the **zstd v1 adapter of the APX-173 parity harness**
(`harness_zstd_v1_compress` / `harness_zstd_v1_decompress`, raw
`ZSTD_compress` / `ZSTD_decompress` at `CompressionDefaultLevel = 3`) — behind
a runtime feature flag (`sk_compression_set_v2_enabled` /
`sk_compression_v2_enabled`, `SK_COMPRESSION_USE_V2` env default) that
**defaulted to the v1 path**. APX-174 proved the flag states produce
byte-identical frames and ran the suite in both states. **APX-164 (below)
removed the flag and the v1 path** once the migration was complete; the flag
API is no longer part of the public surface (§3).

### 9.3 Call-site migration completed (APX-164)

APX-164 completed the migration using the audit call-site list
(`docs/compression-inventory.md` §8) as the checklist:

- **Checklist result:** every inventory call site lives in the C++ engine on
  `main` (`.resources`/`.cooked` archives, font blobs, per-mip texture
  decompress, thumbnails) and none of those modules exist on this branch (§9.1)
  — there are no v2 engine call sites to migrate. The only v1-shaped
  compression code in the v2 tree was the parity harness: the zstd v1 adapter
  (§9.2) and the corpus builder's raw `ZSTD_compress` call. Both now route
  through the v2 descriptor interface:
  - the parity-harness zstd adapter was **removed** — the harness resolves
    codecs only via `sk_compression_codec*` and the zstd parity corpus runs on
    the registry descriptor (pinned by `compression_migration_zstd_routes_through_registry`);
  - the `already_compressed` corpus entry compresses through
    `sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD)->compress` (byte-identical
    frames; APX-176 vectors);
  - the APX-174 feature flag (`sk_compression_set_v2_enabled` /
    `sk_compression_v2_enabled`, `SK_COMPRESSION_USE_V2` env, CI double-run)
    was **removed** as now-dead legacy machinery — no legacy compression entry
    point remains in v2.
- **No on-disk / on-wire format change:** the migrated calls emit and consume
  exactly the same frames as before (standard zstd frames at level 3); APX-174
  proved byte-identity between the v1 and v2 paths, and the frozen APX-176
  compat vectors (§13) are the permanent wire-compatibility gate. No escalation
  was required.
- **Remaining raw-codec uses are deliberate and documented:**
  `scripts/gen-compression-vectors.c` produces the frozen reference frames
  from the reference libraries by design (its output is checked in, not
  linked), and the `compression_zstd_unknown_content_size` test crafts a
  no-content-size zstd frame with the stable zstd API because the v2 codec
  intentionally always writes the content-size header.
- The parity harness keeps the identity v1 reference row (raw-bytes semantics
  — not a compression library) for the `none` wire check; real codecs rely on
  the v2 round-trips plus the frozen vectors.

Verification on this branch (Debug, gcc, Ninja): full build clean (including
clang-tidy warnings-as-errors); CTest 4/4 (`sk-compression-conformance`,
`sk-compression-bench`, `sk-tests`, `sk-integration-tests`) — no regressions;
the migration-completion tests pin that no legacy adapter or flag API remains.

## 10. Build-time gating and vendoring

Optional codecs are gated by a CMake option + a vendored third-party lib, per
the v2 thirdparty rules (in-tree source + license, `add_subdirectory`, no
package managers / FetchContent / submodules):

```cmake
# root CMakeLists.txt — define BEFORE add_subdirectory(thirdparty)
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
  (Meta Platforms, Inc. and affiliates), the library sources (common,
  compress, decompress, `zstd.h`, `zstd_errors.h`), and a project-owned
  CMakeLists that lists sources explicitly — the same approach as
  `thirdparty/yyjson`. Same zstd **1.5.6** as main so frame output stays
  byte-identical. The `huf_decompress_amd64.S` assembly is excluded, matching
  main’s plain-C build (§8). No `ZSTD_MULTITHREAD`.
- The public header stays free of third-party includes: `zstd.h` is included
  only from `compression.c` under `#ifdef SK_COMPRESSION_HAS_ZSTD`
  (`sk_check_header_isolation` stays green).
- With the option OFF the build still compiles (None-only); zstd-specific
  `SK_TEST`s are wrapped in the same define, and the registry test asserts
  `sk_compression_codec(SK_COMPRESSION_CODEC_ZSTD) == NULL`.

## 11. Mapping table — every main-branch public symbol to its v2 replacement

Scope: every public symbol of the abstraction (inventory §1) plus the
main-branch metadata registrations that name it. Symbols with no planned
equivalent are marked **no planned equivalent**.

| Main-branch symbol | v2 replacement | Status / notes |
|---|---|---|
| `enum class CompressionMode { None, ZSTD }` | `sk_compression_codec_id_t` (`SK_COMPRESSION_CODEC_NONE = 0`, `SK_COMPRESSION_CODEC_ZSTD = 1`) | Value-for-value; serialized resource fields decode to the same ids. Members always defined; descriptors build-gated. |
| `CompressionMode::None` (semantics: all four functions return 0) | `SK_COMPRESSION_CODEC_NONE` identity codec (copy in, copy out) | Silent-0 semantics dropped; identity codec replaces caller-side `mode == None` branching. Stored bytes unchanged. |
| `CompressionMode::ZSTD` | `SK_COMPRESSION_CODEC_ZSTD` descriptor | Same codec, same frames. |
| `constexpr i32 CompressionDefaultLevel = 3` | `SK_COMPRESSION_LEVEL_DEFAULT (-1)` sentinel; zstd descriptor `level_default = 3` | Default value preserved per codec; sentinel keeps defaults local. |
| `Compression::Compress(dest, descSize, src, srcSize, mode, level = 3) -> usize` | `codec->compress(allocator, level, src, src_size, dest, dest_cap, &out_written) -> i32` | Same one-shot operation; length becomes an out param, failures explicit. `descSize` typo becomes `dest_cap`; sizes `u64`. |
| `Compression::GetMaxCompressedBufferSize(srcSize, mode) -> usize` | `codec->compress_bound(src_size) -> u64` | Same contract: sufficient for any level. |
| `Compression::Decompress(dest, descSize, src, srcSize, mode) -> usize` | `codec->decompress(allocator, src, src_size, dest, dest_cap, &out_written) -> i32` | `ZSTD_isError` translated to status codes (audit gap a). |
| `Compression::GetMaxDecompressedBufferSize(src, srcSize, mode) -> usize` | `codec->decompressed_size(src, src_size, &out)` (exact declared size) + `codec->decompress_bound(src, src_size)` (upper bound, new) | C++ returned `ZSTD_getFrameContentSize` (exact-or-sentinel). Split fixes the unchecked sentinel; adds a true bound for frames without a content-size header. |
| Reflection value registration of `CompressionMode` (`RegisterIOTypes.cpp:251-253`) | **no planned equivalent** | v2 has no reflection/metadata layer; re-add with the reflection layer. Not part of the runtime API. |
| Texture resource field registration of `CompressionMode` (`RegisterGraphicsTypes.cpp:1104`) | **no planned equivalent** | v2 resource layer stores the raw id (`u32`); no metadata field yet. Not part of the compression module API. |
| Commented-out LZ4 branches in `Compression.cpp` | **no planned equivalent** | Never shipped on main, no LZ4 dependency. If wanted later: new enum id + vendored lib + descriptor entry (APX-161/162 evaluate this). |

Symbols with no v2 equivalent in the table are deliberate: reflection/metadata
belongs to the future reflection layer, and the LZ4 branches were dead code on
main.

The table above is a **maintained artifact**, not a snapshot: the executable
conformance check at `tests/conformance/compression_mapping.c` (APX-171) freezes
every row — mapped replacements must exist and be callable with the documented
signature (compile-time type checks + runtime calls), and the three
no-planned-equivalent symbols must stay declared as intentional gaps. The check
builds and runs as `sk-compression-conformance` in Debug and Release CI, so a
renamed or dropped public symbol fails loudly instead of drifting.

## 12. Test plan (implementation follow-up)

In-source `SK_TEST`s in `core/compression.c` (under `#ifdef SK_TESTS`), plus
host integration coverage, per the v2 testing rules:

- `compression_registry_none_always_present`, `compression_registry_zstd_lookup`,
  `compression_registry_unknown_id_null`;
- `compression_none_identity_roundtrip` (all sizes, incl. 0);
- `compression_zstd_roundtrip` (empty, 1 byte, large, incompressible payloads);
- byte-compatibility: decompress fixtures produced by the main-branch
  implementation (frames written with `ZSTD_compress`, level 3) — proves
  on-disk interop (APX-167);
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

## 13. Cross-engine compatibility vectors (APX-176)

Before any call-site migration, the v2 codecs must be proven wire-compatible
with the C++ engine on `main`. `core/compression.c` (SK_TESTS section) checks
in **frozen frames** for every enabled codec and asserts both directions:

- **decode** — `compression_compat_vectors_decode`: each frozen frame (produced
  by the reference implementation, not by v2's codec glue) decompresses to its
  corpus byte-for-byte, with `decompressed_size` / `decompress_bound` agreeing;
- **encode** — `compression_compat_vectors_encode`: v2 at the default level
  re-emits the frozen frame byte-for-byte (the checked-in expected-output
  comparison; identical bytes mean the C++ engine / reference decoder recovers
  the payload unchanged);
- **robustness** — `compression_compat_vectors_truncated_and_corrupt`:
  truncated and bit-flipped frames return `SK_COMPRESSION_ERR_CORRUPT_DATA`
  with zero bytes written (guarded output buffers prove no out-of-bounds
  write); zstd's empty-input quirk (`ZSTD_decompress` accepts 0 bytes as an
  empty frame, matching main) is asserted as the compatible result.

Frame provenance (see `scripts/gen-compression-vectors.c`):

| Codec | Reference producer | Notes |
|---|---|---|
| zstd | `ZSTD_compress(dst, cap, src, size, 3)` — main `Compression.cpp` `case ZSTD` | Byte-identical v2 output at the default level (same zstd 1.5.6, level 3). |
| lz4 | `LZ4_compress_default` + v2 u64-LE size prefix — main's commented-out LZ4 branch | v2 emits the same bytes (LZ4_compress_fast, acceleration 1). |
| zlib | `mz_compress2(..., 6)` (RFC 1950) + v2 u64-LE size prefix | Same miniz tdefl flags v2 uses; decodable by any RFC 1950 zlib. |

The vectors are a **gate, not a snapshot**: if a codec's output format drifts
the suite fails loudly, and the frozen bytes must never be edited to match v2
output — the mismatch is the finding to fix. Empty payloads are covered too:
v2 emits the reference libraries' canonical empty frames (zstd 9-byte frame,
LZ4 `0x00` block, zlib `78 9C 03 00 00 00 00 01` stream), while still decoding
the older prefix-only empty frames for backward compatibility. Regenerate the
arrays with `./scripts/gen-compression-vectors.sh core/compression.c` (the
script verifies nothing by itself — re-run the full suite afterwards).

## 14. Open questions

- Resource header metadata: main stored only the codec mode. v2 can also store
  the level once the resource pipeline lands; the API already accepts one.
- Frame checksums: keep zstd defaults (checksum off, content-size header as
  emitted by `ZSTD_compress`) so v2 output is byte-identical to main.
- Unknown-size frames: handled via `decompress_bound` (zstd) or streaming; no
  fallback heuristic needed for the built-in codecs.
- Whether one-shot should cache a `ZSTD_CCtx`/`ZSTD_DCtx` per thread later:
  not needed for the port (context creation is off the hot path); revisit only
  if profiling shows a hotspot.
