cmake_minimum_required(VERSION 3.22)
project(skore_ecs_benchmark VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SKORE_ECS_BENCH_ENABLE_SANITIZERS "Enable address/undefined behavior sanitizers" OFF)

# ---------------------------------------------------------------------------
# Skore (git submodule, branch v2)
#
# Skore is pinned as a git submodule tracking branch v2:
#   git submodule update --init skore-ecs-benchmark/thirdparty/skore
# Its CMake root is included here so the ECS targets (sk-core, sk-entities)
# are available to link against. Skore sources are never modified by this
# project.
#
# Skore's own build defaults are relaxed for the benchmark build: clang-tidy
# is a lint gate we do not run here, and Skore's test host is not built inside
# the benchmark. Both remain overridable from the command line.
#
# EXCLUDE_FROM_ALL keeps Skore's own executables (sk-player, editor, tests)
# out of the default build. Their include paths resolve against the top-level
# source dir, which is this benchmark when Skore is a subdirectory, so they
# are not part of this project's build graph. Only targets the benchmark
# depends on (sk-core, sk-entities) are built, plus anything built explicitly.
# ---------------------------------------------------------------------------
set(SK_ENABLE_CLANG_TIDY OFF CACHE BOOL "Disable Skore clang-tidy gate in benchmark builds")
set(BUILD_TESTING OFF CACHE BOOL "Disable Skore tests in benchmark builds")
add_subdirectory(thirdparty/skore EXCLUDE_FROM_ALL)

add_library(skore_ecs_benchmark_core STATIC
	src/benchmark/benchmark.cpp
)
target_include_directories(skore_ecs_benchmark_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(skore_ecs_benchmark_core PUBLIC cxx_std_20)

# Skore engine core: types, allocators, containers, math, app/platform API
# headers. Static link; also makes sk-core's public headers available.
target_link_libraries(skore_ecs_benchmark_core PUBLIC sk-core)
# Skore ECS public header (sk_entities_api_t).
target_include_directories(skore_ecs_benchmark_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/skore/plugins/entities
)

add_executable(skore_ecs_benchmark
	src/benchmark/main.cpp
)
target_link_libraries(skore_ecs_benchmark PRIVATE skore_ecs_benchmark_core)
target_include_directories(skore_ecs_benchmark PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link the Skore ECS plugin (shared library) into the benchmark so the ECS
# backend is part of this build. Skore drops plugins into {bin}/plugins; point
# the executable's runtime rpath there so the shared library resolves at load.
target_link_libraries(skore_ecs_benchmark PRIVATE sk-entities)
set_target_properties(skore_ecs_benchmark PROPERTIES
	BUILD_RPATH "$ORIGIN/plugins"
	INSTALL_RPATH "$ORIGIN/plugins"
)

if(SKORE_ECS_BENCH_ENABLE_SANITIZERS)
	foreach(target skore_ecs_benchmark_core skore_ecs_benchmark)
		target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	endforeach()
endif()
