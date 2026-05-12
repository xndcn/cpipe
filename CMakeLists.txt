cmake_minimum_required(VERSION 3.28)

project(cpipe VERSION 0.1.0 LANGUAGES C CXX)

option(CPIPE_ENABLE_TESTS "Build cpipe tests" ON)
option(CPIPE_ENABLE_SANITIZERS "Enable ASAN/UBSAN for first-party targets" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(CheckCXXSourceCompiles)
include(FetchContent)
include(CompilerOptions)
include(HalideHelpers)
include(EmbedJson)

set(CMAKE_REQUIRED_FLAGS "-std=c++20")
check_cxx_source_compiles(
    "#include <atomic>
     #if !defined(__cpp_lib_atomic_wait) || __cpp_lib_atomic_wait < 201907L
     #error atomic wait is unavailable
     #endif
     int main() { return 0; }"
    CPIPE_HAS_ATOMIC_WAIT)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT CPIPE_HAS_ATOMIC_WAIT)
    message(FATAL_ERROR "TaskFlow v4 requires C++20 atomic wait/notify support; use clang >= 18 or gcc >= 13.")
endif()

find_package(CLI11 CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)

cpipe_fetch_halide()

add_subdirectory(src/cpipe/core)
add_subdirectory(src/cpipe/sdk)
add_subdirectory(src/cpipe/runtime)
add_subdirectory(src/cpipe/nodes)
add_subdirectory(src/cpipe/server)
add_subdirectory(src/cpipe/cli)
add_subdirectory(apps/cli)

if(CPIPE_ENABLE_TESTS)
    include(Catch)
    add_subdirectory(tests)
endif()
