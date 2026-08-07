file(GLOB_RECURSE SKORE_PLAYER_SOURCES *.h *.c)

add_executable(sk-player ${SKORE_PLAYER_SOURCES})

# App entry: sk-app (sk_app_init + platform APIs) + core (transitively via sk-app).
target_link_libraries(sk-player PRIVATE sk-app)

# Header-only access to sk_platform_window_api_t (plugin is loaded at runtime).
# Include path used instead of sk-platform-window-lib because player is
# added before plugins/ in the root CMakeLists.
target_include_directories(sk-player PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/platform_window)

