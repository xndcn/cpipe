// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

void cpipe_link_builtin_passthrough();

namespace {

std::filesystem::path source_path(std::string_view relative) {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / relative;
}

nlohmann::json read_json(std::string_view relative) {
    std::ifstream input{source_path(relative)};
    REQUIRE(input.good());
    return nlohmann::json::parse(input);
}

}  // namespace

TEST_CASE("pipeline-v0.4 schema validates migrated example pipelines") {
    const auto schema = read_json("schemas/pipeline-v0.4.json");
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);

    validator.validate(read_json("examples/pipelines/min-pipeline.cpipe.json"));
    validator.validate(read_json("examples/pipelines/full-classic-pipeline.cpipe.json"));
    validator.validate(read_json("examples/pipelines/full-classic-pipeline-hdr.cpipe.json"));
}

TEST_CASE("pipeline-v0.4 schema accepts optional node ui hints") {
    auto pipeline = read_json("tests/fixtures/passthrough-v0.4.json");
    pipeline["nodes"][0]["ui"] = {
        {"x", 10.5},
        {"y", -4.0},
        {"color", "#33aaff"},
        {"collapsed", false},
    };

    const auto schema = read_json("schemas/pipeline-v0.4.json");
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    validator.validate(pipeline);
}

TEST_CASE("Pipeline loads v0.4 and rejects unmigrated v0.3") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(source_path("tests/fixtures/passthrough-v0.4.json"),
                                           registry, &pipeline, &error) == CPIPE_OK);

    error.clear();
    REQUIRE(cpipe::runtime::Pipeline::load(source_path("tests/fixtures/passthrough-v0.3.json"),
                                           registry, &pipeline, &error) == CPIPE_FAILED);
    REQUIRE(error.find("schema version mismatch") != std::string::npos);
    REQUIRE(error.find("tools/migrate/v03_to_v04.py") != std::string::npos);
}
