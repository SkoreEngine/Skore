file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

# Remove lua.c and luac.c (they contain main())
list(FILTER LUA_SOURCES EXCLUDE REGEX "lua\\.c$")
list(FILTER LUA_SOURCES EXCLUDE REGEX "luac\\.c$")

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
	target_compile_options(lua PRIVATE /W0)
endif()
