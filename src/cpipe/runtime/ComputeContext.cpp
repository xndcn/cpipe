// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/ComputeContext.hpp>
#include <utility>

namespace cpipe::runtime {

void ComputeContext::register_halide(std::string aot_id, HalideFilterEntry entry) {
    halide_entries_[std::move(aot_id)] = entry;
}

cpipe_status_t ComputeContext::submit_halide(std::string_view aot_id,
                                             std::span<const compute::IBuffer* const> inputs,
                                             std::span<compute::IBuffer* const> outputs) const {
    const auto found = halide_entries_.find(std::string(aot_id));
    if (found == halide_entries_.end() || found->second == nullptr) {
        return CPIPE_UNSUPPORTED;
    }
    if (inputs.size() != 1 || outputs.size() != 1) {
        return CPIPE_BAD_INDEX;
    }
    if (inputs[0] == nullptr || outputs[0] == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    HalideBufferAdapter input_adapter(*const_cast<compute::IBuffer*>(inputs[0]),
                                      compute::IBuffer::CpuAccess::Read);
    HalideBufferAdapter output_adapter(*outputs[0], compute::IBuffer::CpuAccess::Write);
    if (input_adapter.buffer().host == nullptr || output_adapter.buffer().host == nullptr) {
        return CPIPE_FAILED;
    }

    return static_cast<cpipe_status_t>(
        found->second(&input_adapter.buffer(), &output_adapter.buffer()));
}

}  // namespace cpipe::runtime
