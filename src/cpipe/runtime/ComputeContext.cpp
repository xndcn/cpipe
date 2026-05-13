// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <vector>

namespace cpipe::runtime {

void ComputeContext::register_halide_filter(std::string aot_id, HalideFilterEntry entry) {
    halide_filters_.insert_or_assign(std::move(aot_id), entry);
}

cpipe_status_t ComputeContext::submit_halide(
    std::string_view aot_id, std::span<const std::shared_ptr<compute::IBuffer>> inputs,
    std::span<const std::shared_ptr<compute::IBuffer>> outputs) {
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

}  // namespace cpipe::runtime
