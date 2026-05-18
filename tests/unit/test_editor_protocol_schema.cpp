// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

namespace {

nlohmann::json read_protocol_schema() {
    const auto path =
        std::filesystem::path{CPIPE_SOURCE_DIR} / "schemas" / "editor-protocol-v0.1.json";
    std::ifstream input{path};
    REQUIRE(input.good());
    return nlohmann::json::parse(input);
}

}  // namespace

TEST_CASE("editor-protocol-v0.1 validates REST envelopes") {
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(read_protocol_schema());

    validator.validate({{"ok", true}, {"data", {{"status", "ready"}}}});
    validator.validate(
        {{"ok", false}, {"error", {{"code", "bad_request"}, {"message", "missing node"}}}});
}

TEST_CASE("editor-protocol-v0.1 validates thumbnail and control frames") {
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(read_protocol_schema());

    validator.validate({{"frame_type", "thumbnail"},
                        {"node_id", 7},
                        {"timestamp_ms", 125},
                        {"payload_length", 12},
                        {"content_type", "image/webp"},
                        {"payload", {{"encoding", "base64"}, {"bytes", "UklGRg=="}}}});

    validator.validate(
        {{"frame_type", "control"},
         {"node_id", 7},
         {"timestamp_ms", 130},
         {"payload_length", 76},
         {"content_type", "application/json"},
         {"payload",
          {{"type", "node.update_param"}, {"node_id", "tone"}, {"key", "ev"}, {"value", 0.5}}}});
}
