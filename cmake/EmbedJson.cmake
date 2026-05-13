if(NOT DEFINED INPUT_JSON OR NOT DEFINED OUTPUT_CPP OR NOT DEFINED SYMBOL_NAME)
    message(FATAL_ERROR "EmbedJson.cmake requires INPUT_JSON, OUTPUT_CPP, and SYMBOL_NAME")
endif()

file(READ "${INPUT_JSON}" json_text)
string(JSON json_type ERROR_VARIABLE json_error TYPE "${json_text}")
if(json_error)
    message(FATAL_ERROR "Invalid JSON in ${INPUT_JSON}: ${json_error}")
endif()

string(REPLACE "\\" "\\\\" json_literal "${json_text}")
string(REPLACE "\"" "\\\"" json_literal "${json_literal}")
string(REPLACE "\n" "\\n\"\n\"" json_literal "${json_literal}")

file(
    WRITE
    "${OUTPUT_CPP}"
    "// SPDX-License-Identifier: Apache-2.0\n"
    "// Copyright (c) 2026 cpipe contributors\n\n"
    "extern const char ${SYMBOL_NAME}[];\n"
    "const char ${SYMBOL_NAME}[] = \"${json_literal}\";\n"
)
