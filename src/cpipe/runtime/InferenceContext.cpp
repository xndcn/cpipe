// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/InferenceContext.hpp"

#include "RuntimeHandles.hpp"
#include "RuntimeSuites.hpp"

namespace cpipe::runtime {

InferenceContext::InferenceContext() : native_(new cpipe_inference_t{this}) {}

InferenceContext::~InferenceContext() {
    delete native_;
}

auto InferenceContext::native() noexcept -> cpipe_inference_t* {
    return native_;
}

auto InferenceContext::native() const noexcept -> const cpipe_inference_t* {
    return native_;
}

auto InferenceContext::submit(const char* model_id, const cpipe_buffer_t* const* inputs,
                              std::size_t n_in, cpipe_buffer_t* const* outputs,
                              std::size_t n_out) noexcept -> int {
    (void)model_id;
    (void)inputs;
    (void)n_in;
    (void)outputs;
    (void)n_out;
    return CPIPE_UNSUPPORTED;
}

namespace detail {

auto unwrap(cpipe_inference_t* handle) noexcept -> InferenceContext* {
    return handle == nullptr ? nullptr : handle->context;
}

namespace {

auto submit_inference(cpipe_inference_t* inference, const char* model_id,
                      const cpipe_buffer_t* const* inputs, std::size_t n_in,
                      cpipe_buffer_t* const* outputs, std::size_t n_out) -> int {
    auto* context = unwrap(inference);
    if (context == nullptr) {
        return CPIPE_UNSUPPORTED;
    }
    return InferenceContext::submit(model_id, inputs, n_in, outputs, n_out);
}

constexpr cpipe_inference_suite_v1 kInferenceSuite{&submit_inference};

}  // namespace

auto inference_suite() noexcept -> const cpipe_inference_suite_v1* {
    return &kInferenceSuite;
}

}  // namespace detail

}  // namespace cpipe::runtime
