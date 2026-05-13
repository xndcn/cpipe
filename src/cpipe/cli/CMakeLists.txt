find_package(CLI11 CONFIG REQUIRED)

add_executable(cpipe main.cpp)
add_executable(cpipe::cli ALIAS cpipe)

cpipe_target_warning_flags(cpipe)

target_link_libraries(
    cpipe
    PRIVATE
        cpipe::runtime
        cpipe::builtin_nodes
        cpipe::server
        CLI11::CLI11
)
