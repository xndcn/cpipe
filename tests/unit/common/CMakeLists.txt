set(COMMON_SRC_DIR ${PROJECT_SOURCE_DIR}/src/common)

add_executable(version_test version_test.cpp)
target_link_libraries(version_test PRIVATE cpipe::common GTest::gtest_main)
gtest_discover_tests(version_test)

add_executable(error_test error_test.cpp)
target_include_directories(error_test PRIVATE ${COMMON_SRC_DIR})
target_link_libraries(error_test PRIVATE cpipe::common GTest::gtest_main)
gtest_discover_tests(error_test)

add_executable(log_test log_test.cpp)
target_include_directories(log_test PRIVATE ${COMMON_SRC_DIR})
target_link_libraries(log_test PRIVATE cpipe::common GTest::gtest_main)
gtest_discover_tests(log_test)

add_executable(json_utils_test json_utils_test.cpp)
target_include_directories(json_utils_test PRIVATE ${COMMON_SRC_DIR})
target_link_libraries(json_utils_test PRIVATE cpipe::common GTest::gtest_main)
gtest_discover_tests(json_utils_test)
