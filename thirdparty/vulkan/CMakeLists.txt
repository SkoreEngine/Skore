# Vulkan-Headers (Khronos) — vendored from skore main, Apache-2.0.
# INTERFACE target `vulkan-sdk` (name matches skore main) exporting the
# <vulkan/*.h> headers used by volk, VMA, and the vulkan_render_device plugin.
#
# The include dir is SYSTEM so plugin TUs that pull in these headers stay quiet
# under first-party -Werror / clang-tidy (thirdparty code is exempt).

add_library(vulkan-sdk INTERFACE)
target_include_directories(vulkan-sdk SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
