file(GLOB_RECURSE SKORE_EDITOR_SOURCES CONFIGURE_DEPENDS *.h *.c)

# main.c is the sk-editor executable entry; library target excludes it so tests
# can link the project host without a second main.
set(SKORE_EDITOR_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/main.c)
list(FILTER SKORE_EDITOR_SOURCES EXCLUDE REGEX "main\\.c$")

# smoke_tests.c is SK_TESTS-only (APX-376); excluded from the production
# targets so their TUs stay non-empty outside test builds (-Wpedantic), and
# added back to the test archive below.
set(SKORE_EDITOR_SMOKE_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/smoke_tests.c)
list(FILTER SKORE_EDITOR_SOURCES EXCLUDE REGEX "smoke_tests\\.c$")

# Header-only plugin surfaces (runtime load via app registry). Same pattern as
# player/: ui.h pulls render_device + dxc_compiler types.
set(SKORE_EDITOR_PLUGIN_INCLUDES
    ${SKORE_SOURCE_DIR}/plugins/platform_window
    ${SKORE_SOURCE_DIR}/plugins/ui
    ${SKORE_SOURCE_DIR}/plugins/render_device
    ${SKORE_SOURCE_DIR}/plugins/dxc_compiler
)

add_library(sk-editor-lib STATIC ${SKORE_EDITOR_SOURCES})
target_include_directories(sk-editor-lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SKORE_EDITOR_PLUGIN_INCLUDES}
)
target_link_libraries(sk-editor-lib PUBLIC sk-foundation stb_image)
set_target_properties(sk-editor-lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(sk-editor ${SKORE_EDITOR_MAIN})
target_link_libraries(sk-editor PRIVATE sk-editor-lib)

if(APPLE)
    target_link_options(sk-editor PRIVATE "LINKER:-export_dynamic")
endif()

if(BUILD_TESTING)
    add_library(sk-editor-tests STATIC ${SKORE_EDITOR_SOURCES} ${SKORE_EDITOR_SMOKE_TESTS})
    target_include_directories(sk-editor-tests PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SKORE_EDITOR_PLUGIN_INCLUDES}
    )
    target_compile_definitions(sk-editor-tests PRIVATE SK_TESTS)
    target_link_libraries(sk-editor-tests PUBLIC sk-foundation-tests sk-test stb_image)
    set_target_properties(sk-editor-tests PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
