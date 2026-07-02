file(GLOB_RECURSE SKORE_TEST_SOURCES Source/*.hpp Source/*.cpp Source/*.h Source/*.c)
add_executable(SkoreTests ${SKORE_TEST_SOURCES})
target_link_libraries(SkoreTests PUBLIC SkoreRuntime SkoreEditor)
target_include_directories(SkoreTests PUBLIC Source)
target_include_directories(SkoreTests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/doctest)
target_compile_definitions(SkoreTests PUBLIC SK_EDITOR_TEST_FILES="${CMAKE_CURRENT_SOURCE_DIR}/Files")
target_compile_definitions(SkoreTests PUBLIC SK_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Shaders")

if (SK_ENABLE_GPU_TESTS)
	target_compile_definitions(SkoreTests PUBLIC SK_GPU_TESTS=1)
	# The DXC shader compiler is loaded from the executable's directory at runtime.
	add_binary_file(SkoreTests ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/dxc dxcompiler)
	# Run from the output directory so dxcompiler.dll (and other runtime dlls) are found.
	add_test(NAME SkoreTests COMMAND SkoreTests WORKING_DIRECTORY $<TARGET_FILE_DIR:SkoreTests>)
else ()
	add_test(NAME SkoreTests COMMAND SkoreTests)
endif ()
