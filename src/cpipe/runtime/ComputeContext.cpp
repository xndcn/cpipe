// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/ComputeContext.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace cpipe::runtime {

void ComputeContext::register_halide(std::string aot_id, HalideFilterEntry entry) {
    halide_entries_[std::move(aot_id)] = entry;
}

cpipe_status_t ComputeContext::submit_halide(
    std::string_view aot_id, std::span<const compute::IBuffer* const> inputs,
    std::span<compute::IBuffer* const> outputs) const {
    const auto found = halide_entries_.find(std::string(aot_id));
    if (found == halide_entries_.end() || found->second == nullptr) {
        return CPIPE_UNSUPPORTED;
    }

    std::vector<std::unique_ptr<HalideBufferAdapter>> input_adapters;
    std::vector<std::unique_ptr<HalideBufferAdapter>> output_adapters;
    std::vector<const HalideBufferView*> input_views;
    std::vector<HalideBufferView*> output_views;

    input_adapters.reserve(inputs.size());
    output_adapters.reserve(outputs.size());
    input_views.reserve(inputs.size());
    output_views.reserve(outputs.size());

    for (const auto* input : inputs) {
        if (input == nullptr) {
            return CPIPE_BAD_INDEX;
        }
        auto adapter = std::make_unique<HalideBufferAdapter>(
            *const_cast<compute::IBuffer*>(input), compute::IBuffer::CpuAccess::Read);
        if (adapter->view().host == nullptr) {
            return CPIPE_FAILED;
        }
        input_views.push_back(&adapter->view());
        input_adapters.push_back(std::move(adapter));
    }

    for (auto* output : outputs) {
        if (output == nullptr) {
            return CPIPE_BAD_INDEX;
        }
        auto adapter =
            std::make_unique<HalideBufferAdapter>(*output, compute::IBuffer::CpuAccess::Write);
        if (adapter->view().host == nullptr) {
            return CPIPE_FAILED;
        }
        output_views.push_back(&adapter->view());
        output_adapters.push_back(std::move(adapter));
    }

    return static_cast<cpipe_status_t>(
        found->second(input_views.data(), input_views.size(), output_views.data(), output_views.size()));
}

}  // namespace cpipe::runtime
