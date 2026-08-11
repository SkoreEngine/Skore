file(GLOB_RECURSE SKORE_UI_SOURCES *.h *.c)

sk_add_plugin(ui SOURCES ${SKORE_UI_SOURCES})

# FreeType + stb_rect_pack: vendored static libs linked into the SHARED plugin.
# sk_add_plugin already statically links sk-core. PRIVATE so third-party
# symbols stay inside the plugin binary (public UI headers do not expose them).
# render_device / dxc_compiler: header-only coupling for the GPU renderer API
# (runtime lookup of tables via the app registry; no link to other plugin .c).
target_link_libraries(sk-ui PRIVATE
    freetype
    stb_rect_pack
    clay
    sk-render-device-lib
    sk-dxc-compiler-lib
)

# Hosts that include ui.h need the RHI / DXC headers on the include path.
target_link_libraries(sk-ui-lib INTERFACE
    sk-render-device-lib
    sk-dxc-compiler-lib
)
