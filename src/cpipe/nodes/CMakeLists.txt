add_halide_generator(cpipe_passthrough_copy_generator SOURCES passthrough_copy_generator.cpp)
add_halide_library(
    cpipe_passthrough_copy
    FROM cpipe_passthrough_copy_generator
    GENERATOR passthrough_copy
    FUNCTION_NAME passthrough_copy
    USE_RUNTIME cpipe-halide-runtime
    TARGETS host)

cpipe_embed_json(
    passthrough_manifest_source
    passthrough.json
    CPIPE_PASSTHROUGH_MANIFEST_JSON
    "cpipe::nodes")

add_library(cpipe-builtin-nodes STATIC empty.cpp passthrough.cpp ${passthrough_manifest_source})
add_library(cpipe::builtin-nodes ALIAS cpipe-builtin-nodes)

target_include_directories(cpipe-builtin-nodes PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(
    cpipe-builtin-nodes
    PUBLIC cpipe-sdk cpipe-runtime
    PRIVATE cpipe_passthrough_copy)
cpipe_target_defaults(cpipe-builtin-nodes)
