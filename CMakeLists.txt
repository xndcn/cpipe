cmake_minimum_required(VERSION 3.28)

project(
    cpipe
    VERSION 0.1.0
    DESCRIPTION "Computational Photography Pipeline"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(EmbedJson)
include(HalideHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()
option(CPIPE_BUILD_TESTS "Build cpipe tests" ON)
option(CPIPE_ENABLE_HALIDE_VULKAN "Enable Halide Vulkan AOT target support" ON)

if(CPIPE_ENABLE_HALIDE_VULKAN)
    message(STATUS "Halide Vulkan backend: ENABLED")
else()
    message(STATUS "Halide Vulkan backend: DISABLED")
endif()

add_subdirectory(src/cpipe/core)
add_subdirectory(src/cpipe/sdk)
add_subdirectory(src/cpipe/runtime)
add_subdirectory(src/cpipe/nodes)
add_subdirectory(src/cpipe/server)
add_subdirectory(src/cpipe/cli)

if(CPIPE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
