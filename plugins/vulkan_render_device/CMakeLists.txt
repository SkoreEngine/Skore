file(GLOB_RECURSE SKORE_VULKAN_RENDER_DEVICE_SOURCES *.h *.c)
# APX-275: exclude the internal-only extension (*.internal.h) — pattern rule, never exported/installed.
list(FILTER SKORE_VULKAN_RENDER_DEVICE_SOURCES EXCLUDE REGEX "\\.internal\\.h$")

sk_add_plugin(vulkan-render-device SOURCES ${SKORE_VULKAN_RENDER_DEVICE_SOURCES})

# v2's sk_vkrd_update_buffer_chunk_plan is only referenced from SK_TESTS.
# Release plugin builds omit SK_TESTS, so that helper is unused and fails
# -Werror / clang-tidy (Linux/macOS/Windows Release CI). Silence this TU
# in Release; Debug still compiles and runs the chunk-plan tests.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set_property(SOURCE vulkan_command_buffer.c APPEND PROPERTY COMPILE_OPTIONS
        "$<$<CONFIG:Release>:-Wno-unused-function>")
endif()
if(CMAKE_C_CLANG_TIDY)
    set_target_properties(sk-vulkan-render-device PROPERTIES
        C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--extra-arg=-Wno-unused-function")
endif()

# VMA's implementation block is C++ (std::*, <cstdint>), so it is compiled in
# its own static TU and linked into the shared plugin (the plugin itself links
# with the C driver, avoiding the CXX -static link flags).
add_library(vma-impl STATIC vulkan_vma.cpp)
set_target_properties(vma-impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(vma-impl PUBLIC vma vulkan-sdk)

# Vulkan backend deps: volk meta-loader (static), VMA (header-only allocator),
# Vulkan-Headers. Mirrors skore main (SkoreRuntime links volk vma vulkan-sdk).
target_link_libraries(sk-vulkan-render-device PUBLIC
    volk
    vma
    vulkan-sdk
    vma-impl
)

# The backend implements sk_render_device_api_t (RHI surface) and consumes the
# sk-platform-window surface handles. Header-only INTERFACE targets; no binary
# coupling to the other plugins.
target_link_libraries(sk-vulkan-render-device PRIVATE
    sk-render-device-lib
    sk-platform-window-lib
)

# Platform surface helpers (Xlib on Linux, none on Windows, Metal on macOS).
if(WIN32)
    # vkCreateWin32SurfaceKHR + GetModuleHandle need no extra link libs.
elseif(APPLE)
    target_link_libraries(sk-vulkan-render-device PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
    )
else()
    find_package(X11 REQUIRED)
    target_link_libraries(sk-vulkan-render-device PUBLIC X11::X11)
endif()
