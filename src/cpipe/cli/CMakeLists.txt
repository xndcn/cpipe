find_package(CLI11 CONFIG REQUIRED)

add_executable(cpipe main.cpp)
add_executable(cpipe::cli ALIAS cpipe)

target_link_libraries(
    cpipe
    PRIVATE
        cpipe-runtime
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,cpipe-builtin-nodes>"
        cpipe-server
        CLI11::CLI11)
cpipe_target_warning_flags(cpipe)
