# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 cpipe contributors

function(cpipe_embed_json out_var input_file namespace symbol)
    get_filename_component(abs_input "${input_file}" ABSOLUTE)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${symbol}.cpp")
    file(READ "${abs_input}" json_text)
    string(CONFIGURE
"// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

namespace @namespace@ {

extern const char @symbol@[] = R\"cpipe_json(@json_text@)cpipe_json\";

}  // namespace @namespace@
"
        generated @ONLY)
    file(GENERATE OUTPUT "${output}" CONTENT "${generated}")
    set(${out_var} "${output}" PARENT_SCOPE)
endfunction()
