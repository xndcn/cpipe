// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <fstream>

#include "cpipe/runtime/Pipeline.hpp"

TEST_CASE("Pipeline loads valid passthrough JSON") {
    auto pipeline = cpipe::runtime::Pipeline::load("tests/fixtures/passthrough.json");
    REQUIRE(pipeline.has_value());
    REQUIRE(pipeline->topo_order().size() == 1);
    REQUIRE(pipeline->topo_order().front() == "copy");
}

TEST_CASE("Pipeline rejects unknown node types") {
    auto pipeline = cpipe::runtime::Pipeline::load("tests/fixtures/invalid_pipeline.json");
    REQUIRE_FALSE(pipeline.has_value());
    REQUIRE(pipeline.error().code == cpipe::StatusCode::NotFound);
}

TEST_CASE("Pipeline rejects dangling edges") {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_dangling_pipeline.json";
    std::ofstream out(path);
    out << R"json({
  "$schema": "https://schemas.cpipe.dev/pipeline/v0.1.json",
  "version": "0.1",
  "id": "dangling",
  "input_layout": {"kind": "Image2D", "format": "R8G8B8A8_UNORM", "dims": [16, 16]},
  "nodes": [{"id": "copy", "type": "com.cpipe.builtin.passthrough", "params": {}}],
  "edges": [{"from": "$input", "to": "copy"}, {"from": "missing", "to": "$output"}]
})json";
    out.close();

    auto pipeline = cpipe::runtime::Pipeline::load(path);
    REQUIRE_FALSE(pipeline.has_value());
    REQUIRE(pipeline.error().code == cpipe::StatusCode::InvalidArgument);
}
