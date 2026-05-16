#based on https://github.com/godotengine/godot/blob/master/modules/openxr/SCsub

file(GLOB_RECURSE OPENXR_SOURCES src/*.hpp src/*.cpp src/*.h src/*.c)
add_library(OpenXR STATIC ${OPENXR_SOURCES})

target_include_directories(OpenXR PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(OpenXR PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_include_directories(OpenXR PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/common>)
target_include_directories(OpenXR PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/external/jsoncpp/include>)


if(ANDROID)
	target_compile_definitions(OpenXR PUBLIC XR_OS_ANDROID=1)
	target_compile_definitions(OpenXR PUBLIC XR_USE_PLATFORM_ANDROID=1)
	target_compile_definitions(OpenXR PUBLIC JSON_USE_EXCEPTION=0)
elseif (WIN32)
	target_compile_definitions(OpenXR PUBLIC XR_OS_WINDOWS=1)
	target_compile_definitions(OpenXR PUBLIC NOMINMAX=1)
	target_compile_definitions(OpenXR PUBLIC XR_USE_PLATFORM_WIN32=1)
elseif (APPLE)
	target_compile_definitions(OpenXR PUBLIC XR_OS_APPLE=1)
elseif(UNIX)
	target_compile_definitions(OpenXR PUBLIC XR_OS_LINUX=1)
	target_compile_definitions(OpenXR PUBLIC XR_USE_PLATFORM_XLIB=1)
endif ()

