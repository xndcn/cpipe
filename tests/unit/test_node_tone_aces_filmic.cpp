// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <vector>

#include "tone_node_test_utils.hpp"

void cpipe_link_builtin_tone_aces_filmic();

namespace {

float aces_fit(float value) {
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float c = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    return std::clamp((value * ((a * value) + b)) / ((value * ((c * value) + d)) + e), 0.0F, 1.0F);
}

}  // namespace

TEST_CASE("tone.aces_filmic applies Narkowicz global tone curve") {
    cpipe_link_builtin_tone_aces_filmic();

    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_tone_node(registry, "com.cpipe.tone.aces_filmic",
                                                       "tone_aces_filmic", "tone.aces_filmic");

    const std::vector<std::array<float, 4>> input{
        {0.0F, 0.0F, 0.0F, 1.0F},
        {0.18F, 0.09F, 0.045F, 0.75F},
        {1.0F, 0.5F, 0.25F, 1.0F},
        {16.0F, 8.0F, 4.0F, 0.5F},
    };
    const auto output = cpipe::tests::run_tone_node(desc, input, 4, 1);

    for (std::size_t i = 0; i < input.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            REQUIRE(output[i][c] == Catch::Approx(aces_fit(input[i][c])).margin(0.002F));
            REQUIRE(output[i][c] >= 0.0F);
            REQUIRE(output[i][c] <= 1.0F);
        }
        REQUIRE(output[i][3] == Catch::Approx(input[i][3]).margin(0.001F));
    }
}

TEST_CASE("tone.aces_filmic appends metadata step") {
    cpipe_link_builtin_tone_aces_filmic();

    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_tone_node(registry, "com.cpipe.tone.aces_filmic",
                                                       "tone_aces_filmic", "tone.aces_filmic");

    auto input = cpipe::tests::make_tone_input({{0.18F, 0.18F, 0.18F, 1.0F}}, 1, 1);
    auto output = cpipe::tests::make_tone_output(1, 1);
    REQUIRE(cpipe::tests::process_single_input_node(desc, input, output) == CPIPE_OK);
    cpipe::tests::require_tone_metadata_step(
        output, {"denoise.bm3d", "denoise.wavelet_bayes_shrink", "tone.aces_filmic"});
}
