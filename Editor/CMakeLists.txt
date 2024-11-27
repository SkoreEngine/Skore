add_subdirectory(ThirdParty)

file(GLOB_RECURSE FYRION_EDITOR_SOURCES Source/*.hpp Source/*.cpp Source/*.h Source/*.c)
add_library(SkoreEditor SHARED ${FYRION_EDITOR_SOURCES})

target_include_directories(SkoreEditor PUBLIC Source)
target_include_directories(SkoreEditor PRIVATE ThirdParty/dxc/include)

target_link_libraries(SkoreEditor PRIVATE stb SPIRV-Reflect cgltf)
target_link_libraries(SkoreEditor PUBLIC SkoreEngine)

add_binary_file(SkoreEditor ThirdParty/dxc dxcompiler)