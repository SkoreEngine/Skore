file(GLOB_RECURSE SKORE_RENDER_DEVICE_SOURCES *.h *.c)

sk_add_plugin(render-device SOURCES ${SKORE_RENDER_DEVICE_SOURCES})

# No external deps — this is a pure RHI stub.
# Backend implementations (vulkan/, d3d12/, metal/) link their own libs.
