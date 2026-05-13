add_library(cpipe-server STATIC stub.cpp)
add_library(cpipe::server ALIAS cpipe-server)

cpipe_target_warning_flags(cpipe-server)

target_link_libraries(cpipe-server PUBLIC cpipe::runtime)
