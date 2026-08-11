# msdf-atlas-gen (vendored) — README

This directory vendors the **msdf-atlas-gen** glyph-atlas library and its
**msdfgen** dependency (both MIT-licensed, by Viktor Chlumsky), plus a
project-owned **C99 wrapper** (`c_api/`) that exposes the C++ pipeline through
a stable C API. This README is the single authoritative document for the
vendored code: provenance, licenses, build options, API usage, and ownership
rules. The earlier `VENDORING.md` was folded into this document and now only
points here.

Contents:

1. [What is vendored here](#1-what-is-vendored-here)
2. [Upstream provenance](#2-upstream-provenance)
3. [Vendored dependencies and licenses](#3-vendored-dependencies-and-licenses)
4. [Build options and CMake targets](#4-build-options-and-cmake-targets)
5. [C API usage — annotated example](#5-c-api-usage--annotated-example)
6. [Ownership and error-handling rules](#6-ownership-and-error-handling-rules)
7. [Engine integration (intentionally out of scope)](#7-engine-integration-intentionally-out-of-scope)
8. [Verification](#8-verification)

## 1. What is vendored here

```
thirdparty/msdf-atlas-gen/
    CMakeLists.txt        # project-owned build shim (NOT upstream's build)
    LICENSE.txt           # msdf-atlas-gen MIT license (upstream file)
    README.md             # this document
    VENDORING.md          # stub; superseded by README.md
    msdf-atlas-gen/       # upstream msdf-atlas-gen sources (trimmed, see §2)
    msdfgen/              # upstream msdfgen sources (trimmed, see §2)
        LICENSE.txt       # msdfgen MIT license (upstream file)
    c_api/
        include/msdf_atlas_c.h   # public C99 API header (the API to use)
        src/msdf_atlas_c.cpp     # C++ wrapper implementing the header
        tests/msdf_atlas_c_smoke.c  # C99 smoke test (see §8)
```

The C++ libraries are vendored verbatim (kept files are byte-identical to
upstream) and are *not* meant to be consumed directly by the engine; the
stable, documented entry point is the C header `<msdf_atlas_c.h>`. C++
consumers (e.g. the legacy `sk-msdf-atlas-smoke` test) may still use
`msdf-atlas-gen.h` / `msdfgen.h` directly.

## 2. Upstream provenance

| Component | Upstream repo | Commit | Date | Version |
|---|---|---|---|---|
| msdf-atlas-gen | <https://github.com/Chlumsky/msdf-atlas-gen> | `77804c2f4108f49eb73491cda9296a2afb714d4c` ("MSDFgen update, MTSDF BMP support") | 2025-07-29 | 1.3.0 (vcpkg.json) |
| msdfgen | <https://github.com/Chlumsky/msdfgen> | `a4dafbac1c29021613fbc89768963f3a7e2105aa` ("BMP format improvements including alpha support for MTSDF") | 2025-07-29 | 1.12.1 (vcpkg.json / CHANGELOG) |

Where it was copied from:

- Both trees were copied from the upstream GitHub repositories at the pinned
  commits above into `thirdparty/msdf-atlas-gen/` (skore v2). The vendored
  `msdfgen/` is exactly the msdfgen submodule pin of msdf-atlas-gen
  `77804c2` (verified via upstream `.gitmodules`/gitlink).
- After vendoring, the trees were trimmed to the library surface: CLI-only
  and optional `.cpp` TUs were deleted; umbrella headers that reference them
  were kept for include compatibility. "Byte-identical" therefore applies to
  the kept subset, not the original upstream directory. The exact TU list is
  in `CMakeLists.txt` (explicit, not globbed, and verified to match the
  tree).
- Trimmed away: msdf-atlas-gen CLI-only files (`main.cpp`, charset-parser,
  csv/json export, shadron preview, utf8, utils.hpp) and msdfgen CLI/optional
  files (export-svg, sdf-error-estimation, save-fl32/rgba/tiff/bmp,
  import-svg, save-png, resolve-shape-geometry, standalone CLI files).
  `size-selectors.cpp` is *required* (TightAtlasPacker instantiates its
  selectors); `GridAtlasPacker.cpp` was restored because the C API exposes
  grid packing.
- Upstream submodules **not** vendored: `artery-font-format` (compiled out,
  see §4) and `msdfgen` (vendored as a plain copy, not a submodule).
- **Do not** upgrade to later upstream msdf-atlas-gen commits (`94390ed`
  "Update to MSDFgen 1.13", `6148900` "Variable font axis addressing"): they
  pair with msdfgen 1.13, not with the pinned msdfgen here.

## 3. Vendored dependencies and licenses

| Dependency | Required? | License | Where |
|---|---|---|---|
| msdf-atlas-gen | yes | MIT, Copyright (c) 2020–2025 Viktor Chlumsky | `LICENSE.txt` (byte-identical to upstream `77804c2`) |
| msdfgen | yes | MIT, Copyright (c) 2014–2025 Viktor Chlumsky | `msdfgen/LICENSE.txt` |
| freetype | yes (msdfgen `ext/import-font.cpp` needs it) | FreeType License / GPLv2 dual | `thirdparty/freetype/LICENSE.TXT` — FreeType 2.13.3, already vendored on v2, *reused* not duplicated; linked PRIVATE |
| Threads (`std::thread` in `Workload.cpp`) | link-time only | system library, nothing vendored | found via `find_package(Threads REQUIRED)` |

Compiled out / deliberately **not** vendored (upstream optional features):
`artery-font-format` (upstream submodule, disabled via
`MSDF_ATLAS_NO_ARTERY_FONT=1`), `tinyxml2` (SVG import, never enabled),
`libpng` (BMP-only fallback), `LodePNG`, `Skia`, OpenMP, and the upstream
CLI. No license obligations arise from these.

## 4. Build options and CMake targets

The project-owned `CMakeLists.txt` builds three **static** libraries (not the
upstream build systems):

| Target | Contents | Include dir (SYSTEM PUBLIC) | Public link |
|---|---|---|---|
| `msdfgen` | msdfgen core + `ext/import-font.cpp` | `msdfgen/` | `freetype` (PRIVATE) |
| `msdf-atlas-gen` | msdf-atlas-gen library TUs (incl. `size-selectors.cpp`, `GridAtlasPacker.cpp`) | `msdf-atlas-gen/` | `msdfgen`, `Threads::Threads` (PUBLIC) |
| `msdf-atlas-c` | C99 wrapper `c_api/src/msdf_atlas_c.cpp` | `c_api/include/` | `msdf-atlas-gen` (PUBLIC) |

Consuming: add `add_subdirectory(thirdparty/msdf-atlas-gen)` (or the parent
`thirdparty`), then link `msdf-atlas-c` for the C API or `msdf-atlas-gen`
for the C++ API. `msdfgen` and `Threads::Threads` arrive transitively; a
consumer only needs to link what it includes headers from.

Compile definitions (set by the shim; consumers do not need to set any):

- `MSDF_ATLAS_NO_ARTERY_FONT=1` on `msdf-atlas-gen` (PUBLIC) — compiles out
  the non-vendored artery-font-format paths.
- `MSDFGEN_PUBLIC=` on `msdfgen` (PUBLIC) — empty DLL-export macro (headers
  default it anyway).
- `MSDF_ATLAS_C_STATIC=1` on `msdf-atlas-c` (PUBLIC) — consumers of this
  static library never see `dllimport`/`dllexport`; the header's
  `visibility("default")` still exports the API symbols from
  hidden-visibility objects so the wrapper can be linked into shared plugins.

Build conventions (v2 thirdparty rules, applied to all three targets):
`CXX_STANDARD 20` (required, no extensions), `POSITION_INDEPENDENT_CODE ON`,
`-fvisibility=hidden` / `CXX_VISIBILITY_PRESET hidden` (Clang/GNU/AppleClang),
and on WIN32 `_CRT_SECURE_NO_WARNINGS` + `_CRT_NONSTDC_NO_WARNINGS` (PRIVATE).
Vendored sources are never reformatted (`thirdparty/.clang-format` has
`DisableFormat: true`).

Optional build switch:

- `BUILD_TESTING` (root option, default ON) additionally builds the
  `msdf-atlas-c-smoke` executable and registers ctest tests (see §8). The
  C++ smoke `sk-msdf-atlas-smoke` lives in `tests/` and is always registered
  when testing is enabled.

The upstream `MSDF_ATLAS_STANDALONE` CLI, and all upstream CMake options
(OpenMP, vcpkg, PNG/SVG/Skia), are intentionally not reproduced.

## 5. C API usage — annotated example

This example mirrors the smoke test's main pipeline: load font → configure →
generate → read glyph layout → destroy. It is a complete C99 program; compile
it against `<msdf_atlas_c.h>` and link `msdf-atlas-c`.

```c
/* atlas_example.c — build with CMake: target_link_libraries(app PRIVATE msdf-atlas-c)
 * or by hand: cc -std=c99 atlas_example.c -I thirdparty/msdf-atlas-gen/c_api/include \
 *   -L <build>/thirdparty/msdf-atlas-gen -lmsdf-atlas-c -lmsdf-atlas-gen -lmsdfgen \
 *   -lfreetype -pthread -o atlas_example */
#include <msdf_atlas_c.h>

#include <stdio.h>

/* Every fallible call returns msdf_atlas_error_t; MSDF_ATLAS_OK == 0. */
static int die(const char *what, msdf_atlas_error_t err) {
    fprintf(stderr, "%s failed: %d (%s) — %s\n", what, (int) err,
            msdf_atlas_error_string(err),
            msdf_atlas_last_error_message() ? msdf_atlas_last_error_message() : "(no detail)");
    return 1;
}
#define TRY(call) do { msdf_atlas_error_t e_ = (call); if (e_ != MSDF_ATLAS_OK) return die(#call, e_); } while (0)

int main(int argc, char **argv) {
    const char *font_path = argc > 1 ? argv[1] : "skore_test_font.ttf";
    msdf_atlas_font_t      *font      = NULL;
    msdf_atlas_charset_t   *charset   = NULL;
    msdf_atlas_glyphset_t  *set       = NULL;
    msdf_atlas_packer_t    *packer    = NULL;
    msdf_atlas_generator_t *generator = NULL;
    msdf_atlas_config_t     config;

    /* 1. Configure. Start from msdf_atlas_config_default() (it sets
     *    struct_size, which every entry point validates), then override. */
    TRY(msdf_atlas_config_default(&config));
    config.image_type              = MSDF_ATLAS_IMAGE_MTSDF;   /* RGBA float distance field */
    config.dimensions_constraint   = MSDF_ATLAS_DIMENSIONS_POWER_OF_TWO_SQUARE;

    /* 2. Load the font (TTF/OTF; FreeType-backed). The file is read eagerly. */
    TRY(msdf_atlas_font_open(font_path, &font));

    /* 3. Pick the glyphs: the 95 printable ASCII characters. */
    TRY(msdf_atlas_charset_create_ascii(&charset));

    /* 4. Load glyph geometry into a glyphset. The font must outlive the
     *    glyphset. MSDF_ATLAS_LOAD_KERNING enables kerning queries. */
    TRY(msdf_atlas_glyphset_create(&set));
    TRY(msdf_atlas_glyphset_load_charset(set, font, 1.0 /* ems */, charset,
                                         MSDF_ATLAS_LOAD_KERNING, NULL));
    /* Edge coloring is required for good MSDF/MTSDF quality. */
    TRY(msdf_atlas_glyphset_edge_color(set, MSDF_ATLAS_EDGE_COLORING_INKTRAP,
                                       3.0 /* angle threshold, degrees */, 0 /* seed */));

    /* 5. Pack: compute the atlas layout and write placement back into the
     *    glyphset. Dimensions are auto-chosen (subject to the constraint). */
    TRY(msdf_atlas_packer_create(&config, &packer));
    TRY(msdf_atlas_packer_pack(packer, set));

    /* 6. Generate the atlas bitmap (synchronous; uses worker threads). */
    TRY(msdf_atlas_generator_create(&config, &generator));
    TRY(msdf_atlas_generator_generate(generator, set));

    /* 7. Read the results. Bitmap pixels and the layout array are owned by
     *    the generator: never free them, and copy what you need before the
     *    next generate()/resize()/destroy() on the generator. */
    msdf_atlas_bitmap_t bitmap;
    const msdf_atlas_glyph_layout_t *layouts = NULL;
    size_t layout_count = 0;
    TRY(msdf_atlas_generator_get_bitmap(generator, &bitmap));
    TRY(msdf_atlas_generator_get_layout_all(generator, &layouts, &layout_count));
    printf("atlas %dx%d, %d channel(s), %zu glyph(s), first glyph at (%d,%d) %dx%d\n",
           bitmap.width, bitmap.height, bitmap.channel_count, layout_count,
           layouts[0].atlas_x, layouts[0].atlas_y, layouts[0].atlas_w, layouts[0].atlas_h);

    /* 8. Destroy everything. Destroy calls accept NULL (no-op). Destroy the
     *    font after the glyphset that was loaded from it. */
    TRY(msdf_atlas_generator_destroy(generator));
    TRY(msdf_atlas_packer_destroy(packer));
    TRY(msdf_atlas_glyphset_destroy(set));
    TRY(msdf_atlas_charset_destroy(charset));
    TRY(msdf_atlas_font_destroy(font));
    return 0;
}
```

Pipeline notes:

- The full pipeline is **font → charset → glyphset (load, edge color) →
  packer (pack) → generator (generate) → bitmap + layout**. A glyphset can
  be loaded, packed and generated repeatedly; each load replaces the
  previously loaded glyphs, and re-packing overwrites the placement.
- `msdf_atlas_glyphset_get_layout()` / `get_advance()` give per-glyph data
  (plane bounds and advance right after load; atlas placement after pack).
  The generator's layout array (same `msdf_atlas_glyph_layout_t` POD) is the
  snapshot to ship to a renderer: one entry per glyph, with codepoint, glyph
  index, advance, plane bounds, and atlas rect.
- Defaults (from `msdf_atlas_config_default()`): MSDF image type (→ RGB32F
  bitmap), tight packing, bottom-up rows, pixel range `{2.0, 2.0}` (a 2-px
  field), miter limit 1.0, auto atlas size. `<= 0` on any dimension/scale/
  count field means "auto". `msdf_atlas_config_validate()` checks a config
  before you use it.
- Charset syntax for `msdf_atlas_charset_parse()`: bare chars with dash
  ranges (`"a-z0-9"`), `U+XXXX` (`"U+0041-U+005A"`), quoted chars/strings
  (`"'A'"`, `"\"xyz\""`), and bracketed lists (`"['A', 'Z']"`, `"[0x61,
  0x63]"`). Parsing is atomic: a syntax error leaves the charset unchanged
  and returns `MSDF_ATLAS_ERROR_CHARSET_PARSE`.
- One C++-API pitfall recorded during vendoring: in the underlying C++
  `TightAtlasPacker`, `setPixelRange` takes a `msdfgen::Range` where the
  single-argument form `Range(2.0)` means the symmetric width [-1, 1]. The C
  API sidesteps this: `msdf_atlas_range_t` is passed through as explicit
  `{lower, upper}` endpoints, with `{2.0, 2.0}` as the default pixel range.

## 6. Ownership and error-handling rules

The complete contract is documented on the public header itself
(`c_api/include/msdf_atlas_c.h`); the rules, in short:

- **Handles.** `msdf_atlas_font_t`, `msdf_atlas_charset_t`,
  `msdf_atlas_glyphset_t`, `msdf_atlas_packer_t`, `msdf_atlas_generator_t`
  are opaque. Each is created by an explicit create/open function and
  released by the matching destroy function. A destroyed handle must not be
  used again; destroying a handle never frees data owned by a different
  handle. Destroy functions accept NULL (no-op, `MSDF_ATLAS_OK`).
- **Errors.** Every fallible call returns `msdf_atlas_error_t`
  (`MSDF_ATLAS_OK == 0`). `msdf_atlas_error_string(code)` maps a code to
  static text (never NULL, never free it). `msdf_atlas_last_error_message()`
  returns a detail message recorded by the most recent *failing* call on the
  calling thread; it is NULL after a success, thread-local, owned by the
  library, and valid until the next `msdf_atlas_*` call on that thread.
- **Returned buffers.** Pointers returned by the API (bitmap pixels, layout
  array, error strings) are owned by the object that returned them, are
  never freed by the caller, and are invalidated by the next mutating call
  on that object or by destroying it. Copy data out before the next call if
  you need it longer. The one exception is the buffer passed to
  `msdf_atlas_font_open_memory()`: the caller keeps ownership and the buffer
  must outlive the font handle.
- **Nullability.** NULL out-parameters are errors
  (`MSDF_ATLAS_ERROR_INVALID_ARGUMENT`) unless documented otherwise; input
  handles must be non-NULL. Config pointers may be NULL where documented
  (`msdf_atlas_packer_create`, `msdf_atlas_generator_create` → defaults).
- **State.** Some calls are only valid in a given object state and return
  `MSDF_ATLAS_ERROR_INVALID_STATE`: pack/generate on an empty glyphset,
  grid-only setters on a tight packer, advance queries without kerning
  loaded, generate on an unpacked glyphset, style changes via
  `msdf_atlas_packer_apply_config`.
- **Lifetimes.** The font must outlive any glyphset loaded from it; the
  glyphset must outlive a pack/generate that reads it. Otherwise handles are
  independent and can be destroyed in any order.
- **Threading.** Handles are not thread-safe: a single handle must not be
  used concurrently from multiple threads. `msdf_atlas_generator_generate()`
  may use internal worker threads (see `config.thread_count`; `<= 0` picks a
  default, generation is synchronous). The last-error message is thread-local.
- **ABI.** Enum values and struct layouts are fixed — do not reorder or
  change, append instead. `msdf_atlas_config_t` is versioned via its
  `struct_size` field, validated by every entry point.
  `MSDF_ATLAS_C_API_VERSION` is currently 1.
- **Export macro.** Define `MSDF_ATLAS_C_STATIC` (the shim does this
  publicly) to force plain declarations on Windows; `MSDF_ATLAS_C_BUILD`
  when building the wrapper; override entirely with `MSDF_ATLAS_C_API`.

## 7. Engine integration (intentionally out of scope)

This task vendors the library and its C API; **no engine code consumes it
yet, and wiring it into the engine is deliberately out of scope here.** The
v2 engine is C-first, so a future integration should go through the C API
(`<msdf_atlas_c.h>`, link `msdf-atlas-c`) rather than the C++ headers.

Known integration points (what a future task would touch):

- `core/resource_asset_builtins.c` — the `FontResource` asset type exists as
  a shell; MSDF atlas cooking is deferred ("Heavy cooks (MSDF fonts, …)
  omitted"). This is where font import would call the C API.
- `plugins/ui` — the UI plugin links `freetype` today and is the natural
  consumer of generated glyph atlases.

Entry points a future integration would call, in pipeline order:

1. `msdf_atlas_config_default()` / `msdf_atlas_config_validate()` — build a
   validated config once per font.
2. `msdf_atlas_font_open()` or `msdf_atlas_font_open_memory()` — load the
   font (plus `msdf_atlas_font_get_metrics()`, `get_glyph_count()`,
   `get_glyph_index()` for font metadata).
3. `msdf_atlas_charset_create*()` / `msdf_atlas_charset_add()` /
   `msdf_atlas_charset_parse()` — select the glyph set.
4. `msdf_atlas_glyphset_create()` + `msdf_atlas_glyphset_load_charset()` /
   `_load_glyphset()` / `_load_glyph_range()` and
   `msdf_atlas_glyphset_edge_color()` — build glyph geometry.
5. `msdf_atlas_packer_create()` + `msdf_atlas_packer_pack()` — compute the
   layout (then `msdf_atlas_packer_get_dimensions()`, `get_scale()`, …).
6. `msdf_atlas_generator_create()` + `msdf_atlas_generator_generate()` —
   render the atlas (then `get_bitmap()`, `get_layout_all()` for upload to
   the GPU and text layout).
7. The matching `*_destroy()` calls for every handle (§6).

The C++ smoke test `tests/msdf_atlas_smoke.cpp` shows the equivalent C++
pipeline (the exact sequence the legacy engine font importer used on main),
kept as a build/link regression check.

## 8. Verification

A reader can build and exercise everything from this README plus the public
header:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target msdf-atlas-c-smoke sk-msdf-atlas-smoke -j
ctest --test-dir build -R 'msdf-atlas' --output-on-failure
```

- `msdf-atlas-c-smoke` (`c_api/tests/msdf_atlas_c_smoke.c`, ctest name
  `msdf-atlas-c-smoke`): pure C99, walks the entire pipeline against the
  checked-in sample font (`plugins/ui/testdata/skore_test_font.ttf`,
  injected via `MSDF_ATLAS_C_SMOKE_FONT_PATH`) — version/errors, config,
  charset (ASCII, parse, ranges), font (file and memory), glyphset (load,
  edge color, layout, advance), packer (tight and grid), generator (bitmap
  and layout, both Y directions), and clean destruction of every handle.
  Built with ASan it doubles as a leak/double-free check.
- `sk-msdf-atlas-smoke` (`tests/msdf_atlas_smoke.cpp`, ctest name
  `sk-msdf-atlas-smoke`): the C++ pipeline (FreetypeHandle → FontGeometry →
  TightAtlasPacker → ImmediateAtlasGenerator<…, mtsdfGenerator,
  BitmapAtlasStorage>) against the same font; dumps
  `msdf_atlas_smoke.raw` + `msdf_atlas_smoke_layout.txt` next to the binary.

Both targets compile warning-free on GCC 13 (Debug and Release); the wrapper
additionally carries the header-documented `MSDF_ATLAS_C_API_VERSION`
versioning and sanitizer coverage via the smoke test.
