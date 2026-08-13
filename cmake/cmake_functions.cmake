# Skore CMake helpers — type-id embedding and shared build utilities.
#
# SK_TYPE_ID("name")  →  SK_TYPE_ID("name", 0x....ULL, 0x....ULL)
# 128-bit id = MD5(name) split into two little-documented u64 halves (hex order).

# ---------------------------------------------------------------------------
# sk_type_id_hash_parts(<name> <out_lo> <out_hi>)
#   MD5 of <name> → two 16-hex-digit u64 strings (no 0x / ULL suffix).
# ---------------------------------------------------------------------------
function(sk_type_id_hash_parts type_name out_lo out_hi)
    string(MD5 _hash_hex "${type_name}")
    string(SUBSTRING "${_hash_hex}" 0 16 _lo_hex)
    string(SUBSTRING "${_hash_hex}" 16 16 _hi_hex)
    set(${out_lo} "${_lo_hex}" PARENT_SCOPE)
    set(${out_hi} "${_hi_hex}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# sk_embed_type_ids_in_file(<filepath>)
#   In-place: replace unexpanded SK_TYPE_ID("…") with hashed 3-arg form.
#   Already-expanded forms (extra comma args) are left untouched.
# ---------------------------------------------------------------------------
function(sk_embed_type_ids_in_file filepath)
    if(NOT EXISTS "${filepath}")
        message(WARNING "sk_embed_type_ids_in_file: file not found: ${filepath}")
        return()
    endif()

    file(READ "${filepath}" _content)
    set(_result "${_content}")
    set(_changed FALSE)

    # Match only the one-string form: SK_TYPE_ID("name")
    # Does not match SK_TYPE_ID("name", 0x…, 0x…)
    set(_pattern "SK_TYPE_ID[ \t]*\\([ \t]*\"([^\"]+)\"[ \t]*\\)")

    while(TRUE)
        string(REGEX MATCH "${_pattern}" _match "${_result}")
        if(NOT _match)
            break()
        endif()

        set(_type_name "${CMAKE_MATCH_1}")
        sk_type_id_hash_parts("${_type_name}" _lo_hex _hi_hex)

        set(_replacement
            "SK_TYPE_ID(\"${_type_name}\", 0x${_lo_hex}ULL, 0x${_hi_hex}ULL)")

        string(REPLACE "${_match}" "${_replacement}" _result "${_result}")
        set(_changed TRUE)
    endwhile()

    if(_changed)
        file(WRITE "${filepath}" "${_result}")
        message(STATUS "sk_type_id: embedded hashes in ${filepath}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# sk_embed_type_ids(<file>...)
#   Embed type ids in each listed source/header file.
# ---------------------------------------------------------------------------
function(sk_embed_type_ids)
    foreach(_file IN LISTS ARGN)
        sk_embed_type_ids_in_file("${_file}")
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# sk_embed_type_ids_in_dir(<dir>...)
#   Recursively process *.h / *.c / *.hpp / *.cpp under each directory.
# ---------------------------------------------------------------------------
function(sk_embed_type_ids_in_dir)
    set(_files "")
    foreach(_dir IN LISTS ARGN)
        if(IS_DIRECTORY "${_dir}")
            file(GLOB_RECURSE _found
                "${_dir}/*.h"
                "${_dir}/*.c"
                "${_dir}/*.hpp"
                "${_dir}/*.cpp"
            )
            list(APPEND _files ${_found})
        else()
            message(WARNING "sk_embed_type_ids_in_dir: not a directory: ${_dir}")
        endif()
    endforeach()
    if(_files)
        sk_embed_type_ids(${_files})
    endif()
endfunction()

# ---------------------------------------------------------------------------
# sk_target_enable_tests(<target>)
#   Compile with SK_TESTS and link sk-test when BUILD_TESTING and not Release.
#   Never enables SK_TESTS for CONFIG:Release (do not ship tests).
# ---------------------------------------------------------------------------
function(sk_target_enable_tests target)
    if(NOT BUILD_TESTING)
        return()
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "sk_target_enable_tests: unknown target ${target}")
    endif()

    # Multi-config: Debug / RelWithDebInfo / MinSizeRel get tests; Release never.
    # Single-config: same via $<CONFIG:...>.
    target_compile_definitions(${target} PRIVATE
        $<$<AND:$<NOT:$<CONFIG:Release>>,$<NOT:$<CONFIG:MinSizeRel>>>:SK_TESTS>
    )
    target_link_libraries(${target} PRIVATE
        $<$<AND:$<NOT:$<CONFIG:Release>>,$<NOT:$<CONFIG:MinSizeRel>>>:sk-test>
    )
endfunction()

# ---------------------------------------------------------------------------
# sk_add_plugin(<name>
#   SOURCES <src>...
#   [NO_STATIC]   # ignored (kept for call-site compatibility; static twins removed)
# )
#
# <name> is the short plugin name without the sk- prefix (e.g. "platform-window",
# "example"). Creates:
#
#   sk-<name>-plugin       SHARED library (production load path)
#   sk-<name>-plugin-lib   INTERFACE target exporting this plugin's headers
#
# The SHARED plugin is written to {runtime}/plugins so sk_app_init can scan
# that folder next to the executable and auto-load every shared library.
#
# When BUILD_TESTING and not Release: SK_TESTS + link sk-test (plugin-local
# Unity registry). sk_plugin_run_tests is compiled only under SK_TESTS.
#
# Callers may add extra includes/links on sk-<name>-plugin after this returns.
# ---------------------------------------------------------------------------
function(sk_add_plugin name)
    cmake_parse_arguments(SK_PLUGIN "NO_STATIC" "" "SOURCES" ${ARGN})

    if(NOT name)
        message(FATAL_ERROR "sk_add_plugin: name is required")
    endif()
    if(NOT SK_PLUGIN_SOURCES)
        message(FATAL_ERROR "sk_add_plugin(${name}): SOURCES is required")
    endif()

    # APX-275: headers with the internal-only extension (*.internal.h) are
    # compile-time-only and are never part of the exported header set. This is
    # an exclusion pattern, not a filename list — any future internal header
    # is excluded automatically. They stay reachable for this plugin's own TUs
    # via the PRIVATE include dir below.
    list(FILTER SK_PLUGIN_SOURCES EXCLUDE REGEX "\\.internal\\.h$")

    set(_plugin "sk-${name}")
    set(_lib    "sk-${name}-lib")

    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_plugins_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/plugins")
    else()
        set(_plugins_dir "${CMAKE_BINARY_DIR}/bin/plugins")
    endif()

    # Shared plugin (production / auto-load path). No static twin.
    # PREFIX "" so Unix/macOS match Windows naming: sk-<name>-plugin.so/.dylib
    # (not libsk-…); host scan and tests use the unprefixed name on every OS.
    add_library(${_plugin} SHARED ${SK_PLUGIN_SOURCES})
    target_include_directories(${_plugin} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${_plugin} PRIVATE sk-foundation)
    set_target_properties(${_plugin} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${_plugins_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${_plugins_dir}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_plugins_dir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_plugins_dir}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_plugins_dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_plugins_dir}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_plugins_dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_plugins_dir}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${_plugins_dir}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${_plugins_dir}"
    )
    # In-source tests: non-Release only (never ship tests in Release plugins).
    sk_target_enable_tests(${_plugin})

    # Darwin -exported_symbol is exclusive. v2 also passes
    # -exported_symbol,_sk_logger_bind_api, which would hide the plugin
    # entry points from dlsym (macOS CI: "plugin missing sk_plugin_entry_point").
    # Re-list the host-resolved symbols so they stay visible. run_tests exists
    # only in non-Release plugin builds.
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_options(${_plugin} PRIVATE
            "LINKER:-exported_symbol,_sk_plugin_entry_point"
            "$<$<AND:$<NOT:$<CONFIG:Release>>,$<NOT:$<CONFIG:MinSizeRel>>>:LINKER:-exported_symbol,_sk_plugin_run_tests>"
        )
    endif()

    # Export public headers for consumers / host tests that include this plugin.
    # APX-275: the consumer-facing header surface is public-only — internal
    # *.internal.h files are excluded from every header glob (see above) and
    # from the root install rule, so they are never installed or exported.
    add_library(${_lib} INTERFACE)
    target_include_directories(${_lib} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

# ---------------------------------------------------------------------------
# sk_copy_dxc_shared_library(<target>)
#   POST_BUILD-copy the platform DirectX Shader Compiler (DXC) shared library
#   into the plugins output dir used by sk_add_plugin (CMAKE_RUNTIME_OUTPUT_
#   DIRECTORY/plugins or ${CMAKE_BINARY_DIR}/bin/plugins).
#
#   Modeled on skore main's add_binary_file(): WIN32 → bin/win-x64/dxcompiler
#   .dll, APPLE → bin/macOS/libdxcompiler.dylib, UNIX → bin/linux-x64/
#   libdxcompiler.so. Vendored runtimes: win-x64 (dxcompiler.dll), macOS
#   (libdxcompiler.dylib, universal x86_64+arm64 from LunarG Vulkan SDK), and
#   linux-x64 (libdxcompiler.so + libdxil.so from Microsoft DXC linux release).
#   Missing platform files emit a WARNING and skip the copy. libdxil.so is
#   copied alongside so DXC can dlopen it for DXIL validation if needed. Call
#   from a plugin CMakeLists after the plugin target exists.
# ---------------------------------------------------------------------------
function(sk_copy_dxc_shared_library target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "sk_copy_dxc_shared_library: unknown target ${target}")
    endif()

    # thirdparty/dxc lives relative to this file (cmake/cmake_functions.cmake).
    set(_dxc_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../thirdparty/dxc")
    if(NOT IS_DIRECTORY "${_dxc_dir}")
        message(FATAL_ERROR "sk_copy_dxc_shared_library: dxc dir not found: ${_dxc_dir}")
    endif()

    # Same plugins output dir sk_add_plugin writes SHARED plugins to.
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_plugins_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/plugins")
    else()
        set(_plugins_dir "${CMAKE_BINARY_DIR}/bin/plugins")
    endif()

    if(WIN32)
        set(_dxc_src "${_dxc_dir}/bin/win-x64/dxcompiler.dll")
    elseif(APPLE)
        set(_dxc_src "${_dxc_dir}/bin/macOS/libdxcompiler.dylib")
    elseif(UNIX)
        set(_dxc_src "${_dxc_dir}/bin/linux-x64/libdxcompiler.so")
        set(_dxc_aux "${_dxc_dir}/bin/linux-x64/libdxil.so")
    else()
        message(FATAL_ERROR "sk_copy_dxc_shared_library: unsupported platform '${CMAKE_SYSTEM_NAME}'")
    endif()

    if(NOT EXISTS "${_dxc_src}")
        message(WARNING
            "sk_copy_dxc_shared_library(${target}): DXC runtime not vendored "
            "for ${CMAKE_SYSTEM_NAME} (${_dxc_src}). Skipping copy; expected "
            "bin/win-x64/dxcompiler.dll, bin/macOS/libdxcompiler.dylib, or "
            "bin/linux-x64/libdxcompiler.so.")
        return()
    endif()

    if(UNIX AND NOT APPLE AND EXISTS "${_dxc_aux}")
        # DXC may dlopen libdxil.so at runtime (DXIL validation); ship it
        # beside libdxcompiler.so so the loader finds it.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_plugins_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dxc_src}" "${_plugins_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dxc_aux}" "${_plugins_dir}"
            COMMENT "Copying DXC shared libraries for ${target}"
        )
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_plugins_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dxc_src}" "${_plugins_dir}"
            COMMENT "Copying DXC shared library for ${target}"
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# sk_check_header_isolation(<dir>...)
#   Fail the configure if any public header under <dir> includes an OS
#   threading/mutex header (<pthread.h>, <windows.h>, <semaphore.h>, <sched.h>,
#   <mtx.h>, <thread.h>, <threads.h>). Enforces the rule that OS headers live
#   only in .c implementation files, never in public .h headers. Quoted
#   project includes ("mutex.h", "thread.h", …) do not match, so this stays
#   clean for sk_* public headers that include each other.
# ---------------------------------------------------------------------------
function(sk_check_header_isolation)
    set(_pattern
        "^[ \t]*#[ \t]*include[ \t]*<(pthread|windows|Windows|WINDOWS|semaphore|sched|mtx|thread|threads)[.]h>")
    foreach(_dir IN LISTS ARGN)
        if(NOT IS_DIRECTORY "${_dir}")
            message(WARNING "sk_check_header_isolation: not a directory: ${_dir}")
            continue()
        endif()
        file(GLOB_RECURSE _headers "${_dir}/*.h")
        foreach(_hdr IN LISTS _headers)
            file(STRINGS "${_hdr}" _matches REGEX "${_pattern}")
            if(_matches)
                message(FATAL_ERROR
                    "Header-isolation violation: ${_hdr} includes an OS "
                    "threading/mutex header (${_matches}). OS headers are only "
                    "allowed in .c implementation files.")
            endif()
        endforeach()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# CTest labels / integration gate
#
# Default `ctest` registers and runs integration binaries (label `integration`).
# SK_RUN_INTEGRATION=0 under SK_CTEST_GATE returns SK_TEST_SKIP_CODE (77).
# Direct invocation of the binary is never gated. Must match SK_TEST_SKIP_CODE
# in foundation/test.h.
# ---------------------------------------------------------------------------
if(NOT DEFINED SK_TEST_SKIP_CODE)
    set(SK_TEST_SKIP_CODE 77 CACHE INTERNAL "CTest skip code; must match SK_TEST_SKIP_CODE in foundation/test.h")
endif()

function(sk_ctest_mark_unit name)
    set_tests_properties(${name} PROPERTIES LABELS "unit")
endfunction()

function(sk_ctest_mark_integration name)
    set(_labels "integration")
    set(_timeout 1800)
    cmake_parse_arguments(SK_IT "" "TIMEOUT" "LABELS" ${ARGN})
    if(SK_IT_LABELS)
        set(_labels "integration;${SK_IT_LABELS}")
    endif()
    if(SK_IT_TIMEOUT)
        set(_timeout ${SK_IT_TIMEOUT})
    endif()
    set_tests_properties(${name} PROPERTIES
        LABELS "${_labels}"
        ENVIRONMENT "SK_CTEST_GATE=1"
        SKIP_RETURN_CODE ${SK_TEST_SKIP_CODE}
        TIMEOUT ${_timeout}
    )
endfunction()
