cpipe_add_unit_test(test_pixel_format test_pixel_format.cpp)
cpipe_add_unit_test(test_buffer_layout test_buffer_layout.cpp)
cpipe_add_unit_test(test_buffer_usage test_buffer_usage.cpp)
cpipe_add_unit_test(test_cpu_buffer test_cpu_buffer.cpp)
cpipe_add_unit_test(test_status test_status.cpp)

add_library(test_registry_node STATIC test_registry_node.cpp)
target_link_libraries(test_registry_node PRIVATE cpipe-sdk)
cpipe_target_defaults(test_registry_node)

cpipe_add_unit_test(test_registry test_registry.cpp test_cpipe_node_c_abi.c)
target_link_libraries(test_registry PRIVATE cpipe-runtime cpipe-sdk test_registry_node)
set_property(TARGET test_registry PROPERTY C_STANDARD 99)
set_property(TARGET test_registry PROPERTY C_STANDARD_REQUIRED ON)

cpipe_add_unit_test(test_scheduler_topo test_scheduler_topo.cpp)
target_link_libraries(test_scheduler_topo PRIVATE cpipe-runtime)

add_halide_generator(test_passthrough_copy_generator SOURCES test_passthrough_copy_generator.cpp)
add_halide_library(
    test_passthrough_copy
    FROM test_passthrough_copy_generator
    GENERATOR passthrough_copy
    FUNCTION_NAME passthrough_copy
    USE_RUNTIME cpipe-halide-runtime
    TARGETS host)

cpipe_add_unit_test(test_halide_adapter test_halide_adapter.cpp)
target_link_libraries(test_halide_adapter PRIVATE cpipe-runtime test_passthrough_copy)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

cpipe_add_unit_test(test_passthrough_node test_passthrough_node.cpp)
target_compile_definitions(
    test_passthrough_node
    PRIVATE CPIPE_NODE_SCHEMA_SOURCE="${PROJECT_SOURCE_DIR}/schemas/node-v0.1.json"
            CPIPE_PASSTHROUGH_MANIFEST_SOURCE="${PROJECT_SOURCE_DIR}/src/cpipe/nodes/passthrough.json")
target_link_libraries(
    test_passthrough_node
    PRIVATE cpipe-runtime
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>"
            nlohmann_json::nlohmann_json
            nlohmann_json_schema_validator::validator)
