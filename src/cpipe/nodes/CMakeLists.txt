add_library(cpipe-builtin-nodes STATIC NodesStub.cpp)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

target_include_directories(
    cpipe-builtin-nodes
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(cpipe-builtin-nodes PUBLIC cpipe-sdk cpipe-runtime)

cpipe_configure_target(cpipe-builtin-nodes)
