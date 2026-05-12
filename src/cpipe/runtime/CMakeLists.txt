add_library(cpipe-runtime STATIC Registry.cpp RuntimeStub.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(
    cpipe-runtime
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(cpipe-runtime PUBLIC cpipe-core cpipe-sdk)

cpipe_configure_target(cpipe-runtime)
