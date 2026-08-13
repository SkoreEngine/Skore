add_executable(sk-sandbox dock_preview_sandbox.c)

# App entry: sk-foundation (sk_app_init + platform APIs + engine utilities).
target_link_libraries(sk-sandbox PRIVATE sk-foundation)

if(APPLE)
    target_link_options(sk-sandbox PRIVATE "LINKER:-export_dynamic")
endif()

# Header-only access to plugin APIs (plugins are loaded at runtime).
target_include_directories(sk-sandbox PRIVATE
    ${SKORE_SOURCE_DIR}/plugins/ui
    ${SKORE_SOURCE_DIR}/plugins/ui/testdata
    ${SKORE_SOURCE_DIR}/plugins/render_device
    ${SKORE_SOURCE_DIR}/plugins/dxc_compiler)

add_dependencies(sk-sandbox
    sk-ui
    sk-vulkan-render-device
    sk-dxc-compiler)
