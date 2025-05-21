file(GLOB_RECURSE IMGUI_SOURCES src/*.hpp src/*.cpp src/*.h src/*.c)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC src)
target_link_libraries(imgui PRIVATE
				SDL3::SDL3-shared
				freetype
				volk
				vulkan-sdk
)

target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)

