# miniz — Rich Geldreich et al. (vendored, MIT; see LICENSE)
# Amalgamated 3.0.2 single-file build. Archive / stdio / time APIs stripped;
# one-shot deflate/inflate + zlib-style helpers remain for the v2 zlib codec
# (docs/compression-codecs-evaluation.md §4.3).

add_library(miniz STATIC
    src/miniz.c
)

target_include_directories(miniz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Keep the TU lean: no ZIP, no stdio, no time. Leave the zlib-style and
# low-level tdefl/tinfl surfaces enabled for the compression glue.
target_compile_definitions(miniz PUBLIC
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_APIS
    MINIZ_NO_ARCHIVE_WRITING_APIS
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

set_target_properties(miniz PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)
