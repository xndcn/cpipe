find_package(Taskflow CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanLoader CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
include(HalideHelpers)
cpipe_ensure_halide()

set(pipeline_schema_cpp "${CMAKE_CURRENT_BINARY_DIR}/pipeline_schema.cpp")
add_custom_command(
    OUTPUT "${pipeline_schema_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.1.json"
        -DOUTPUT_CPP="${pipeline_schema_cpp}"
        -DSYMBOL_NAME=PIPELINE_SCHEMA_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.1.json" "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

add_library(
    cpipe-runtime
    STATIC
        ComputeContext.cpp
        BufferHandle.cpp
        HalideBufferAdapter.cpp
        HostContext.cpp
        InferenceContext.cpp
        Pipeline.cpp
        "${pipeline_schema_cpp}"
        Registry.cpp
        Scheduler.cpp
        Sync.cpp
        stub.cpp
        VulkanBuffer.cpp
        VulkanDevicePlane.cpp
        VulkanImage.cpp
)
add_library(cpipe::runtime ALIAS cpipe-runtime)

cpipe_target_warning_flags(cpipe-runtime)

target_link_libraries(
    cpipe-runtime
    PUBLIC
        cpipe::core
        Halide::Runtime
        GPUOpen::VulkanMemoryAllocator
        Vulkan::Headers
    PRIVATE
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator::validator
        Taskflow::Taskflow
        Vulkan::Loader
        spdlog::spdlog
)
