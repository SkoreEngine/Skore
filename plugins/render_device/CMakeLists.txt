file(GLOB_RECURSE SKORE_RENDER_DEVICE_SOURCES *.h *.c)
# APX-275: exclude the internal-only extension (*.internal.h) — pattern rule, never exported/installed.
list(FILTER SKORE_RENDER_DEVICE_SOURCES EXCLUDE REGEX "\\.internal\\.h$")

sk_add_plugin(render-device SOURCES ${SKORE_RENDER_DEVICE_SOURCES})

# No external deps — this is a pure RHI stub.
# Backend implementations (vulkan/, d3d12/, metal/) link their own libs.
