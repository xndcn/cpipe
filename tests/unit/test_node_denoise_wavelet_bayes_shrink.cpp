// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_denoise_wavelet_bayes_shrink();

namespace {

std::shared_ptr<cpipe::tests::BufferMetadata> scene_metadata() {
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"color_matrix", "denoise.guided_filter"};
    return metadata;
}

float chroma_span(const std::vector<std::array<float, 4>>& pixels) {
    auto min_value = pixels.front()[0] - pixels.front()[2];
    auto max_value = min_value;
    for (const auto& pixel : pixels) {
        const auto chroma = pixel[0] - pixel[2];
        min_value = std::min(min_value, chroma);
        max_value = std::max(max_value, chroma);
    }
    return max_value - min_value;
}

}  // namespace

TEST_CASE("denoise.wavelet_bayes_shrink attenuates 2x2 chroma detail and preserves metadata") {
    cpipe_link_builtin_denoise_wavelet_bayes_shrink();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.denoise.wavelet_bayes_shrink");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") ==
            nlohmann::json::array({"denoise_wavelet_bayes_shrink"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"denoise.wavelet_bayes_shrink"}));

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(2, 2),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input, {{{0.38F, 0.30F, 0.22F, 1.0F},
                                         {0.22F, 0.30F, 0.38F, 0.8F},
                                         {0.37F, 0.30F, 0.23F, 0.6F},
                                         {0.23F, 0.30F, 0.37F, 0.4F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(2, 2), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto input_pixels = cpipe::tests::read_rgba16(*input, 4);
    const auto output_pixels = cpipe::tests::read_rgba16(*output, 4);
    REQUIRE(chroma_span(output_pixels) < chroma_span(input_pixels));
    for (std::size_t i = 0; i < output_pixels.size(); ++i) {
        const auto in_luma =
            (input_pixels[i][0] + (2.0F * input_pixels[i][1]) + input_pixels[i][2]) * 0.25F;
        const auto out_luma =
            (output_pixels[i][0] + (2.0F * output_pixels[i][1]) + output_pixels[i][2]) * 0.25F;
        REQUIRE(out_luma == Catch::Approx(in_luma).margin(0.002F));
        REQUIRE(output_pixels[i][3] == Catch::Approx(input_pixels[i][3]).margin(0.001F));
    }

    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "scene_linear_rec2020");
    REQUIRE(metadata->applied_steps == std::vector<std::string>{"color_matrix",
                                                                "denoise.guided_filter",
                                                                "denoise.wavelet_bayes_shrink"});
}
