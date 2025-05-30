function(add_binary_file project folder file)

	if (WIN32)
		add_custom_command(TARGET ${project} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${folder}/bin/win-x64/${file}.dll" $<TARGET_FILE_DIR:${project}>)
	elseif (APPLE)
		add_custom_command(TARGET ${project} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${folder}/bin/macOS/lib${file}.dylib" $<TARGET_FILE_DIR:${project}>)
	elseif (UNIX)
		add_custom_command(TARGET ${project} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${folder}/bin/linux-x64/lib${file}.so" $<TARGET_FILE_DIR:${project}>)
	else ()
		message(FATAL_ERROR "Unsupported target platform '${CMAKE_SYSTEM_NAME}'")
	endif ()

endfunction()


function(sk_add_executable name)
	file(GLOB_RECURSE ${name}_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.c ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

	if (WIN32)
		add_executable(${name} WIN32 ${${name}_SOURCES} ${ARGN})
	else ()
		add_executable(${name} ${${name}_SOURCES} ${ARGN})
	endif ()

	target_link_libraries(${name} PUBLIC SkoreRuntime)
	target_include_directories(${name} PUBLIC Source)

endfunction(sk_add_executable)