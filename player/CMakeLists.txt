file(GLOB_RECURSE SKORE_PLAYER_SOURCES *.h *.c)

add_executable(sk-player ${SKORE_PLAYER_SOURCES})

# App entry: sk-app (sk_app_init + platform APIs) + core (transitively via sk-app).
target_link_libraries(sk-player PRIVATE sk-app)

# Export the executable's dynamic symbols so the POSIX stacktrace backend can
# resolve function names in crash traces via dladdr (-rdynamic equivalent).
# Linux only: --export-dynamic would leak sk-player's statically-linked
# sk-core/mimalloc copies into the global scope and interpose over the
# plugins' own static copies at dlopen time, so it is deliberately not used;
# unknown frames fall back to module + base offset (see stacktrace.h).
if(APPLE)
    # macOS uses the two-level namespace, so plugin symbols stay bound to the
    # plugins' own static copies; exporting the player's symbols is safe here.
    target_link_options(sk-player PRIVATE "LINKER:-export_dynamic")
endif()

# Header-only access to plugin APIs (plugins are loaded at runtime).
# Include paths used instead of plugin-lib targets because player is
# added before plugins/ in the root CMakeLists. ui.h pulls render_device and
# dxc_compiler headers for the GPU renderer types (not linked here).
target_include_directories(sk-player PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/platform_window
    ${CMAKE_SOURCE_DIR}/plugins/ui
    ${CMAKE_SOURCE_DIR}/plugins/ui/testdata
    ${CMAKE_SOURCE_DIR}/plugins/render_graph
    ${CMAKE_SOURCE_DIR}/plugins/render_device
    ${CMAKE_SOURCE_DIR}/plugins/dxc_compiler)

# Ensure sk-ui is built so auto-load from {app_folder}/plugins finds it.
# (add_dependencies after plugins are defined via root order is not available
# here; root CMakeLists places player before plugins. Hosts that run from
# build/bin still get sk-ui when the full tree is built.)
