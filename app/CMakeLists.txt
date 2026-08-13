file(GLOB_RECURSE SKORE_APP_SOURCES *.h *.c)

# Compile only the platform/filesystem backends for the host OS (win32 vs unix/mac).
list(FILTER SKORE_APP_SOURCES EXCLUDE REGEX "platform_.*\\.c$")
list(FILTER SKORE_APP_SOURCES EXCLUDE REGEX "filesystem_.*\\.c$")
if(WIN32)
    list(APPEND SKORE_APP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_win32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_win32.c)
else()
    list(APPEND SKORE_APP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_unix.c
        ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_unix.c)
endif()

add_library(sk-app STATIC ${SKORE_APP_SOURCES})
target_include_directories(sk-app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Host lifecycle wiring includes plugins/profiler (sk_profiler_api_t table +
# render_device handles for the CPU-only init call). Header-only; the plugin
# itself is loaded at runtime from the plugins folder.
target_include_directories(sk-app PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/profiler
    ${CMAKE_SOURCE_DIR}/plugins/render_device)

# App is the host-facing layer; it always uses core.
target_link_libraries(sk-app PUBLIC sk-core)

# dlopen/dlsym live in libdl on many Unixes; CMAKE_DL_LIBS is empty on macOS/Windows.
if(UNIX)
    target_link_libraries(sk-app PUBLIC ${CMAKE_DL_LIBS})
endif()

# SHGetFolderPath / documents path on Windows.
if(WIN32)
    target_link_libraries(sk-app PUBLIC shell32)
endif()

# Test twin: same sources with SK_TESTS (in-source tests in app.c). Host only.
if(BUILD_TESTING)
    add_library(sk-app-tests STATIC ${SKORE_APP_SOURCES})
    target_include_directories(sk-app-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sk-app-tests PRIVATE SK_TESTS)
    # Plugin headers for integration-style tests that include platform_window.h
    target_link_libraries(sk-app-tests PUBLIC sk-core-tests)
    # Integration tests include platform_window.h (plugin public header).
    target_include_directories(sk-app-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/platform_window)
    # Integration tests include entities.h (plugin public header).
    target_include_directories(sk-app-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/entities)
    # Integration tests include dxc_compiler.h (plugin public header).
    target_include_directories(sk-app-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/dxc_compiler)
    # Integration tests include render_graph.h (plugin public header).
    target_include_directories(sk-app-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/render_graph
        ${CMAKE_SOURCE_DIR}/plugins/render_device)
    # Integration tests include jolt_components.h (plugin public header).
    target_include_directories(sk-app-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/jolt)
    # Integration tests include profiler.h (plugin public header).
    target_include_directories(sk-app-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/profiler)
    if(UNIX)
        target_link_libraries(sk-app-tests PUBLIC ${CMAKE_DL_LIBS})
    endif()
    if(WIN32)
        target_link_libraries(sk-app-tests PUBLIC shell32)
    endif()
endif()
