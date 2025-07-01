sk_add_executable(Skore Source/Skore/Main.cpp)


target_link_libraries(Skore PUBLIC
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
)

add_binary_file(Skore ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/dxc dxcompiler)

