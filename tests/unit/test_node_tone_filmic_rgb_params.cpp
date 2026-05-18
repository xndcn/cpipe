// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_tone_filmic_rgb();

TEST_CASE("tone.filmic_rgb live params change the rendered output") {
    cpipe_link_builtin_tone_filmic_rgb();
    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_builtin_node(registry, "com.cpipe.tone.filmic_rgb");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").size() == 5);
    REQUIRE(manifest.at("params").at(0).at("name") == "ev");

    const std::vector<std::array<float, 4>> input{{0.18F, 0.12F, 0.08F, 1.0F},
                                                  {1.0F, 0.5F, 0.25F, 1.0F}};
    const auto low = cpipe::tests::run_single_input_node_with_params(desc, input, 2, 1,
                                                                     {{"ev", -1.0},
                                                                      {"contrast", 1.0},
                                                                      {"saturation", 1.0},
                                                                      {"highlights", 1.0},
                                                                      {"shadows", 1.0}});
    const auto high = cpipe::tests::run_single_input_node_with_params(desc, input, 2, 1,
                                                                      {{"ev", 1.0},
                                                                       {"contrast", 1.0},
                                                                       {"saturation", 1.0},
                                                                       {"highlights", 1.0},
                                                                       {"shadows", 1.0}});

    REQUIRE(high[0][0] > low[0][0]);
    REQUIRE(cpipe::tests::max_channel_delta(low, high) > 0.05F);
}
