find_package(Taskflow CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(cpipe-runtime STATIC HostContext.cpp Registry.cpp stub.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

cpipe_target_warning_flags(cpipe-runtime)

target_link_libraries(
    cpipe-runtime
    PUBLIC
        cpipe::core
    PRIVATE
        Taskflow::Taskflow
        spdlog::spdlog
)
