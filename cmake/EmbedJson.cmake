function(cpipe_embed_json target input output_symbol)
    set(options)
    set(one_value_args OUTPUT)
    set(multi_value_args)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_OUTPUT)
        message(FATAL_ERROR "cpipe_embed_json requires OUTPUT <path>")
    endif()

    add_custom_command(
        OUTPUT "${ARG_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
                -DINPUT=${input}
                -DOUTPUT=${ARG_OUTPUT}
                -DSYMBOL=${output_symbol}
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedJsonScript.cmake"
        DEPENDS "${input}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedJsonScript.cmake"
        VERBATIM)

    target_sources(${target} PRIVATE "${ARG_OUTPUT}")
endfunction()
