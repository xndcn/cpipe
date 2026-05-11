find_package(spdlog CONFIG REQUIRED)

add_library(cpipe-runtime STATIC HostContext.cpp Registry.cpp runtime_stub.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(cpipe-runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-runtime PUBLIC cpipe-core spdlog::spdlog)
cpipe_target_warning_flags(cpipe-runtime)
