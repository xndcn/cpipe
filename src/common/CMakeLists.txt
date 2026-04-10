configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    @ONLY
)

add_library(cpipe_core STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

target_include_directories(cpipe_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(cpipe_core PUBLIC spdlog::spdlog)

add_library(cpipe::core ALIAS cpipe_core)
