// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Pipeline.hpp>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(CPIPE_SOURCE_DIR) / "tests/fixtures" / name;
}

}  // namespace

TEST_CASE("test_pipeline_load: valid passthrough pipeline loads") {
    std::string error;
    const auto pipeline = cpipe::runtime::Pipeline::load_file(fixture("passthrough.json"), &error);

    REQUIRE(pipeline.has_value());
    CHECK(error.empty());
    CHECK(pipeline->node_count() == 1);
}

TEST_CASE("test_pipeline_load: unknown node type is rejected") {
    std::string error;
    const auto pipeline =
        cpipe::runtime::Pipeline::load_file(fixture("unknown_node_pipeline.json"), &error);

    CHECK_FALSE(pipeline.has_value());
    CHECK(error.find("unknown node type") != std::string::npos);
}

TEST_CASE("test_pipeline_load: dangling edge is rejected") {
    std::string error;
    const auto pipeline =
        cpipe::runtime::Pipeline::load_file(fixture("invalid_pipeline.json"), &error);

    CHECK_FALSE(pipeline.has_value());
    CHECK(error.find("dangling edge") != std::string::npos);
}
