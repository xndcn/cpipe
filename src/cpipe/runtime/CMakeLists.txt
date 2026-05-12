cpipe_embed_json(PIPELINE_SCHEMA_SOURCE "${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.1.json"
    "cpipe::runtime" "kPipelineSchemaJson")

add_library(cpipe-runtime STATIC
    ComputeContext.cpp
    HalideBufferAdapter.cpp
    HostSuites.cpp
    InferenceContext.cpp
    Pipeline.cpp
    Registry.cpp
    Scheduler.cpp
    ${PIPELINE_SCHEMA_SOURCE})

add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(cpipe-runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(cpipe-runtime
    PUBLIC
        cpipe-core
        cpipe-sdk
        Halide::Runtime
        Taskflow::Taskflow
        nlohmann_json::nlohmann_json
    PRIVATE
        nlohmann_json_schema_validator::validator
        spdlog::spdlog)

cpipe_target_warning_flags(cpipe-runtime)
