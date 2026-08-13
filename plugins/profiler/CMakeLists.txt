file(GLOB_RECURSE SKORE_PROFILER_SOURCES *.h *.c)
# APX-275: exclude the internal-only extension (*.internal.h) — pattern rule, never exported/installed.
list(FILTER SKORE_PROFILER_SOURCES EXCLUDE REGEX "\\.internal\\.h$")

sk_add_plugin(profiler SOURCES ${SKORE_PROFILER_SOURCES})

# Public surface includes render_device.h (sk_command_buffer_t for GPU zones,
# sk_render_device_t for init). Link the header INTERFACE only — the live RHI
# is resolved through the app registry at runtime, never linked directly.
target_link_libraries(sk-profiler PRIVATE sk-render-device-lib)
