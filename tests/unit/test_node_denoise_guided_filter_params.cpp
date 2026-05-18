// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_denoise_guided_filter();

TEST_CASE("denoise.guided_filter radius and eps live params change filtering") {
    cpipe_link_builtin_denoise_guided_filter();
    cpipe::runtime::Registry registry;
    const auto& desc =
        cpipe::tests::require_builtin_node(registry, "com.cpipe.denoise.guided_filter");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "radius");
    REQUIRE(manifest.at("params").at(1).at("name") == "eps");

    std::vector<std::array<float, 4>> input(25, {0.2F, 0.2F, 0.2F, 1.0F});
    input[12] = {0.8F, 0.2F, 0.2F, 1.0F};
    const auto narrow = cpipe::tests::run_single_input_node_with_params(
        desc, input, 5, 5, {{"radius", 1}, {"eps", 1e-5}});
    const auto wide = cpipe::tests::run_single_input_node_with_params(
        desc, input, 5, 5, {{"radius", 2}, {"eps", 1e-1}});

    REQUIRE(cpipe::tests::max_channel_delta(narrow, wide) > 0.02F);
}
