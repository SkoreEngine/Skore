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
