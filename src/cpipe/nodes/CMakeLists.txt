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

set(dng_input_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/dng_input_manifest.cpp")
add_custom_command(
    OUTPUT "${dng_input_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/dng_input/dng_input.json"
        -DOUTPUT_CPP="${dng_input_manifest_cpp}"
        -DSYMBOL_NAME=DNG_INPUT_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS dng_input/dng_input.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(linearize_dng_lut_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/linearize_dng_lut_manifest.cpp")
add_custom_command(
    OUTPUT "${linearize_dng_lut_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/linearize/linearize_dng_lut.json"
        -DOUTPUT_CPP="${linearize_dng_lut_manifest_cpp}"
        -DSYMBOL_NAME=LINEARIZE_DNG_LUT_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS linearize/linearize_dng_lut.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(blacklevel_dng_levels_manifest_cpp
    "${CMAKE_CURRENT_BINARY_DIR}/blacklevel_dng_levels_manifest.cpp"
)
add_custom_command(
    OUTPUT "${blacklevel_dng_levels_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/blacklevel/blacklevel_dng_levels.json"
        -DOUTPUT_CPP="${blacklevel_dng_levels_manifest_cpp}"
        -DSYMBOL_NAME=BLACKLEVEL_DNG_LEVELS_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS blacklevel/blacklevel_dng_levels.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

add_library(
    cpipe-builtin-nodes
    STATIC
        passthrough.cpp
        dng_input/dng_input.cpp
        linearize/linearize_dng_lut.cpp
        blacklevel/blacklevel_dng_levels.cpp
        "${passthrough_manifest_cpp}"
        "${dng_input_manifest_cpp}"
        "${linearize_dng_lut_manifest_cpp}"
        "${blacklevel_dng_levels_manifest_cpp}"
)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

cpipe_target_warning_flags(cpipe-builtin-nodes)

target_link_libraries(
    cpipe-builtin-nodes
    PUBLIC
        cpipe::sdk
        cpipe::runtime
        passthrough_copy
)
