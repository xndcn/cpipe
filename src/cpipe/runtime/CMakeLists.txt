find_package(Taskflow CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(libraw CONFIG REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanLoader CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
if(CPIPE_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()
include(HalideHelpers)
cpipe_ensure_halide()

set(pipeline_schema_cpp "${CMAKE_CURRENT_BINARY_DIR}/pipeline_schema.cpp")
add_custom_command(
    OUTPUT "${pipeline_schema_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.4.json"
        -DOUTPUT_CPP="${pipeline_schema_cpp}"
        -DSYMBOL_NAME=PIPELINE_SCHEMA_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.4.json" "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

add_library(
    cpipe-runtime
    STATIC
        ../ingest/dng/DngReader.cpp
        ../ingest/dng_opcode/OpcodeList2.cpp
        ../ingest/dng_opcode/OpcodeList3.cpp
        ../ingest/dng_opcode/OpcodeListParser.cpp
        ComputeContext.cpp
        BufferHandle.cpp
        ../color/OcioVulkanProcessor.cpp
        HalideBufferAdapter.cpp
        HalideFilterRegistry.cpp
        HostContext.cpp
        InferenceContext.cpp
        MemoryPlanner.cpp
        MetadataHandle.cpp
        ParamHandle.cpp
        Pipeline.cpp
        PrecisionPlanner.cpp
        "${pipeline_schema_cpp}"
        Registry.cpp
        Scheduler.cpp
        Sync.cpp
        stub.cpp
        VulkanBuffer.cpp
        VulkanCommandBuffer.cpp
        VulkanDevicePlane.cpp
        VulkanImage.cpp
)
add_library(cpipe::runtime ALIAS cpipe-runtime)

cpipe_target_warning_flags(cpipe-runtime)

set_source_files_properties(
    ../color/OcioVulkanProcessor.cpp
    PROPERTIES
        COMPILE_OPTIONS "-fno-sanitize=vptr"
)

target_compile_definitions(
    cpipe-runtime
    PRIVATE
        CPIPE_ENABLE_TRACY=$<BOOL:${CPIPE_ENABLE_TRACY}>
)

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
        libraw::raw_r
        OpenColorIO::OpenColorIO
        Taskflow::Taskflow
        Vulkan::Loader
        glslang::GenericCodeGen
        glslang::MachineIndependent
        glslang::OSDependent
        glslang::SPIRV
        glslang::glslang
        glslang::glslang-default-resource-limits
        yaml-cpp::yaml-cpp
        spdlog::spdlog
)

if(CPIPE_ENABLE_TRACY)
    target_link_libraries(cpipe-runtime PRIVATE Tracy::TracyClient)
endif()
