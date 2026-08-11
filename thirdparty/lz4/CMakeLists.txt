# LZ4 — Yann Collet (vendored, BSD-2-Clause; see LICENSE)
# One-shot block API only (lz4.c / lz4.h). No HC/frame/xxhash — those land
# when streaming or LZ4-HC is enabled (docs/compression-codecs-evaluation.md).

add_library(lz4 STATIC
    src/lz4.c
)

target_include_directories(lz4 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(lz4 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)
