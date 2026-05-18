// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_tone_aces_filmic();

TEST_CASE("tone.aces_filmic toggle false passes pixels through") {
    cpipe_link_builtin_tone_aces_filmic();
    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_builtin_node(registry, "com.cpipe.tone.aces_filmic");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("type") == "boolean");

    const std::vector<std::array<float, 4>> input{{0.8F, 0.4F, 0.2F, 0.5F}};
    const auto disabled =
        cpipe::tests::run_single_input_node_with_params(desc, input, 1, 1, {{"toggle", false}});
    const auto enabled =
        cpipe::tests::run_single_input_node_with_params(desc, input, 1, 1, {{"toggle", true}});

    REQUIRE(disabled[0][0] == Catch::Approx(input[0][0]).margin(0.001F));
    REQUIRE(disabled[0][3] == Catch::Approx(input[0][3]).margin(0.001F));
    REQUIRE(cpipe::tests::max_channel_delta(disabled, enabled) > 0.02F);
}
