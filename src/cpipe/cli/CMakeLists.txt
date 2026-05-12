add_executable(cpipe main.cpp)
add_executable(cpipe-cli ALIAS cpipe)

target_link_libraries(cpipe
    PRIVATE
        cpipe-runtime
        cpipe-server
        CLI11::CLI11
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>")

cpipe_target_warning_flags(cpipe)
