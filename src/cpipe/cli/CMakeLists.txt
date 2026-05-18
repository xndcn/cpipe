find_package(CLI11 CONFIG REQUIRED)

add_executable(cpipe main.cpp)
add_executable(cpipe::cli ALIAS cpipe)

cpipe_target_warning_flags(cpipe)

target_compile_definitions(
    cpipe PRIVATE CPIPE_INSTALLED_EDITOR_STATIC="${CMAKE_INSTALL_PREFIX}/share/cpipe/editor"
)

target_link_libraries(
    cpipe
    PRIVATE
        cpipe::runtime
        cpipe::builtin_nodes
        cpipe::server
        CLI11::CLI11
)
