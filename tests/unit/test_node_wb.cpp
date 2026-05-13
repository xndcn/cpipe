// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "color_node_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <nlohmann/json.hpp>
#include <string>

void cpipe_link_builtin_wb_dual_illuminant();

TEST_CASE("wb.dual_illuminant applies inverse AsShotNeutral gains") {
    cpipe_link_builtin_wb_dual_illuminant();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.wb.dual_illuminant");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("metadata").at("requires_steps_applied") ==
            nlohmann::json::array({"demosaic"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"white_balance"}));

    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic"};
    metadata->capture.as_shot_neutral = {0.5F, 1.0F, 0.25F};

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(2, 1),
        cpipe::tests::BufferUsage::Input | cpipe::tests::BufferUsage::CpuRead |
            cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    cpipe::tests::write_rgba16(*input, {{{0.125F, 0.25F, 0.0625F, 0.5F},
                                         {0.25F, 0.5F, 0.125F, 1.0F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(2, 1),
        cpipe::tests::BufferUsage::Output | cpipe::tests::BufferUsage::CpuRead |
            cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto pixels = cpipe::tests::read_rgba16(*output, 2);
    REQUIRE(pixels[0][0] == Catch::Approx(0.25F));
    REQUIRE(pixels[0][1] == Catch::Approx(0.25F));
    REQUIRE(pixels[0][2] == Catch::Approx(0.25F));
    REQUIRE(pixels[0][3] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][0] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][1] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][2] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][3] == Catch::Approx(1.0F));

    REQUIRE(output->metadata()->cs_role == "raw_camera");
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"linearization", "black_white_scaling", "demosaic",
                                     "white_balance"});
}
