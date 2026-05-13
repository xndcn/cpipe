cmake_minimum_required(VERSION 3.28)

project(
    cpipe
    VERSION 0.1.0
    DESCRIPTION "Computational Photography Pipeline"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(CompilerOptions)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_subdirectory(src/cpipe/core)
add_subdirectory(src/cpipe/sdk)
add_subdirectory(src/cpipe/runtime)
add_subdirectory(src/cpipe/nodes)
add_subdirectory(src/cpipe/server)
add_subdirectory(src/cpipe/cli)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
