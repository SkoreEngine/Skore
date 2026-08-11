# FreeType 2.13.3 — vendored (FTL / GPLv2 dual license).
# Project-owned CMake: static + PIC for shared plugins, no external deps
# (bundled zlib for FT_CONFIG_OPTION_USE_ZLIB; bzip2/png/harfbuzz/brotli off).
# Sources match FreeType's modular single-object amalgamation files.

add_library(freetype STATIC
    src/autofit/autofit.c
    src/base/ftbase.c
    src/base/ftbbox.c
    src/base/ftbdf.c
    src/base/ftbitmap.c
    src/base/ftcid.c
    src/base/ftdebug.c
    src/base/ftfstype.c
    src/base/ftgasp.c
    src/base/ftglyph.c
    src/base/ftgxval.c
    src/base/ftinit.c
    src/base/ftmm.c
    src/base/ftotval.c
    src/base/ftpatent.c
    src/base/ftpfr.c
    src/base/ftstroke.c
    src/base/ftsynth.c
    src/base/ftsystem.c
    src/base/fttype1.c
    src/base/ftwinfnt.c
    src/bdf/bdf.c
    src/bzip2/ftbzip2.c
    src/cache/ftcache.c
    src/cff/cff.c
    src/cid/type1cid.c
    src/gzip/ftgzip.c
    src/lzw/ftlzw.c
    src/pcf/pcf.c
    src/pfr/pfr.c
    src/psaux/psaux.c
    src/pshinter/pshinter.c
    src/psnames/psnames.c
    src/raster/raster.c
    src/sdf/sdf.c
    src/sfnt/sfnt.c
    src/smooth/smooth.c
    src/svg/svg.c
    src/truetype/truetype.c
    src/type1/type1.c
    src/type42/type42.c
    src/winfonts/winfnt.c
)

target_include_directories(freetype SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(freetype PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(freetype PRIVATE
    FT2_BUILD_LIBRARY
)

if(WIN32)
    target_compile_definitions(freetype PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
endif()

# Keep FreeType symbols private when linked into SHARED plugins.
if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(freetype PRIVATE -fvisibility=hidden)
endif()

set_target_properties(freetype PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)

# Math library on Unix (used by some FreeType modules).
if(UNIX AND NOT APPLE)
    target_link_libraries(freetype PUBLIC m)
endif()
