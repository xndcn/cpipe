// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "tone_node_test_utils.hpp"

void cpipe_link_builtin_tone_mertens_local();

namespace {

cpipe_status_t process_mertens_node(
    const cpipe_plugin_desc_t& desc,
    const std::array<std::shared_ptr<cpipe::compute::CpuBuffer>, 3>& inputs,
    const std::shared_ptr<cpipe::compute::CpuBuffer>& output) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                            &instance) == CPIPE_OK);

    auto under_handle = cpipe::runtime::make_buffer_handle(inputs[0]);
    auto normal_handle = cpipe::runtime::make_buffer_handle(inputs[1]);
    auto over_handle = cpipe::runtime::make_buffer_handle(inputs[2]);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    std::vector<std::shared_ptr<const cpipe::compute::BufferMetadata>> source_metadata{
        inputs[0]->metadata(), inputs[1]->metadata(), inputs[2]->metadata()};
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(inputs[1]->metadata(), source_metadata);
    const cpipe_buffer_t* process_inputs[] = {under_handle.get(), normal_handle.get(),
                                              over_handle.get()};
    cpipe_buffer_t* process_outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = process_inputs,
        .n_in = 3,
        .outputs = process_outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status = static_cast<cpipe_status_t>(
        desc.main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                        reinterpret_cast<cpipe_node_t*>(instance), nullptr, &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

std::array<std::shared_ptr<cpipe::compute::CpuBuffer>, 3> make_exposure_stack(
    const std::vector<std::array<float, 4>>& normal, std::uint32_t width, std::uint32_t height) {
    std::array<std::vector<std::array<float, 4>>, 3> exposures;
    for (const auto pixel : normal) {
        exposures[0].push_back({0.25F * pixel[0], 0.25F * pixel[1], 0.25F * pixel[2], pixel[3]});
        exposures[1].push_back(pixel);
        exposures[2].push_back({4.0F * pixel[0], 4.0F * pixel[1], 4.0F * pixel[2], pixel[3]});
    }
    return {cpipe::tests::make_tone_input(exposures[0], width, height),
            cpipe::tests::make_tone_input(exposures[1], width, height),
            cpipe::tests::make_tone_input(exposures[2], width, height)};
}

const cpipe_plugin_desc_t& require_mertens_node(cpipe::runtime::Registry& registry) {
    cpipe_link_builtin_tone_mertens_local();
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.tone.mertens_local");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") ==
            nlohmann::json::array({"tone_mertens_local"}));
    REQUIRE(manifest.at("ports").size() == 4);
    REQUIRE(manifest.at("ports").at(3).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"tone_mertens_local"}));
    REQUIRE(manifest.at("color").at("input_role") == "scene_linear_rec2020");
    REQUIRE(manifest.at("color").at("output_role") == "scene_linear_rec2020");
    return *desc;
}

}  // namespace

TEST_CASE("tone.mertens_local fuses a synthetic exposure stack to SDR") {
    cpipe::runtime::Registry registry;
    const auto& desc = require_mertens_node(registry);

    const std::vector<std::array<float, 4>> normal{
        {0.02F, 0.03F, 0.04F, 1.0F},
        {0.12F, 0.10F, 0.08F, 1.0F},
        {0.48F, 0.44F, 0.40F, 0.5F},
        {0.90F, 0.72F, 0.54F, 0.25F},
    };
    const auto inputs = make_exposure_stack(normal, 4, 1);
    auto output = cpipe::tests::make_tone_output(4, 1);
    REQUIRE(process_mertens_node(desc, inputs, output) == CPIPE_OK);
    const auto pixels = cpipe::tests::read_rgba16(*output, normal.size());

    REQUIRE(pixels[0][0] > normal[0][0]);
    REQUIRE(pixels[1][1] > normal[1][1]);
    REQUIRE(pixels[2][0] == Catch::Approx(normal[2][0]).margin(0.08F));
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            REQUIRE(pixels[i][c] >= 0.0F);
            REQUIRE(pixels[i][c] <= 1.0F);
        }
        REQUIRE(pixels[i][3] == Catch::Approx(normal[i][3]).margin(0.001F));
    }
}

TEST_CASE("tone.mertens_local appends metadata step") {
    cpipe::runtime::Registry registry;
    const auto& desc = require_mertens_node(registry);

    const auto inputs = make_exposure_stack({{0.18F, 0.18F, 0.18F, 1.0F}}, 1, 1);
    auto output = cpipe::tests::make_tone_output(1, 1);
    REQUIRE(process_mertens_node(desc, inputs, output) == CPIPE_OK);
    cpipe::tests::require_tone_metadata_step(
        output, {"denoise.bm3d", "denoise.wavelet_bayes_shrink", "tone_mertens_local"});
}
