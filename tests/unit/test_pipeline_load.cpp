// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <filesystem>
#include <string>

void cpipe_link_builtin_passthrough();

namespace {

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "tests" / "fixtures" / name;
}

}  // namespace

TEST_CASE("Pipeline loads valid passthrough graph") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough.json"), registry, &pipeline,
                                           &error) == CPIPE_OK);
    REQUIRE(pipeline.node_count() == 1);
    REQUIRE(pipeline.layout().size_bytes() == 64ULL * 64ULL * 4ULL);
}

TEST_CASE("Pipeline rejects unknown node type") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("invalid_pipeline.json"), registry,
                                           &pipeline, &error) == CPIPE_BAD_INDEX);
    REQUIRE(error.find("unknown node type") != std::string::npos);
}

TEST_CASE("Pipeline rejects dangling edge") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("dangling_pipeline.json"), registry,
                                           &pipeline, &error) == CPIPE_BAD_INDEX);
    REQUIRE(error.find("dangling edge") != std::string::npos);
}
