# Test host bootstrap + reusable harness. Test bodies live next to production
# code under `#ifdef SK_TESTS` (see foundation/test.h and AGENTS.md).
# Integration cases (Vulkan / UI capture / physics scene / MSDF smoke) live in
# the skore-test-suite repo. Harness sources in integration/ stay here.

enable_testing()

if(NOT BUILD_TESTING)
    return()
endif()

# APX-284: no-static-variables + no-deleted-accessor guard for the foundation
# module and its plugins (scripts/check-no-statics.py + no-statics-allowlist.txt).
# Runs in every CI matrix cell (all platforms, Debug and Release) via ctest.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME sk-no-statics-guard
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check-no-statics.py
    )
    sk_ctest_mark_unit(sk-no-statics-guard)
else()
    message(WARNING "Python3 not found; sk-no-statics-guard test disabled (run scripts/check-no-statics.py manually)")
endif()

# Compression header compile-check (APX-169) and symbol-mapping conformance
# (APX-171) live in skore-test-suite.

add_executable(sk-tests main.c)

# sk-foundation-tests pulls sk-test transitively.
# sk-editor-tests covers editor project open/import as a foundation-API consumer.
target_link_libraries(sk-tests PRIVATE
    sk-foundation-tests
    sk-editor-tests
    sk-platform-window-lib
    sk-entities-lib
    sk-vulkan-render-device-lib
    sk-dxc-compiler-lib
    sk-render-graph-lib
    sk-profiler-lib
    sk-ui-lib
)

# Export only the stacktrace test helpers so dladdr can resolve their names in
# the test executable. Full --export-dynamic would leak the host's
# statically-linked sk-foundation / sk-test / mimalloc copies into the global scope,
# interposing over the plugins' own static copies at dlopen time (plugins must
# keep their registries and allocators private; see AGENTS.md "Plugins are
# SHARED libraries that statically link sk-foundation").
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
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-foundation-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-editor-tests>"
        "/WHOLEARCHIVE:$<TARGET_FILE:sk-test>"
    )
elseif(APPLE)
    target_link_options(sk-tests PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:sk-foundation-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-editor-tests>"
        "LINKER:-force_load,$<TARGET_FILE:sk-test>"
    )
else()
    target_link_options(sk-tests PRIVATE
        "LINKER:--whole-archive"
        "$<TARGET_FILE:sk-foundation-tests>"
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

add_dependencies(sk-tests sk-platform-window sk-entities sk-render-device sk-vulkan-render-device sk-dxc-compiler sk-render-graph sk-profiler sk-ui sk-jolt)

# Run from the runtime output dir ({build}/bin) so plugin shared libraries and
# the copied DXC runtime (plugins/libdxcompiler.so) are found relative to cwd.
add_test(NAME sk-tests COMMAND sk-tests WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
sk_ctest_mark_unit(sk-tests)

# Opt-in crash-trigger tool: raises each handled fault kind so the crash
# handler can be exercised deliberately (child-process tests + sample capture).
# BUILD_TESTING only — never shipped with production binaries.
add_executable(sk-crash-trigger crash_trigger/main.c)
target_link_libraries(sk-crash-trigger PRIVATE sk-foundation)
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

# Vendored C-API smoke is registered in thirdparty/; label it with the unit suite.
if(TEST msdf-atlas-c-smoke)
    sk_ctest_mark_unit(msdf-atlas-c-smoke)
endif()

# Integration cases live in skore-test-suite. Reusable harness sources remain
# in integration/ (ui_capture_harness, text-screenshot, fixture loaders,
# integration main.c) for the test suite and later task runners.
