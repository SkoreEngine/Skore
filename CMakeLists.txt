cmake_minimum_required(VERSION 3.30)
project(Skore)

set(CMAKE_CXX_STANDARD 20)

set(SK_ENGINE_VERSION "0.0.1")
add_compile_definitions(SK_ENGINE_VERSION="${SK_ENGINE_VERSION}")

string(TIMESTAMP TODAY "%Y%m%d")
add_compile_definitions(SK_VERSION="${SK_ENGINE_VERSION}-${TODAY}")

#add_compile_definitions(SK_DEV_ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Assets")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR (CMAKE_CXX_COMPILER_ID MATCHES "GNU"))
	add_compile_options(-fms-extensions)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -std=c++20")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif()

if ( MSVC )
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
endif (MSVC)

if (UNIX AND NOT APPLE)
	set(CMAKE_CXX_FLAGS                "${CMAKE_CXX_FLAGS_INIT} -fPIC")
	set(CMAKE_C_FLAGS                  "${CMAKE_C_FLAGS_INIT} -fPIC")
	SET(CMAKE_SKIP_BUILD_RPATH FALSE)
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	SET(CMAKE_INSTALL_RPATH "\${ORIGIN}")
elseif (APPLE)
	set(CMAKE_CXX_FLAGS                "${CMAKE_CXX_FLAGS_INIT} -fPIC")
	set(CMAKE_C_FLAGS                  "${CMAKE_C_FLAGS_INIT} -fPIC")
	SET(CMAKE_SKIP_BUILD_RPATH FALSE)
	SET(CMAKE_INSTALL_RPATH "${ORIGIN}/")
endif()

enable_testing()

include(CMake/CMakeRC.cmake)
include(CMake/CMakeFunctions.cmake)

file(GLOB_RECURSE SK_ALL_ASSETS Assets/*)

cmrc_add_resource_library(
				SkoreContent
				ALIAS SkoreContent::RS
				NAMESPACE StaticContent

				Content/Fonts/DejaVuSans.ttf
				Content/Fonts/fa-solid-900.otf

				Content/Images/FileIcon.png
				Content/Images/FolderIcon.png
				Content/Images/Logo.jpeg
				Content/Images/LogoSmall.jpeg
				Content/Images/minimalist-logo.png

				${SK_ALL_ASSETS}
)


add_subdirectory(ThirdParty)
add_subdirectory(Editor)
add_subdirectory(Player)
add_subdirectory(Runtime)
add_subdirectory(Tests)