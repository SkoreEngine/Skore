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
    if(UNIX)
        target_link_libraries(sk-app-tests PUBLIC ${CMAKE_DL_LIBS})
    endif()
    if(WIN32)
        target_link_libraries(sk-app-tests PUBLIC shell32)
    endif()
endif()
