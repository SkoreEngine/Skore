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
    target_link_libraries(${_plugin} PRIVATE sk-core)
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

    # Export public headers for consumers / host tests that include this plugin.
    add_library(${_lib} INTERFACE)
    target_include_directories(${_lib} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()
