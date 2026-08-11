# Clay (nicbarker/clay) — vendored, zlib/libpng (see LICENSE.md).
# Single-header immediate-mode layout library, pinned to upstream v0.14
# (commit b25a31c1a152915cd7dd6796e6592273e5a10aac; see README.md).
# The implementation is compiled in exactly one TU (clay_impl.c) so
# consumers include clay.h and link the static target.

add_library(clay STATIC
    clay_impl.c
    clay.h
)

target_include_directories(clay SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(clay PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(clay PRIVATE -fvisibility=hidden)
endif()
