file(GLOB_RECURSE SKORE_RUNTIME_SOURCES Source/*.hpp Source/*.cpp Source/*.h Source/*.c)

add_library(SkoreRuntime SHARED ${SKORE_RUNTIME_SOURCES})

target_include_directories(SkoreRuntime PUBLIC Source)

target_include_directories(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/rapidyaml)
target_include_directories(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/concurrentqueue)

target_link_libraries(SkoreRuntime PRIVATE
				SDL3::SDL3-shared
				yyjson
				stb
				mikktspace
				Jolt
				miniaudio
)

target_link_libraries(SkoreRuntime PUBLIC
				vulkan-sdk
				volk
				vma
)

target_compile_definitions(SkoreRuntime PRIVATE SK_DLL_EXPORT=1)
target_compile_definitions(SkoreRuntime PUBLIC FMT_LIB_EXPORT=1)

target_precompile_headers(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source/Skore/PCH.hpp)