set(SDL_SHARED ON)
set(SDL_STATIC OFF)
set(SDL_AUDIO_DISABLED ON)
set(SDL_HIDAPI_DISABLED ON CACHE BOOL "")
set(SDL_GPU_DISABLED ON CACHE BOOL "")
set(SDL_RENDER_DISABLED ON CACHE BOOL "")


set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)

set(SPIRV_CROSS_ENABLE_TESTS OFF)

set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "No MINIAUDIO_NO_LIBVORBIS")
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "No MINIAUDIO_NO_LIBVORBIS")


set(MI_BUILD_TESTS OFF)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "")
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "")
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(MESHOPT_INSTALL OFF CACHE BOOL "")

#rmlui
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")


if (SK_ENABLE_TRACY)
	option(TRACY_ENABLE "" ON)
endif()

add_subdirectory(base64)
add_subdirectory(cpp-httplib)
add_subdirectory(efsw)
add_subdirectory(cgltf)
add_subdirectory(dotnet)
add_subdirectory(dxc)
add_subdirectory(stb)
add_subdirectory(tinyobjloader)
add_subdirectory(SDL3)
#add_subdirectory(SPIRV-Cross) -- not used
add_subdirectory(SPIRV-Reflect)
add_subdirectory(imgui)
add_subdirectory(ImGuizmo)
add_subdirectory(JoltPhysics)
add_subdirectory(recastnavigation)
add_subdirectory(freetype)

add_library(Freetype::Freetype ALIAS freetype)
add_subdirectory(RmlUi)

add_subdirectory(meshoptimizer)
add_subdirectory(mikktspace)
add_subdirectory(mimalloc)
add_subdirectory(miniaudio)
add_subdirectory(msdf-atlas-gen)
add_subdirectory(OpenXR)
if (SK_ENABLE_TRACY)
	add_subdirectory(tracy)
endif()
add_subdirectory(ufbx)
add_subdirectory(vma)
add_subdirectory(volk)
add_subdirectory(vulkan)
add_subdirectory(xatlas)
add_subdirectory(yyjson)
add_subdirectory(zstd)