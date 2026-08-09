# Test host bootstrap only. Test bodies live next to production code under
# `#ifdef SK_TESTS` (see core/test.h and AGENTS.md).

enable_testing()

if(NOT BUILD_TESTING)
    return()
endif()

add_executable(sk-tests main.c)

# sk-app-tests pulls sk-core-tests + sk-test transitively.
target_link_libraries(sk-tests PRIVATE
    sk-app-tests
    sk-platform-window-lib
    sk-entities-lib
)

# Pull all objects so MSVC/GCC do not drop TUs whose only live symbols are
# constructor-registered SK_TEST functions.
if(MSVC)
    target_link_options(sk-tests PRIVATE
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-core-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-app-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-test>"
    )
elseif(APPLE)
    target_link_options(sk-tests PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:sk-core-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-app-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-test>"
    )
else()
    target_link_options(sk-tests PRIVATE
        "LINKER:--whole-archive"
        "$<TARGET_FILE:sk-core-tests>"
        "$<TARGET_FILE:sk-app-tests>"
        "$<TARGET_FILE:sk-test>"
        "LINKER:--no-whole-archive"
    )
endif()

# Place next to plugins/ so app_folder resolution finds DLLs.
set_target_properties(sk-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(sk-tests sk-platform-window sk-entities)

add_test(NAME sk-tests COMMAND sk-tests)
