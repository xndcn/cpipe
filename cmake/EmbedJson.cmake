function(cpipe_embed_json out_var input_json symbol namespace_name)
    get_filename_component(input_abs "${input_json}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(output_cpp "${CMAKE_CURRENT_BINARY_DIR}/${symbol}_embedded.cpp")

    file(READ "${input_abs}" json_content)
    string(REPLACE "\\" "\\\\" json_content "${json_content}")
    string(REPLACE "\"" "\\\"" json_content "${json_content}")
    string(REPLACE "\n" "\\n\"\n\"" json_content "${json_content}")

    file(WRITE "${output_cpp}" "// SPDX-License-Identifier: Apache-2.0\n")
    file(APPEND "${output_cpp}" "// Copyright (c) 2026 cpipe contributors\n\n")
    file(APPEND "${output_cpp}" "#include <string_view>\n\n")
    file(APPEND "${output_cpp}" "namespace ${namespace_name} {\n")
    file(APPEND "${output_cpp}" "const std::string_view ${symbol} = \"${json_content}\";\n")
    file(APPEND "${output_cpp}" "}  // namespace ${namespace_name}\n")

    set(${out_var} "${output_cpp}" PARENT_SCOPE)
endfunction()
