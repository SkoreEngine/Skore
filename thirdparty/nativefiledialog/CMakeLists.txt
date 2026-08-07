# Native File Dialog (mlabbe) — zlib license, vendored sources only.
# Linux: zenity backend (no GTK link). Windows: COM IFileDialog. macOS: Cocoa.

add_library(nativefiledialog STATIC
    src/nfd_common.c
)

target_include_directories(nativefiledialog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    # nfd_win.cpp is C++ COM (IFileDialog); keep the .cpp extension.
    target_sources(nativefiledialog PRIVATE src/nfd_win.cpp)
    target_link_libraries(nativefiledialog PUBLIC ole32 uuid shell32)
elseif(APPLE)
    target_sources(nativefiledialog PRIVATE src/nfd_cocoa.m)
    target_link_libraries(nativefiledialog PUBLIC
        "-framework AppKit"
        "-framework Foundation"
    )
else()
    # Zenity CLI backend — no GTK dependency at link time.
    target_sources(nativefiledialog PRIVATE src/nfd_zenity.c)
endif()

set_target_properties(nativefiledialog PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(nativefiledialog PRIVATE -fvisibility=hidden)
endif()
