find_package(spdlog CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

add_library(
    cpipe-runtime
    STATIC
        ComputeContext.cpp
        HalideBufferAdapter.cpp
        HostContext.cpp
        InferenceContext.cpp
        Pipeline.cpp
        Registry.cpp
        Scheduler.cpp
        runtime_stub.cpp)
add_library(cpipe::runtime ALIAS cpipe-runtime)

target_include_directories(cpipe-runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(
    cpipe-runtime
    PUBLIC
        cpipe-core
        spdlog::spdlog
        Taskflow::Taskflow
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator)
cpipe_embed_json(
    cpipe-runtime
    "${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.1.json"
    PIPELINE_SCHEMA_JSON
    OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/pipeline_schema.cpp")
cpipe_target_warning_flags(cpipe-runtime)
