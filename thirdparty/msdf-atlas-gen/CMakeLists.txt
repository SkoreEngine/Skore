# msdf-atlas-gen 1.3.0 + msdfgen — vendored (MIT; see VENDORING.md + LICENSE.txt).
#
# Upstream pins (byte-identical sources; see VENDORING.md):
#   msdf-atlas-gen  https://github.com/Chlumsky/msdf-atlas-gen  77804c2f4108f49eb73491cda9296a2afb714d4c
#   msdfgen         https://github.com/Chlumsky/msdfgen         a4dafbac1c29021613fbc89768963f3a7e2105aa
#
# Project-owned CMake (not upstream builds). v2 thirdparty conventions:
# static + PIC for shared plugins, SYSTEM PUBLIC includes, -fvisibility=hidden,
# explicit per-target CXX_STANDARD 20.
#
# Optional features compiled out (manifest §5–§6): no artery-font-format
# (MSDF_ATLAS_NO_ARTERY_FONT=1), no tinyxml2/libpng/lodepng/skia/OpenMP/CLI.
# CLI-only and optional TUs are omitted from the source lists and removed from
# the tree; umbrella headers that reference them remain for include compatibility.
# size-selectors.cpp is required (TightAtlasPacker instantiates its selectors).
# freetype is reused from thirdparty/freetype (already on v2) — not duplicated.

add_library(msdfgen STATIC
    msdfgen/core/Contour.cpp
    msdfgen/core/DistanceMapping.cpp
    msdfgen/core/EdgeHolder.cpp
    msdfgen/core/MSDFErrorCorrection.cpp
    msdfgen/core/Projection.cpp
    msdfgen/core/Scanline.cpp
    msdfgen/core/Shape.cpp
    msdfgen/core/contour-combiners.cpp
    msdfgen/core/convergent-curve-ordering.cpp
    msdfgen/core/edge-coloring.cpp
    msdfgen/core/edge-segments.cpp
    msdfgen/core/edge-selectors.cpp
    msdfgen/core/equation-solver.cpp
    msdfgen/core/msdf-error-correction.cpp
    msdfgen/core/msdfgen.cpp
    msdfgen/core/rasterization.cpp
    msdfgen/core/render-sdf.cpp
    msdfgen/core/shape-description.cpp
    msdfgen/ext/import-font.cpp
)

target_include_directories(msdfgen SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/msdfgen
)

# Empty DLL-export macro; matches the skore main shim (headers default it).
target_compile_definitions(msdfgen PUBLIC
    MSDFGEN_PUBLIC=
)

# FreeType is PRIVATE: only ext/import-font.cpp needs its headers/link.
target_link_libraries(msdfgen PRIVATE freetype)

add_library(msdf-atlas-gen STATIC
    msdf-atlas-gen/Charset.cpp
    msdf-atlas-gen/FontGeometry.cpp
    msdf-atlas-gen/GlyphGeometry.cpp
    msdf-atlas-gen/Padding.cpp
    msdf-atlas-gen/RectanglePacker.cpp
    msdf-atlas-gen/TightAtlasPacker.cpp
    msdf-atlas-gen/Workload.cpp
    msdf-atlas-gen/bitmap-blit.cpp
    msdf-atlas-gen/glyph-generators.cpp
    msdf-atlas-gen/size-selectors.cpp
)

target_include_directories(msdf-atlas-gen SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/msdf-atlas-gen
)

# artery-font-format is compiled out (upstream submodule not vendored).
target_compile_definitions(msdf-atlas-gen PUBLIC
    MSDF_ATLAS_NO_ARTERY_FONT=1
)

# Workload.cpp uses std::thread; find_package(Threads) is already done by
# other thirdparty deps (glfw). Link PUBLIC so consumers get it.
find_package(Threads REQUIRED)
target_link_libraries(msdf-atlas-gen PUBLIC msdfgen Threads::Threads)

# --- Common properties (v2 thirdparty conventions) ---
foreach(_target IN ITEMS msdfgen msdf-atlas-gen)
    # Keep symbols private when linked into SHARED plugins.
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(${_target} PRIVATE -fvisibility=hidden)
    endif()

    if(WIN32)
        target_compile_definitions(${_target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
        )
    endif()

    set_target_properties(${_target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
    )
endforeach()
