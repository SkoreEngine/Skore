file(GLOB_RECURSE SKORE_JOLT_SOURCES *.h *.c *.cpp)

sk_add_plugin(jolt SOURCES ${SKORE_JOLT_SOURCES})

# The public surface is C headers only (jolt.h); the implementation is C++ and
# links the vendored Jolt static library (thirdparty/jolt). jolt publishes
# cxx_std_17 so this target compiles jolt.cpp / plugin_entry_point.cpp as
# C++17 (required by <Jolt/...>). C sources in this plugin are unaffected.
target_link_libraries(sk-jolt PRIVATE jolt)

# Physics ECS components register with the entities API (headers only; the
# entities table is resolved at runtime from the app registry).
target_link_libraries(sk-jolt PRIVATE sk-entities-lib)
