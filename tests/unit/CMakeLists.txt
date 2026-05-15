find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)
find_package(lcms2 CONFIG REQUIRED)
include(HalideHelpers)

function(cpipe_add_unit_test target)
    add_executable(${target} ${ARGN})
    cpipe_target_warning_flags(${target})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain cpipe::core)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_library(test-registry-fixture STATIC registry_fixture.cpp)
cpipe_target_warning_flags(test-registry-fixture)
target_link_libraries(test-registry-fixture PRIVATE cpipe::sdk)

cpipe_add_unit_test(test_pixel_format test_pixel_format.cpp)
cpipe_add_unit_test(test_buffer_layout test_buffer_layout.cpp)
cpipe_add_unit_test(test_buffer_usage test_buffer_usage.cpp)
cpipe_add_unit_test(test_buffer_metadata test_buffer_metadata.cpp)
cpipe_add_unit_test(test_cpu_buffer test_cpu_buffer.cpp)
cpipe_add_unit_test(test_status test_status.cpp)
cpipe_add_unit_test(test_registry test_registry.cpp test_abi_c_compile.c)
target_link_libraries(
    test_registry
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        test-registry-fixture
)
cpipe_add_unit_test(test_scheduler_topo test_scheduler_topo.cpp)
target_link_libraries(test_scheduler_topo PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_halide_adapter test_halide_adapter.cpp)
target_link_libraries(test_halide_adapter PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_compute_suite_v1_ext test_compute_suite_v1_ext.cpp)
target_compile_definitions(test_compute_suite_v1_ext PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(test_compute_suite_v1_ext PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_memory_planner test_memory_planner.cpp)
target_link_libraries(test_memory_planner PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_precision_planner test_precision_planner.cpp)
target_compile_definitions(test_precision_planner PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(
    test_precision_planner
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_metadata_suite test_metadata_suite.cpp)
target_link_libraries(test_metadata_suite PRIVATE cpipe::runtime cpipe::sdk cpipe::builtin_nodes)
cpipe_add_unit_test(test_opcode_list_parser test_opcode_list_parser.cpp)
target_link_libraries(test_opcode_list_parser PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_opcode_list_2_parser test_opcode_list_2_parser.cpp)
target_link_libraries(test_opcode_list_2_parser PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_opcode_list_3_parser test_opcode_list_3_parser.cpp)
target_link_libraries(test_opcode_list_3_parser PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_dng_reader test_dng_reader.cpp)
target_compile_definitions(test_dng_reader PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(
    test_dng_reader
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_vulkan_device_plane test_vulkan_device_plane.cpp)
target_link_libraries(test_vulkan_device_plane PRIVATE cpipe::runtime)
set_tests_properties(test_vulkan_device_plane PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
cpipe_add_unit_test(test_vulkan_command_buffer test_vulkan_command_buffer.cpp)
target_link_libraries(test_vulkan_command_buffer PRIVATE cpipe::runtime)
set_tests_properties(test_vulkan_command_buffer PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
cpipe_add_unit_test(test_passthrough_node test_passthrough_node.cpp)
target_compile_definitions(test_passthrough_node PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(
    test_passthrough_node
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
)
cpipe_add_unit_test(test_node_linearize test_node_linearize.cpp)
target_link_libraries(
    test_node_linearize
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_blacklevel test_node_blacklevel.cpp)
target_link_libraries(
    test_node_blacklevel
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_demosaic_bilinear test_node_demosaic_bilinear.cpp)
target_link_libraries(
    test_node_demosaic_bilinear
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_demosaic_bilinear_vulkan test_node_demosaic_bilinear_vulkan.cpp)
target_link_libraries(
    test_node_demosaic_bilinear_vulkan
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
set_tests_properties(
    test_node_demosaic_bilinear_vulkan PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)
cpipe_add_unit_test(test_node_demosaic_rcd test_node_demosaic_rcd.cpp)
target_link_libraries(
    test_node_demosaic_rcd
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_demosaic_rcd_vulkan test_node_demosaic_rcd_vulkan.cpp)
target_link_libraries(
    test_node_demosaic_rcd_vulkan
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
set_tests_properties(
    test_node_demosaic_rcd_vulkan PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
)
cpipe_add_unit_test(test_node_demosaic_amaze test_node_demosaic_amaze.cpp)
target_link_libraries(
    test_node_demosaic_amaze
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_demosaic_quad_bayer_remosaic test_node_demosaic_quad_bayer_remosaic.cpp)
target_link_libraries(
    test_node_demosaic_quad_bayer_remosaic
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_lens_shading_gainmap test_node_lens_shading_gainmap.cpp)
target_link_libraries(
    test_node_lens_shading_gainmap
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_lens_dng_opcode_list_3 test_node_lens_dng_opcode_list_3.cpp)
target_link_libraries(
    test_node_lens_dng_opcode_list_3
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_wb test_node_wb.cpp)
target_link_libraries(
    test_node_wb
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_wb_greyworld_auto test_node_wb_greyworld_auto.cpp)
target_link_libraries(
    test_node_wb_greyworld_auto
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_colormatrix test_node_colormatrix.cpp)
target_link_libraries(
    test_node_colormatrix
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_denoise_guided_filter test_node_denoise_guided_filter.cpp)
target_link_libraries(
    test_node_denoise_guided_filter
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(
    test_node_denoise_wavelet_bayes_shrink test_node_denoise_wavelet_bayes_shrink.cpp
)
target_link_libraries(
    test_node_denoise_wavelet_bayes_shrink
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_denoise_bm3d test_node_denoise_bm3d.cpp)
target_link_libraries(
    test_node_denoise_bm3d
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_sharpen_edge_aware_usm test_node_sharpen_edge_aware_usm.cpp)
target_link_libraries(
    test_node_sharpen_edge_aware_usm
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_tone_filmic_rgb test_node_tone_filmic_rgb.cpp)
target_link_libraries(
    test_node_tone_filmic_rgb
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_tone_aces_filmic test_node_tone_aces_filmic.cpp)
target_link_libraries(
    test_node_tone_aces_filmic
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_tone_reinhard test_node_tone_reinhard.cpp)
target_link_libraries(
    test_node_tone_reinhard
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_tone_mertens_local test_node_tone_mertens_local.cpp)
target_link_libraries(
    test_node_tone_mertens_local
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_cube_3d_lut_loader test_cube_3d_lut_loader.cpp)
target_link_libraries(test_cube_3d_lut_loader PRIVATE cpipe::color)
cpipe_add_unit_test(test_node_color_3d_lut test_node_color_3d_lut.cpp)
target_compile_definitions(test_node_color_3d_lut PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(
    test_node_color_3d_lut
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
)
cpipe_add_unit_test(test_ocio_config_v0_2 test_ocio_config_v0_2.cpp)
target_compile_definitions(test_ocio_config_v0_2 PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(test_ocio_config_v0_2 PRIVATE cpipe::runtime OpenColorIO::OpenColorIO)
cpipe_add_unit_test(
    test_node_color_scene_linear_to_display test_node_color_scene_linear_to_display.cpp
)
target_compile_definitions(
    test_node_color_scene_linear_to_display PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(
    test_node_color_scene_linear_to_display
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
)
cpipe_add_unit_test(test_node_fusion_hdr_plus_derivative test_node_fusion_hdr_plus_derivative.cpp)
target_link_libraries(
    test_node_fusion_hdr_plus_derivative
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_precision_convert test_node_precision_convert.cpp)
target_link_libraries(
    test_node_precision_convert
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_node_manifest_v0_2 test_node_manifest_v0_2.cpp)
target_compile_definitions(test_node_manifest_v0_2 PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(
    test_node_manifest_v0_2
    PRIVATE
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
)
cpipe_add_unit_test(test_golden_isp_nodes test_golden_isp_nodes.cpp)
target_compile_definitions(test_golden_isp_nodes PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(
    test_golden_isp_nodes
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
        OpenImageIO::OpenImageIO
)
set_tests_properties(test_golden_isp_nodes PROPERTIES LABELS golden)
cpipe_add_unit_test(test_output_heif_sdr test_output_heif_sdr.cpp)
add_dependencies(test_output_heif_sdr cpipe)
target_compile_definitions(
    test_output_heif_sdr
    PRIVATE
        CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CPIPE_CLI_PATH="$<TARGET_FILE:cpipe>"
)
target_link_libraries(
    test_output_heif_sdr
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_icc_v4_4_writer test_icc_v4_4_writer.cpp)
target_link_libraries(test_icc_v4_4_writer PRIVATE cpipe::color lcms2::lcms2)
cpipe_add_unit_test(test_output_heif_hdr_pq test_output_heif_hdr_pq.cpp)
target_link_libraries(
    test_output_heif_hdr_pq
    PRIVATE
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        nlohmann_json::nlohmann_json
)
cpipe_add_unit_test(test_pipeline_load test_pipeline_load.cpp)
target_compile_definitions(test_pipeline_load PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(test_pipeline_load PRIVATE cpipe::runtime cpipe::sdk cpipe::builtin_nodes)

add_halide_generator(cpipe_unit_halide_generators SOURCES noop_halide_generator.cpp)
cpipe_add_halide_library(
    metadata_noop_halide
    FROM
        cpipe_unit_halide_generators
    GENERATOR
        metadata_noop
    FUNCTION_NAME
        metadata_noop
    TARGETS
        x86-64-linux
        host-vulkan
)
add_dependencies(test_buffer_metadata metadata_noop_halide)
