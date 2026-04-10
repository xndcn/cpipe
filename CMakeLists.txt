cmake_minimum_required(VERSION 3.25)
project(cpipe VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(spdlog CONFIG REQUIRED)

add_subdirectory(src/common)

option(CPIPE_BUILD_TESTS "Build tests" ON)
if(CPIPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
