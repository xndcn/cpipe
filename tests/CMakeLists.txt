add_subdirectory(unit)
add_subdirectory(integration)

add_executable(gen_passthrough_input fixtures/gen_passthrough_input.cpp)
cpipe_target_warning_flags(gen_passthrough_input)

set(generated_fixture_dir "${PROJECT_BINARY_DIR}/tests/fixtures")
set(generated_passthrough_bin "${generated_fixture_dir}/passthrough.bin")
add_custom_command(
    OUTPUT "${generated_passthrough_bin}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_fixture_dir}"
    COMMAND gen_passthrough_input "${generated_passthrough_bin}"
    DEPENDS gen_passthrough_input
)
add_custom_target(cpipe-test-fixtures ALL DEPENDS "${generated_passthrough_bin}")
