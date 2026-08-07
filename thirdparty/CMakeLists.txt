# Vendored third-party libraries. One folder per lib; list each below.
# add_subdirectory(<lib_name>)

add_subdirectory(unity)
add_subdirectory(mimalloc)

# GLFW 3.4 (window / input). Static + PIC for shared plugins.
include(${CMAKE_CURRENT_SOURCE_DIR}/glfw/skore_glfw.cmake)
add_subdirectory(glfw)
# Ensure plugins can link glfw statically into a SHARED module.
if(TARGET glfw)
    set_target_properties(glfw PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Native File Dialog (open/save/folder). Zenity on Linux, COM on Win, Cocoa on macOS.
add_subdirectory(nativefiledialog)
