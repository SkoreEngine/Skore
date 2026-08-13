file(GLOB_RECURSE SKORE_TEST_RENDER_DEVICE_SOURCES *.h *.c)
# APX-275: exclude the internal-only extension (*.internal.h) — pattern rule, never exported/installed.
list(FILTER SKORE_TEST_RENDER_DEVICE_SOURCES EXCLUDE REGEX "\\.internal\\.h$")

sk_add_plugin(test-render-device SOURCES ${SKORE_TEST_RENDER_DEVICE_SOURCES})

# Test double for the RHI: needs only the render_device.h public surface.
# Link the render_device header interface, not the plugin binary.
target_link_libraries(sk-test-render-device PRIVATE sk-render-device-lib)
