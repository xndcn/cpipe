// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_sharpen_edge_aware_usm();

TEST_CASE("sharpen.edge_aware_usm strength live param changes sharpening") {
    cpipe_link_builtin_sharpen_edge_aware_usm();
    cpipe::runtime::Registry registry;
    const auto& desc =
        cpipe::tests::require_builtin_node(registry, "com.cpipe.sharpen.edge_aware_usm");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "strength");
    REQUIRE(manifest.at("params").at(1).at("name") == "radius");
    REQUIRE(manifest.at("params").at(2).at("name") == "threshold");

    std::vector<std::array<float, 4>> input(9, {0.2F, 0.2F, 0.2F, 1.0F});
    input[4] = {0.6F, 0.6F, 0.6F, 1.0F};
    const auto off = cpipe::tests::run_single_input_node_with_params(
        desc, input, 3, 3, {{"strength", 0.0}, {"radius", 1}, {"threshold", 0.0}});
    const auto strong = cpipe::tests::run_single_input_node_with_params(
        desc, input, 3, 3, {{"strength", 2.0}, {"radius", 1}, {"threshold", 0.0}});

    REQUIRE(cpipe::tests::max_channel_delta(off, strong) > 0.05F);
}
