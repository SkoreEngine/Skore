# yyjson — YaoYuan (vendored, MIT)
# Minimal static build: single translation unit (src/yyjson.c).
# sk-core links yyjson PRIVATE so the yyjson headers never leak to consumers
# (same pattern as mimalloc-static); only sk-core internals include yyjson.h.

add_library(yyjson STATIC
    src/yyjson.c
    src/yyjson.h
)

target_include_directories(yyjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(yyjson PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
)
