add_executable(version_test version_test.cpp)
target_link_libraries(version_test PRIVATE cpipe::core GTest::gtest_main)
gtest_discover_tests(version_test)
