file(GLOB_RECURSE SKORE_EDITOR_SOURCES CONFIGURE_DEPENDS *.h *.c)

# main.c is the sk-editor executable entry; library target excludes it so tests
# can link the project host without a second main.
set(SKORE_EDITOR_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/main.c)
list(FILTER SKORE_EDITOR_SOURCES EXCLUDE REGEX "main\\.c$")

add_library(sk-editor-lib STATIC ${SKORE_EDITOR_SOURCES})
target_include_directories(sk-editor-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sk-editor-lib PUBLIC sk-app)
set_target_properties(sk-editor-lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(sk-editor ${SKORE_EDITOR_MAIN})
target_link_libraries(sk-editor PRIVATE sk-editor-lib)

if(APPLE)
    target_link_options(sk-editor PRIVATE "LINKER:-export_dynamic")
endif()

if(BUILD_TESTING)
    add_library(sk-editor-tests STATIC ${SKORE_EDITOR_SOURCES})
    target_include_directories(sk-editor-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sk-editor-tests PRIVATE SK_TESTS)
    target_link_libraries(sk-editor-tests PUBLIC sk-app-tests sk-test)
    set_target_properties(sk-editor-tests PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
