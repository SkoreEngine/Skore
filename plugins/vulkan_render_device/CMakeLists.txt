file(GLOB_RECURSE SKORE_VULKAN_RENDER_DEVICE_SOURCES *.h *.c)

sk_add_plugin(vulkan-render-device SOURCES ${SKORE_VULKAN_RENDER_DEVICE_SOURCES})

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
