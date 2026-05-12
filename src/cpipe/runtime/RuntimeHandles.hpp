// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/InferenceContext.hpp"

struct cpipe_buffer_s {
    cpipe::compute::IBuffer* buffer = nullptr;
};

struct cpipe_compute_s {
    cpipe::runtime::ComputeContext* context = nullptr;
};

struct cpipe_inference_s {
    cpipe::runtime::InferenceContext* context = nullptr;
};

namespace cpipe::runtime::detail {

[[nodiscard]] auto unwrap(cpipe_buffer_t* handle) noexcept -> compute::IBuffer*;
[[nodiscard]] auto unwrap(const cpipe_buffer_t* handle) noexcept -> compute::IBuffer*;
[[nodiscard]] auto unwrap(cpipe_compute_t* handle) noexcept -> ComputeContext*;
[[nodiscard]] auto unwrap(cpipe_inference_t* handle) noexcept -> InferenceContext*;

}  // namespace cpipe::runtime::detail
