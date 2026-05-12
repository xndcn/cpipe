add_executable(cpipe main.cpp)

target_link_libraries(
    cpipe
    PRIVATE cpipe-runtime "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>" cpipe-server)
cpipe_target_defaults(cpipe)
