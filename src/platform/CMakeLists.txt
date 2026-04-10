add_library(cpipe_platform STATIC
    common/buffer_pool.cpp
)

target_include_directories(cpipe_platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(cpipe_platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/common
)

target_link_libraries(cpipe_platform PUBLIC cpipe::common)

add_library(cpipe::platform ALIAS cpipe_platform)
