add_executable(buffer_pool_test buffer_pool_test.cpp)
target_link_libraries(buffer_pool_test PRIVATE cpipe::platform GTest::gtest_main)
gtest_discover_tests(buffer_pool_test)
