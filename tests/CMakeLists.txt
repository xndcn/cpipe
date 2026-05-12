add_executable(gen_passthrough_input fixtures/gen_passthrough_input.cpp)
cpipe_target_warning_flags(gen_passthrough_input)

set(PASSTHROUGH_BIN "${PROJECT_SOURCE_DIR}/tests/fixtures/passthrough.bin")
add_custom_command(
    OUTPUT "${PASSTHROUGH_BIN}"
    COMMAND gen_passthrough_input "${PASSTHROUGH_BIN}"
    DEPENDS gen_passthrough_input)
add_custom_target(cpipe-fixtures DEPENDS "${PASSTHROUGH_BIN}")

function(cpipe_add_unit_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/cpipe/runtime")
    target_link_libraries(${name}
        PRIVATE
            Catch2::Catch2WithMain
            cpipe-runtime
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>")
    cpipe_target_warning_flags(${name})
    add_dependencies(${name} cpipe-fixtures)
    catch_discover_tests(${name} WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endfunction()

cpipe_add_unit_test(test_pixel_format unit/test_pixel_format.cpp)
cpipe_add_unit_test(test_buffer_layout unit/test_buffer_layout.cpp)
cpipe_add_unit_test(test_buffer_usage unit/test_buffer_usage.cpp)
cpipe_add_unit_test(test_status unit/test_status.cpp)
cpipe_add_unit_test(test_cpu_buffer unit/test_cpu_buffer.cpp)
if(CPIPE_ENABLE_SANITIZERS)
    set_source_files_properties(unit/test_registry.cpp PROPERTIES COMPILE_OPTIONS -fno-sanitize=address)
endif()
cpipe_add_unit_test(test_registry unit/test_registry.cpp)
cpipe_add_unit_test(test_pipeline_load unit/test_pipeline_load.cpp)
cpipe_add_unit_test(test_scheduler_topo unit/test_scheduler_topo.cpp)
cpipe_add_unit_test(test_halide_adapter unit/test_halide_adapter.cpp)
cpipe_add_unit_test(test_passthrough_node unit/test_passthrough_node.cpp)

add_executable(test_abi_c_compile unit/test_abi_c_compile.c)
target_include_directories(test_abi_c_compile PRIVATE "${PROJECT_SOURCE_DIR}/include")
cpipe_target_c_warning_flags(test_abi_c_compile)
add_test(NAME test_abi_c_compile COMMAND test_abi_c_compile)

add_executable(test_passthrough_end_to_end integration/test_passthrough_end_to_end.cpp)
target_include_directories(test_passthrough_end_to_end PRIVATE
    "${PROJECT_SOURCE_DIR}/src/cpipe/runtime")
target_link_libraries(test_passthrough_end_to_end
    PRIVATE
        Catch2::Catch2WithMain
        cpipe-runtime
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>")
cpipe_target_warning_flags(test_passthrough_end_to_end)
add_dependencies(test_passthrough_end_to_end cpipe-fixtures)
catch_discover_tests(test_passthrough_end_to_end WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
