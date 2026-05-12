add_library(cpipe-server STATIC empty.cpp)
add_library(cpipe::server ALIAS cpipe-server)

target_include_directories(cpipe-server PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-server PUBLIC cpipe-runtime)
cpipe_target_defaults(cpipe-server)
