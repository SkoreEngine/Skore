function(add_binary_file project folder file)

	if (WIN32)
		add_custom_command(TARGET ${project} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/bin/win-x64/${file}.dll" $<TARGET_FILE_DIR:${project}>)
	elseif (APPLE)
		add_custom_command(TARGET ${project} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/bin/macOS/lib${file}.dylib" $<TARGET_FILE_DIR:${project}>)
	elseif (UNIX)
		add_custom_command(TARGET ${project} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/bin/linux-x64/lib${file}.so" $<TARGET_FILE_DIR:${project}>)
	else ()
		message(FATAL_ERROR "Unsupported target platform '${CMAKE_SYSTEM_NAME}'")
	endif ()

endfunction()


function(configure_plugin name)
	set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries")
	set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/Binaries")
	set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/Binaries")
endfunction(configure_plugin name)


function(add_plugin name)
	file(GLOB_RECURSE ${name}_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.c ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

	add_library(${name} SHARED ${${name}_SOURCES} ${${name}_TESTS} ${ARGN})
	configure_plugin(${name})

	target_include_directories(${name} PUBLIC Source)
	target_link_libraries(${name} PUBLIC FyrionEngine)

endfunction(add_plugin)