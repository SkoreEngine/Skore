# Integration tests (APX-61/APX-79): real device/resource lifecycle and a full
# offscreen triangle render against the sk-vulkan-render-device + sk-dxc-compiler
# plugins through the app registry.
#
# Runs as its own ctest so the Vulkan path can be exercised without a window.
# sk_app_init auto-loads the plugin DLLs from {app_folder}/plugins; the
# executable is placed next to them in bin/ (same layout as sk-tests). The
# vendored DXC runtime is required (missing lib fails the triangle test).
#
# Skips cleanly when no Vulkan loader/ICD is present.

if(NOT BUILD_TESTING)
    return()
endif()

add_executable(sk-integration-tests
    main.c
    vulkan_device.c
    vulkan_triangle.c
    test_render_device.c
    ui_render.c
)

# App host (sk_app_init / registry / filesystem / platform backends) +
# test registry/Unity (SK_TESTS via sk-test) + render_device.h + dxc_compiler.h
# + ui.h headers (sk-render-device-lib / sk-dxc-compiler-lib / sk-ui-lib).
target_link_libraries(sk-integration-tests PRIVATE
    sk-app
    sk-test
    sk-render-device-lib
    sk-dxc-compiler-lib
    sk-ui-lib
)

# Fixture font header lives under plugins/ui/testdata/.
target_include_directories(sk-integration-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/plugins/ui/testdata"
)

# Where the render test writes its PPM / PNG artifacts (bin/, next to the exe).
# Golden source dir is the checked-in testdata (not the build tree).
target_compile_definitions(sk-integration-tests PRIVATE
    SK_INTEGRATION_ARTIFACT_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    SK_UI_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/plugins/ui/testdata"
)

# Place next to plugins/ so app_folder resolution finds the plugin DLLs.
set_target_properties(sk-integration-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# Ensure the real Vulkan backend + stub render device + DXC compiler + the
# CPU-mock test render device + UI plugin exist (and the vendored DXC runtime
# is copied beside the plugin) before the run.
add_dependencies(sk-integration-tests sk-vulkan-render-device sk-render-device sk-test-render-device sk-dxc-compiler sk-ui)

# Run from bin/ so the copied DXC runtime (plugins/libdxcompiler.so) is found
# relative to cwd, matching the sk-tests working directory.
add_test(NAME sk-integration-tests COMMAND sk-integration-tests WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
