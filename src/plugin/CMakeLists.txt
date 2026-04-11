add_library(cpipe_plugin STATIC
    plugin_loader.cpp
    plugin_registry.cpp
)

target_include_directories(cpipe_plugin PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(cpipe_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/common
)

target_link_libraries(cpipe_plugin PUBLIC
    cpipe::common
    cpipe::platform
)

target_link_libraries(cpipe_plugin PRIVATE
    $<$<PLATFORM_ID:Linux>:dl>
)

add_library(cpipe::plugin ALIAS cpipe_plugin)
