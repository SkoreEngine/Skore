file(GLOB RECAST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Recast/Source/*.cpp)
file(GLOB DETOUR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Detour/Source/*.cpp)
file(GLOB DETOUR_CROWD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/DetourCrowd/Source/*.cpp)
file(GLOB DETOUR_TILE_CACHE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/DetourTileCache/Source/*.cpp)

add_library(RecastNavigation STATIC ${RECAST_SRC} ${DETOUR_SRC} ${DETOUR_CROWD_SRC} ${DETOUR_TILE_CACHE_SRC})

target_include_directories(RecastNavigation PUBLIC
    Recast/Include  Detour/Include  DetourCrowd/Include  DetourTileCache/Include)

if (MSVC)
    target_compile_options(RecastNavigation PRIVATE /W0)
else()
    target_compile_options(RecastNavigation PRIVATE -w)
endif()
