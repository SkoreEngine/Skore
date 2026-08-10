file(GLOB_RECURSE SKORE_RENDER_GRAPH_SOURCES *.h *.c)

sk_add_plugin(render-graph SOURCES ${SKORE_RENDER_GRAPH_SOURCES})

# No external deps — skeleton stub; sk_add_plugin already statically links
# sk-core into the SHARED plugin.
