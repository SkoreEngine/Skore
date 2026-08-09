file(GLOB_RECURSE SKORE_ENTITIES_SOURCES *.h *.c)

sk_add_plugin(entities SOURCES ${SKORE_ENTITIES_SOURCES})

# No external deps — pure ECS logic; sk_add_plugin already statically links
# sk-core into the SHARED plugin.
