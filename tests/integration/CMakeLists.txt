# Integration tests (APX-61/APX-79/APX-183): real device/resource lifecycle, a
# full offscreen triangle render against the sk-vulkan-render-device +
# sk-dxc-compiler plugins through the app registry, and ResourceObject buffer
# fixture harness loading.
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
    ui_capture.c
    ui_capture_harness.c
    ui_integration.c
    ui_vision_assert.c
    ui_widget_vision.c
    ui_flexbox_vision.c
    ui_test_authoring.c
    ui_interaction_suite.c
    ui_dock.c
    "${CMAKE_SOURCE_DIR}/plugins/ui/ui_test.c"
    render_graph.c
    resource_object_fixtures.c
    resource_object_buffer.c
)
add_executable(sk-isolate-tests
    main.c
    ui_integration.c
    ui_capture_harness.c
    ui_vision_assert.c
    ui_widget_vision.c
    ui_flexbox_vision.c
    ui_test_authoring.c
    ui_interaction_suite.c
    ui_dock.c
    "${CMAKE_SOURCE_DIR}/plugins/ui/ui_test.c"
)
target_link_libraries(sk-isolate-tests PRIVATE sk-app sk-test sk-render-device-lib sk-dxc-compiler-lib sk-ui-lib)
target_include_directories(sk-isolate-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/plugins/ui/testdata")
target_compile_definitions(sk-isolate-tests PRIVATE SK_TESTS SK_TEST_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/test-artifacts" SK_UI_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/plugins/ui/testdata")
set_target_properties(sk-isolate-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(sk-isolate-tests sk-vulkan-render-device sk-render-device sk-test-render-device sk-dxc-compiler sk-ui sk-render-graph)


# App host (sk_app_init / registry / filesystem / platform backends) +
# test registry/Unity (SK_TESTS via sk-test) + render_device.h + dxc_compiler.h
# + ui.h + render_graph.h headers (plugin libs are header INTERFACE only).
target_link_libraries(sk-integration-tests PRIVATE
    sk-app
    sk-test
    sk-render-device-lib
    sk-dxc-compiler-lib
    sk-ui-lib
    sk-render-graph-lib
)

# Local headers (resource_object_fixtures.h) live next to the .c sources.
# Fixture font header lives under plugins/ui/testdata/.
target_include_directories(sk-integration-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_SOURCE_DIR}/plugins/ui/testdata"
)

# Where the render test writes its PPM / PNG artifacts (bin/, next to the exe).
# SK_TEST_ARTIFACT_DIR is the single shared root for all test image/PNG
# artifacts (env-var overridable at runtime; never under the source tree).
# SK_TEST_DATA_DIR points at the source-tree tests/data so fixture path
# resolution works when the binary runs from {build}/bin (CTest cwd).
# Golden source dir is the checked-in testdata (not the build tree).
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-artifacts")
target_compile_definitions(sk-integration-tests PRIVATE
    SK_INTEGRATION_ARTIFACT_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    SK_TEST_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/test-artifacts"
    SK_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
    SK_UI_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/plugins/ui/testdata"
)

# Place next to plugins/ so app_folder resolution finds the plugin DLLs.
set_target_properties(sk-integration-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# Ensure the real Vulkan backend + stub render device + DXC compiler + the
# CPU-mock test render device + UI + render_graph plugins exist (and the
# vendored DXC runtime is copied beside the plugin) before the run.
add_dependencies(sk-integration-tests
    sk-vulkan-render-device
    sk-render-device
    sk-test-render-device
    sk-dxc-compiler
    sk-ui
    sk-render-graph)

# Run from bin/ so the copied DXC runtime (plugins/libdxcompiler.so) is found
# relative to cwd, matching the sk-tests working directory.
add_test(NAME sk-integration-tests COMMAND sk-integration-tests WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# APX-263: focused UI suites (widget-vision + flexbox + interaction bridge).
# Prefer scripts/run-ui-integration-tests.sh for the full one-command workflow
# (also runs plugin interaction tests via sk-tests and prints vision skip notes).
# Prefix wildcards require SK_TEST_FILTER trailing-'*' support in core/test.c.
add_test(
    NAME sk-ui-integration-suites
    COMMAND ${CMAKE_COMMAND} -E env
        "SK_TEST_FILTER=ui_widget_vision_*,ui_flexbox_vision_*,ui_ix_*,ui_vision_assert_*,ui_vision_rubrics_*"
        "SK_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts"
        $<TARGET_FILE:sk-integration-tests>
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
set_tests_properties(sk-ui-integration-suites PROPERTIES
    LABELS "ui;ui-integration;ui-vision"
    TIMEOUT 1800
)

# APX-291: docking public-API e2e (headless layout + input_dispatch).
add_test(
    NAME sk-ui-dock-integration
    COMMAND ${CMAKE_COMMAND} -E env
        "SK_TEST_FILTER=ui_dock_*"
        "SK_TEST_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/test-artifacts"
        $<TARGET_FILE:sk-integration-tests>
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
set_tests_properties(sk-ui-dock-integration PROPERTIES
    LABELS "ui;ui-integration;ui-dock"
    TIMEOUT 180
)

# TEMP repro: repeated harness cycles (remove before commit)
add_executable(sk-repro-cycles repro_cycles.c ui_capture_harness.c)
target_link_libraries(sk-repro-cycles PRIVATE sk-app sk-test sk-render-device-lib sk-dxc-compiler-lib sk-ui-lib)
target_include_directories(sk-repro-cycles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/plugins/ui/testdata")
target_compile_definitions(sk-repro-cycles PRIVATE SK_TESTS SK_TEST_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/test-artifacts")
set_target_properties(sk-repro-cycles PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_dependencies(sk-repro-cycles sk-vulkan-render-device sk-render-device sk-test-render-device sk-dxc-compiler sk-ui sk-render-graph)
