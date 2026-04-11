set(PLUGIN_SRC_DIR ${PROJECT_SOURCE_DIR}/src/plugin)

add_executable(plugin_loader_test plugin_loader_test.cpp)
target_include_directories(plugin_loader_test PRIVATE ${PLUGIN_SRC_DIR})
target_link_libraries(plugin_loader_test PRIVATE cpipe::plugin GTest::gtest_main)
target_compile_definitions(plugin_loader_test PRIVATE
    MOCK_PLUGIN_PATH="$<TARGET_FILE:mock_plugin>"
    MOCK_PLUGIN_ABI_MISMATCH_PATH="$<TARGET_FILE:mock_plugin_abi_mismatch>"
    MOCK_PLUGIN_MISSING_SYMBOLS_PATH="$<TARGET_FILE:mock_plugin_missing_symbols>"
    MOCK_PLUGIN_DIRECTORY="$<TARGET_FILE_DIR:mock_plugin>"
)
gtest_discover_tests(plugin_loader_test)

add_executable(plugin_registry_test plugin_registry_test.cpp)
target_include_directories(plugin_registry_test PRIVATE ${PLUGIN_SRC_DIR})
target_link_libraries(plugin_registry_test PRIVATE cpipe::plugin GTest::gtest_main)
target_compile_definitions(plugin_registry_test PRIVATE
    MOCK_PLUGIN_PATH="$<TARGET_FILE:mock_plugin>"
)
gtest_discover_tests(plugin_registry_test)
