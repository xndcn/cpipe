add_library(cpipe-server STATIC ServerStub.cpp)
add_library(cpipe::server ALIAS cpipe-server)

target_include_directories(
    cpipe-server
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(cpipe-server PUBLIC cpipe-runtime)

cpipe_configure_target(cpipe-server)
