add_library(mock_plugin MODULE mock_plugin.c)
set_target_properties(mock_plugin PROPERTIES PREFIX "")
target_include_directories(mock_plugin PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_library(mock_plugin_abi_mismatch MODULE mock_plugin_abi_mismatch.c)
set_target_properties(mock_plugin_abi_mismatch PROPERTIES PREFIX "")
target_include_directories(mock_plugin_abi_mismatch PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_library(mock_plugin_missing_symbols MODULE mock_plugin_missing_symbols.c)
set_target_properties(mock_plugin_missing_symbols PROPERTIES PREFIX "")
target_include_directories(mock_plugin_missing_symbols PRIVATE ${PROJECT_SOURCE_DIR}/include)
