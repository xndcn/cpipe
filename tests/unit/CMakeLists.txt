find_package(Catch2 CONFIG REQUIRED)

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
