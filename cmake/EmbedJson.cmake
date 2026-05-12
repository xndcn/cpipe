function(cpipe_embed_json target input output_symbol)
    get_filename_component(input_abs "${input}" ABSOLUTE)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${output_symbol}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND
            "${CMAKE_COMMAND}" -DCPIPE_EMBED_JSON_INPUT=${input_abs}
            -DCPIPE_EMBED_JSON_OUTPUT=${output} -DCPIPE_EMBED_JSON_SYMBOL=${output_symbol} -P
            "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
        DEPENDS "${input_abs}" "${PROJECT_SOURCE_DIR}/cmake/EmbedJson.cmake"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${output}")
endfunction()

if(DEFINED CPIPE_EMBED_JSON_INPUT)
    if(NOT DEFINED CPIPE_EMBED_JSON_OUTPUT OR NOT DEFINED CPIPE_EMBED_JSON_SYMBOL)
        message(FATAL_ERROR "CPIPE_EMBED_JSON_OUTPUT and CPIPE_EMBED_JSON_SYMBOL are required")
    endif()

    file(READ "${CPIPE_EMBED_JSON_INPUT}" manifest_json)
    if(manifest_json MATCHES "\\)cpipe_json\"")
        message(FATAL_ERROR "JSON manifest contains the cpipe_json raw-string delimiter")
    endif()

    file(
        WRITE "${CPIPE_EMBED_JSON_OUTPUT}"
        "// SPDX-License-Identifier: Apache-2.0\n"
        "// Copyright (c) 2026 cpipe contributors\n"
        "\n"
        "extern const char ${CPIPE_EMBED_JSON_SYMBOL}[];\n"
        "const char ${CPIPE_EMBED_JSON_SYMBOL}[] = R\"cpipe_json(${manifest_json})cpipe_json\";\n"
    )
endif()
