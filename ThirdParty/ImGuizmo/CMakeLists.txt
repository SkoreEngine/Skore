


add_library(ImGuizmo STATIC ImGuizmo.cpp)
target_include_directories(ImGuizmo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ImGuizmo PUBLIC imgui)