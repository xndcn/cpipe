// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>

#include "tone_node_test_utils.hpp"

namespace cpipe::tests {

inline cpipe_status_t process_single_input_node_with_params(
    const cpipe_plugin_desc_t& desc, const std::shared_ptr<CpuBuffer>& input,
    const std::shared_ptr<CpuBuffer>& output, const nlohmann::json& params_json) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(params_json);
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(input->metadata(), {input->metadata()});
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

inline cpipe_status_t process_three_input_node_with_params(
    const cpipe_plugin_desc_t& desc, std::span<const std::shared_ptr<CpuBuffer>, 3> inputs,
    const std::shared_ptr<CpuBuffer>& output, const nlohmann::json& params_json) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(params_json);
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

    auto input0 = cpipe::runtime::make_buffer_handle(inputs[0]);
    auto input1 = cpipe::runtime::make_buffer_handle(inputs[1]);
    auto input2 = cpipe::runtime::make_buffer_handle(inputs[2]);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    std::vector<std::shared_ptr<const BufferMetadata>> source_metadata{
        inputs[0]->metadata(), inputs[1]->metadata(), inputs[2]->metadata()};
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(inputs[1]->metadata(), source_metadata);
    const cpipe_buffer_t* process_inputs[] = {input0.get(), input1.get(), input2.get()};
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

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

inline std::vector<std::array<float, 4>> run_single_input_node_with_params(
    const cpipe_plugin_desc_t& desc, const std::vector<std::array<float, 4>>& pixels,
    std::uint32_t width, std::uint32_t height, const nlohmann::json& params_json) {
    auto input = make_tone_input(pixels, width, height);
    auto output = make_tone_output(width, height);
    REQUIRE(process_single_input_node_with_params(desc, input, output, params_json) == CPIPE_OK);
    return read_rgba16(*output, pixels.size());
}

inline float max_channel_delta(const std::vector<std::array<float, 4>>& a,
                               const std::vector<std::array<float, 4>>& b) {
    REQUIRE(a.size() == b.size());
    float delta = 0.0F;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            delta = std::max(delta, std::abs(a[i][c] - b[i][c]));
        }
    }
    return delta;
}

inline const cpipe_plugin_desc_t& require_builtin_node(cpipe::runtime::Registry& registry,
                                                       const char* node_id) {
    registry.load_builtin_nodes();
    const auto* desc = registry.find(node_id);
    REQUIRE(desc != nullptr);
    return *desc;
}

}  // namespace cpipe::tests
