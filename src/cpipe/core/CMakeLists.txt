add_library(cpipe-core STATIC BufferLayout.cpp CpuBuffer.cpp)
add_library(cpipe::core ALIAS cpipe-core)

target_include_directories(
    cpipe-core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

cpipe_configure_target(cpipe-core)
