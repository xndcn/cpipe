// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <span>
#include <string_view>

#include <cpipe/core/IBuffer.hpp>
#include <cpipe/sdk/cpipe_node.h>

namespace cpipe::runtime {

class InferenceContext {
public:
    [[nodiscard]] cpipe_status_t submit(std::string_view model_id,
                                        std::span<const compute::IBuffer* const> inputs,
                                        std::span<compute::IBuffer* const> outputs) const;
};

}  // namespace cpipe::runtime
