file(GLOB_RECURSE SKORE_CORE_SOURCES CONFIGURE_DEPENDS *.h *.c)

# test.c / test.h are the registry harness — production sk-core excludes test.c.
# sk-test (see below) owns the registry implementation when BUILD_TESTING.
set(SKORE_CORE_TEST_C ${CMAKE_CURRENT_SOURCE_DIR}/test.c)
list(FILTER SKORE_CORE_SOURCES EXCLUDE REGEX "test\\.c$")

# pthread backend for core/mutex.c (Windows uses SRWLOCK, no extra link).
find_package(Threads REQUIRED)

add_library(sk-core STATIC ${SKORE_CORE_SOURCES})
target_include_directories(sk-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Core is statically linked into shared plugins; objects must be PIC.
set_target_properties(sk-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Default heap backend (mimalloc). PRIVATE: hide mimalloc from core consumers;
# only sk_allocator_t is public.
target_link_libraries(sk-core PRIVATE mimalloc-static)

# Threads (pthread): PUBLIC so static consumers link it at their final link.
target_link_libraries(sk-core PUBLIC Threads::Threads)

# libm for sinf/cosf/sqrtf/etc. (required on Linux; typically implicit on MSVC/macOS).
find_library(SK_LIBM m)
if(SK_LIBM)
    target_link_libraries(sk-core PUBLIC ${SK_LIBM})
endif()

# dladdr (POSIX stacktrace symbolization) lives in libdl on older glibc;
# musl and macOS provide it in libc, so find_library finds nothing there.
find_library(SK_LIBDL dl)
if(SK_LIBDL)
    target_link_libraries(sk-core PUBLIC ${SK_LIBDL})
endif()

# DbgHelp (Win32 stacktrace symbolization: SymInitialize / SymFromAddr /
# SymGetLineFromAddr64). Import library; PUBLIC so static consumers
# (sk-app, sk-player, plugins) resolve it at their final link. Works for
# MSVC (dbghelp.lib), clang-cl, and MinGW (-ldbghelp).
if(WIN32)
    target_link_libraries(sk-core PUBLIC dbghelp)
endif()

# Optional header-only interface (prefer linking sk-core STATIC for code + headers).
add_library(sk-core-lib INTERFACE)
target_include_directories(sk-core-lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Testing: registry (sk-test) + core sources recompiled with SK_TESTS
# Never used by sk-player / Release plugins.
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    add_library(sk-test STATIC ${SKORE_CORE_TEST_C})
    target_include_directories(sk-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sk-test PUBLIC SK_TESTS)
    target_link_libraries(sk-test PUBLIC unity)
    set_target_properties(sk-test PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(sk-core-tests STATIC ${SKORE_CORE_SOURCES})
    target_include_directories(sk-core-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sk-core-tests PRIVATE SK_TESTS)
    target_link_libraries(sk-core-tests PUBLIC sk-test PRIVATE mimalloc-static)
    target_link_libraries(sk-core-tests PUBLIC Threads::Threads)
    set_target_properties(sk-core-tests PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(SK_LIBM)
        target_link_libraries(sk-core-tests PUBLIC ${SK_LIBM})
    endif()
    if(SK_LIBDL)
        target_link_libraries(sk-core-tests PUBLIC ${SK_LIBDL})
    endif()
    if(WIN32)
        target_link_libraries(sk-core-tests PUBLIC dbghelp)
    endif()
endif()
