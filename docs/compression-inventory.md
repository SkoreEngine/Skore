# Compression abstraction inventory (`main` C++ engine) — port target v2

Task: APX-158. Branch audited: `main` (C++ engine). Port target: `v2` (C
rewrite). All paths and line numbers below are relative to the `main` branch of
the C++ engine. The abstraction is small and self-contained; the consumers are
concentrated in the resource/asset pipeline (see §8), but the interface itself
is **not** entangled with that subsystem — it is a thin, one-shot wrapper around
Zstandard (§10).

---

## 1. Public interface

Header: `Runtime/Source/Skore/IO/Compression.hpp`

```cpp
namespace Skore
{
    enum class CompressionMode
    {
        None,   // no codec; API returns 0 (see §6)
        ZSTD
    };

    constexpr i32 CompressionDefaultLevel = 3;
}

namespace Skore::Compression
{
    SK_API usize Compress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode, i32 level = CompressionDefaultLevel);
    SK_API usize GetMaxCompressedBufferSize(usize srcSize, CompressionMode mode);
    SK_API usize Decompress(u8* dest, usize descSize, const u8* src, usize srcSize, CompressionMode mode);
    SK_API usize GetMaxDecompressedBufferSize(const u8* src, usize srcSize, CompressionMode mode);
}
```

| Symbol | Kind | Notes |
|---|---|---|
| `CompressionMode` | enum class | `None`, `ZSTD`. Also registered in the reflection system (§2). |
| `CompressionDefaultLevel` | `constexpr i32` | 3 (zstd default level). |
| `Compression::Compress` | free function | One-shot compress; `descSize` is the destination capacity (note: parameter is spelled `descSize`, not `destSize`). Returns bytes written. |
| `Compression::GetMaxCompressedBufferSize` | free function | Upper bound for a compressed buffer of `srcSize` bytes (`ZSTD_compressBound`). |
| `Compression::Decompress` | free function | One-shot decompress. Returns bytes written. |
| `Compression::GetMaxDecompressedBufferSize` | free function | Declared output size of a frame (`ZSTD_getFrameContentSize`); requires the frame header to be present in `src`. |

Types used in the signature come from `Runtime/Source/Skore/Common.hpp`:
`u8` (unsigned char), `i32` (signed int), `usize` (`decltype(sizeof(0))`, i.e.
`size_t`). `SK_API` is `__declspec(dllexport)` on Win64 and
`__attribute__((visibility("default")))` on Linux/macOS; the functions are
exported from the `SkoreRuntime` shared library.

`CompressionMode` is also referenced (as a resource-field type) by
`Runtime/Source/Skore/Graphics/GraphicsResources.hpp:72` and registered as a
resource field in `RegisterGraphicsTypes.cpp:1104`.

## 2. Codecs implemented and how they register

Implementation: `Runtime/Source/Skore/IO/Compression.cpp`

- **Only Zstandard is implemented.** Each wrapper function is a `switch (mode)`
  with a single `case CompressionMode::ZSTD` dispatching to:
  - `ZSTD_compress(dest, descSize, src, srcSize, level)`
  - `ZSTD_compressBound(srcSize)`
  - `ZSTD_decompress(dest, descSize, src, srcSize)`
  - `ZSTD_getFrameContentSize(src, srcSize)`
- **LZ4 is not implemented.** `Compression.cpp` contains commented-out LZ4
  branches (`LZ4_compress_default`, `LZ4_compressBound`, `LZ4_decompress_safe`)
  and a commented `#include "lz4.h"`; there is no LZ4 vendored code, no `LZ4`
  enum value, and no LZ4 call sites.
- `CompressionMode::None` has **no** switch case; it falls through to `return 0`.

There is no runtime codec registry — codec selection is a compile-time `switch`
on the enum. Callers do not "register" codecs; the enum is the only extension
point. The engine's reflection layer knows about the enum via
`Runtime/Source/Skore/IO/RegisterIOTypes.cpp:251-253`, which registers the two
values without display-name strings:

```cpp
auto compressionMode = Reflection::Type<CompressionMode>();
compressionMode.Value<CompressionMode::None>();
compressionMode.Value<CompressionMode::ZSTD>();
```

## 3. One-shot vs streaming

**One-shot only.** The API mirrors the zstd one-shot functions
(`ZSTD_compress` / `ZSTD_decompress`) and nothing else:

- No streaming entry points (`ZSTD_CStream`/`ZSTD_DStream`), no context objects
  (`ZSTD_CCtx`/`ZSTD_DCtx`), no `ZSTD_customMem`, no
  `ZSTD_MULTITHREAD`/`ZSTDMT` usage anywhere outside `ThirdParty`.
- Every call site buffers the full input in memory (byte containers or the
  whole `.resources` archive) and either compresses or decompresses in a single
  call.
- The only sizing helpers are the one-shot bounds helpers (§1).

## 4. Memory allocation and ownership

- The wrapper API is **caller-owned buffers**: `dest`/`src` are raw `u8*`
  spans with explicit sizes; the caller pre-sizes `dest` using
  `GetMaxCompressedBufferSize` / `GetMaxDecompressedBufferSize` and reads the
  returned byte count as the actual result length.
- Callers allocate those buffers with engine containers backed by the engine
  heap allocator (`MemoryGlobals::GetHeapAllocator()`), e.g.
  `Array<u8>`, `ByteBuffer`/`BasicByteBuffer`
  (`Runtime/Source/Skore/Core/ByteBuffer.hpp`), or plain GPU staging buffers.
- **zstd's internal working memory bypasses the engine allocator.** The wrapper
  calls plain `ZSTD_compress`/`ZSTD_decompress`, which allocate their internal
  context via libc `malloc`/`free` (no `ZSTD_customMem`). This is the main
  allocation-convention mismatch to resolve in the v2 port (v2 requires
  allocator-explicit code, see §9).
- Ownership of compressed blobs is transferred by value into resources:
  `SetBuffer`/`SetBlob` copies or wraps the bytes; e.g. fonts store
  `compressedData` + `FontDataUncompressedSize`; importers store compressed
  original bytes plus `OriginalSize`; texture mips store per-mip
  `DataSize`/`UncompressedSize`.

## 5. Error handling

- **Return codes, not exceptions.** The four functions return `usize` byte
  counts. zstd signals failure by returning an error code that encodes a
  negative value (`ZSTD_isError` distinguishes it); the engine wrapper does
  **not** translate or check it.
- **No call site calls `ZSTD_isError`/`ZSTD_getErrorName`** (grep for
  `ZSTD_isError` outside `ThirdParty` returns nothing). In practice every call
  site either ignores the return value (decompression into a pre-sized buffer)
  or stores it as a "size" (compression), so a zstd failure would silently
  produce a garbage size/corrupt blob instead of an error.
- `GetMaxDecompressedBufferSize` does not guard the sentinel results of
  `ZSTD_getFrameContentSize` (`ZSTD_CONTENTSIZE_UNKNOWN` /
  `ZSTD_CONTENTSIZE_ERROR`, both `(size_t)-1`); a bad/unknown frame size flows
  into a `Resize` of the destination.
- `CompressionMode::None` returns `0` for every function (no explicit case).
  Call sites avoid it by branching on `compressionMode == CompressionMode::None`
  before calling.

## 6. Threading / SIMD assumptions

- **No threading primitives and no engine-level threading assumptions.** The
  wrapper is a stateless pure dispatch. The zstd one-shot API is thread-safe
  per call (entropy tables use thread-local state), and the engine relies on
  that:
  - Decompression runs on background `ResourceWorker` threads during texture
    upload (`RenderResourceCache.cpp`, `WorkerLoop` at `:231`, workers spawned
    at `:132`).
  - Compression/decompression runs on editor background-task threads
    (thumbnail generation and imports via `Editor::AddTask`, a thread-pool
    backed scheduler).
  - `Resources::LoadResources` decompresses the whole `.resources` archive on
    the asset-load path.
- **No engine-level SIMD/alignment requirements.** Buffers are plain `u8`
  arrays with no special alignment contract. zstd internally uses runtime
  CPU dispatch; the vendored build compiles as plain C because the CMake glob
  only picks `*.c`/`*.h` — the `huf_decompress_amd64.S` assembly file is
  excluded (see §7), so no hand-written x86 assembly is linked in this build.

## 7. Third-party / vendored compression dependencies and licenses

- **zstd 1.5.6**, vendored at `ThirdParty/zstd/` (full source:
  `src/common`, `src/compress`, `src/decompress`, `src/zstd.h`,
  `src/zstd_errors.h`).
  - License: `ThirdParty/zstd/LICENSE` — **BSD 3-Clause**, Copyright (c) Meta
    Platforms, Inc. and affiliates.
  - Build: `ThirdParty/zstd/CMakeLists.txt` globs `src/*.h src/*.c` into the
    static target `zstd-skore`; registered via `add_subdirectory(zstd)` in
    `ThirdParty/CMakeLists.txt`; linked PRIVATE by `SkoreRuntime`
    (`Runtime/CMakeLists.txt:27`).
  - Build flags: none set — no `ZSTD_MULTITHREAD` (single-threaded build), no
    `ZSTD_STATIC_LINKING_ONLY`, no custom memory hooks, and the
    `huf_decompress_amd64.S` assembly is not compiled (glob limitation).
- **No other compression dependency.** No LZ4, zlib, libdeflate, miniz, etc.
  (Freetype is configured with `FT_DISABLE_ZLIB`/`FT_DISABLE_BZIP2` in
  `ThirdParty/CMakeLists.txt`).

## 8. Every call site in the C++ engine

`Compression.hpp` is included by 10 TUs (9 consumers plus the implementation
`Compression.cpp`); the direct API call sites are:

**Runtime (`SkoreRuntime` shared library)**
- `Runtime/Source/Skore/Resource/Resources.cpp:2317,2321` — `LoadResources`:
  decompress an entire `.resources` archive (size via
  `GetMaxDecompressedBufferSize`, then `Decompress` into `Array<u8>`).
- `Runtime/Source/Skore/Graphics/RenderResourceCache.cpp`
  - `:253` reads `CompressionMode` from the texture resource (no API call).
  - `:377` — texture-upload worker: per-mip decompress into a scratch buffer
    for oversized mips.
  - `:403` — texture-upload worker: per-mip decompress straight into the GPU
    staging buffer.
  - `:2407` — `GetFontCache`: decompress the font-data blob, then shrink the
    buffer to the returned size.
- `Runtime/Source/Skore/Graphics/RegisterGraphicsTypes.cpp:1104` — registers
  `TextureResource::CompressionMode` as an enum resource field (metadata, not
  an API call).
- `Runtime/Source/Skore/IO/RegisterIOTypes.cpp:251-253` — registers the
  `CompressionMode` enum values in reflection (metadata).

**Editor (`SkoreEditor` shared library)**
- `Editor/Source/Skore/Resource/Handlers/TextureHandler.cpp:31,63,72` —
  `GenerateThumbnail`: reads `CompressionMode`, decompresses mip 0 into a
  mapped GPU buffer (runs on an editor background task).
- `Editor/Source/Skore/Resource/ResourceAssets.cpp`
  - `:182-185` — background thumbnail load: decompress thumbnail image data.
  - `:1055` — `LoadDependencyData`: decompress a stored dependency blob.
  - `:1091-1094` — `ApplyIngest`: compress each dependency's bytes.
  - `:1193-1196` — `LoadCookedResource`: decompress a `.cooked` archive.
  - `:1234-1237` — `WriteCookedResource`: compress a `.cooked` archive.
  - `:1525` — cook imported assets: decompress the stored original source.
  - `:1689-1692` — `ReimportWrapperFromFile`: compress the reimported file.
  - `:2035-2038` — `IngestImportedAsset`: compress the imported source file.
  - `:2614-2618` — whole-resource save: compress the `.resources` archive.
  - `:2727-2730` — `UpdateThumbnail`: compress thumbnail image data.
- `Editor/Source/Skore/Resource/Importers/TextureImporter.cpp:291-337` —
  `ProcessTextureAsset`: per-mip compress (only for `RGBA32_FLOAT` textures).
  Note: the compressed buffer is sized with `Resize(totalUncompressedSize)`
  instead of `GetMaxCompressedBufferSize` (`:291`), and `compressedSize`
  (`usize`) is stored into a `u32` resource field (`:321`) — both are latent
  overflow risks for incompressible or large data.
- `Editor/Source/Skore/Resource/Importers/FontImpoter.cpp:164-167` — font
  importer: compress the serialized atlas archive (`BinaryArchiveWriter` data)
  into the font blob.

**Tests**
- `Tests/Source/Editor/ImportedAssetTests.cpp:249-252, 303-306, 487-490` —
  compress small in-memory payloads as test fixture data (no decompression
  assertions on the API itself).

**Not consumed by:** the Dotnet/C# scripting layer, `Player/`, or any other
runtime module (verified by grep for `Compression`/`CompressionMode`/`zstd`
across `Dotnet/`, `Runtime/`, `Editor/`).

## 9. v2 branch survey (port target)

Branch `v2` is the C rewrite (`sk-` prefix public API, CMake multi-target
layout; see `AGENTS.md` in the v2 tree).

**Existing compression code**
- **None.** There is no compression module on v2 and no zstd/zlib/LZ4 vendored
  dependency. The only "compress" hits are unrelated: texture-format comments
  in `plugins/render_device/render_device.h:103,119,126` (BC/ETC/ASTC block
  formats) and upstream docs inside `thirdparty/glfw` /
  `thirdparty/nativefiledialog`.
- Vendored libs on v2: `thirdparty/{unity, mimalloc, glfw, nativefiledialog}`
  — all in-tree with licenses (Unity MIT, mimalloc MIT, GLFW zlib, Native File
  Dialog zlib). A port therefore must **vendor zstd** per the repo's
  thirdparty rules (in-tree source + license, `add_subdirectory`, no package
  managers / FetchContent / submodules).

**Allocator API (v2 house style)**
- `core/allocator.h`: `sk_allocator_t` — a **value/object** table
  (`instance` + `alloc`/`free`/`realloc` function pointers), explicitly
  documented as *not* an `_api_t` module surface. `sk_allocator_get_default()`
  fills one; `sk_allocator_default()` returns a process-lifetime pointer.
  Backend is mimalloc (linked PRIVATE into `sk-core`).
- Containers take the allocator explicitly: `SK_ARRAY(T)` is initialized with
  `sk_array_init(&arr, allocator)` and "never uses malloc/free". The port's
  compression module should accept `const sk_allocator_t*` (or the codec stays
  allocator-agnostic for one-shot ops but zstd's internal state must be fed
  through `ZSTD_customMem` to honor the engine allocator).

**Error-code conventions**
- `i32` status returns: `0` = success, non-zero = failure, documented per
  function (e.g. `core/filesystem.h` `current_dir` returns `i32`; `core/path.h`
  returns written length or a negative status). Enum statuses like
  `sk_file_status_t` for domain states. No exceptions (C), no magic ints, no
  defensive null-police guards.
- The port should expose `i32` codes (0 success) and reserve non-zero values
  for real failures (codec error, corrupt frame, OOM at a known boundary), and
  keep `usize`-returning helpers only for bounds queries.

**Build system**
- CMake + Ninja, `BUILD_TESTING` default ON; `ctest` runs `sk-tests`.
- Targets: `sk-core` (STATIC, PIC), `sk-core-lib` (INTERFACE headers),
  `sk-test` (test registry, links Unity), `sk-core-tests` (core sources with
  `SK_TESTS`), `sk-app`/`sk-app-tests` (host layer), `sk-tests` executable
  (whole-archive host + plugin scan), plugins via `sk_add_plugin` (SHARED,
  statically link `sk-core`, no static twin, `sk_plugin_entry_point` +
  `sk_plugin_run_tests` under `SK_TESTS`).
- House rules enforced at configure time: `sk_embed_type_ids_in_dir`
  (`SK_TYPE_ID` rewriting) and `sk_check_header_isolation` (no OS headers in
  public headers). First-party code builds with `-Werror` +
  `-Wconversion -Wsign-conversion` and clang-tidy
  `-warnings-as-errors=*` — a zstd glue layer must be careful with `usize`↔`u32`
  conversions.
- Tests are in-source (`SK_TEST` under `#ifdef SK_TESTS`); new modules are
  expected to ship unit tests plus plugin/host integration coverage.

## 10. Port-relevant observations (from the audit)

- The C++ abstraction is small enough to port wholesale: 4 free functions + 1
  enum + 1 default level constant. Codec dispatch is a compile-time switch, so
  the v2 port can keep the same shape (`sk_compression_*` free functions or a
  `sk_*_t` codec table) and add error codes without changing the mental model.
- Resolve these gaps in the port: (a) errors are currently invisible (no
  `ZSTD_isError` translation) — v2 wants explicit `i32` failure codes; (b) zstd
  currently uses libc malloc internally — v2 wants `ZSTD_customMem` wired to
  `sk_allocator_t`; (c) `CompressionMode::None` silently returns 0 — v2 should
  define the contract explicitly; (d) the `TextureImporter` buffer sizing and
  `u32` truncation issues should not be carried over.
- v2 has no compression code at all, so the migration is additive: vendor zstd
  (BSD-3-Clause, keep `LICENSE`), add the module to `core/` (or `app/` if it
  needs host backing — it does not; it is a pure utility and belongs in
  `core/`), add `SK_TEST` coverage, and only then port the asset-pipeline call
  sites that the C++ engine already shows are the only consumers.
- Scope note: the interface is cleanly separable from the serialization/asset
  pipeline — the port can land the module + tests independently of any
  resource-system rewrite, so this does **not** expand into a larger subsystem
  rewrite.
