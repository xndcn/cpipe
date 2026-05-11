// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>

#include <array>
#include <cpipe/core/IBuffer.hpp>

namespace cpipe::runtime {

class HalideBufferAdapter {
public:
    HalideBufferAdapter(compute::IBuffer& buffer, compute::IBuffer::CpuAccess access);
    ~HalideBufferAdapter();

    HalideBufferAdapter(const HalideBufferAdapter&) = delete;
    HalideBufferAdapter& operator=(const HalideBufferAdapter&) = delete;
    HalideBufferAdapter(HalideBufferAdapter&&) = delete;
    HalideBufferAdapter& operator=(HalideBufferAdapter&&) = delete;

    [[nodiscard]] halide_buffer_t& buffer() noexcept;
    [[nodiscard]] const halide_buffer_t& buffer() const noexcept;

private:
    compute::IBuffer& buffer_;
    compute::IBuffer::CpuAccess access_;
    bool locked_ = false;
    std::array<halide_dimension_t, 1> dims_{};
    halide_buffer_t halide_buffer_{};
};

using HalideFilterEntry = int (*)(halide_buffer_t* input, halide_buffer_t* output);

}  // namespace cpipe::runtime
