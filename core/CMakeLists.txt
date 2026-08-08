file(GLOB_RECURSE SKORE_CORE_SOURCES *.h *.c)

# test.c / test.h are the registry harness — production sk-core excludes test.c.
# sk-test (see below) owns the registry implementation when BUILD_TESTING.
set(SKORE_CORE_TEST_C ${CMAKE_CURRENT_SOURCE_DIR}/test.c)
list(FILTER SKORE_CORE_SOURCES EXCLUDE REGEX "test\\.c$")

# atomic_isolation.c is a compile-only include-graph fixture proving atomic.h
# pulls no window/UI headers. Excluded from production sk-core like test.c;
# compiled into sk-core-tests so CI exercises it on every platform.
set(SKORE_CORE_ISOLATION_C ${CMAKE_CURRENT_SOURCE_DIR}/atomic_isolation.c)
list(FILTER SKORE_CORE_SOURCES EXCLUDE REGEX "atomic_isolation\\.c$")

add_library(sk-core STATIC ${SKORE_CORE_SOURCES})
target_include_directories(sk-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Core is statically linked into shared plugins; objects must be PIC.
set_target_properties(sk-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Default heap backend (mimalloc). PRIVATE: hide mimalloc from core consumers;
# only sk_allocator_t is public.
target_link_libraries(sk-core PRIVATE mimalloc-static)

# libm for sinf/cosf/sqrtf/etc. (required on Linux; typically implicit on MSVC/macOS).
find_library(SK_LIBM m)
if(SK_LIBM)
    target_link_libraries(sk-core PUBLIC ${SK_LIBM})
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

    add_library(sk-core-tests STATIC ${SKORE_CORE_SOURCES} ${SKORE_CORE_ISOLATION_C})
    target_include_directories(sk-core-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sk-core-tests PRIVATE SK_TESTS)
    target_link_libraries(sk-core-tests PUBLIC sk-test PRIVATE mimalloc-static)
    set_target_properties(sk-core-tests PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(SK_LIBM)
        target_link_libraries(sk-core-tests PUBLIC ${SK_LIBM})
    endif()
endif()
