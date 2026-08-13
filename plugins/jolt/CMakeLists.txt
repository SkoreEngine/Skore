file(GLOB_RECURSE SKORE_JOLT_SOURCES *.h *.c *.cpp)

sk_add_plugin(jolt SOURCES ${SKORE_JOLT_SOURCES})

# The public surface is C headers only (jolt.h); the implementation is C++ and
# links the vendored Jolt static library (thirdparty/jolt). At this scaffold
# stage the stubs do not call into Jolt yet, but the dependency is wired now so
# the Jolt headers/archive are part of this plugin's build and later
# integration TUs can include <Jolt/...> without touching the build setup.
target_link_libraries(sk-jolt PRIVATE jolt)

# Physics ECS components register with the entities API (headers only; the
# entities table is resolved at runtime from the app registry).
target_link_libraries(sk-jolt PRIVATE sk-entities-lib)
