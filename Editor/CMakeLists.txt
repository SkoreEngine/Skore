file(GLOB_RECURSE SKORE_EDITOR_SOURCES
				Source/*.hpp
				Source/*.cpp
				Source/*.h
				Source/*.c)


add_library(SkoreEditor SHARED ${SKORE_EDITOR_SOURCES})
target_include_directories(SkoreEditor PUBLIC Source)

target_link_libraries(SkoreEditor PUBLIC
				SkoreRuntime
				SDL3::SDL3-shared
				SkoreContent::RS
				imgui
				dxc
				cgltf
				ufbx
				SPIRV-Reflect
				base64
				stb
				tinyobjloader
				msdf-atlas-gen
				ImGuizmo
				efsw-static
				yyjson
)


if (SK_DEV_ASSETS)
	target_compile_definitions(SkoreEditor PRIVATE SK_DEV_ASSETS=true)
endif ()


add_binary_file(SkoreEditor ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/dxc dxcompiler)


add_executable(Skore
				Source/Main/Main.cpp
)

target_link_libraries(Skore PUBLIC SkoreEditor)


if(WIN32)
	set_target_properties(Skore PROPERTIES WIN32_EXECUTABLE TRUE)
endif()