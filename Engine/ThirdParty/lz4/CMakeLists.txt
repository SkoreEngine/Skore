file(GLOB_RECURSE LZ4_SOURCES src/*.h src/*.c)
add_library(lz4 ${LZ4_SOURCES})
target_include_directories(lz4 PUBLIC src)