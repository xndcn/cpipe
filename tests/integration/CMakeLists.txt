find_package(Catch2 CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

add_executable(test_passthrough_end_to_end test_passthrough_end_to_end.cpp)
cpipe_target_warning_flags(test_passthrough_end_to_end)
target_compile_definitions(
    test_passthrough_end_to_end
    PRIVATE
        CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CPIPE_BINARY_DIR="${PROJECT_BINARY_DIR}"
)
target_link_libraries(
    test_passthrough_end_to_end
    PRIVATE
        Catch2::Catch2WithMain
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
)
add_test(NAME test_passthrough_end_to_end COMMAND test_passthrough_end_to_end)
set_tests_properties(test_passthrough_end_to_end PROPERTIES LABELS integration)

add_executable(test_diamond_dag_parallel test_diamond_dag_parallel.cpp)
cpipe_target_warning_flags(test_diamond_dag_parallel)
target_link_libraries(
    test_diamond_dag_parallel
    PRIVATE
        Catch2::Catch2WithMain
        cpipe::runtime
)
add_test(NAME test_diamond_dag_parallel COMMAND test_diamond_dag_parallel)
set_tests_properties(test_diamond_dag_parallel PROPERTIES LABELS integration)

add_executable(test_min_pipeline_dng_to_heif test_min_pipeline_dng_to_heif.cpp)
add_dependencies(test_min_pipeline_dng_to_heif cpipe)
cpipe_target_warning_flags(test_min_pipeline_dng_to_heif)
target_compile_definitions(
    test_min_pipeline_dng_to_heif
    PRIVATE
        CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CPIPE_CLI_PATH="$<TARGET_FILE:cpipe>"
)
target_link_libraries(
    test_min_pipeline_dng_to_heif
    PRIVATE
        Catch2::Catch2WithMain
        cpipe::color
        cpipe::runtime
        cpipe::sdk
        cpipe::builtin_nodes
        OpenImageIO::OpenImageIO
)
add_test(NAME test_min_pipeline_dng_to_heif COMMAND test_min_pipeline_dng_to_heif)
set_tests_properties(test_min_pipeline_dng_to_heif PROPERTIES LABELS integration)
