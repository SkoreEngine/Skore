file(GLOB_RECURSE SKORE_UI_SOURCES *.h *.c)

sk_add_plugin(ui SOURCES ${SKORE_UI_SOURCES})

# FreeType + stb_rect_pack + stb_image_write + msdf-atlas-c: vendored static
# libs linked into the SHARED plugin. sk_add_plugin already statically links
# sk-core. PRIVATE so third-party symbols stay inside the plugin binary
# (public UI headers do not expose them). FreeType remains for the current
# R8 paint path; msdf-atlas-c generates MSDF atlases (APX-265) and pulls
# FreeType privately through msdfgen for outline import. render_device /
# dxc_compiler: header-only coupling for the GPU renderer API (runtime lookup
# of tables via the app registry; no link to other plugin .c).
target_link_libraries(sk-ui PRIVATE
    freetype
    msdf-atlas-c
    stb_rect_pack
    clay
    stb_image_write
    stb_image
    sk-render-device-lib
    sk-dxc-compiler-lib
)

# Single shared test-artifact root under the build tree (never the source tree).
# Override at runtime with SK_TEST_ARTIFACT_DIR.
# SK_UI_PLUGIN_BUILD enables authoring sample tests that call ui_get_api_table.
target_compile_definitions(sk-ui PRIVATE
    SK_TEST_ARTIFACT_DIR="${CMAKE_BINARY_DIR}/test-artifacts"
    SK_UI_PLUGIN_BUILD=1
)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-artifacts")

# Hosts that include ui.h need the RHI / DXC headers on the include path.
target_link_libraries(sk-ui-lib INTERFACE
    sk-render-device-lib
    sk-dxc-compiler-lib
)
