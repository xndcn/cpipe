// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_tone_reinhard();

TEST_CASE("tone.reinhard white_point live param changes shoulder compression") {
    cpipe_link_builtin_tone_reinhard();
    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_builtin_node(registry, "com.cpipe.tone.reinhard");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "white_point");

    const std::vector<std::array<float, 4>> input{{2.0F, 1.0F, 0.5F, 0.75F}};
    const auto low =
        cpipe::tests::run_single_input_node_with_params(desc, input, 1, 1, {{"white_point", 0.5}});
    const auto high =
        cpipe::tests::run_single_input_node_with_params(desc, input, 1, 1, {{"white_point", 10.0}});

    REQUIRE(cpipe::tests::max_channel_delta(low, high) > 0.05F);
    REQUIRE(low[0][3] == high[0][3]);
}
