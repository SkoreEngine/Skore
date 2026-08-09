cmake_minimum_required(VERSION 3.22)
project(skore_ecs_benchmark VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SKORE_ECS_BENCH_ENABLE_SANITIZERS "Enable address/undefined behavior sanitizers" OFF)

add_library(skore_ecs_benchmark_core STATIC
	src/benchmark/benchmark.cpp
)
target_include_directories(skore_ecs_benchmark_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(skore_ecs_benchmark_core PUBLIC cxx_std_20)

add_executable(skore_ecs_benchmark
	src/benchmark/main.cpp
)
target_link_libraries(skore_ecs_benchmark PRIVATE skore_ecs_benchmark_core)
target_include_directories(skore_ecs_benchmark PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(SKORE_ECS_BENCH_ENABLE_SANITIZERS)
	foreach(target skore_ecs_benchmark_core skore_ecs_benchmark)
		target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	endforeach()
endif()
