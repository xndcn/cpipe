cpipe_add_halide_library(
    passthrough_copy
    SOURCES
    passthrough_copy_generator.cpp
    GENERATOR
    PassthroughCopy
    FUNCTION_NAME
    passthrough_copy
    TARGETS
    host
    USE_RUNTIME
    cpipe-halide-runtime)

add_library(cpipe-builtin-nodes STATIC nodes_stub.cpp passthrough.cpp)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

cpipe_embed_json(
    cpipe-builtin-nodes
    "${CMAKE_CURRENT_SOURCE_DIR}/passthrough.json"
    PASSTHROUGH_MANIFEST_JSON
    OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/passthrough_manifest.cpp")

target_include_directories(cpipe-builtin-nodes PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-builtin-nodes PUBLIC cpipe-sdk cpipe-runtime passthrough_copy)
cpipe_target_warning_flags(cpipe-builtin-nodes)
