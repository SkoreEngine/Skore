file(GLOB_RECURSE ZLIB_SRC src/*.h src/*.c)
add_library(SK-zlib STATIC ${ZLIB_SRC})
target_include_directories(SK-zlib PUBLIC src)