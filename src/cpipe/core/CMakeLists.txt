add_library(cpipe-core STATIC
    BufferLayout.cpp
    CpuBuffer.cpp
    PixelFormat.cpp
    Status.cpp)

add_library(cpipe::core ALIAS cpipe-core)

target_include_directories(cpipe-core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(cpipe-core
    PUBLIC
        tl::expected
    PRIVATE
        spdlog::spdlog)

cpipe_target_warning_flags(cpipe-core)
