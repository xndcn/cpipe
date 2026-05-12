cpipe_provide_halide()

add_executable(passthrough_copy_generator passthrough_copy_generator.cpp)
target_link_libraries(passthrough_copy_generator PRIVATE Halide::Generator)
cpipe_configure_target(passthrough_copy_generator)

cpipe_add_halide_library(
    passthrough_copy
    FROM passthrough_copy_generator
    GENERATOR passthrough_copy
    TARGETS x86-64-linux
    USE_RUNTIME cpipe-halide-runtime
)

add_library(cpipe-builtin-nodes STATIC NodesStub.cpp passthrough.cpp)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

target_include_directories(
    cpipe-builtin-nodes
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(cpipe-builtin-nodes PUBLIC cpipe-sdk cpipe-runtime PRIVATE passthrough_copy)

cpipe_embed_json(
    cpipe-builtin-nodes
    "${CMAKE_CURRENT_SOURCE_DIR}/passthrough.json"
    PASSTHROUGH_MANIFEST_JSON
)

cpipe_configure_target(cpipe-builtin-nodes)
