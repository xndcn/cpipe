// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace {

nlohmann::json read_json(const std::string& relative_path) {
    std::ifstream input{std::string{CPIPE_SOURCE_DIR} + "/" + relative_path};
    REQUIRE(input.good());
    return nlohmann::json::parse(input);
}

}  // namespace

TEST_CASE("node-v0.2 schema accepts v0.2 param declarations") {
    const auto schema = read_json("schemas/node-v0.2.json");
    const auto manifest = read_json("src/cpipe/nodes/linearize/linearize_dng_lut.json");

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    REQUIRE_NOTHROW(validator.validate(manifest));
}

TEST_CASE("node-v0.2 schema keeps v0.1 manifests forward-compatible") {
    const auto schema = read_json("schemas/node-v0.2.json");
    const auto manifest = read_json("src/cpipe/nodes/passthrough.json");

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    REQUIRE_NOTHROW(validator.validate(manifest));
}

TEST_CASE("node-v0.2 schema accepts the P3-PD-37 live-param manifests") {
    const auto schema = read_json("schemas/node-v0.2.json");
    constexpr std::string_view manifests[] = {
        "src/cpipe/nodes/tone/filmic_rgb.json",
        "src/cpipe/nodes/tone/reinhard.json",
        "src/cpipe/nodes/tone/aces_filmic.json",
        "src/cpipe/nodes/denoise/bm3d.json",
        "src/cpipe/nodes/denoise/guided_filter.json",
        "src/cpipe/nodes/denoise/wavelet_bayes_shrink.json",
        "src/cpipe/nodes/sharpen/edge_aware_usm.json",
        "src/cpipe/nodes/color/lut3d.json",
        "src/cpipe/nodes/tone/mertens_local.json",
    };

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    for (const auto manifest_path : manifests) {
        INFO(manifest_path);
        const auto manifest = read_json(std::string{manifest_path});
        REQUIRE_NOTHROW(validator.validate(manifest));
        REQUIRE(manifest.at("params").empty() == false);
    }
}
