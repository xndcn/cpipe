add_library(cpipe-core STATIC stub.cpp)
add_library(cpipe::core ALIAS cpipe-core)

cpipe_target_warning_flags(cpipe-core)

target_include_directories(
    cpipe-core
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
