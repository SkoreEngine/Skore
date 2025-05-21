

add_library(ufbx src/ufbx.c)

#target_compile_definitions(ufbx PUBLIC UFBX_STATIC=1)
target_compile_definitions(ufbx PUBLIC UFBX_REAL_IS_FLOAT=1)
target_include_directories(ufbx PUBLIC src)