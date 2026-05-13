find_package(Taskflow CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
include(HalideHelpers)
cpipe_ensure_halide()

add_library(
    cpipe-runtime
    STATIC
        ComputeContext.cpp
        BufferHandle.cpp
        HalideBufferAdapter.cpp
        HostContext.cpp
        InferenceContext.cpp
        Registry.cpp
        Scheduler.cpp
        stub.cpp
)
add_library(cpipe::runtime ALIAS cpipe-runtime)

cpipe_target_warning_flags(cpipe-runtime)

target_link_libraries(
    cpipe-runtime
    PUBLIC
        cpipe::core
        Halide::Runtime
    PRIVATE
        Taskflow::Taskflow
        spdlog::spdlog
)
