find_package(Catch2 CONFIG REQUIRED)

add_executable(cpipe-integration-tests test_passthrough_end_to_end.cpp)

target_link_libraries(
    cpipe-integration-tests
    PRIVATE
        cpipe-core
        cpipe-runtime
        cpipe-sdk
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>"
        Catch2::Catch2WithMain)
target_compile_definitions(cpipe-integration-tests PRIVATE CPIPE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
cpipe_target_warning_flags(cpipe-integration-tests)

include(CTest)
include(Catch)
catch_discover_tests(
    cpipe-integration-tests
    TEST_PREFIX
    ""
    EXTRA_ARGS
    --colour-mode
    ansi)
