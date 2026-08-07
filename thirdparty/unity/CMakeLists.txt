# Unity — ThrowTheSwitch.org (vendored, MIT)
# Minimal in-tree build: core framework only.

add_library(unity STATIC
    src/unity.c
)

target_include_directories(unity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Project types use u64/i64 everywhere; always enable 64-bit asserts
# (required on 32-bit hosts where Unity defaults UNITY_SUPPORT_64 off).
target_compile_definitions(unity PUBLIC
    UNITY_INCLUDE_DOUBLE
    UNITY_SUPPORT_64
)

set_target_properties(unity PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
