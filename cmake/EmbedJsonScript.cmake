file(READ "${INPUT}" json_content)
string(CONFIGURE [[
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

extern const char @SYMBOL@[] = R"cpipe_json(@json_content@)cpipe_json";
]] generated @ONLY)
file(WRITE "${OUTPUT}" "${generated}")
