file(GLOB_RECURSE SKORE_UI_SOURCES *.h *.c)

sk_add_plugin(ui SOURCES ${SKORE_UI_SOURCES})

# FreeType + stb_rect_pack: vendored static libs linked into the SHARED plugin.
# sk_add_plugin already statically links sk-core. PRIVATE so third-party
# symbols stay inside the plugin binary (public UI headers do not expose them).
target_link_libraries(sk-ui PRIVATE
    freetype
    stb_rect_pack
)
