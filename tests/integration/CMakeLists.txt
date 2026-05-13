find_package(Catch2 CONFIG REQUIRED)

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

add_executable(test_diamond_dag_parallel test_diamond_dag_parallel.cpp)
cpipe_target_warning_flags(test_diamond_dag_parallel)
target_link_libraries(
    test_diamond_dag_parallel
    PRIVATE
        Catch2::Catch2WithMain
        cpipe::runtime
)
add_test(NAME test_diamond_dag_parallel COMMAND test_diamond_dag_parallel)
