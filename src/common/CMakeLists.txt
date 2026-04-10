configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    @ONLY
)

add_library(cpipe_common STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    error.cpp
    log.cpp
    json_utils.cpp
)

target_include_directories(cpipe_common PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(cpipe_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(cpipe_common PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    tl::expected
)

add_library(cpipe::common ALIAS cpipe_common)
# Keep the old alias so any stale references still resolve during transition
add_library(cpipe::core ALIAS cpipe_common)
