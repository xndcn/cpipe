// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_denoise_bm3d();

TEST_CASE("denoise.bm3d sigma live param changes smoothing strength") {
    cpipe_link_builtin_denoise_bm3d();
    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_builtin_node(registry, "com.cpipe.denoise.bm3d");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "sigma");
    REQUIRE(manifest.at("params").at(1).at("name") == "sigma_override");

    const std::vector<std::array<float, 4>> input{{0.1F, 0.1F, 0.1F, 1.0F},
                                                  {0.9F, 0.1F, 0.1F, 1.0F},
                                                  {0.1F, 0.1F, 0.1F, 1.0F},
                                                  {0.9F, 0.1F, 0.1F, 1.0F}};
    const auto off =
        cpipe::tests::run_single_input_node_with_params(desc, input, 2, 2, {{"sigma", 0.0}});
    const auto strong =
        cpipe::tests::run_single_input_node_with_params(desc, input, 2, 2, {{"sigma", 0.2}});

    REQUIRE(cpipe::tests::max_channel_delta(off, strong) > 0.1F);
}
