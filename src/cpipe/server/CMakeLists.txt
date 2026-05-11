add_library(cpipe-server STATIC server_stub.cpp)
add_library(cpipe::server ALIAS cpipe-server)

target_include_directories(cpipe-server PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-server PUBLIC cpipe-runtime)
cpipe_target_warning_flags(cpipe-server)
