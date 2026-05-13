find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
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
target_link_libraries(test_registry PRIVATE cpipe::runtime cpipe::sdk test-registry-fixture)
cpipe_add_unit_test(test_scheduler_topo test_scheduler_topo.cpp)
target_link_libraries(test_scheduler_topo PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_halide_adapter test_halide_adapter.cpp)
target_link_libraries(test_halide_adapter PRIVATE cpipe::runtime)
cpipe_add_unit_test(test_metadata_suite test_metadata_suite.cpp)
target_link_libraries(test_metadata_suite PRIVATE cpipe::runtime cpipe::sdk cpipe::builtin_nodes)
cpipe_add_unit_test(test_vulkan_device_plane test_vulkan_device_plane.cpp)
target_link_libraries(test_vulkan_device_plane PRIVATE cpipe::runtime)
set_tests_properties(test_vulkan_device_plane PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
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
