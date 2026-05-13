// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/IBuffer.hpp>
#include <initializer_list>
#include <memory>
#include <string_view>

namespace cpipe::runtime {

class InferenceContext {
public:
    [[nodiscard]] cpipe_status_t submit(
        std::string_view model_id, std::initializer_list<std::shared_ptr<compute::IBuffer>> inputs,
        std::initializer_list<std::shared_ptr<compute::IBuffer>> outputs) noexcept;
};

}  // namespace cpipe::runtime
