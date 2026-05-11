find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

add_executable(
    cpipe-unit-tests
    test_abi_c_compile.c
    test_buffer_layout.cpp
    test_buffer_usage.cpp
    test_cpu_buffer.cpp
    test_halide_adapter.cpp
    test_passthrough_node.cpp
    test_pipeline_load.cpp
    test_pixel_format.cpp
    test_registry.cpp
    test_scheduler_topo.cpp
    test_status.cpp)

target_link_libraries(
    cpipe-unit-tests
    PRIVATE
        cpipe-core
        cpipe-runtime
        cpipe-sdk
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>"
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
        Catch2::Catch2WithMain)
target_compile_definitions(cpipe-unit-tests PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
cpipe_target_warning_flags(cpipe-unit-tests)

include(CTest)
include(Catch)
catch_discover_tests(
    cpipe-unit-tests
    TEST_PREFIX
    ""
    EXTRA_ARGS
    --colour-mode
    ansi)
