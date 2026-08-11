# stb_image_write (Sean Barrett / nothings) — vendored, public domain / MIT.
# Single-header PNG/BMP/TGA/JPEG/HDR writer; implementation compiled in one TU
# so consumers can include the header and link the static target.

add_library(stb_image_write STATIC
    stb_image_write_impl.c
    stb_image_write.h
)

target_include_directories(stb_image_write SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(stb_image_write PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(stb_image_write PRIVATE -fvisibility=hidden)
endif()

# stb uses math for some formats; PNG path is fine without, but link m on Unix.
if(UNIX AND NOT APPLE)
    target_link_libraries(stb_image_write PUBLIC m)
endif()
