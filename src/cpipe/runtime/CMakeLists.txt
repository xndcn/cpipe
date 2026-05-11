find_package(spdlog CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)

add_library(
    cpipe-runtime
    STATIC
        ComputeContext.cpp
        HalideBufferAdapter.cpp
        HostContext.cpp
        InferenceContext.cpp
        Registry.cpp
        Scheduler.cpp
        runtime_stub.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(cpipe-runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cpipe-runtime PUBLIC cpipe-core spdlog::spdlog Taskflow::Taskflow)
cpipe_target_warning_flags(cpipe-runtime)
