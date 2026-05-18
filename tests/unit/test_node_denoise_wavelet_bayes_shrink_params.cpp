// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_denoise_wavelet_bayes_shrink();

TEST_CASE("denoise.wavelet_bayes_shrink chroma_strength changes chroma thresholding") {
    cpipe_link_builtin_denoise_wavelet_bayes_shrink();
    cpipe::runtime::Registry registry;
    const auto& desc =
        cpipe::tests::require_builtin_node(registry, "com.cpipe.denoise.wavelet_bayes_shrink");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "chroma_strength");

    const std::vector<std::array<float, 4>> input{{0.8F, 0.2F, 0.1F, 1.0F},
                                                  {0.1F, 0.7F, 0.8F, 1.0F},
                                                  {0.7F, 0.1F, 0.2F, 1.0F},
                                                  {0.2F, 0.8F, 0.7F, 1.0F}};
    const auto off = cpipe::tests::run_single_input_node_with_params(desc, input, 2, 2,
                                                                     {{"chroma_strength", 0.0}});
    const auto strong = cpipe::tests::run_single_input_node_with_params(desc, input, 2, 2,
                                                                        {{"chroma_strength", 2.0}});

    REQUIRE(cpipe::tests::max_channel_delta(off, strong) > 0.02F);
}
