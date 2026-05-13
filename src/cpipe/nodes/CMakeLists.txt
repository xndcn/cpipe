include(HalideHelpers)
cpipe_ensure_halide()

add_halide_generator(cpipe_halide_generators SOURCES passthrough_copy_generator.cpp)
cpipe_add_halide_library(
    passthrough_copy
    FROM
        cpipe_halide_generators
    GENERATOR
        passthrough_copy
    FUNCTION_NAME
        passthrough_copy
    TARGETS
        x86-64-linux
)

set(passthrough_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/passthrough_manifest.cpp")
add_custom_command(
    OUTPUT "${passthrough_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/passthrough.json"
        -DOUTPUT_CPP="${passthrough_manifest_cpp}"
        -DSYMBOL_NAME=PASSTHROUGH_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS passthrough.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

add_library(cpipe-builtin-nodes STATIC passthrough.cpp "${passthrough_manifest_cpp}")
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

cpipe_target_warning_flags(cpipe-builtin-nodes)

target_link_libraries(
    cpipe-builtin-nodes
    PUBLIC
        cpipe::sdk
        cpipe::runtime
        passthrough_copy
)
