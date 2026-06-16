file(GLOB_RECURSE SKORE_RUNTIME_SOURCES
				Source/*.hpp
				Source/*.cpp
				Source/*.h
				Source/*.c)

if (SK_ENABLE_TRACY)
	list(APPEND SKORE_RUNTIME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/tracy/public/TracyClient.cpp)
endif ()


add_library(SkoreRuntime SHARED ${SKORE_RUNTIME_SOURCES})

target_include_directories(SkoreRuntime PUBLIC Source)

target_include_directories(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/rapidyaml)
target_include_directories(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/concurrentqueue)
target_include_directories(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Shaders)

target_link_libraries(SkoreRuntime PRIVATE
				mikktspace
				yyjson
				stb
				Jolt
				RecastNavigation
				miniaudio
				zstd-skore
				mimalloc
				OpenXR
				xatlas
				rmlui
				dotnet
)

target_link_libraries(SkoreRuntime PUBLIC
				SDL3::SDL3
				vulkan-sdk
				volk
				vma
				ufbx
)


if (SK_ENABLE_TRACY)
	target_link_libraries(SkoreRuntime PUBLIC Tracy::TracyClient)
endif ()

if(ANDROID)
	target_link_libraries(SkoreRuntime PRIVATE log android)
endif ()

target_compile_definitions(SkoreRuntime PRIVATE SK_DLL_EXPORT=1)
target_compile_definitions(SkoreRuntime PUBLIC FMT_LIB_EXPORT=1)

target_precompile_headers(SkoreRuntime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source/Skore/PCH.hpp)