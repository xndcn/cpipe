add_executable(cpipe main.cpp)

target_link_libraries(cpipe PRIVATE cpipe-runtime cpipe-builtin-nodes cpipe-server)
cpipe_target_defaults(cpipe)
