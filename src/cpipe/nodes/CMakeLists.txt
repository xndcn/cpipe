include(HalideHelpers)
cpipe_ensure_halide()

add_halide_generator(
    cpipe_halide_generators
    SOURCES
        passthrough_copy_generator.cpp
        demosaic/demosaic_amaze_generator.cpp
        demosaic/demosaic_bilinear_generator.cpp
        demosaic/demosaic_quad_bayer_remosaic_generator.cpp
        demosaic/demosaic_rcd_generator.cpp
        precision/precision_convert_generator.cpp
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
    demosaic_amaze
    FROM
        cpipe_halide_generators
    GENERATOR
        demosaic_amaze
    FUNCTION_NAME
        demosaic_amaze
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
cpipe_add_halide_library(
    demosaic_quad_bayer_remosaic
    FROM
        cpipe_halide_generators
    GENERATOR
        demosaic_quad_bayer_remosaic
    FUNCTION_NAME
        demosaic_quad_bayer_remosaic
    TARGETS
        x86-64-linux
)
cpipe_add_halide_library(
    demosaic_rcd
    FROM
        cpipe_halide_generators
    GENERATOR
        demosaic_rcd
    FUNCTION_NAME
        demosaic_rcd
    TARGETS
        x86-64-linux
        ${Halide_HOST_TARGET}-vulkan-vk_float16
)
cpipe_add_halide_library(
    precision_convert_r16u_to_f32
    FROM
        cpipe_halide_generators
    GENERATOR
        precision_convert_r16u_to_f32
    FUNCTION_NAME
        precision_convert_r16u_to_f32
    TARGETS
        x86-64-linux
)
cpipe_add_halide_library(
    precision_convert_f32_to_rgba16f
    FROM
        cpipe_halide_generators
    GENERATOR
        precision_convert_f32_to_rgba16f
    FUNCTION_NAME
        precision_convert_f32_to_rgba16f
    TARGETS
        x86-64-linux
)
cpipe_add_halide_library(
    precision_convert_rgba16f_to_rgba8
    FROM
        cpipe_halide_generators
    GENERATOR
        precision_convert_rgba16f_to_rgba8
    FUNCTION_NAME
        precision_convert_rgba16f_to_rgba8
    TARGETS
        x86-64-linux
)
cpipe_add_halide_library(
    precision_convert_rgba16f_to_rgba16u
    FROM
        cpipe_halide_generators
    GENERATOR
        precision_convert_rgba16f_to_rgba16u
    FUNCTION_NAME
        precision_convert_rgba16f_to_rgba16u
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

set(demosaic_amaze_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/demosaic_amaze_manifest.cpp")
add_custom_command(
    OUTPUT "${demosaic_amaze_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/demosaic/demosaic_amaze.json"
        -DOUTPUT_CPP="${demosaic_amaze_manifest_cpp}"
        -DSYMBOL_NAME=DEMOSAIC_AMAZE_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS demosaic/demosaic_amaze.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(demosaic_rcd_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/demosaic_rcd_manifest.cpp")
add_custom_command(
    OUTPUT "${demosaic_rcd_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/demosaic/demosaic_rcd.json"
        -DOUTPUT_CPP="${demosaic_rcd_manifest_cpp}"
        -DSYMBOL_NAME=DEMOSAIC_RCD_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS demosaic/demosaic_rcd.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(demosaic_quad_bayer_remosaic_manifest_cpp
    "${CMAKE_CURRENT_BINARY_DIR}/demosaic_quad_bayer_remosaic_manifest.cpp"
)
add_custom_command(
    OUTPUT "${demosaic_quad_bayer_remosaic_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/demosaic/demosaic_quad_bayer_remosaic.json"
        -DOUTPUT_CPP="${demosaic_quad_bayer_remosaic_manifest_cpp}"
        -DSYMBOL_NAME=DEMOSAIC_QUAD_BAYER_REMOSAIC_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS demosaic/demosaic_quad_bayer_remosaic.json
            "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
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

set(wb_greyworld_auto_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/wb_greyworld_auto_manifest.cpp")
add_custom_command(
    OUTPUT "${wb_greyworld_auto_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/wb/greyworld_auto.json"
        -DOUTPUT_CPP="${wb_greyworld_auto_manifest_cpp}"
        -DSYMBOL_NAME=WB_GREYWORLD_AUTO_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS wb/greyworld_auto.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
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

set(lens_shading_gainmap_manifest_cpp
    "${CMAKE_CURRENT_BINARY_DIR}/lens_shading_gainmap_manifest.cpp"
)
add_custom_command(
    OUTPUT "${lens_shading_gainmap_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/lens/shading_gainmap.json"
        -DOUTPUT_CPP="${lens_shading_gainmap_manifest_cpp}"
        -DSYMBOL_NAME=LENS_SHADING_GAINMAP_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS lens/shading_gainmap.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(lens_dng_opcode_list_3_manifest_cpp
    "${CMAKE_CURRENT_BINARY_DIR}/lens_dng_opcode_list_3_manifest.cpp"
)
add_custom_command(
    OUTPUT "${lens_dng_opcode_list_3_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/lens/dng_opcode_list_3.json"
        -DOUTPUT_CPP="${lens_dng_opcode_list_3_manifest_cpp}"
        -DSYMBOL_NAME=LENS_DNG_OPCODE_LIST_3_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS lens/dng_opcode_list_3.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(precision_convert_manifest_cpp "${CMAKE_CURRENT_BINARY_DIR}/precision_convert_manifest.cpp")
add_custom_command(
    OUTPUT "${precision_convert_manifest_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/precision/precision_convert.json"
        -DOUTPUT_CPP="${precision_convert_manifest_cpp}"
        -DSYMBOL_NAME=PRECISION_CONVERT_MANIFEST_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS precision/precision_convert.json "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
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
        demosaic/demosaic_amaze.cpp
        demosaic/demosaic_amaze_dispatch.cpp
        demosaic/demosaic_bilinear.cpp
        demosaic/demosaic_bilinear_dispatch.cpp
        demosaic/demosaic_quad_bayer_remosaic.cpp
        demosaic/demosaic_quad_bayer_remosaic_dispatch.cpp
        demosaic/demosaic_rcd.cpp
        demosaic/demosaic_rcd_dispatch.cpp
        wb/wb_dual_illuminant.cpp
        wb/wb_dual_illuminant_dispatch.cpp
        wb/greyworld_auto.cpp
        colormatrix/colormatrix_dng_to_working.cpp
        colormatrix/colormatrix_dng_to_working_dispatch.cpp
        lens/shading_gainmap.cpp
        lens/shading_gainmap_dispatch.cpp
        lens/dng_opcode_list_3.cpp
        lens/dng_opcode_list_3_dispatch.cpp
        precision/precision_convert.cpp
        precision/precision_convert_dispatch.cpp
        output/heif_sdr.cpp
        "${passthrough_manifest_cpp}"
        "${dng_input_manifest_cpp}"
        "${linearize_dng_lut_manifest_cpp}"
        "${blacklevel_dng_levels_manifest_cpp}"
        "${demosaic_amaze_manifest_cpp}"
        "${demosaic_bilinear_manifest_cpp}"
        "${demosaic_quad_bayer_remosaic_manifest_cpp}"
        "${demosaic_rcd_manifest_cpp}"
        "${wb_dual_illuminant_manifest_cpp}"
        "${wb_greyworld_auto_manifest_cpp}"
        "${colormatrix_dng_to_working_manifest_cpp}"
        "${lens_shading_gainmap_manifest_cpp}"
        "${lens_dng_opcode_list_3_manifest_cpp}"
        "${precision_convert_manifest_cpp}"
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
        demosaic_amaze
        demosaic_bilinear
        demosaic_quad_bayer_remosaic
        demosaic_rcd
        precision_convert_r16u_to_f32
        precision_convert_f32_to_rgba16f
        precision_convert_rgba16f_to_rgba8
        precision_convert_rgba16f_to_rgba16u
)
