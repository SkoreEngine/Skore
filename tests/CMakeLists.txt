# Test host bootstrap only. Test bodies live next to production code under
# `#ifdef SK_TESTS` (see core/test.h and AGENTS.md).

enable_testing()

if(NOT BUILD_TESTING)
    return()
endif()

add_executable(sk-tests main.c)

# sk-app-tests pulls sk-core-tests + sk-test transitively.
# sk-editor-tests covers editor project open/import as a core-API consumer.
target_link_libraries(sk-tests PRIVATE
    sk-app-tests
    sk-editor-tests
    sk-platform-window-lib
    sk-entities-lib
    sk-vulkan-render-device-lib
    sk-dxc-compiler-lib
    sk-render-graph-lib
    sk-ui-lib
)

# Export only the stacktrace test helpers so dladdr can resolve their names in
# the test executable. Full --export-dynamic would leak the host's
# statically-linked sk-core / sk-test / mimalloc copies into the global scope,
# interposing over the plugins' own static copies at dlopen time (plugins must
# keep their registries and allocators private; see AGENTS.md "Plugins are
# SHARED libraries that statically link sk-core").
if(UNIX AND NOT APPLE)
    target_link_options(sk-tests PRIVATE
        "LINKER:--export-dynamic-symbol=stacktrace_test_leaf"
        "LINKER:--export-dynamic-symbol=stacktrace_test_middle")
elseif(APPLE)
    # macOS dladdr resolves executable symbols from the Mach-O symbol table
    # (dyld's dladdr returns dli_sname for unstripped executables), so no
    # export flags are needed here.
endif()

# Pull all objects so MSVC/GCC do not drop TUs whose only live symbols are
# constructor-registered SK_TEST functions.
if(MSVC)
    target_link_options(sk-tests PRIVATE
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-core-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-app-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-editor-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-test>"
    )
elseif(APPLE)
    target_link_options(sk-tests PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:sk-core-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-app-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-editor-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-test>"
    )
else()
    target_link_options(sk-tests PRIVATE
        "LINKER:--whole-archive"
        "$<TARGET_FILE:sk-core-tests>"
        "$<TARGET_FILE:sk-app-tests>"
        "$<TARGET_FILE:sk-editor-tests>"
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

add_dependencies(sk-tests sk-platform-window sk-entities sk-render-device sk-vulkan-render-device sk-dxc-compiler sk-render-graph sk-ui)

# Run from the runtime output dir ({build}/bin) so plugin shared libraries and
# the copied DXC runtime (plugins/libdxcompiler.so) are found relative to cwd.
add_test(NAME sk-tests COMMAND sk-tests WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Opt-in crash-trigger tool: raises each handled fault kind so the crash
# handler can be exercised deliberately (child-process tests + sample capture).
# BUILD_TESTING only — never shipped with production binaries.
add_executable(sk-crash-trigger crash_trigger/main.c)
target_link_libraries(sk-crash-trigger PRIVATE sk-core)
set_target_properties(sk-crash-trigger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
# Export dynamic symbols on ELF so sample captures can later resolve frames
# offline; Apple resolves unstripped Mach-O without extra flags.
if(UNIX AND NOT APPLE)
    target_link_options(sk-crash-trigger PRIVATE "LINKER:--export-dynamic")
elseif(APPLE)
    target_link_options(sk-crash-trigger PRIVATE "LINKER:-export_dynamic")
endif()
add_dependencies(sk-tests sk-crash-trigger)

# Integration tests: device/resource lifecycle against the real Vulkan backend.
add_subdirectory(integration)
