cmake_minimum_required(VERSION 3.22)
project(skore)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_functions.cmake)

set(SK_ENGINE_VERSION "0.0.1")
add_compile_definitions(SK_ENGINE_VERSION="${SK_ENGINE_VERSION}")

string(TIMESTAMP TODAY "%Y%m%d")
add_compile_definitions(SK_VERSION="${SK_ENGINE_VERSION}-${TODAY}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Embed 128-bit SK_TYPE_ID("name") hashes into sources at configure time.
sk_embed_type_ids_in_dir(
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/player
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Public headers must never pull OS threading/mutex headers (OS includes are
# confined to .c backends). Any violation fails the configure.
sk_check_header_isolation(
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/player
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR (CMAKE_CXX_COMPILER_ID MATCHES "GNU"))
    add_compile_options(-fms-extensions)
endif()

<<<<<<< HEAD
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
    if(NOT APPLE)
        # libc++ is the default C++ standard library on AppleClang; passing
        # -stdlib=libc++ explicitly there is unused and fails -Werror builds.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
=======
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # libc++ is not available under the MSVC toolchain (clang on Windows uses
    # the MSVC STL; passing -stdlib=libc++ is an unused-argument diagnostic).
    if(NOT WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
>>>>>>> origin/v2
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif()

if ( MSVC )
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
endif (MSVC)

if (UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS                "${CMAKE_CXX_FLAGS_INIT} -fPIC")
    set(CMAKE_C_FLAGS                  "${CMAKE_C_FLAGS_INIT} -fPIC")
    SET(CMAKE_SKIP_BUILD_RPATH FALSE)
    SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    SET(CMAKE_INSTALL_RPATH "\${ORIGIN}")
elseif (APPLE)
    set(CMAKE_CXX_FLAGS                "${CMAKE_CXX_FLAGS_INIT} -fPIC")
    set(CMAKE_C_FLAGS                  "${CMAKE_C_FLAGS_INIT} -fPIC")
    SET(CMAKE_SKIP_BUILD_RPATH FALSE)
    SET(CMAKE_INSTALL_RPATH "${ORIGIN}/")
endif()

# BUILD_TESTING option (default ON) + enable CTest.
include(CTest)

# compile_commands.json for clang-tidy / IDE tooling (Ninja, Make, multi-config).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Third-party first so it does not inherit first-party warning flags or clang-tidy.
add_subdirectory(thirdparty)

# ---------------------------------------------------------------------------
# clang-tidy on first-party targets only (thirdparty is already added above).
# Any clang-tidy diagnostic is a hard build failure (-warnings-as-errors=*).
# Config: project root .clang-tidy. Disable with -DSK_ENABLE_CLANG_TIDY=OFF.
# ---------------------------------------------------------------------------
option(SK_ENABLE_CLANG_TIDY "Run clang-tidy on first-party code; warnings fail the build" ON)
if(SK_ENABLE_CLANG_TIDY)
    # Clear a stale NOTFOUND from older configures so find_program re-searches.
    if(SK_CLANG_TIDY_EXE STREQUAL "SK_CLANG_TIDY_EXE-NOTFOUND")
        unset(SK_CLANG_TIDY_EXE CACHE)
    endif()
    # ProgramFiles(x86) needs escaped parens in CMake ENV refs.
    set(_sk_pf "$ENV{ProgramFiles}")
    set(_sk_pf86)
    if(DEFINED ENV{ProgramFiles\(x86\)})
        set(_sk_pf86 "$ENV{ProgramFiles\(x86\)}")
    endif()
    find_program(SK_CLANG_TIDY_EXE
        NAMES clang-tidy clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16
        HINTS
            "${_sk_pf}/LLVM/bin"
            "${_sk_pf86}/LLVM/bin"
            "${_sk_pf}/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
            "${_sk_pf}/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin"
            "${_sk_pf}/Microsoft Visual Studio/18/Enterprise/VC/Tools/Llvm/x64/bin"
            "${_sk_pf}/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
            "${_sk_pf}/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin"
            "${_sk_pf}/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin"
            "${_sk_pf}/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin"
        DOC "Path to clang-tidy executable"
    )
    if(NOT SK_CLANG_TIDY_EXE AND WIN32)
        # Fallback: glob common VS LLVM install layouts (version folder varies).
        file(GLOB _sk_vs_clang_tidy
            "${_sk_pf}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/clang-tidy.exe"
        )
        if(_sk_vs_clang_tidy)
            list(GET _sk_vs_clang_tidy 0 SK_CLANG_TIDY_EXE)
            set(SK_CLANG_TIDY_EXE "${SK_CLANG_TIDY_EXE}" CACHE FILEPATH "Path to clang-tidy executable" FORCE)
        endif()
        unset(_sk_vs_clang_tidy)
    endif()
    unset(_sk_pf)
    unset(_sk_pf86)
    if(SK_CLANG_TIDY_EXE)
        # Semicolon list: CMake runs clang-tidy with these args around each compile.
        # -Wno-unknown-warning-option: compile line may include GCC-only flags
        # (-Wlogical-op, -Wduplicated-cond, …) that Clang/clang-tidy reject.
        set(SK_CLANG_TIDY_COMMAND
            "${SK_CLANG_TIDY_EXE}"
            "-warnings-as-errors=*"
            "--quiet"
            "--extra-arg=-Wno-unknown-warning-option"
        )
        set(CMAKE_C_CLANG_TIDY "${SK_CLANG_TIDY_COMMAND}")
        set(CMAKE_CXX_CLANG_TIDY "${SK_CLANG_TIDY_COMMAND}")
        message(STATUS "clang-tidy: ${SK_CLANG_TIDY_EXE} (warnings-as-errors; thirdparty excluded)")
    else()
        message(FATAL_ERROR
            "SK_ENABLE_CLANG_TIDY is ON but clang-tidy was not found on PATH. "
            "Install LLVM clang-tidy or pass -DSK_ENABLE_CLANG_TIDY=OFF."
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Aggressive warnings on all first-party targets. Any warning is an error.
# MSVC, GCC, and Clang. Applied after thirdparty so vendored code is exempt.
# ---------------------------------------------------------------------------
if(WIN32)
    # Portable ISO C string APIs (strncpy, etc.) — not the MSVC-only *_s variants.
    # The Windows CRT marks these deprecated unless this define is set; applies
    # to MSVC and clang-on-Windows alike (CI compiles with clang here).
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    add_compile_options(
        /W4
        /WX
        /permissive-
        /w14242 # conversion possible loss of data
        /w14254 # bit-field conversion possible loss of data
        /w14263 # member function does not override any base class virtual
        /w14265 # class has virtual functions but destructor is not virtual
        /w14287 # unsigned/negative constant mismatch
        /we4289 # loop control variable used outside the for-loop scope
        /w14296 # expression is always true/false
        /w14311 # pointer truncation
        /w14545 # expression before comma evaluates to a function missing args
        /w14546 # function call before comma missing argument list
        /w14547 # operator before comma has no effect
        /w14549 # operator before comma has no effect; did you intend 'operator'?
        /w14555 # expression has no effect
        /w14619 # pragma warning: there is no warning number
        /w14640 # thread-unsafe static member initialization
        /w14826 # conversion is sign-extended
        /w14905 # wide string literal cast to LPSTR
        /w14906 # string literal cast to LPWSTR
        /w14928 # illegal copy-initialization; multiple user-defined conversions
        /external:anglebrackets
        /external:W0
    )
else()
    # GCC and Clang (and AppleClang)
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wcast-align
        -Wcast-qual
        -Wformat=2
        -Wundef
        -Wwrite-strings
        -Wpointer-arith
        -Wmissing-include-dirs
        -Wnull-dereference
        -Wdouble-promotion
        -Wfloat-equal
        -Wswitch-enum
        -Wredundant-decls
        -Wstrict-overflow=2
        $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
        $<$<COMPILE_LANGUAGE:C>:-Wold-style-definition>
        $<$<COMPILE_LANGUAGE:C>:-Wnested-externs>
    )
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(
            -Wlogical-op
            -Wduplicated-cond
            -Wduplicated-branches
        )
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -Wmost
            -Wcomma
            -Wmove
        )
    endif()
endif()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(editor)
add_subdirectory(player)
add_subdirectory(plugins)
add_subdirectory(tests)