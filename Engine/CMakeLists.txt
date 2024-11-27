set(GLFW_BUILD_WAYLAND OFF)
set(FT_DISABLE_HARFBUZZ ON)

add_subdirectory(ThirdParty)

cmrc_add_resource_library(
				SkoreContent
				ALIAS SkoreContent::RS
				NAMESPACE StaticContent

				Content/Fonts/DejaVuSans.ttf
				Content/Fonts/fa-solid-900.otf

				Content/Images/file.png
				Content/Images/FolderIcon.png
				Content/Images/Logo.jpeg
				Content/Images/LogoSmall.jpeg
)


file(GLOB_RECURSE FYRION_ENGINE_SOURCES Source/*.hpp Source/*.cpp Source/*.h Source/*.c)
add_library(SkoreEngine SHARED ${FYRION_ENGINE_SOURCES})

target_include_directories(SkoreEngine PUBLIC Source)
target_include_directories(SkoreEngine PUBLIC Source/Skore/ImGui/Lib)

target_compile_definitions(SkoreEngine PUBLIC FMT_LIB_EXPORT=1)
target_compile_definitions(SkoreEngine PUBLIC SK_DEV_ASSETS_PATH="${SK_DEV_ASSET_FILES}")

target_link_libraries(SkoreEngine PRIVATE
				mimalloc
				glfw
				vma
				volk
				vulkan-sdk
				freetype
				nfd
				cpptrace::cpptrace
				stb
				zstd
				lz4
				yyjson
				mikktspace
				SkoreContent::RS
				Jolt
				SkoreAssets
)

##tests
file(GLOB_RECURSE FYRION_ENGINE_TESTS_SOURCES Test/*.hpp Test/*.cpp Test/*.h Test/*.c)
add_executable(SkoreEngineTests ${FYRION_ENGINE_TESTS_SOURCES})

target_link_libraries(SkoreEngineTests PUBLIC SkoreEngine)

target_include_directories(SkoreEngineTests PUBLIC ThirdParty/doctest)
target_include_directories(SkoreEngineTests PUBLIC Test)
target_compile_definitions(SkoreEngineTests PRIVATE SK_TEST_FILES="${CMAKE_CURRENT_SOURCE_DIR}/Test/Files")

add_test(NAME SkoreEngineTests COMMAND SkoreEngineTests)