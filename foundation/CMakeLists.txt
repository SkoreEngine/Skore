file(GLOB_RECURSE SKORE_FOUNDATION_SOURCES CONFIGURE_DEPENDS *.h *.c)

# test.c / test.h are the registry harness — production sk-foundation excludes
# test.c. sk-test (see below) owns the registry implementation when BUILD_TESTING.
# test_cases.c is host-only SK_TESTs (common.h / atomics / harness); not linked
# into plugins, which already statically link sk-test for the runner.
set(SKORE_FOUNDATION_TEST_C ${CMAKE_CURRENT_SOURCE_DIR}/test.c)
set(SKORE_FOUNDATION_TEST_CASES_C ${CMAKE_CURRENT_SOURCE_DIR}/test_cases.c)
list(FILTER SKORE_FOUNDATION_SOURCES EXCLUDE REGEX "test\\.c$")
list(FILTER SKORE_FOUNDATION_SOURCES EXCLUDE REGEX "test_cases\\.c$")

# Compile only the platform/filesystem backends for the host OS (win32 vs unix/mac).
list(FILTER SKORE_FOUNDATION_SOURCES EXCLUDE REGEX "platform_.*\\.c$")
list(FILTER SKORE_FOUNDATION_SOURCES EXCLUDE REGEX "filesystem_.*\\.c$")
if(WIN32)
    list(APPEND SKORE_FOUNDATION_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_win32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_win32.c)
else()
    list(APPEND SKORE_FOUNDATION_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_unix.c
        ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_unix.c)
endif()

# pthread backend for mutex.c (Windows uses SRWLOCK, no extra link).
find_package(Threads REQUIRED)

add_library(sk-foundation STATIC ${SKORE_FOUNDATION_SOURCES})
target_include_directories(sk-foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Foundation is statically linked into shared plugins; objects must be PIC.
set_target_properties(sk-foundation PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Host lifecycle wiring includes plugins/profiler (sk_profiler_api_t table +
# render_device handles for the CPU-only init call). Header-only; the plugin
# itself is loaded at runtime from the plugins folder.
# Engine physics wiring includes plugins/entities (scene world) + plugins/jolt
# (physics API table) — also header-only; both load at runtime.
target_include_directories(sk-foundation PRIVATE
    ${SKORE_SOURCE_DIR}/plugins/profiler
    ${SKORE_SOURCE_DIR}/plugins/render_device
    ${SKORE_SOURCE_DIR}/plugins/entities
    ${SKORE_SOURCE_DIR}/plugins/jolt)

# Default heap backend (mimalloc). PRIVATE: hide mimalloc from consumers;
# only sk_allocator_t is public.
target_link_libraries(sk-foundation PRIVATE mimalloc-static)

# JSON parsing (yyjson, vendored). PRIVATE: yyjson headers never leak into the
# public sk-foundation interface; only internals include yyjson.h.
target_link_libraries(sk-foundation PRIVATE yyjson)

# Compression codecs (vendored). PRIVATE: third-party headers never leak into
# the public sk-foundation interface; compression.c includes them only under
# SK_COMPRESSION_HAS_* (docs/compression-design-v2.md §5, §6;
# docs/compression-codecs-evaluation.md §7).
if(SK_COMPRESSION_ZSTD)
    target_compile_definitions(sk-foundation PRIVATE SK_COMPRESSION_HAS_ZSTD)
    target_link_libraries(sk-foundation PRIVATE zstd)
endif()
if(SK_COMPRESSION_LZ4)
    target_compile_definitions(sk-foundation PRIVATE SK_COMPRESSION_HAS_LZ4)
    target_link_libraries(sk-foundation PRIVATE lz4)
endif()
if(SK_COMPRESSION_MINIZ)
    target_compile_definitions(sk-foundation PRIVATE SK_COMPRESSION_HAS_MINIZ)
    target_link_libraries(sk-foundation PRIVATE miniz)
endif()

# Threads (pthread): PUBLIC so static consumers link it at their final link.
target_link_libraries(sk-foundation PUBLIC Threads::Threads)

# libm for sinf/cosf/sqrtf/etc. (required on Linux; typically implicit on MSVC/macOS).
find_library(SK_LIBM m)
if(SK_LIBM)
    target_link_libraries(sk-foundation PUBLIC ${SK_LIBM})
endif()

# dladdr (POSIX stacktrace symbolization) lives in libdl on older glibc;
# musl and macOS provide it in libc, so find_library finds nothing there.
find_library(SK_LIBDL dl)
if(SK_LIBDL)
    target_link_libraries(sk-foundation PUBLIC ${SK_LIBDL})
endif()

# dlopen/dlsym live in libdl on many Unixes; CMAKE_DL_LIBS is empty on macOS/Windows.
if(UNIX)
    target_link_libraries(sk-foundation PUBLIC ${CMAKE_DL_LIBS})
endif()

# DbgHelp (Win32 stacktrace symbolization: SymInitialize / SymFromAddr /
# SymGetLineFromAddr64). Import library; PUBLIC so static consumers
# (sk-player, plugins) resolve it at their final link. Works for
# MSVC (dbghelp.lib), clang-cl, and MinGW (-ldbghelp).
# SHGetFolderPath / documents path on Windows.
if(WIN32)
    target_link_libraries(sk-foundation PUBLIC dbghelp shell32)
endif()

# Optional header-only interface (prefer linking sk-foundation STATIC).
add_library(sk-foundation-lib INTERFACE)
target_include_directories(sk-foundation-lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Testing: registry (sk-test) + foundation sources recompiled with SK_TESTS
# Never used by sk-player / Release plugins.
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    add_library(sk-test STATIC ${SKORE_FOUNDATION_TEST_C})
    target_include_directories(sk-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # Absolute engine root so fixture/golden lookup does not depend on cwd or
    # which repo is CMAKE_SOURCE_DIR (skore vs skore-test-suite).
    file(TO_CMAKE_PATH "${SKORE_SOURCE_DIR}" _skore_src_norm)
    target_compile_definitions(sk-test PUBLIC
        SK_TESTS
        SKORE_SOURCE_DIR="${_skore_src_norm}"
    )
    unset(_skore_src_norm)
    target_link_libraries(sk-test PUBLIC unity)
    set_target_properties(sk-test PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(sk-foundation-tests STATIC ${SKORE_FOUNDATION_SOURCES} ${SKORE_FOUNDATION_TEST_CASES_C})
    target_include_directories(sk-foundation-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sk-foundation-tests PRIVATE SK_TESTS)
    target_link_libraries(sk-foundation-tests PUBLIC sk-test PRIVATE mimalloc-static yyjson)
    target_link_libraries(sk-foundation-tests PUBLIC Threads::Threads)
    set_target_properties(sk-foundation-tests PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(SK_COMPRESSION_ZSTD)
        target_compile_definitions(sk-foundation-tests PRIVATE SK_COMPRESSION_HAS_ZSTD)
        target_link_libraries(sk-foundation-tests PRIVATE zstd)
    endif()
    if(SK_COMPRESSION_LZ4)
        target_compile_definitions(sk-foundation-tests PRIVATE SK_COMPRESSION_HAS_LZ4)
        target_link_libraries(sk-foundation-tests PRIVATE lz4)
    endif()
    if(SK_COMPRESSION_MINIZ)
        target_compile_definitions(sk-foundation-tests PRIVATE SK_COMPRESSION_HAS_MINIZ)
        target_link_libraries(sk-foundation-tests PRIVATE miniz)
    endif()
    if(SK_LIBM)
        target_link_libraries(sk-foundation-tests PUBLIC ${SK_LIBM})
    endif()
    if(SK_LIBDL)
        target_link_libraries(sk-foundation-tests PUBLIC ${SK_LIBDL})
    endif()
    if(UNIX)
        target_link_libraries(sk-foundation-tests PUBLIC ${CMAKE_DL_LIBS})
    endif()
    if(WIN32)
        target_link_libraries(sk-foundation-tests PUBLIC dbghelp shell32)
    endif()
    # Plugin headers for integration-style tests that include platform_window.h
    # (and other plugin public headers). Same list as the former sk-app-tests.
    target_include_directories(sk-foundation-tests PRIVATE
        ${SKORE_SOURCE_DIR}/plugins/platform_window
        ${SKORE_SOURCE_DIR}/plugins/entities
        ${SKORE_SOURCE_DIR}/plugins/dxc_compiler
        ${SKORE_SOURCE_DIR}/plugins/render_graph
        ${SKORE_SOURCE_DIR}/plugins/render_device
        ${SKORE_SOURCE_DIR}/plugins/profiler
        ${SKORE_SOURCE_DIR}/plugins/jolt)
endif()
