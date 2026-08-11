# Compression API guide (v2 C engine)

Task: APX-164 — user-facing documentation for `core/compression.h`, the v2
compression codec abstraction. This guide covers the one-shot usage pattern,
the (deferred) streaming contract, codec selection, and the build flags that
gate the optional codecs. The authoritative design is
[`docs/compression-design-v2.md`](compression-design-v2.md); the call-site
audit is [`docs/compression-inventory.md`](compression-inventory.md); the
codec evaluation with full measurements is
[`docs/compression-codecs-evaluation.md`](compression-codecs-evaluation.md).

Status: **one-shot surface implemented; streaming interface defined but not
implemented by any built-in codec yet** (deferred — design §4).

## 1. Overview

The module is a C port of the main-branch C++ `Compression.hpp` abstraction.
Codecs are described by a **multi-instance function-pointer table**
(`sk_compression_codec_t`, the same shape as `sk_allocator_t` — deliberately
not a process-global `sk_*_api_t`): callers look up a codec by its stable id
from the build-time registry and call through the descriptor.

- Registry: `sk_compression_codec(id)` → descriptor or `NULL`;
  `sk_compression_codec_count()` / `sk_compression_codec_at(index)` for
  iteration. The identity codec (`SK_COMPRESSION_CODEC_NONE`) is always
  present; optional codecs are compile-time gated (see §5) and decode to
  `NULL` when disabled — map `NULL` to `SK_COMPRESSION_ERR_UNSUPPORTED_CODEC`.
- Codec ids are **stable on-disk values** (`NONE = 0`, `ZSTD = 1`,
  `LZ4 = 2`, `ZLIB = 3`): never renumber or reuse.
- All buffers are **caller-owned**: the module never allocates output. Size
  every destination with the codec's bound queries and read the actual length
  from `*out_written`. Byte sizes are `u64`; `src` and `dest` must not
  overlap.
- Status codes: `0` = success, non-zero = recoverable failure
  (`SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT`, `_CORRUPT_DATA`,
  `_UNSUPPORTED_CODEC`, `_OUT_OF_MEMORY`, `_CODEC_FAILURE`).
- Levels: `SK_COMPRESSION_LEVEL_DEFAULT` (`-1`) selects the codec's
  `level_default`; any other `i32` is clamped to `[level_min, level_max]`.
- Threading: the one-shot entries are safe to call from worker threads (zstd
  one-shot is thread-safe). A streaming session is owned by one thread at a
  time.

## 2. Codec selection guidance

Distilled from [`docs/compression-codecs-evaluation.md`](compression-codecs-evaluation.md)
(APX-161). Engine data shapes are one-shot over fully buffered blobs, and
**decompress latency dominates** (assets, mips, and fonts decompress on
load/upload paths; compress happens at cook/import time).

| Codec | Id | Frame format | Ratio / speed (Silesia-class) | Pick it when |
|---|---|---|---|---|
| `none` | 0 | raw bytes (identity) | — | Storing bytes unchanged; codec-agnostic pass-through |
| `zstd` | 1 | standard zstd frame | 2.887 at 510 MB/s C / 1580 MB/s D (level 1) | **Default** for archives, cooked data, serialized scenes; best ratio/speed balance; byte-identical to main's output at the default level |
| `lz4` | 2 | u64-LE size prefix + raw LZ4 block | ≈2.10 at ≈400 MB/s C / ≈4 GB/s D | Decompress-latency-dominated paths (per-mip texture upload, thumbnails, future network framing) |
| `zlib` | 3 | u64-LE size prefix + RFC 1950 zlib stream | ≈2.74 at ≈95 MB/s C / ≈400 MB/s D | Interop with existing zlib/deflate formats and third-party tooling |

Rules of thumb:

- Default to **zstd at `SK_COMPRESSION_LEVEL_DEFAULT`** (level 3, matching
  main). Levels 1–3 are the operating sweet spot; higher levels buy little
  ratio at large compress cost.
- Use **lz4** when decompress speed matters more than ratio (high-entropy
  binary converges to ≈0.91 ratio in every codec, so throughput decides).
- Use **zlib** only when the on-disk/on-wire format must interoperate with
  RFC 1950 zlib consumers.
- `none` stores bytes unchanged — the identity codec replaces the C++ mode
  that silently returned 0, so callers do not branch on codec before calling.
- LZ4-HC, zstd dictionaries, brotli, xz, and bzip2 were evaluated and
  deferred/rejected (see the evaluation doc §4–§6).

## 3. One-shot usage

The one-shot surface (`compress_bound`, `compress`, `decompressed_size`,
`decompress_bound`, `decompress`) is implemented by every enabled codec and is
the only surface required for the migration (every audit call site is
one-shot over a fully buffered blob).

```c
#include "compression.h"
#include "allocator.h"

/* Compress a caller-owned buffer. */
static i32 compress_blob(const sk_compression_codec_t* codec, const u8* src, u64 src_size,
                         u8** out_compressed, u64* out_size, const sk_allocator_t* a) {
    const u64 bound = codec->compress_bound(src_size);
    u8* dest = NULL;
    u64 written = 0u;

    if (bound == SK_COMPRESSION_SIZE_UNKNOWN) {
        return SK_COMPRESSION_ERR_CODEC_FAILURE; /* codec cannot size the output */
    }
    dest = a->alloc(a->instance, bound);
    if (dest == NULL) {
        return SK_COMPRESSION_ERR_OUT_OF_MEMORY;
    }
    {
        const i32 status = codec->compress(a, SK_COMPRESSION_LEVEL_DEFAULT,
                                           src, src_size, dest, bound, &written);
        if (status != SK_COMPRESSION_OK) { /* INSUFFICIENT_OUTPUT / OOM / CODEC_FAILURE */
            a->free(a->instance, dest);
            return status;
        }
    }
    *out_compressed = dest; /* caller owns; actual size is written, not bound */
    *out_size = written;
    return SK_COMPRESSION_OK;
}

/* Decompress one frame. Size the destination from the frame itself. */
static i32 decompress_blob(const sk_compression_codec_t* codec, const u8* frame, u64 frame_size,
                           u8** out_data, u64* out_size, const sk_allocator_t* a) {
    u64 declared = 0u;
    u8* dest = NULL;
    u64 written = 0u;

    /* Prefer the exact declared size; fall back to the safe upper bound for
     * frames without a content-size header. */
    if (codec->decompressed_size(frame, frame_size, &declared) != SK_COMPRESSION_OK) {
        return SK_COMPRESSION_ERR_CORRUPT_DATA;
    }
    if (declared == SK_COMPRESSION_SIZE_UNKNOWN) {
        const u64 bound = codec->decompress_bound(frame, frame_size);
        if (bound == SK_COMPRESSION_SIZE_UNKNOWN) {
            return SK_COMPRESSION_ERR_CODEC_FAILURE; /* use streaming instead */
        }
        declared = bound;
    }
    dest = a->alloc(a->instance, declared > 0u ? declared : 1u);
    if (dest == NULL) {
        return SK_COMPRESSION_ERR_OUT_OF_MEMORY;
    }
    {
        const i32 status = codec->decompress(a, frame, frame_size, dest, declared, &written);
        if (status != SK_COMPRESSION_OK) { /* INSUFFICIENT_OUTPUT / CORRUPT_DATA / ... */
            a->free(a->instance, dest);
            return status;
        }
    }
    *out_data = dest;
    *out_size = written;
    return SK_COMPRESSION_OK;
}

/* Dispatch by stable on-disk id. */
static const sk_compression_codec_t* codec_for_id(sk_compression_codec_id_t id) {
    return sk_compression_codec(id); /* NULL when compiled out -> UNSUPPORTED_CODEC */
}
```

Recovery contract: `SK_COMPRESSION_ERR_INSUFFICIENT_OUTPUT` means the
destination was too small and **nothing was written** — size with the codec's
bound query and retry. `SK_COMPRESSION_ERR_CORRUPT_DATA` means the input is
not a valid frame for this codec (truncated, mangled, or foreign). A `NULL`
codec lookup means the id has no enabled descriptor in this build.

## 4. Streaming (interface defined, deferred)

The descriptor exposes optional streaming slots — `stream_init`,
`stream_update`, `stream_finish`, `stream_destroy` — but **no built-in codec
implements them yet** (`stream_init == NULL`; check before use). The contract
for when a streaming codec lands (or a caller implements one):

- `stream_init(out, mode, level, allocator)` creates a session; the codec
  captures the allocator for all subsequent allocations. One stream is owned
  by one thread at a time.
- `stream_update(instance, src, src_size, &in_consumed, dest, dest_cap,
  &out_written)` consumes up to `src_size` input bytes and writes up to
  `dest_cap` output bytes; unconsumed input is buffered inside the stream.
  Caller loop: advance `src` by `*in_consumed` and repeat until all input is
  consumed, then call with `src_size == 0` until `*out_written == 0` (flush).
  This entry never returns `INSUFFICIENT_OUTPUT` — output drains
  incrementally, so pass a fresh `dest` buffer each iteration.
- `stream_finish(instance, dest, dest_cap, &out_written, &finished)`
  finalizes the frame (compress) or verifies the end of the frame
  (decompress); may be called repeatedly until `*finished` is non-zero.
- `stream_destroy(instance)` releases the session (NULL is a no-op).

The one-shot frame format is unaffected by streaming — the deferred slots are
a transport concern (large imports that should not buffer the whole input).

## 5. Build flags for optional codecs

Optional codecs are gated by CMake options in the root `CMakeLists.txt`
(default **ON**); each is vendored in-tree per the repo's thirdparty rules
(in-tree source + license, `add_subdirectory`, no package managers):

| CMake option | Define (auto) | Vendored lib | License | Codec |
|---|---|---|---|---|
| `SK_COMPRESSION_ZSTD` | `SK_COMPRESSION_HAS_ZSTD` | zstd 1.5.6 (same as main) | BSD-3-Clause | `SK_COMPRESSION_CODEC_ZSTD` |
| `SK_COMPRESSION_LZ4` | `SK_COMPRESSION_HAS_LZ4` | LZ4 | BSD-2-Clause | `SK_COMPRESSION_CODEC_LZ4` |
| `SK_COMPRESSION_MINIZ` | `SK_COMPRESSION_HAS_MINIZ` | miniz | MIT | `SK_COMPRESSION_CODEC_ZLIB` |

```bash
# Disable a codec (build still compiles; the id decodes to NULL):
cmake -S . -B build -G Ninja -DSK_COMPRESSION_LZ4=OFF
```

With a codec disabled, its enum id stays defined so on-disk ids compile and
read, `sk_compression_codec(id)` returns `NULL`, and callers map that to
`SK_COMPRESSION_ERR_UNSUPPORTED_CODEC`. The identity codec is never gated.
The public header stays free of third-party includes (`zstd.h`/`lz4.h`/
`miniz.h` appear only in `core/compression.c` under the matching define), so
`sk_check_header_isolation` stays green in any configuration.

## 6. Allocator contract

Entries that may allocate internally take `const sk_allocator_t*` (one-shot)
or capture it at `stream_init` (streaming). Pass `sk_allocator_default()`
when you have no reason to inject a specific allocator:

- the zstd glue routes every internal allocation through
  `ZSTD_customMem` built from the injected table;
- the zlib codec allocates its compressor state through the injected table
  (decompress is heap-free via `tinfl`);
- LZ4 one-shot and the identity codec are heap-free and ignore the allocator.

## 7. Migration status (APX-164)

The v1→v2 call-site migration is complete: every compression call in the v2
tree goes through the registry descriptors, and the APX-174 runtime migration
flag plus the legacy raw-codec paths were removed (design §9.3). Wire
compatibility with the main-branch C++ engine is enforced by the frozen
APX-176 compatibility vectors in `core/compression.c` (decode, encode, and
corruption directions; regenerate with `scripts/gen-compression-vectors.sh`
and re-run the full suite — never hand-edit the frozen bytes). The only
raw-codec uses left are deliberate: the vector generator
(`scripts/gen-compression-vectors.c`, which produces reference frames by
design) and the unknown-content-size test crafting (the v2 zstd codec
intentionally always writes the content-size header).
