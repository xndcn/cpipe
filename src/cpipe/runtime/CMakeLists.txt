cpipe_find_halide()
find_package(Taskflow CONFIG REQUIRED)

add_halide_runtime(cpipe-halide-runtime TARGETS host)

add_library(
    cpipe-runtime STATIC
    BufferHandle.cpp
    ComputeContext.cpp
    HalideBufferAdapter.cpp
    InferenceContext.cpp
    Registry.cpp
    Scheduler.cpp
    TaskExecutor.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(cpipe-runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(
    cpipe-runtime
    PUBLIC cpipe-core cpipe-sdk cpipe-halide-runtime
    PRIVATE Taskflow::Taskflow)
cpipe_target_defaults(cpipe-runtime)
