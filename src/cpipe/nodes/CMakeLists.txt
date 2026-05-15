include(HalideHelpers)
cpipe_ensure_halide()

add_halide_generator(
    cpipe_halide_generators
    SOURCES
        passthrough_copy_generator.cpp
        demosaic/demosaic_bilinear_generator.cpp
)
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
cpipe_add_halide_library(
    demosaic_bilinear
    FROM
        cpipe_halide_generators
    GENERATOR
        demosaic_bilinear
    FUNCTION_NAME
        demosaic_bilinear
    TARGETS
        x86-64-linux
        ${Halide_HOST_TARGET}-vulkan-vk_float16
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

set(demosaic_bilinear_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/demosaic_bilinear_manifest.cpp")
add_custom_command(
    OUTPUT "${demosaic_bilinear_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/demosaic/demosaic_bilinear.json"
        -DOUTPUT_CPP="${demosaic_bilinear_manifest_cpp}"
        -DSYMBOL_NAME=DEMOSAIC_BILINEAR_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS demosaic/demosaic_bilinear.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(wb_dual_illuminant_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/wb_dual_illuminant_manifest.cpp")
add_custom_command(
    OUTPUT "${wb_dual_illuminant_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/wb/wb_dual_illuminant.json"
        -DOUTPUT_CPP="${wb_dual_illuminant_manifest_cpp}"
        -DSYMBOL_NAME=WB_DUAL_ILLUMINANT_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS wb/wb_dual_illuminant.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(colormatrix_dng_to_working_manifest_cpp
    "${CMAKE_CURRENT_BINARY_DIR}/colormatrix_dng_to_working_manifest.cpp"
)
add_custom_command(
    OUTPUT "${colormatrix_dng_to_working_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/colormatrix/colormatrix_dng_to_working.json"
        -DOUTPUT_CPP="${colormatrix_dng_to_working_manifest_cpp}"
        -DSYMBOL_NAME=COLORMATRIX_DNG_TO_WORKING_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS colormatrix/colormatrix_dng_to_working.json
            "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(output_heif_sdr_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/output_heif_sdr_manifest.cpp")
add_custom_command(
    OUTPUT "${output_heif_sdr_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/output/heif_sdr.json"
        -DOUTPUT_CPP="${output_heif_sdr_manifest_cpp}"
        -DSYMBOL_NAME=OUTPUT_HEIF_SDR_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS output/heif_sdr.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

add_library(
    cpipe-builtin-nodes
    STATIC
        builtin_link.cpp
        passthrough.cpp
        passthrough_copy_dispatch.cpp
        dng_input/dng_input.cpp
        linearize/linearize_dng_lut.cpp
        linearize/linearize_dng_lut_dispatch.cpp
        blacklevel/blacklevel_dng_levels.cpp
        blacklevel/blacklevel_dng_levels_dispatch.cpp
        demosaic/demosaic_bilinear.cpp
        demosaic/demosaic_bilinear_dispatch.cpp
        wb/wb_dual_illuminant.cpp
        wb/wb_dual_illuminant_dispatch.cpp
        colormatrix/colormatrix_dng_to_working.cpp
        colormatrix/colormatrix_dng_to_working_dispatch.cpp
        output/heif_sdr.cpp
        "${passthrough_manifest_cpp}"
        "${dng_input_manifest_cpp}"
        "${linearize_dng_lut_manifest_cpp}"
        "${blacklevel_dng_levels_manifest_cpp}"
        "${demosaic_bilinear_manifest_cpp}"
        "${wb_dual_illuminant_manifest_cpp}"
        "${colormatrix_dng_to_working_manifest_cpp}"
        "${output_heif_sdr_manifest_cpp}"
)
add_library(cpipe::builtin_nodes ALIAS cpipe-builtin-nodes)

cpipe_target_warning_flags(cpipe-builtin-nodes)

target_compile_definitions(
    cpipe-builtin-nodes
    PRIVATE
        CPIPE_DEFAULT_OCIO_CONFIG="${PROJECT_SOURCE_DIR}/share/cpipe/ocio/v0.1/config.ocio"
)

target_link_libraries(
    cpipe-builtin-nodes
    PUBLIC
        cpipe::color
        cpipe::sdk
        cpipe::runtime
        passthrough_copy
        demosaic_bilinear
)
