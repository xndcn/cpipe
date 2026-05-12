find_package(Catch2 CONFIG REQUIRED)

function(cpipe_add_unit_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain cpipe-core)
    cpipe_target_defaults(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_subdirectory(unit)
