// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/ComputeContext.hpp"

#include <cstdint>
#include <string>

#include "RuntimeHandles.hpp"
#include "RuntimeSuites.hpp"
#include "cpipe/runtime/HalideBufferAdapter.hpp"

namespace cpipe::runtime {

ComputeContext::ComputeContext(TaskExecutor& executor, std::span<const HalideFilter> halide_filters)
    : executor_(&executor), native_(new cpipe_compute_t{this}) {
    for (const auto& filter : halide_filters) {
        if (!filter.id.empty() && filter.entry != nullptr) {
            halide_filters_.emplace(std::string{filter.id}, filter.entry);
        }
    }
}

ComputeContext::~ComputeContext() {
    delete native_;
}

auto ComputeContext::native() noexcept -> cpipe_compute_t* {
    return native_;
}

auto ComputeContext::native() const noexcept -> const cpipe_compute_t* {
    return native_;
}

auto ComputeContext::submit_halide(const char* aot_id, const cpipe_buffer_t* const* inputs,
                                   std::size_t n_in, cpipe_buffer_t* const* outputs,
                                   std::size_t n_out) -> int {
    (void)executor_;
    if (aot_id == nullptr || inputs == nullptr || outputs == nullptr || n_in != 1U || n_out != 1U) {
        return CPIPE_BAD_INDEX;
    }

    const auto filter = halide_filters_.find(aot_id);
    if (filter == halide_filters_.end()) {
        return CPIPE_UNSUPPORTED;
    }

    auto* input = detail::unwrap(inputs[0]);
    auto* output = detail::unwrap(outputs[0]);
    if (input == nullptr || output == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    HalideBufferAdapter input_adapter{*input, compute::IBuffer::CpuAccess::Read};
    HalideBufferAdapter output_adapter{*output, compute::IBuffer::CpuAccess::Write};
    if (input_adapter.status() != CPIPE_OK) {
        return input_adapter.status();
    }
    if (output_adapter.status() != CPIPE_OK) {
        return output_adapter.status();
    }

    const auto halide_status = filter->second(input_adapter.get(), output_adapter.get());
    return halide_status == 0 ? CPIPE_OK : CPIPE_FAILED;
}

namespace detail {

auto unwrap(cpipe_compute_t* handle) noexcept -> ComputeContext* {
    return handle == nullptr ? nullptr : handle->context;
}

namespace {

auto submit_halide(cpipe_compute_t* compute, const char* aot_id,
                   const cpipe_buffer_t* const* inputs, std::size_t n_in,
                   cpipe_buffer_t* const* outputs, std::size_t n_out) -> int {
    auto* context = unwrap(compute);
    if (context == nullptr) {
        return CPIPE_BAD_INDEX;
    }
    return context->submit_halide(aot_id, inputs, n_in, outputs, n_out);
}

auto submit_slang(cpipe_compute_t* compute, const char* module_id, const char* entry_point,
                  const cpipe_buffer_t* const* inputs, std::size_t n_in,
                  cpipe_buffer_t* const* outputs, std::size_t n_out, const void* push_constants,
                  std::size_t pc_size) -> int {
    (void)compute;
    (void)module_id;
    (void)entry_point;
    (void)inputs;
    (void)n_in;
    (void)outputs;
    (void)n_out;
    (void)push_constants;
    (void)pc_size;
    return CPIPE_UNSUPPORTED;
}

auto request_scratch(cpipe_compute_t* compute, std::uint64_t bytes, int kind,
                     cpipe_buffer_t** out) -> int {
    (void)compute;
    (void)bytes;
    (void)kind;
    if (out != nullptr) {
        *out = nullptr;
    }
    return CPIPE_UNSUPPORTED;
}

void record_marker(cpipe_compute_t* compute, const char* label) {
    (void)compute;
    (void)label;
}

constexpr cpipe_compute_suite_v1 kComputeSuite{&submit_halide, &submit_slang, &request_scratch,
                                               &record_marker};

}  // namespace

auto compute_suite() noexcept -> const cpipe_compute_suite_v1* {
    return &kComputeSuite;
}

}  // namespace detail

}  // namespace cpipe::runtime
