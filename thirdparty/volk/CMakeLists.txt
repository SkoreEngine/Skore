# volk (zeux) — vendored from skore main, MIT. Vulkan meta-loader.
# Static + PIC so it links into the shared vulkan_render_device plugin.
# PUBLIC include is SYSTEM: plugin TUs that include volk.h stay quiet under
# first-party -Werror / clang-tidy (thirdparty code is exempt).

add_library(volk STATIC src/volk.c)

target_include_directories(volk SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(volk PRIVATE vulkan-sdk)

# dlopen/dlsym for loader discovery (empty on Windows/macOS).
if(NOT WIN32)
    target_link_libraries(volk PUBLIC ${CMAKE_DL_LIBS})
endif()

set_target_properties(volk PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Keep volk symbols out of the plugin's exported surface (only SK_API entry
# points are exported). Harmless on Windows (link-time only; not dllexport).
if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(volk PRIVATE -fvisibility=hidden)
endif()
