find_package(spdlog CONFIG REQUIRED)

add_library(cpipe-core STATIC BufferLayout.cpp CpuBuffer.cpp)
add_library(cpipe::core ALIAS cpipe-core)

target_include_directories(cpipe-core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-core PUBLIC spdlog::spdlog)
cpipe_target_defaults(cpipe-core)
