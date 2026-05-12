find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

function(cpipe_add_unit_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain cpipe-core)
    cpipe_configure_target(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

cpipe_add_unit_test(test_pixel_format test_pixel_format.cpp)
cpipe_add_unit_test(test_buffer_layout test_buffer_layout.cpp)
cpipe_add_unit_test(test_buffer_usage test_buffer_usage.cpp)
cpipe_add_unit_test(test_cpu_buffer test_cpu_buffer.cpp)
cpipe_add_unit_test(test_status test_status.cpp)

add_executable(test_registry test_registry.cpp)
target_link_libraries(test_registry PRIVATE Catch2::Catch2WithMain cpipe-runtime cpipe-sdk)
cpipe_configure_target(test_registry)
add_test(NAME test_registry COMMAND test_registry)

add_executable(test_abi_c_compile test_abi_c_compile.c)
target_include_directories(test_abi_c_compile PRIVATE ${PROJECT_SOURCE_DIR}/include)
cpipe_configure_target(test_abi_c_compile)
add_test(NAME test_abi_c_compile COMMAND test_abi_c_compile)

add_executable(test_scheduler_topo test_scheduler_topo.cpp)
target_link_libraries(test_scheduler_topo PRIVATE Catch2::Catch2WithMain cpipe-runtime)
cpipe_configure_target(test_scheduler_topo)
add_test(NAME test_scheduler_topo COMMAND test_scheduler_topo)

add_executable(test_halide_adapter_generator test_halide_adapter_generator.cpp)
target_link_libraries(test_halide_adapter_generator PRIVATE Halide::Generator)
cpipe_configure_target(test_halide_adapter_generator)

add_halide_library(
    test_halide_passthrough
    FROM test_halide_adapter_generator
    GENERATOR test_passthrough_copy
    TARGETS x86-64-linux
    USE_RUNTIME cpipe-halide-runtime
)

add_executable(test_halide_adapter test_halide_adapter.cpp)
target_link_libraries(
    test_halide_adapter PRIVATE Catch2::Catch2WithMain cpipe-runtime test_halide_passthrough
)
cpipe_configure_target(test_halide_adapter)
add_test(NAME test_halide_adapter COMMAND test_halide_adapter)

add_executable(test_passthrough_node test_passthrough_node.cpp)
target_link_libraries(
    test_passthrough_node
    PRIVATE
        Catch2::Catch2WithMain
        cpipe-runtime
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>"
        nlohmann_json::nlohmann_json
        passthrough_copy
)
target_compile_definitions(
    test_passthrough_node
    PRIVATE
        CPIPE_PASSTHROUGH_MANIFEST_PATH="${PROJECT_SOURCE_DIR}/src/cpipe/nodes/passthrough.json"
)
cpipe_configure_target(test_passthrough_node)
add_test(NAME test_passthrough_node COMMAND test_passthrough_node)
