# Flecs 4.1.6 — vendored single-file ECS library (amalgamated flecs.c + flecs.h).
# Static library; the benchmark links it directly. License is MIT (see LICENSE).

add_library(flecs STATIC flecs.c)
target_include_directories(flecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
