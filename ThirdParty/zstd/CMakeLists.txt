file(GLOB_RECURSE ZSTD_SOURCES src/*.h src/*.c)
add_library(zstd-skore ${ZSTD_SOURCES})
target_include_directories(zstd-skore PUBLIC src)