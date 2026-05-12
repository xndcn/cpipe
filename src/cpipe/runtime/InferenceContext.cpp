// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/InferenceContext.hpp"

namespace cpipe::runtime {

int InferenceContext::submit(std::string_view, const cpipe_buffer_t* const*, std::size_t,
                             cpipe_buffer_t* const*, std::size_t) noexcept {
    return CPIPE_UNSUPPORTED;
}

}  // namespace cpipe::runtime
