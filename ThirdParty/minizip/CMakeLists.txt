file(GLOB_RECURSE minizip_SRC src/*.h src/*.c)
add_library(SK-minizip STATIC ${minizip_SRC})
target_link_libraries(SK-minizip PRIVATE SK-zlib)

target_include_directories(SK-minizip PUBLIC src)