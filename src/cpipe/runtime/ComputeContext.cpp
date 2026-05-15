// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cpipe/runtime/Trace.hpp>
#include <memory>
#include <vector>

namespace cpipe::runtime {

ComputeContext::ComputeContext()
    : halide_filters_(HalideFilterRegistry::instance().halide_filters()),
      halide_param_filters_(HalideFilterRegistry::instance().halide_param_filters()) {}

void ComputeContext::register_halide_filter(std::string aot_id, HalideFilterEntry entry) {
    halide_filters_.insert_or_assign(std::move(aot_id), entry);
}

void ComputeContext::register_halide_param_filter(std::string aot_id,
                                                  HalideParamFilterEntry entry) {
    halide_param_filters_.insert_or_assign(std::move(aot_id), entry);
}

cpipe_status_t ComputeContext::submit_halide(
    std::string_view aot_id, std::span<const std::shared_ptr<compute::IBuffer>> inputs,
    std::span<const std::shared_ptr<compute::IBuffer>> outputs) {
    CPIPE_TRACE_SCOPE("ComputeContext::submit_halide");

    const auto found = halide_filters_.find(std::string{aot_id});
    if (found == halide_filters_.end() || found->second == nullptr) {
        return CPIPE_UNSUPPORTED;
    }
    if (inputs.size() != 1 || outputs.size() != 1 || inputs.front() == nullptr ||
        outputs.front() == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    HalideBufferAdapter input{*inputs.front(), compute::IBuffer::CpuAccess::Read};
    HalideBufferAdapter output{*outputs.front(), compute::IBuffer::CpuAccess::Write};
    const auto status = found->second(input.get(), output.get());
    return status == 0 ? CPIPE_OK : CPIPE_FAILED;
}

cpipe_status_t ComputeContext::submit_halide(
    std::string_view aot_id, std::initializer_list<std::shared_ptr<compute::IBuffer>> inputs,
    std::initializer_list<std::shared_ptr<compute::IBuffer>> outputs) {
    const std::vector<std::shared_ptr<compute::IBuffer>> input_vec{inputs};
    const std::vector<std::shared_ptr<compute::IBuffer>> output_vec{outputs};
    return submit_halide(aot_id, std::span<const std::shared_ptr<compute::IBuffer>>{input_vec},
                         std::span<const std::shared_ptr<compute::IBuffer>>{output_vec});
}

cpipe_status_t ComputeContext::submit_halide_with_params(
    std::string_view aot_id, std::span<const std::shared_ptr<compute::IBuffer>> inputs,
    std::span<const std::shared_ptr<compute::IBuffer>> outputs,
    std::span<const std::byte> param_blob) {
    CPIPE_TRACE_SCOPE("ComputeContext::submit_halide_with_params");

    const auto found = halide_param_filters_.find(std::string{aot_id});
    if (found == halide_param_filters_.end() || found->second == nullptr) {
        return CPIPE_UNSUPPORTED;
    }

    std::vector<std::unique_ptr<HalideBufferAdapter>> input_adapters;
    input_adapters.reserve(inputs.size());
    std::vector<halide_buffer_t*> raw_inputs;
    raw_inputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        if (input == nullptr) {
            return CPIPE_BAD_INDEX;
        }
        input_adapters.push_back(
            std::make_unique<HalideBufferAdapter>(*input, compute::IBuffer::CpuAccess::Read));
        raw_inputs.push_back(input_adapters.back()->get());
    }

    std::vector<std::unique_ptr<HalideBufferAdapter>> output_adapters;
    output_adapters.reserve(outputs.size());
    std::vector<halide_buffer_t*> raw_outputs;
    raw_outputs.reserve(outputs.size());
    for (const auto& output : outputs) {
        if (output == nullptr) {
            return CPIPE_BAD_INDEX;
        }
        output_adapters.push_back(
            std::make_unique<HalideBufferAdapter>(*output, compute::IBuffer::CpuAccess::Write));
        raw_outputs.push_back(output_adapters.back()->get());
    }

    const auto status = found->second(raw_inputs.data(), raw_inputs.size(), raw_outputs.data(),
                                      raw_outputs.size(), param_blob.data(), param_blob.size());
    return status == 0 ? CPIPE_OK : static_cast<cpipe_status_t>(status);
}

}  // namespace cpipe::runtime
