// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <vector>

#include "tone_node_test_utils.hpp"

void cpipe_link_builtin_tone_filmic_rgb();

TEST_CASE("tone.filmic_rgb compresses a 0..16 stop synthetic gradient") {
    cpipe_link_builtin_tone_filmic_rgb();

    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_tone_node(registry, "com.cpipe.tone.filmic_rgb",
                                                       "tone_filmic_rgb", "tone.filmic_rgb");

    const std::vector<std::array<float, 4>> input{
        {0.0F, 0.0F, 0.0F, 1.0F}, {0.0625F, 0.04F, 0.03F, 1.0F}, {0.18F, 0.14F, 0.10F, 0.8F},
        {1.0F, 0.7F, 0.4F, 1.0F}, {4.0F, 2.0F, 1.0F, 1.0F},      {16.0F, 8.0F, 4.0F, 0.5F},
    };
    const auto output = cpipe::tests::run_tone_node(desc, input, 6, 1);

    REQUIRE(output.front()[0] == Catch::Approx(0.0F).margin(0.001F));
    for (std::size_t i = 1; i < output.size(); ++i) {
        REQUIRE(output[i][0] >= output[i - 1U][0]);
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            REQUIRE(output[i][c] >= 0.0F);
            REQUIRE(output[i][c] <= 1.0F);
        }
        REQUIRE(output[i][3] == Catch::Approx(input[i][3]).margin(0.001F));
    }
    REQUIRE(output.back()[0] < 1.0F);
    REQUIRE(output.back()[0] > 0.90F);
    REQUIRE(output[2][0] > output[1][0]);
}

TEST_CASE("tone.filmic_rgb appends metadata step") {
    cpipe_link_builtin_tone_filmic_rgb();

    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_tone_node(registry, "com.cpipe.tone.filmic_rgb",
                                                       "tone_filmic_rgb", "tone.filmic_rgb");

    auto input = cpipe::tests::make_tone_input({{0.18F, 0.18F, 0.18F, 1.0F}}, 1, 1);
    auto output = cpipe::tests::make_tone_output(1, 1);
    REQUIRE(cpipe::tests::process_single_input_node(desc, input, output) == CPIPE_OK);
    cpipe::tests::require_tone_metadata_step(
        output, {"denoise.bm3d", "denoise.wavelet_bayes_shrink", "tone.filmic_rgb"});
}
