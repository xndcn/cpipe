cpipe_embed_json(PASSTHROUGH_MANIFEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/passthrough.json"
    "cpipe::nodes" "kPassthroughManifestJson")

add_halide_generator(passthrough_copy_generator SOURCES passthrough_copy_generator.cpp)
add_halide_library(passthrough_copy
    FROM passthrough_copy_generator
    GENERATOR passthrough_copy
    FUNCTION_NAME passthrough_copy
    TARGETS host)

add_library(cpipe-builtin-nodes STATIC
    passthrough.cpp
    ${PASSTHROUGH_MANIFEST_SOURCE})

if(CPIPE_ENABLE_SANITIZERS)
    set_source_files_properties(passthrough.cpp PROPERTIES COMPILE_OPTIONS -fno-sanitize=address)
endif()

add_library(cpipe::builtin-nodes ALIAS cpipe-builtin-nodes)

target_include_directories(cpipe-builtin-nodes
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(cpipe-builtin-nodes
    PUBLIC
        cpipe-sdk
        cpipe-runtime
    PRIVATE
        Halide::Runtime
        passthrough_copy)

cpipe_target_warning_flags(cpipe-builtin-nodes)
