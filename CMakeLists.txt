cmake_minimum_required(VERSION 3.28)

project(
    cpipe
    VERSION 0.1.0
    DESCRIPTION "Computational Photography Pipeline"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(HalideHelpers)
include(EmbedJson)
cpipe_enable_ccache()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPIPE_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CPIPE_ENABLE_HALIDE_VULKAN "Enable Halide Vulkan AOT targets" OFF)

include(CTest)

add_subdirectory(src/cpipe/core)
add_subdirectory(src/cpipe/sdk)
add_subdirectory(src/cpipe/runtime)
add_subdirectory(src/cpipe/nodes)
add_subdirectory(src/cpipe/server)
add_subdirectory(src/cpipe/cli)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
