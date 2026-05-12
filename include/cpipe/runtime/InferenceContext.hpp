// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

namespace cpipe::runtime {

class InferenceContext {
public:
    [[nodiscard]] auto handle() noexcept -> cpipe_inference_t*;
};

[[nodiscard]] auto inference_suite_v1() noexcept -> const cpipe_inference_suite_v1&;

}  // namespace cpipe::runtime
