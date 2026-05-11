// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/InferenceContext.hpp>

namespace cpipe::runtime {

cpipe_status_t InferenceContext::submit(std::string_view, std::span<const compute::IBuffer* const>,
                                        std::span<compute::IBuffer* const>) const {
    return CPIPE_UNSUPPORTED;
}

}  // namespace cpipe::runtime
