# zstd — Meta Platforms (vendored, BSD-3-Clause; see LICENSE)
# zstd 1.5.6 single-threaded plain-C build (no ZSTD_MULTITHREAD). Mirrors the
# main-branch vendored tree (ThirdParty/zstd); huf_decompress_amd64.S is not
# compiled (the project builds zstd as plain C, see
# docs/compression-design-v2.md §8). sk-core links this target PRIVATE, so zstd
# headers never leak into the public sk-core interface.

add_library(zstd STATIC
    src/common/debug.c
    src/common/entropy_common.c
    src/common/error_private.c
    src/common/fse_decompress.c
    src/common/pool.c
    src/common/threading.c
    src/common/xxhash.c
    src/common/zstd_common.c
    src/compress/fse_compress.c
    src/compress/hist.c
    src/compress/huf_compress.c
    src/compress/zstd_compress.c
    src/compress/zstd_compress_literals.c
    src/compress/zstd_compress_sequences.c
    src/compress/zstd_compress_superblock.c
    src/compress/zstd_double_fast.c
    src/compress/zstd_fast.c
    src/compress/zstd_lazy.c
    src/compress/zstd_ldm.c
    src/compress/zstd_opt.c
    src/compress/zstdmt_compress.c
    src/decompress/huf_decompress.c
    src/decompress/zstd_ddict.c
    src/decompress/zstd_decompress.c
    src/decompress/zstd_decompress_block.c
)

target_include_directories(zstd PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ZSTD_DISABLE_ASM: the only assembly in-tree is huf_decompress_amd64.S; this
# macro forces the C Huffman decode loops so the .S stays out of the build.
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM)

set_target_properties(zstd PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)
