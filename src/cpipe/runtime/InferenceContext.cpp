// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/InferenceContext.hpp>

namespace cpipe::runtime {

cpipe_status_t InferenceContext::submit(
    std::string_view model_id, std::initializer_list<std::shared_ptr<compute::IBuffer>> inputs,
    std::initializer_list<std::shared_ptr<compute::IBuffer>> outputs) noexcept {
    (void)model_id;
    (void)inputs;
    (void)outputs;
    return CPIPE_UNSUPPORTED;
}

}  // namespace cpipe::runtime
