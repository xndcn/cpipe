// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_tone_mertens_local();

TEST_CASE("tone.mertens_local weights live params change exposure fusion") {
    cpipe_link_builtin_tone_mertens_local();
    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_builtin_node(registry, "com.cpipe.tone.mertens_local");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "weight_contrast");
    REQUIRE(manifest.at("params").at(1).at("name") == "weight_saturation");
    REQUIRE(manifest.at("params").at(2).at("name") == "weight_well_exposedness");

    const std::vector<std::array<float, 4>> under{{0.05F, 0.08F, 0.10F, 1.0F},
                                                  {0.20F, 0.10F, 0.05F, 1.0F}};
    const std::vector<std::array<float, 4>> normal{{0.25F, 0.35F, 0.40F, 1.0F},
                                                   {0.50F, 0.35F, 0.20F, 1.0F}};
    const std::vector<std::array<float, 4>> over{{0.9F, 1.2F, 1.4F, 1.0F},
                                                 {1.2F, 0.9F, 0.5F, 1.0F}};
    const std::array<std::shared_ptr<cpipe::tests::CpuBuffer>, 3> inputs{
        cpipe::tests::make_tone_input(under, 2, 1), cpipe::tests::make_tone_input(normal, 2, 1),
        cpipe::tests::make_tone_input(over, 2, 1)};

    auto balanced_output = cpipe::tests::make_tone_output(2, 1);
    REQUIRE(cpipe::tests::process_three_input_node_with_params(
                desc, inputs, balanced_output,
                {{"weight_contrast", 0.0},
                 {"weight_saturation", 0.0},
                 {"weight_well_exposedness", 0.0}}) == CPIPE_OK);
    auto weighted_output = cpipe::tests::make_tone_output(2, 1);
    REQUIRE(cpipe::tests::process_three_input_node_with_params(
                desc, inputs, weighted_output,
                {{"weight_contrast", 2.0},
                 {"weight_saturation", 2.0},
                 {"weight_well_exposedness", 2.0}}) == CPIPE_OK);

    const auto balanced = cpipe::tests::read_rgba16(*balanced_output, 2);
    const auto weighted = cpipe::tests::read_rgba16(*weighted_output, 2);
    REQUIRE(cpipe::tests::max_channel_delta(balanced, weighted) > 0.03F);
}
