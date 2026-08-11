file(GLOB_RECURSE SKORE_RENDER_GRAPH_SOURCES *.h *.c)

sk_add_plugin(render-graph SOURCES ${SKORE_RENDER_GRAPH_SOURCES})

# Public surface includes render_device.h (handles / formats / resource state).
# Link the header INTERFACE only — do not link another plugin's SHARED binary;
# couple to a live RHI via the app registry at runtime.
target_link_libraries(sk-render-graph PRIVATE sk-render-device-lib)
