// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>
#include <string_view>

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

class InferenceContext {
public:
    int submit(std::string_view model_id, const cpipe_buffer_t* const* inputs, std::size_t n_in,
               cpipe_buffer_t* const* outputs, std::size_t n_out) noexcept;
};

}  // namespace cpipe::runtime
