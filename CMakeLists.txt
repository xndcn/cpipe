cmake_minimum_required(VERSION 3.28)

project(cpipe VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(CheckCXXSourceCompiles)
include(CompilerOptions)
include(EmbedJson)
include(HalideHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_REQUIRED_FLAGS "-std=c++20")
check_cxx_source_compiles(
    "#include <atomic>
     #ifndef __cpp_lib_atomic_wait
     #error atomic wait is unavailable
     #endif
     int main() {
         std::atomic<int> v{0};
         v.notify_one();
         return 0;
     }"
    CPIPE_HAS_ATOMIC_WAIT)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT CPIPE_HAS_ATOMIC_WAIT)
    message(FATAL_ERROR "cpipe requires C++20 atomic wait/notify support; upgrade the compiler")
endif()

add_subdirectory(src/cpipe/core)
add_subdirectory(src/cpipe/sdk)
add_subdirectory(src/cpipe/runtime)
add_subdirectory(src/cpipe/nodes)
add_subdirectory(src/cpipe/server)
add_subdirectory(src/cpipe/cli)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
