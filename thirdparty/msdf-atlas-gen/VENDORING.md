# Vendoring manifest: msdf-atlas-gen (APX-217)

Survey of the legacy C++ `main` branch (`ThirdParty/msdf-atlas-gen/`) to prepare
vendoring msdf-atlas-gen on `v2`. No code was copied in this task — this file is
the written manifest the vendoring task must follow.

## 1. Upstream identity (verified byte-for-byte against upstream trees)

| Component | Upstream repo | Commit | Date | Version reported upstream |
|---|---|---|---|---|
| msdf-atlas-gen | https://github.com/Chlumsky/msdf-atlas-gen | `77804c2f4108f49eb73491cda9296a2afb714d4c` ("MSDFgen update, MTSDF BMP support") | 2025-07-29 | 1.3.0 (vcpkg.json) |
| msdfgen | https://github.com/Chlumsky/msdfgen | `a4dafbac1c29021613fbc89768963f3a7e2105aa` ("BMP format improvements including alpha support for MTSDF") | 2025-07-29 | 1.12.1 (vcpkg.json / CHANGELOG); sits between tag `v1.12.1` (`6574da1`) and `v1.13` (`1874bcf`) |

- The vendored `msdf-atlas-gen/` directory (54 files) is byte-identical to
  upstream msdf-atlas-gen `77804c2` (`diff -rq` clean).
- The vendored `msdfgen/` directory is byte-identical to upstream msdfgen
  `a4dafba` — which is exactly the msdfgen submodule pin of msdf-atlas-gen
  `77804c2` (checked `.gitmodules`/gitlink). It is the full upstream msdfgen
  repo tree, not a trimmed subset.
- Post-vendoring (commits `a76f352`, `0af4517`): the tree was trimmed per §6 —
  CLI-only/optional `.cpp` TUs were deleted and only the library surface
  remains. The "byte-identical" claims above now apply to the kept subset;
  headers referenced by the umbrella `msdf-atlas-gen.h` were kept even when
  their `.cpp` was dropped.
- Vendored `LICENSE.txt` is byte-identical to upstream `LICENSE.txt` at `77804c2`.
- Do NOT tag along the later upstream msdf-atlas-gen commits
  (`94390ed` "Update to MSDFgen 1.13", `6148900` "Variable font axis addressing"):
  they pair with msdfgen 1.13, not with the pinned msdfgen used on main.

## 2. Exact source paths on `main`

```
ThirdParty/msdf-atlas-gen/CMakeLists.txt      <- skore shim (replaces upstream root
                                                  CMakeLists.txt AND upstream
                                                  msdf-atlas-gen/CMakeLists.txt)
ThirdParty/msdf-atlas-gen/LICENSE.txt         <- msdf-atlas-gen MIT license (upstream file)
ThirdParty/msdf-atlas-gen/msdf-atlas-gen/     <- 54 files, upstream dir verbatim
ThirdParty/msdf-atlas-gen/msdfgen/            <- full upstream msdfgen repo tree:
    core/ (27 .h/.hpp/.cpp), ext/ (4 .cpp/.h pairs), main.cpp (CLI),
    msdfgen.h, msdfgen-ext.h, CMakeLists.txt, cmake/, vcpkg.json,
    CHANGELOG.md, README.md, LICENSE.txt, CMakePresets.json, icon.ico,
    msdfgen.rc, resource.h, build-release.bat, .gitattributes, .gitignore
```

Upstream submodules **not** vendored: `artery-font-format` (compiled out, see §5)
and `msdfgen` (vendored as a plain copy instead of a submodule).

## 3. Consumer on `main` (the only user)

- `Editor/Source/Skore/Resource/Importers/FontImpoter.cpp` (filename has the
  historical typo) — `#include "msdf-atlas-gen.h"` and uses:
  - `msdfgen`: `initializeFreetype`, `loadFontData`, `deinitializeFreetype`,
    `FontMetrics`, `edgeColoringInkTrap`, `BitmapConstRef`
  - `msdf_atlas`: `Charset`, `FontGeometry` (`loadCharset`, `getMetrics`,
    `getKerning`), `GlyphGeometry` (`edgeColoring`, quad bounds, codepoint,
    index, advance), `TightAtlasPacker` (`setPixelRange(2.0)`, `setMiterLimit`,
    `setScale`, `pack`, `getDimensions`), `ImmediateAtlasGenerator<float,float,4,
    mtsdfGenerator>` with `BitmapAtlasStorage`, `GeneratorAttributes`
    (`scanlinePass=true`, `overlapSupport=true`)
- `Editor/CMakeLists.txt`: `target_link_libraries(SkoreEditor PUBLIC ... msdf-atlas-gen ...)`
- `ThirdParty/CMakeLists.txt`: `add_subdirectory(freetype)` +
  `add_library(Freetype::Freetype ALIAS freetype)` + `add_subdirectory(msdf-atlas-gen)`

## 4. Build configuration used on `main`

Root `CMakeLists.txt`: `CMAKE_CXX_STANDARD 20`, `CMAKE_C_STANDARD 11`; Clang gets
`-stdlib=libc++ -std=c++20`, GCC gets `-static`, non-Windows gets `-fPIC`.

Shim `ThirdParty/msdf-atlas-gen/CMakeLists.txt` (must be reproduced on v2):

```cmake
file(GLOB_RECURSE MSDFGEN_SRC msdfgen/core/*.{h,hpp,cpp} msdfgen/ext/*.{h,hpp,cpp})
add_library(msdfgen STATIC ${MSDFGEN_SRC})
target_link_libraries(msdfgen PRIVATE freetype)
target_compile_definitions(msdfgen PUBLIC MSDFGEN_PUBLIC=)
target_include_directories(msdfgen PUBLIC msdfgen)

file(GLOB_RECURSE MSDF_ATLAS_GEN_SRC msdf-atlas-gen/*.{h,hpp,cpp,c})
add_library(msdf-atlas-gen STATIC ${MSDF_ATLAS_GEN_SRC})
target_compile_definitions(msdf-atlas-gen PUBLIC MSDF_ATLAS_NO_ARTERY_FONT=1)
target_include_directories(msdf-atlas-gen PUBLIC msdf-atlas-gen)
target_link_libraries(msdf-atlas-gen PUBLIC msdfgen)
```

Required compile definitions (the complete set on main):
- `MSDFGEN_PUBLIC=` on `msdfgen` (empty DLL-export macro; headers default it anyway)
- `MSDF_ATLAS_NO_ARTERY_FONT=1` on `msdf-atlas-gen`
- Nothing else. No `MSDF_ATLAS_*` version defines, no `MSDFGEN_USE_*` defines.

Corrections recorded while wiring the v2 build (APX-224):
- `find_package(Threads REQUIRED)` + `target_link_libraries(msdf-atlas-gen
  PUBLIC msdfgen Threads::Threads)` must be in the v2 shim itself. §5's note
  that "v2 core links Threads::Threads PUBLIC" is true but insufficient:
  thirdparty targets are self-contained and must not reach into core's link
  interface.
- The final v2 shim (post-trim) lists sources explicitly instead of globbing:
  19 msdfgen TUs (`core/*` + `ext/import-font.cpp`) and 10 msdf-atlas-gen TUs
  (`Charset`, `FontGeometry`, `GlyphGeometry`, `Padding`, `RectanglePacker`,
  `TightAtlasPacker`, `Workload`, `bitmap-blit`, `glyph-generators`,
  `size-selectors`). This matches the kept tree exactly (`diff` of CMake
  source list vs. tree is clean) and avoids globbing stale headers.
- v2 conventions add `_CRT_SECURE_NO_WARNINGS` + `_CRT_NONSTDC_NO_WARNINGS`
  (PRIVATE, WIN32 only) and `-fvisibility=hidden` / `CXX_VISIBILITY_PRESET
  hidden` (Clang/GNU) — the latter two also make the `MSDFGEN_PUBLIC=` and
  default-visibility export macros irrelevant for the static lib.
- API note (verified by the smoke check): `TightAtlasPacker::setPixelRange`
  takes a symmetric `msdfgen::Range` — `Range(2.0)` = [-1, 1] is the correct
  2-px field; `Range(2.0, 2.0)` produces a zero-span interval (degenerate
  field). The main-branch consumer's `setPixelRange(2.0)` is correct.

Notes:
- `msdfgen` links `freetype` **PRIVATE**; the freetype include dirs are not
  needed by consumers (ext/import-font.h does not include freetype headers).
- `msdf-atlas-gen` links `msdfgen` PUBLIC (include dir + transitive link).
- `msdf-atlas-gen/main.cpp` is wrapped in `#ifdef MSDF_ATLAS_STANDALONE`, so
  globbing it into the static lib produces an empty TU (upstream builds it only
  for the CLI).
- `Workload.cpp` uses `std::thread` → needs `Threads::Threads` at final link
  (v2 core already links it PUBLIC).
- msdfgen is C++11-compatible; on main it compiles as C++20 via the global
  standard. On v2 set an explicit per-target `CXX_STANDARD` (17 or 20).

## 5. Transitive dependencies actually required

| Dependency | Required? | Where | Already on v2? |
|---|---|---|---|
| freetype | YES — msdfgen `ext/import-font.cpp` (`FreetypeHandle`, `loadFontData`) | `ThirdParty/freetype` on main (upstream FreeType build, FT_DISABLE_ZLIB/BZIP2/PNG/HARFBUZZ/BROTLI set in ThirdParty/CMakeLists.txt) | **YES** — `thirdparty/freetype`, FreeType 2.13.3, project-owned CMake, static+PIC, bundled zlib, other deps off. Target name `freetype` (same as main; consumed by `plugins/ui`). The shim's `PRIVATE freetype` link carries over unchanged. |
| artery-font-format | NO — upstream submodule, compiled out via `MSDF_ATLAS_NO_ARTERY_FONT=1` (`artery-font-export.cpp/.h` and CLI export paths are `#ifndef`-guarded) | not vendored on main | n/a — must stay compiled out |
| tinyxml2 | NO — referenced only by msdfgen `ext/import-svg.cpp` behind `MSDFGEN_USE_TINYXML2` (never defined on main) | — | not needed |
| libpng | NO — referenced only behind `MSDFGEN_USE_LIBPNG` (msdfgen `ext/save-png.cpp`, msdf-atlas-gen `image-encode.cpp`); never defined on main; `image-encode.cpp` falls back to BMP-only | — | not needed |
| Skia | NO — `MSDFGEN_USE_SKIA` never defined; msdfgen `ext/resolve-shape-geometry.cpp` and atlas `GlyphGeometry.cpp` Skia blocks compile out | — | not needed |
| LodePNG | NO — `MSDFGEN_USE_LODEPNG` never defined, not vendored | — | not needed |
| Threads | YES (link-time only) — `Workload.cpp` (`std::thread`) | implicit on main | v2 core links `Threads::Threads` PUBLIC; the v2 shim nevertheless does its own `find_package(Threads REQUIRED)` + PUBLIC `Threads::Threads` (thirdparty targets must be self-contained, see §4 corrections) |
| OpenMP / vcpkg / standalone CLI | NO — upstream options, all off/unused on main | — | — |

## 6. Optional features to compile out (minimize vendored surface)

Keep the *library* build (used by FontImporter) and drop everything CLI-only.
Files below are not referenced by `FontImporter.cpp` and can be omitted from the
static lib / deleted from the vendored tree (headers may stay if the umbrella
`msdf-atlas-gen.h` is kept intact; otherwise trim the umbrella too):

- `msdf-atlas-gen/main.cpp` — CLI entry, empty TU without `MSDF_ATLAS_STANDALONE`
- `msdf-atlas-gen/charset-parser.cpp`, `csv-export.{h,cpp}`, `json-export.{h,cpp}`,
  `shadron-preview-generator.{h,cpp}`, `size-selectors.{h,cpp}` — CLI-only
- `msdf-atlas-gen/GridAtlasPacker.{h,cpp}` — CLI `-uniformgrid` mode only
- `msdf-atlas-gen/DynamicAtlas.{h,hpp}` — runtime dynamic-atlas API, unused
- `msdf-atlas-gen/image-encode.{h,cpp}`, `image-save.{h,hpp}` — PNG/BMP file
  output, used only by the CLI (BMP-only build without libpng/lodepng)
- `msdf-atlas-gen/artery-font-export.{h,cpp}` — already excluded by
  `MSDF_ATLAS_NO_ARTERY_FONT=1`
- `msdf-atlas-gen/utf8.{h,cpp}` — used only by `charset-parser.cpp`
- `msdf-atlas-gen/utils.hpp` — header-only helpers for GridAtlasPacker/DynamicAtlas

> Correction (found while wiring the v2 build): `size-selectors.cpp` is NOT
> CLI-only — `TightAtlasPacker.cpp` calls `packRectangles<SquarePowerOfTwoSizeSelector>`
> / `<PowerOfTwoSizeSelector>` / `<SquareSizeSelector<4|2|1>>`, and the
> non-template member definitions of the first two live in `size-selectors.cpp`.
> It must stay in the library build.

Required library sources (keep): `AtlasGenerator.h`, `AtlasStorage.h`,
`BitmapAtlasStorage.{h,hpp}`, `Charset.{h,cpp}`, `FontGeometry.{h,cpp}`,
`GlyphBox.h`, `GlyphGeometry.{h,cpp}`, `ImmediateAtlasGenerator.{h,hpp}`,
`Padding.{h,cpp}`, `Rectangle.h`, `RectanglePacker.{h,cpp}`,
`rectangle-packing.{h,hpp}`, `Remap.h`, `TightAtlasPacker.{h,cpp}`,
`Workload.{h,cpp}`, `bitmap-blit.{h,cpp}`, `glyph-generators.{h,cpp}`,
`msdf-atlas-gen.h`, `types.h`.

msdfgen: `core/` (all of it) + `ext/import-font.{h,cpp}` are required.
Optionally drop (CLI-only, self-contained): `core/export-svg.cpp`,
`core/sdf-error-estimation.cpp`, `core/save-fl32.cpp`, `core/save-rgba.cpp`,
`core/save-tiff.cpp`, `core/save-bmp.cpp` (used only via `image-save.hpp` by the
CLI), `ext/import-svg.{h,cpp}`, `ext/save-png.{h,cpp}`,
`ext/resolve-shape-geometry.{h,cpp}` (no-op without Skia), and the standalone
CLI files (`main.cpp`, `msdfgen.rc`, `icon.ico`, `resource.h`).
If `ext/import-svg.cpp` is kept, compile with `MSDFGEN_DISABLE_SVG=1` (upstream
msdf-atlas-gen sets this itself to avoid the tinyxml2 dependency).

## 7. Licenses that must travel with the code

- `msdf-atlas-gen/LICENSE.txt` — MIT, Copyright (c) 2020–2025 Viktor Chlumsky (required)
- `msdfgen/LICENSE.txt` — MIT, Copyright (c) 2014–2025 Viktor Chlumsky (required)
- `thirdparty/freetype/LICENSE.TXT` — FreeType License / GPLv2 dual (already present on v2)
- No other licenses needed: tinyxml2, libpng, Skia, LodePNG, artery-font-format
  are neither vendored nor used.

## 8. v2 `thirdparty/` layout and CMake conventions (the next task must follow)

- `thirdparty/CMakeLists.txt` — one folder per lib; each lib registered with
  `add_subdirectory(<name>)` plus a short comment; `thirdparty` is added from the
  root `CMakeLists.txt` (~line 92) *before* first-party targets so vendored code
  is exempt from the warnings-as-errors flags and clang-tidy (first-party only).
- Per-lib conventions (see `thirdparty/freetype`, `thirdparty/stb_rect_pack`,
  `thirdparty/mimalloc`):
  - project-owned `CMakeLists.txt` (not the upstream build), static library
  - `POSITION_INDEPENDENT_CODE ON` (plugins are SHARED modules)
  - `target_include_directories(<name> SYSTEM PUBLIC <src dir>)`
  - C libs: `C_STANDARD 99`, `C_STANDARD_REQUIRED ON`, `C_EXTENSIONS OFF`
  - `-fvisibility=hidden` for AppleClang/Clang/GNU
  - compile definitions PRIVATE where possible (see freetype's
    `FT_CONFIG_OPTION_USE_ZLIB` etc.)
  - license file at the lib root: `LICENSE`, `LICENSE.txt` or `LICENSE.TXT`
  - source tree kept as upstream files, unmodified
- Target naming: plain lowercase (`freetype`, `stb_rect_pack`, `yyjson`,
  `mimalloc-static`, `volk`, `vma`, `vulkan-sdk`); first-party targets use the
  `sk-` prefix. No `Foo::Foo` aliases on v2 (main used `Freetype::Freetype`
  only for RmlUi).
- `thirdparty/.clang-format` has `DisableFormat: true` — vendored sources are
  never reformatted.
- C++ on v2: project is C-first, but the toolchain already builds C++ (precedent:
  `plugins/vulkan_render_device/vulkan_vma.cpp`, compiled as a separate STATIC
  C++ TU and linked into the shared plugin; global `CMAKE_CXX_FLAGS` add
  `-std=c++20` for Clang and `-static` for GCC). msdf-atlas-gen/msdfgen should
  set an explicit per-target `CXX_STANDARD` (17 or 20) and `POSITION_INDEPENDENT_CODE ON`.

## 9. Recommended v2 layout (for the vendoring task)

```
thirdparty/msdf-atlas-gen/
    CMakeLists.txt        # project-owned shim mirroring §4 (freetype PRIVATE,
                          #   MSDF_ATLAS_NO_ARTERY_FONT=1, MSDFGEN_PUBLIC=)
    LICENSE.txt           # msdf-atlas-gen MIT (from upstream 77804c2)
    msdf-atlas-gen/       # upstream 77804c2 sources, trimmed per §6
    msdfgen/
        LICENSE.txt       # msdfgen MIT (from upstream a4dafba)
        core/ ext/        # trimmed per §6
```

The v2 consumer point already exists: `core/resource_asset_builtins.c` creates a
"FontResource shell" with MSDF atlas generation deferred, and `plugins/ui` links
`freetype` today.

## 10. Verification on v2 (APX-224)

Status: the vendored build is wired and verified. `thirdparty/CMakeLists.txt`
registers `add_subdirectory(msdf-atlas-gen)`; the shim builds `msdfgen` and
`msdf-atlas-gen` as static libs with the §4 flags, `Threads::Threads` PUBLIC,
`CXX_STANDARD 20`, PIC, hidden visibility, and MSVC CRT define suppressions.

- **Clean build**: both targets compile warning-free on GCC 13 (Linux) in
  Debug and Release, and the full `skore` tree builds with the shim in place
  (first-party `-Werror` + clang-tidy pass; thirdparty is exempt). MSVC and
  AppleClang were not run here; the shim's MSVC defines and AppleClang
  visibility guards follow the §8 conventions used by the other vendored libs.
- **Smoke check** (`tests/msdf_atlas_smoke.cpp`, ctest `sk-msdf-atlas-smoke`):
  loads `plugins/ui/testdata/skore_test_font.ttf` via FreeType, loads all 38
  glyphs, edge-colors, packs with `TightAtlasPacker` (2-px symmetric range,
  power-of-two-square, 32×32), and generates an MTSDF atlas with
  `ImmediateAtlasGenerator<float,4,mtsdfGenerator,BitmapAtlasStorage>` using a
  2-thread `Workload`. It asserts the atlas bitmap (non-null pixels, correct
  dimensions, non-zero content) and the layout array (one `GlyphBox` per
  loaded glyph, all boxes inside the atlas), and dumps the artifacts
  `msdf_atlas_smoke.raw` + `msdf_atlas_smoke_layout.txt` next to the test
  binary. This exercises the exact library pipeline the engine's font
  importer uses on main (§3).
- **Link reality**: the smoke executable links only `msdf-atlas-gen` +
  `freetype` explicitly — `msdfgen` and `Threads::Threads` arrive via the
  PUBLIC/`LINK_ONLY` interfaces, confirming §4/§5's dependency graph.

Manifest inaccuracies found and corrected while building:
1. §5's "v2 core links Threads::Threads PUBLIC" is true but not sufficient for
   a self-contained thirdparty target — the shim must (and does) call
   `find_package(Threads REQUIRED)` itself (corrected in §4/§5).
2. §4's "complete set of compile definitions" was complete for the library
   semantics but v2 conventions additionally add the two MSVC CRT
   define-suppressions (PRIVATE, WIN32) — recorded in §4.
3. §4's GLOB-based shim no longer describes the final v2 shim: after the §6
   trim the shim lists the 19 + 10 TUs explicitly (verified to match the tree
   exactly). Recorded in §4.
4. §1's "byte-identical" claims now apply to the post-trim kept subset, not
   the original 54-file directory — recorded in §1.
5. API-usage pitfall (not an inaccuracy, but worth recording):
   `TightAtlasPacker::setPixelRange` wants a symmetric `msdfgen::Range`;
   `Range(2.0)` = [-1, 1]. A `{2,2}` range yields a zero-span field. See §4.
