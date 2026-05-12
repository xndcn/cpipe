find_package(Taskflow CONFIG REQUIRED)
cpipe_provide_halide()
add_halide_runtime(cpipe-halide-runtime TARGETS host)

add_library(
    cpipe-runtime STATIC
    ComputeContext.cpp
    HalideBufferAdapter.cpp
    InferenceContext.cpp
    Registry.cpp
    RuntimeStub.cpp
    Scheduler.cpp
)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(
    cpipe-runtime
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    cpipe-runtime
    PUBLIC cpipe-core cpipe-sdk cpipe-halide-runtime
    PRIVATE Taskflow::Taskflow
)

cpipe_configure_target(cpipe-runtime)
