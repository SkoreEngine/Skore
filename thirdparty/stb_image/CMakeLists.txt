# stb_image (Sean Barrett / nothings) — vendored, public domain / MIT.
# Single-header image loader; implementation compiled in one TU so
# consumers can include the header and link the static target.
# Used for PNG round-trip verification of UI capture image writes.

add_library(stb_image STATIC
    stb_image_impl.c
    stb_image.h
)

target_include_directories(stb_image SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(stb_image PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(stb_image PRIVATE -fvisibility=hidden)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(stb_image PUBLIC m)
endif()
