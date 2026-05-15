// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_sharpen_edge_aware_usm();

namespace {

std::shared_ptr<cpipe::tests::BufferMetadata> scene_metadata() {
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"tone.filmic_rgb", "color.3d_lut"};
    return metadata;
}

std::vector<std::array<float, 4>> edge_patch() {
    std::vector<std::array<float, 4>> pixels;
    pixels.reserve(25);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            const auto value = x < 2 ? 0.28F : 0.68F;
            pixels.push_back({value, value, value, 1.0F});
        }
    }
    return pixels;
}

}  // namespace

TEST_CASE("sharpen.edge_aware_usm enhances edges and preserves metadata") {
    cpipe_link_builtin_sharpen_edge_aware_usm();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.sharpen.edge_aware_usm");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") ==
            nlohmann::json::array({"sharpen_edge_aware_usm"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"sharpen.edge_aware_usm"}));

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(5, 5),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input, edge_patch());

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(5, 5), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto pixels = cpipe::tests::read_rgba16(*output, 25);
    const auto dark_edge = pixels[11][0];
    const auto bright_edge = pixels[12][0];
    REQUIRE(dark_edge < 0.28F);
    REQUIRE(bright_edge > 0.68F);
    REQUIRE(pixels[12][3] == Catch::Approx(1.0F).margin(0.001F));

    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "scene_linear_rec2020");
    REQUIRE(metadata->applied_steps ==
            std::vector<std::string>{"tone.filmic_rgb", "color.3d_lut", "sharpen.edge_aware_usm"});
}
