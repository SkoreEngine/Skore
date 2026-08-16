add_executable(sk-sandbox
    dock_preview_sandbox.c
    widget_review.c)

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

# APX-366: v2 editor shell render check (sk-sandbox --shell equivalent, own
# main()). Links sk-editor-lib for the editor registry + shell implementation.
add_executable(sk-sandbox-shell
    editor_shell_sandbox.c)

target_link_libraries(sk-sandbox-shell PRIVATE sk-foundation sk-editor-lib)

if(APPLE)
    target_link_options(sk-sandbox-shell PRIVATE "LINKER:-export_dynamic")
endif()

target_include_directories(sk-sandbox-shell PRIVATE
    ${SKORE_SOURCE_DIR}/editor
    ${SKORE_SOURCE_DIR}/plugins/ui
    ${SKORE_SOURCE_DIR}/plugins/ui/testdata
    ${SKORE_SOURCE_DIR}/plugins/render_device
    ${SKORE_SOURCE_DIR}/plugins/dxc_compiler)

add_dependencies(sk-sandbox-shell
    sk-ui
    sk-vulkan-render-device
    sk-dxc-compiler)

# APX-139: --ui-migration dual-stack render check (own main()).
add_executable(sk-sandbox-ui-migration
    ui_migration_sandbox.c)

target_link_libraries(sk-sandbox-ui-migration PRIVATE sk-foundation sk-editor-lib)

if(APPLE)
    target_link_options(sk-sandbox-ui-migration PRIVATE "LINKER:-export_dynamic")
endif()

target_include_directories(sk-sandbox-ui-migration PRIVATE
    ${SKORE_SOURCE_DIR}/editor
    ${SKORE_SOURCE_DIR}/plugins/ui
    ${SKORE_SOURCE_DIR}/plugins/ui/testdata
    ${SKORE_SOURCE_DIR}/plugins/render_device
    ${SKORE_SOURCE_DIR}/plugins/dxc_compiler)

add_dependencies(sk-sandbox-ui-migration
    sk-ui
    sk-vulkan-render-device
    sk-dxc-compiler)
