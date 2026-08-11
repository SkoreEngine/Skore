file(GLOB_RECURSE SKORE_ENTITIES_SOURCES *.h *.c)

sk_add_plugin(entities SOURCES ${SKORE_ENTITIES_SOURCES})

# Instrumentation macros (profiler.h) — the profiler plugin is resolved via
# the app registry at runtime; only headers are needed here (render_device.h
# for the GPU-zone command-buffer handles inside the macros).
target_include_directories(sk-entities PRIVATE ${CMAKE_SOURCE_DIR}/plugins/profiler)
target_link_libraries(sk-entities PRIVATE sk-render-device-lib)
