file(GLOB_RECURSE SKORE_PLATFORM_WINDOW_SOURCES *.h *.c)

sk_add_plugin(platform-window SOURCES ${SKORE_PLATFORM_WINDOW_SOURCES})

# Extra deps for the SHARED plugin (glfw / nfd / OS libs).
target_link_libraries(sk-platform-window PUBLIC
    glfw
    nativefiledialog
)
if(WIN32)
    target_link_libraries(sk-platform-window PUBLIC dwmapi gdi32 user32 shell32)
elseif(APPLE)
    target_link_libraries(sk-platform-window PUBLIC
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
else()
    find_package(Threads)
    if(Threads_FOUND)
        target_link_libraries(sk-platform-window PUBLIC Threads::Threads)
    endif()
    target_link_libraries(sk-platform-window PUBLIC m ${CMAKE_DL_LIBS})
endif()
