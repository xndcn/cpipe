add_library(cpipe-sdk INTERFACE)
add_library(cpipe::sdk ALIAS cpipe-sdk)

target_include_directories(
    cpipe-sdk
    INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(cpipe-sdk INTERFACE cpipe::core)
