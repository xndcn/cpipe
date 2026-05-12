// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>

#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

class InferenceContext final {
public:
    InferenceContext();
    ~InferenceContext();

    InferenceContext(const InferenceContext&) = delete;
    auto operator=(const InferenceContext&) -> InferenceContext& = delete;

    InferenceContext(InferenceContext&&) = delete;
    auto operator=(InferenceContext&&) -> InferenceContext& = delete;

    [[nodiscard]] auto native() noexcept -> cpipe_inference_t*;
    [[nodiscard]] auto native() const noexcept -> const cpipe_inference_t*;

    [[nodiscard]] static auto submit(const char* model_id, const cpipe_buffer_t* const* inputs,
                                     std::size_t n_in, cpipe_buffer_t* const* outputs,
                                     std::size_t n_out) noexcept -> int;

private:
    cpipe_inference_t* native_ = nullptr;
};

}  // namespace cpipe::runtime
