# mimalloc — Microsoft Research (vendored, MIT)
# Minimal static build: single translation unit (src/static.c).
# MI_MALLOC_OVERRIDE is intentionally not defined so system malloc stays untouched;
# skore uses mi_* only through sk_allocator (library is hidden behind that API).

add_library(mimalloc-static STATIC
    src/static.c
)

target_include_directories(mimalloc-static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Static library; do not export malloc overrides.
target_compile_definitions(mimalloc-static
    PRIVATE
        MI_STATIC_LIB
)

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(mimalloc-static PRIVATE
        -Wno-unknown-pragmas
        -fvisibility=hidden
    )
endif()

set_target_properties(mimalloc-static PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)

# Platform system libs (same set as upstream static target).
if(WIN32)
    target_link_libraries(mimalloc-static PUBLIC
        psapi shell32 user32 advapi32 bcrypt
    )
else()
    find_package(Threads)
    if(Threads_FOUND)
        target_link_libraries(mimalloc-static PUBLIC Threads::Threads)
    endif()
    # librt for clock_gettime on older glibc; may be empty on modern Linux/macOS.
    find_library(SK_MIMALLOC_LIBRT rt)
    if(SK_MIMALLOC_LIBRT)
        target_link_libraries(mimalloc-static PUBLIC ${SK_MIMALLOC_LIBRT})
    endif()
endif()
