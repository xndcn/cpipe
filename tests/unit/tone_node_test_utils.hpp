// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

namespace cpipe::tests {

inline std::shared_ptr<BufferMetadata> scene_linear_metadata() {
    auto metadata = std::make_shared<BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"denoise.bm3d", "denoise.wavelet_bayes_shrink"};
    return metadata;
}

inline std::shared_ptr<CpuBuffer> make_tone_input(const std::vector<std::array<float, 4>>& pixels,
                                                  std::uint32_t width, std::uint32_t height) {
    auto input = std::make_shared<CpuBuffer>(
        rgba16_layout(width, height),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(scene_linear_metadata());
    write_rgba16(*input, pixels);
    return input;
}

inline std::shared_ptr<CpuBuffer> make_tone_output(std::uint32_t width, std::uint32_t height) {
    return std::make_shared<CpuBuffer>(rgba16_layout(width, height), BufferUsage::Output |
                                                                         BufferUsage::CpuRead |
                                                                         BufferUsage::CpuWrite);
}

inline const cpipe_plugin_desc_t& require_tone_node(cpipe::runtime::Registry& registry,
                                                    const char* node_id, const char* aot_id,
                                                    const char* step) {
    registry.load_builtin_nodes();
    const auto* desc = registry.find(node_id);
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") == nlohmann::json::array({aot_id}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({step}));
    REQUIRE(manifest.at("color").at("input_role") == "scene_linear_rec2020");
    REQUIRE(manifest.at("color").at("output_role") == "scene_linear_rec2020");
    return *desc;
}

inline std::vector<std::array<float, 4>> run_tone_node(const cpipe_plugin_desc_t& desc,
                                                       const std::vector<std::array<float, 4>>& in,
                                                       std::uint32_t width, std::uint32_t height) {
    auto input = make_tone_input(in, width, height);
    auto output = make_tone_output(width, height);
    REQUIRE(process_single_input_node(desc, input, output) == CPIPE_OK);
    return read_rgba16(*output, in.size());
}

inline void require_tone_metadata_step(const std::shared_ptr<CpuBuffer>& output,
                                       const std::vector<std::string>& expected_steps) {
    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "scene_linear_rec2020");
    REQUIRE(metadata->applied_steps == expected_steps);
}

}  // namespace cpipe::tests
