# stb_rect_pack (Sean Barrett / nothings) — vendored, public domain / MIT.
# Single-header rectangle packer; implementation compiled in one TU so
# consumers can include the header and link the static target.

add_library(stb_rect_pack STATIC
    stb_rect_pack_impl.c
    stb_rect_pack.h
)

target_include_directories(stb_rect_pack SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(stb_rect_pack PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(stb_rect_pack PRIVATE -fvisibility=hidden)
endif()
