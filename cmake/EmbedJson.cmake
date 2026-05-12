function(cpipe_embed_json out_var input_json symbol namespace_name)
    get_filename_component(input_abs "${input_json}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(output_cpp "${CMAKE_CURRENT_BINARY_DIR}/${symbol}_embedded.cpp")

    add_custom_command(
        OUTPUT "${output_cpp}"
        COMMAND
            "${CMAKE_COMMAND}" "-DCPIPE_EMBED_JSON_INPUT=${input_abs}"
            "-DCPIPE_EMBED_JSON_OUTPUT=${output_cpp}" "-DCPIPE_EMBED_JSON_SYMBOL=${symbol}"
            "-DCPIPE_EMBED_JSON_NAMESPACE=${namespace_name}" -P
            "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
        DEPENDS "${input_abs}" "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
        VERBATIM)

    set(${out_var} "${output_cpp}" PARENT_SCOPE)
endfunction()

if(DEFINED CPIPE_EMBED_JSON_INPUT)
    file(READ "${CPIPE_EMBED_JSON_INPUT}" json_content)
    string(REPLACE "\\" "\\\\" json_content "${json_content}")
    string(REPLACE "\"" "\\\"" json_content "${json_content}")
    string(REPLACE "\n" "\\n\"\n\"" json_content "${json_content}")

    file(WRITE "${CPIPE_EMBED_JSON_OUTPUT}" "// SPDX-License-Identifier: Apache-2.0\n")
    file(APPEND "${CPIPE_EMBED_JSON_OUTPUT}" "// Copyright (c) 2026 cpipe contributors\n\n")
    file(APPEND "${CPIPE_EMBED_JSON_OUTPUT}" "namespace ${CPIPE_EMBED_JSON_NAMESPACE} {\n")
    file(APPEND "${CPIPE_EMBED_JSON_OUTPUT}"
         "extern const char* const ${CPIPE_EMBED_JSON_SYMBOL};\n")
    file(APPEND "${CPIPE_EMBED_JSON_OUTPUT}"
         "const char* const ${CPIPE_EMBED_JSON_SYMBOL} = \"${json_content}\";\n")
    file(APPEND "${CPIPE_EMBED_JSON_OUTPUT}" "}  // namespace ${CPIPE_EMBED_JSON_NAMESPACE}\n")
endif()
