# Compression codec evaluation — v2 (APX-161)

Task: APX-161 — bounded evaluation of additional compression codecs for the v2
C engine, satisfying the goal's "check for other useful compression types"
requirement. This report scores candidates and recommends what to implement now
versus defer. It is an evaluation only; implementation is tracked separately
(APX-162).

## 1. Scope and method

Candidates considered: **LZ4 / LZ4-HC**, **Zstandard** (already vendored —
baseline), **zlib / deflate via miniz**, and lightweight single-header / LZ77-RLE
options (minilzo, heatshrink, shoco), plus a screen of libdeflate, brotli, xz,
and bzip2. Each is scored on:

- license compatibility with this repo (permissive-only, in-tree vendoring),
- pure-C portability across v2's target platforms (Windows / Linux / macOS),
- vendoring and build cost (thirdparty rules: in-tree source, no package
  managers / FetchContent / submodules),
- compression ratio,
- compress / decompress throughput for the engine's actual data shapes.

Method: shapes come from the audit (`docs/compression-inventory.md` §8); local
single-run wall-clock measurements on synthetic fixtures approximate the audit
shapes; cross-codec ratio/throughput figures come from the upstream Silesia
benchmark tables (LZ4 and zstd READMEs, mirrored sources).

**Limitations (stated explicitly):** no interactive web research was performed
for this run — figures for LZ4 / miniz / minilzo / heatshrink are drawn from
documented offline knowledge and upstream README / license text. Local numbers
are wall-clock, single-run, on synthetic fixtures, and are directional, not
microbenchmarks. No LZ4 CLI was available locally, so LZ4 numbers are
literature values only.

## 2. Data shapes the codecs must serve (audit summary)

Every current call site is **one-shot over a fully buffered blob**
(inventory §3, §8) — there is no streaming call site in v2 or in the C++ main
branch:

| Shape | Call sites (audit §8) | Size | Latency profile |
|---|---|---|---|
| Asset archives (`.resources`, `.cooked`) | whole-archive compress at save/cook; decompress at load | MB-scale | decompress on load (blocking or background) |
| Per-mip texture data | decompress on texture-upload workers; compress at import (RGBA32_FLOAT) | MB-scale per mip | decompress latency-sensitive on upload |
| Font blobs / dependency blobs | one-shot at cache/load | KB–MB | one-shot at load |
| Thumbnails | compress at save, decompress on background threads | KB | background, latency-tolerant |
| Serialized scenes | same shape as `.cooked` archives (one-shot buffered); no dedicated call site today | MB | n/a |
| Network payloads | **no network code exists in v2 today** — future/aspirational | n/a | n/a |

Implications:

- **Decompress latency dominates**: assets, mips, and fonts are decompressed on
  load/upload paths; compress happens at cook/import time and is less
  latency-critical.
- **High-entropy binary caps all codecs**: texture mip data and the binary half
  of asset archives are nearly incompressible; every candidate converges near
  the same ratio, so throughput (especially decompress) is the differentiator
  there.
- Textual/scene/JSON data compresses well (≈5x) and is where ratio differences
  between codecs show up.
- Any codec can be added behind the existing one-shot descriptor surface; the
  deferred streaming slots (`stream_init` etc.) are a transport concern and do
  not change the on-disk one-shot frame format.

## 3. Empirical baseline (local, single-run wall-clock)

Synthetic fixtures approximating the audit shapes (zstd 1.5.5 / gzip / xz /
bzip2 CLIs; `compressed/original` size ratio; wall-clock compress/decompress):

| Fixture | Codec | Ratio | Compress | Decompress |
|---|---|---|---|---|
| scene.json (5.3 MB, serialized-scene shape) | zstd -1 | 0.196 | 0.02 s | 0.01 s |
| scene.json | zstd -3 | 0.206 | 0.03 s | 0.01 s |
| scene.json | zstd -19 | 0.163 | 5.8 s | 0.02 s |
| scene.json | gzip -1 | 0.250 | 0.04 s | 0.03 s |
| scene.json | gzip -9 | 0.193 | 0.24 s | 0.03 s |
| scene.json | xz -1 | 0.191 | 0.29 s | 0.09 s |
| scene.json | bzip2 -9 | 0.125 | 0.47 s | 0.13 s |
| asset.blob (19.2 MB, half-random binary) | zstd -1 | 0.909 | 0.05 s | 0.03 s |
| asset.blob | zstd -3 | 0.909 | 0.08 s | 0.03 s |
| asset.blob | zstd -19 | 0.909 | 7.0 s | 0.03 s |
| asset.blob | gzip -1 | 0.915 | 0.61 s | 0.14 s |
| asset.blob | gzip -9 | 0.911 | 0.77 s | 0.13 s |
| asset.blob | xz -1 | 0.907 | 7.6 s | 0.96 s |
| asset.blob | bzip2 -9 | 0.933 | 1.76 s | 1.25 s |
| small.blob (206 KB, repetitive) | zstd -1 | 0.013 | 0.005 s | 0.006 s |
| small.blob | gzip -1 | 0.017 | — | — |
| small.blob | xz -1 | 0.006 | — | — |
| small.blob | bzip2 -9 | 0.009 | — | — |

Readings:

- On high-entropy binary (asset.blob) all codecs land ≈0.91; zstd decompresses
  ~4–5x faster than gzip and ~30x faster than xz on the same data — throughput,
  not ratio, decides here.
- On textual data (scene.json) zstd -1 already matches gzip -9 ratio (0.196 vs
  0.193) at ~10x less compress time; zstd -19 buys only ~17% more ratio at ~200x
  compress cost — not worth it for cook-time data.
- Small repetitive blobs compress well in every codec; the ratio differences are
  irrelevant at this size.

## 4. Candidate scoring

### 4.1 LZ4 / LZ4-HC — implement LZ4 now, defer LZ4-HC

- **License**: BSD-2-Clause — compatible with the repo's permissive-only,
  in-tree vendoring (same family as the vendored zstd BSD-3-Clause).
- **Portability**: pure C (C90-style core lib + C99/C11 backing), zero
  dependencies, no OS calls; builds unchanged on Windows/Linux/macOS.
- **Vendoring/build cost**: very low — `lib/lz4.c` + `lib/lz4.h` (add
  `lz4hc.*` / `lz4frame.*` / `xxhash.*` only when HC/frame support is enabled);
  one CMake target, no build options.
- **Ratio**: standard LZ4 ≈2.10 on Silesia (literature, r101); LZ4-HC r129
  ≈2.72.
- **Throughput** (literature, Silesia): LZ4 ≈400 MB/s compress / ≈4 GB/s
  decompress; LZ4-HC ≈22 MB/s compress / ≈1.8 GB/s decompress.
- **Fit**: the fastest decompressor in the shortlist — the right tool for
  decompress-latency-dominated asset streaming, per-mip texture upload, and
  thumbnails; also the natural future network codec. LZ4-HC targets
  bake-time/cook-time ratio, which today is served fine by zstd -1/-3.

### 4.2 Zstandard (vendored baseline) — keep as default

- Already in-tree: BSD-3-Clause, pure C, trimmed `thirdparty/zstd/`, gated by
  `SK_COMPRESSION_ZSTD`, wired into the registry as
  `SK_COMPRESSION_CODEC_ZSTD = 1` with allocator injection
  (`ZSTD_customMem`, design §6).
- **Ratio/throughput** (literature, Silesia, zstd 1.5.6 -1): 2.887 at 510 MB/s
  compress / 1580 MB/s decompress; level 1–3 is the operating sweet spot
  confirmed by the local runs.
- Dictionary support exists in the library but is not yet exposed — that is
  feature work on the existing codec, not a new codec.
- Recommendation: unchanged default for archives and cooked data.

### 4.3 zlib / deflate via miniz — implement now

- **License**: MIT; miniz is a completely independent implementation, so zlib's
  (old) advertising-clause licensing requirements do not apply — fully
  compatible with the repo's vendoring policy.
- **Portability**: single-file pair `miniz.c` / `miniz.h`, plain C, no OS deps.
- **Vendoring/build cost**: very low — one source file per target (PNG/archive
  helpers can be stripped with `MINIZ_NO_PNG` / `MINIZ_NO_ARCHIVE_APIS`); one
  CMake target.
- **Ratio**: zlib-class (zlib 1.2.11 -1: 2.743, Silesia literature).
- **Throughput** (literature, Silesia): zlib-class ≈95 MB/s compress /
  ≈400 MB/s decompress — slower than zstd, but the format (RFC 1950 zlib /
  RFC 1951 deflate) is what third-party tools and existing zlib-compressed
  formats speak.
- **Fit**: interop with existing zlib/deflate-based formats (PNG-style
  containers, external tools) and future network framing that must interoperate.
  Low-level `tdefl`/`tinfl` are heap-free with caller-managed state — fits the
  v2 allocator contract cleanly.

### 4.4 Lightweight single-header / LZ77-RLE options

- **minilzo** (LZO): **reject** — GPL-2.0-or-later with a separate commercial
  license. Incompatible with the repo's permissive-only in-tree vendoring
  (AGENTS.md: always keep licenses; the project has no copyleft dependencies).
- **heatshrink**: ISC license; LZSS for embedded/real-time streaming with
  ~50–300 bytes of memory. **Defer** — no embedded / tiny-memory target exists
  in v2; its niche (bounded-memory streaming) is served by LZ4 frame streaming
  when streaming lands.
- **shoco**: MIT but domain-specific (short strings, requires trained character
  models) — not a general-purpose codec. **Reject** for engine use.
- **libdeflate**: MIT and fast DEFLATE, but not single-file; miniz covers the
  zlib-compat niche with less build surface. **Defer/reject** as redundant.

### 4.5 Screened and rejected (heavier)

- **brotli** (MIT): multi-file, large static dictionary; strong ratio but slow
  compress and no current call-site need. Reject for now; revisit only if asset
  sizes demand max-ratio.
- **xz / liblzma**: very strong ratio but slow decompress (≈33 MB/s class) and
  heavier build; zstd already covers the ratio niche at much higher speed.
  Reject.
- **bzip2**: old, slow in both directions, no interop need. Reject.

## 5. Scoring matrix

| Candidate | License | Portability | Vendoring/build cost | Ratio (Silesia) | Throughput C/D | Shape fit | Verdict |
|---|---|---|---|---|---|---|---|
| LZ4 | BSD-2 | pure C, no deps | low (1–3 files) | ≈2.10 | ≈400 / ≈4000 MB/s | asset streaming, mips, future network | **Implement now** |
| LZ4-HC | BSD-2 | pure C, no deps | trivial (same lib) | ≈2.72 | ≈22 / ≈1800 MB/s | cook-time ratio | Defer (glue only) |
| Zstd | BSD-3 (in-tree) | pure C, in-tree | done | 2.887 (-1) | 510 / 1580 MB/s | default for archives/scenes | Keep (baseline) |
| miniz (zlib) | MIT | pure C, single file | low (1 file) | ≈2.74 | ≈95 / ≈400 MB/s | zlib interop, network framing | **Implement now** |
| minilzo | GPL-2.0+ | pure C | low | ≈2.1 | ≈400 / ≈620 MB/s | — | Reject (license) |
| heatshrink | ISC | pure C | low | LZO-class | embedded-class | embedded streaming | Defer |
| shoco | MIT | pure C | low | short strings | fast | — | Reject (domain) |
| libdeflate | MIT | pure C | medium | zlib-class | fast | — | Defer (redundant) |
| brotli | MIT | pure C | medium-high | 3.0–4.0 | slow compress / ≈430 MB/s | — | Reject for now |
| xz / liblzma | PD/0BSD | pure C | high | ≈3.26 | ≈6 / ≈33 MB/s | — | Reject |
| bzip2 | BSD-style | pure C | medium | moderate | slow | — | Reject |

## 6. Recommendation

**Implement now (next codec work):**

1. **LZ4** as `SK_COMPRESSION_CODEC_LZ4 = 2`, behind a `SK_COMPRESSION_LZ4`
   CMake gate. Rationale: fastest decompress of any candidate (the dominant
   metric for asset/mip/font load paths), tiny pure-C BSD-2 vendoring, and the
   natural future network codec. Standard LZ4 level/acceleration covers
   streaming-adjacent needs; the frame API can back the deferred streaming
   slots later.
2. **miniz** as `SK_COMPRESSION_CODEC_ZLIB = 3`, behind a `SK_COMPRESSION_MINIZ`
   CMake gate. Rationale: RFC 1950/1951 compatibility with existing formats and
   third-party tooling, MIT single-file vendoring, and heap-free low-level
   `tdefl`/`tinfl` that fit the v2 allocator contract.

**Defer:**

- **LZ4-HC** — same vendored library; add a descriptor / HC-level mapping when a
  bake-time use case actually needs the extra ratio (measure first; today zstd
  -1/-3 covers cook-time).
- **zstd dictionary mode** — feature work inside the existing codec, not a new
  codec; revisit if small-blob ratio (e.g. thumbnails) justifies a shared
  dictionary.
- **heatshrink** — only if a Tiny/embedded target with bounded-memory streaming
  materializes.
- **Streaming slots** (`stream_init` etc.) — already deferred by design; LZ4
  frame and miniz can implement them without changing the one-shot frame
  format.

**Reject:** minilzo (GPL — license-incompatible with permissive-only
vendoring), shoco (domain-specific), libdeflate (redundant with miniz),
brotli / xz / bzip2 (heavier vendoring/build cost, no current call-site need).

**Escalation:** not required — LZ4 and miniz are lightweight, permissive, and
vendorable in-tree under the repo's existing thirdparty rules; no heavyweight
external dependency is being added.

## 7. Integration notes (for the implementation task)

- **On-disk ids**: add `SK_COMPRESSION_CODEC_LZ4 = 2` and
  `SK_COMPRESSION_CODEC_ZLIB = 3` to `sk_compression_codec_id_t`
  (`core/compression.h`). Members are always defined (ids stay readable in any
  build); only descriptors are gated — never renumber (design §5).
- **Registry**: one `static const sk_compression_codec_t` per codec in
  `core/compression.c`, appended to the build-time `codecs[]` table under
  `#ifdef SK_COMPRESSION_HAS_LZ4` / `SK_COMPRESSION_HAS_MINIZ`.
- **CMake**: root options `SK_COMPRESSION_LZ4` / `SK_COMPRESSION_MINIZ`
  (default ON, mirroring `SK_COMPRESSION_ZSTD`); `thirdparty/CMakeLists.txt`
  gates `add_subdirectory(lz4)` / `add_subdirectory(miniz)`; `core/CMakeLists.txt`
  adds the compile definitions + PRIVATE link in both the `sk-core` and
  `sk-core-tests` blocks.
- **Vendoring**: `thirdparty/lz4/` (keep `lib/lz4.c`, `lib/lz4.h`, `LICENSE`;
  add `lz4hc.*`/`lz4frame.*`/`xxhash.*` only if enabled) and
  `thirdparty/miniz/` (`miniz.c`, `miniz.h`, `LICENSE`, PNG/archive helpers
  stripped). Licenses must be kept per repo rules.
- **Allocator contract**: LZ4 one-shot (`LZ4_compress_default` /
  `LZ4_decompress_safe`) and the `_fast`/`_safe` continue variants are
  heap-free (caller-owned state); avoid `LZ4_createStream` (internal malloc) or
  route its allocation through the injected allocator. miniz one-shot helpers
  (`mz_compress*`) allocate via `MZ_MALLOC`; use the low-level `tdefl`/`tinfl`
  with caller-managed state (allocated through the injected allocator) instead.
  Bounds: `LZ4_compressBound(src_size)` and the zlib formula
  `src + src/16 + 64 + 3` (`mz_compressBound`).
- **Frame format decision** (implementation task): raw LZ4 blocks / miniz
  deflate streams wrapped in the v2 size-header framing, or LZ4 frame / RFC 1950
  zlib streams — pick once per codec for on-disk stability; the registry design
  is agnostic either way.
- **Tests**: in-source `SK_TEST` cases under `#ifdef SK_TESTS` for bounds,
  round-trip, insufficient-output, corrupt-data, and cross-codec interop on the
  same fixture; plus host-level integration coverage per the repo test rules.

## 8. Sources

- LZ4 README benchmark table (Silesia; mirrored at android.googlesource
  external/lz4): LZ4 r101 2.101 / ≈400 / ≈3990 MB/s; LZ4-HC r129 2.720 /
  ≈22 / ≈1830 MB/s.
- zstd README benchmark table (Silesia; zstd 1.5.6): zstd -1 2.887 /
  510 / 1580 MB/s; zlib 1.2.11 -1 2.743 / 95 / 400 MB/s.
- miniz README: MIT, independent zlib implementation (RFC 1950/1951), low-level
  `tdefl`/`tinfl` heap-free with streaming support.
- minilzo license: GPL-2.0-or-later, commercial license separate.
- heatshrink: ISC, embedded/real-time LZSS streaming.
- Local single-run wall-clock runs (zstd/gzip/xz/bzip2 CLIs on synthetic
  fixtures) — §3.
- Repo sources: `core/compression.h`, `core/compression.c`,
  `docs/compression-inventory.md`, `docs/compression-design-v2.md`, and the
  thirdparty CMake gating in the root / `core/` / `thirdparty/` CMakeLists.
