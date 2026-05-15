// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <vector>

#include "tone_node_test_utils.hpp"

void cpipe_link_builtin_tone_reinhard();

namespace {

float reinhard(float value) {
    return value / (1.0F + value);
}

}  // namespace

TEST_CASE("tone.reinhard applies closed-form global tone curve") {
    cpipe_link_builtin_tone_reinhard();

    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_tone_node(registry, "com.cpipe.tone.reinhard",
                                                       "tone_reinhard", "tone.reinhard");

    const std::vector<std::array<float, 4>> input{
        {0.0F, 0.0F, 0.0F, 1.0F},
        {0.18F, 0.09F, 0.045F, 1.0F},
        {1.0F, 0.5F, 0.25F, 0.5F},
        {16.0F, 8.0F, 4.0F, 0.25F},
    };
    const auto output = cpipe::tests::run_tone_node(desc, input, 4, 1);

    for (std::size_t i = 0; i < input.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            REQUIRE(output[i][c] == Catch::Approx(reinhard(input[i][c])).margin(0.002F));
            REQUIRE(output[i][c] >= 0.0F);
            REQUIRE(output[i][c] < 1.0F);
        }
        REQUIRE(output[i][3] == Catch::Approx(input[i][3]).margin(0.001F));
    }
}

TEST_CASE("tone.reinhard appends metadata step") {
    cpipe_link_builtin_tone_reinhard();

    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_tone_node(registry, "com.cpipe.tone.reinhard",
                                                       "tone_reinhard", "tone.reinhard");

    auto input = cpipe::tests::make_tone_input({{0.18F, 0.18F, 0.18F, 1.0F}}, 1, 1);
    auto output = cpipe::tests::make_tone_output(1, 1);
    REQUIRE(cpipe::tests::process_single_input_node(desc, input, output) == CPIPE_OK);
    cpipe::tests::require_tone_metadata_step(
        output, {"denoise.bm3d", "denoise.wavelet_bayes_shrink", "tone.reinhard"});
}
