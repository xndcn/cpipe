add_executable(cpipe main.cpp)
add_executable(cpipe::cli ALIAS cpipe)

target_link_libraries(cpipe PRIVATE cpipe-runtime cpipe-builtin-nodes cpipe-server)

cpipe_configure_target(cpipe)
