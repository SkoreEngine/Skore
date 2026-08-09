# Integration tests (APX-61): real device/resource lifecycle against the
# sk-vulkan-render-device plugin through the app registry.
#
# Runs as its own ctest so the Vulkan path can be exercised without a window or
# shaders. sk_app_init auto-loads the plugin DLLs from {app_folder}/plugins;
# the executable is placed next to them in bin/ (same layout as sk-tests).
#
# Skips cleanly when no Vulkan loader/ICD is present (e.g. CI without Vulkan).

if(NOT BUILD_TESTING)
    return()
endif()

add_executable(sk-integration-tests
    main.c
    vulkan_device.c
)

# App host (sk_app_init / registry / filesystem / platform backends) +
# test registry/Unity (SK_TESTS via sk-test) + render_device.h headers.
target_link_libraries(sk-integration-tests PRIVATE
    sk-app
    sk-test
    sk-render-device-lib
)

# Place next to plugins/ so app_folder resolution finds the plugin DLLs.
set_target_properties(sk-integration-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# Ensure the real Vulkan backend + stub plugins exist before the test runs.
add_dependencies(sk-integration-tests sk-vulkan-render-device sk-render-device)

add_test(NAME sk-integration-tests COMMAND sk-integration-tests)
