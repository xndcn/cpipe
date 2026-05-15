// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_denoise_guided_filter();

namespace {

std::shared_ptr<cpipe::tests::BufferMetadata> scene_metadata() {
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"color_matrix"};
    return metadata;
}

}  // namespace

TEST_CASE("denoise.guided_filter smooths local noise and preserves metadata") {
    cpipe_link_builtin_denoise_guided_filter();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.denoise.guided_filter");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") ==
            nlohmann::json::array({"denoise_guided_filter"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"denoise.guided_filter"}));

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(3, 3),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input, {{{0.20F, 0.20F, 0.20F, 1.0F},
                                         {0.22F, 0.20F, 0.20F, 1.0F},
                                         {0.20F, 0.20F, 0.20F, 1.0F},
                                         {0.20F, 0.20F, 0.20F, 1.0F},
                                         {0.65F, 0.20F, 0.20F, 0.75F},
                                         {0.20F, 0.20F, 0.20F, 1.0F},
                                         {0.20F, 0.20F, 0.20F, 1.0F},
                                         {0.18F, 0.20F, 0.20F, 1.0F},
                                         {0.20F, 0.20F, 0.20F, 1.0F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(3, 3), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto pixels = cpipe::tests::read_rgba16(*output, 9);
    REQUIRE(pixels[4][0] < 0.65F);
    REQUIRE(pixels[4][0] > 0.24F);
    REQUIRE(pixels[4][1] == Catch::Approx(0.20F).margin(0.002F));
    REQUIRE(pixels[4][2] == Catch::Approx(0.20F).margin(0.002F));
    REQUIRE(pixels[4][3] == Catch::Approx(0.75F).margin(0.001F));

    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "scene_linear_rec2020");
    REQUIRE(metadata->applied_steps ==
            std::vector<std::string>{"color_matrix", "denoise.guided_filter"});
}
