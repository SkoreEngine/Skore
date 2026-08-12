# Vendored third-party libraries. One folder per lib; list each below.
# add_subdirectory(<lib_name>)

add_subdirectory(unity)
add_subdirectory(mimalloc)
add_subdirectory(yyjson)

# zstd 1.5.6 (baseline compression codec; BSD-3-Clause). Gated by
# SK_COMPRESSION_ZSTD so codec-less builds stay possible.
if(SK_COMPRESSION_ZSTD)
    add_subdirectory(zstd)
endif()

# LZ4 1.10.0 (fast one-shot block codec; BSD-2-Clause). Gated by
# SK_COMPRESSION_LZ4 (docs/compression-codecs-evaluation.md §6).
if(SK_COMPRESSION_LZ4)
    add_subdirectory(lz4)
endif()

# miniz 3.0.2 (zlib/deflate one-shot codec; MIT). Gated by
# SK_COMPRESSION_MINIZ (docs/compression-codecs-evaluation.md §6).
if(SK_COMPRESSION_MINIZ)
    add_subdirectory(miniz)
endif()

# GLFW 3.4 (window / input). Static + PIC for shared plugins.
include(${CMAKE_CURRENT_SOURCE_DIR}/glfw/skore_glfw.cmake)
add_subdirectory(glfw)
# Ensure plugins can link glfw statically into a SHARED module.
if(TARGET glfw)
    set_target_properties(glfw PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Native File Dialog (open/save/folder). Zenity on Linux, COM on Win, Cocoa on macOS.
add_subdirectory(nativefiledialog)

# Vulkan stack for the vulkan_render_device plugin (vendored from skore main).
# Order matters: vulkan (vulkan-sdk headers) must exist before volk links it.
add_subdirectory(vulkan)
add_subdirectory(volk)
add_subdirectory(vma)

# DirectX Shader Compiler (DXC) — headers + platform runtimes (win-x64 dll,
# macOS dylib, linux-x64 .so). Plugins copy the matching shared library to
# the plugins output dir via sk_copy_dxc_shared_library().
add_subdirectory(dxc)

# FreeType (font rasterization) + stb_rect_pack (glyph atlas packing) for ui.
add_subdirectory(freetype)
add_subdirectory(stb_rect_pack)

# Clay v0.14 (immediate-mode layout, single header) for ui (APX-213).
add_subdirectory(clay)

# stb_image_write — PNG (and other) writers for UI capture / test artifacts.
add_subdirectory(stb_image_write)
# stb_image — PNG reader for capture write round-trip verification (and future loads).
add_subdirectory(stb_image)
# msdf-atlas-gen + msdfgen (MSDF glyph atlas generation for font resources).
# Reuses freetype from above; PNG/SVG/skia/artery-font paths compiled out.
add_subdirectory(msdf-atlas-gen)

# Jolt Physics v5.6.0 (rigid/soft-body dynamics; MIT). Library-only subtree
# vendored from jrouwe/JoltPhysics; GPU compute and debug renderer compiled
# out via upstream defaults (thirdparty/jolt/README.md).
add_subdirectory(jolt)
