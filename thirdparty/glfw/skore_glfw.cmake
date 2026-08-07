# Options applied before add_subdirectory(glfw) from thirdparty/CMakeLists.txt.
# Keep GLFW as a static PIC library for linking into shared plugins.

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW docs" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Install GLFW" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)

# Prefer X11 on Linux; Wayland needs extra tools/protocols not always present.
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Build GLFW Wayland support" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "Build GLFW X11 support" FORCE)
endif()
