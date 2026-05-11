// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/InferenceContext.hpp>

struct cpipe_buffer_s {
    cpipe::compute::IBuffer* buffer = nullptr;
};

struct cpipe_compute_s {
    cpipe::runtime::ComputeContext* context = nullptr;
};

struct cpipe_inference_s {
    cpipe::runtime::InferenceContext* context = nullptr;
};

namespace cpipe::runtime {

class BufferHandle {
public:
    explicit BufferHandle(compute::IBuffer& buffer) : handle_{&buffer} {}

    [[nodiscard]] cpipe_buffer_t* c_buffer() noexcept {
        return &handle_;
    }

    [[nodiscard]] const cpipe_buffer_t* c_buffer() const noexcept {
        return &handle_;
    }

private:
    cpipe_buffer_t handle_{};
};

class ComputeHandle {
public:
    explicit ComputeHandle(ComputeContext& context) : handle_{&context} {}

    [[nodiscard]] cpipe_compute_t* c_compute() noexcept {
        return &handle_;
    }

private:
    cpipe_compute_t handle_{};
};

class InferenceHandle {
public:
    explicit InferenceHandle(InferenceContext& context) : handle_{&context} {}

    [[nodiscard]] cpipe_inference_t* c_inference() noexcept {
        return &handle_;
    }

private:
    cpipe_inference_t handle_{};
};

}  // namespace cpipe::runtime
