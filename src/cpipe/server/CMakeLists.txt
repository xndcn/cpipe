find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(CPIPE_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

set(node_schema_cpp "${CMAKE_CURRENT_BINARY_DIR}/node_schema.cpp")
add_custom_command(
    OUTPUT "${node_schema_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${PROJECT_SOURCE_DIR}/schemas/node-v0.2.json"
        -DOUTPUT_CPP="${node_schema_cpp}"
        -DSYMBOL_NAME=NODE_SCHEMA_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/schemas/node-v0.2.json" "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

set(server_pipeline_schema_cpp "${CMAKE_CURRENT_BINARY_DIR}/server_pipeline_schema.cpp")
add_custom_command(
    OUTPUT "${server_pipeline_schema_cpp}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DINPUT_JSON="${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.4.json"
        -DOUTPUT_CPP="${server_pipeline_schema_cpp}"
        -DSYMBOL_NAME=SERVER_PIPELINE_SCHEMA_JSON
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/schemas/pipeline-v0.4.json" "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
)

add_library(cpipe-server STATIC EditorServer.cpp "${node_schema_cpp}" "${server_pipeline_schema_cpp}" stub.cpp)
add_library(cpipe::server ALIAS cpipe-server)

cpipe_target_warning_flags(cpipe-server)

target_compile_definitions(
    cpipe-server
    PRIVATE
        CPIPE_ENABLE_TRACY=$<BOOL:${CPIPE_ENABLE_TRACY}>
)

target_link_libraries(
    cpipe-server
    PUBLIC
        cpipe::runtime
    PRIVATE
        nlohmann_json::nlohmann_json
        unofficial::uwebsockets::uwebsockets
        WebP::webp
)

if(CPIPE_ENABLE_TRACY)
    target_link_libraries(cpipe-server PRIVATE Tracy::TracyClient)
endif()
