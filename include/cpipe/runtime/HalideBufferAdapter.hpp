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
    HalideBufferAdapter(const HalideBufferAdapter&) = delete;
    HalideBufferAdapter& operator=(const HalideBufferAdapter&) = delete;
    HalideBufferAdapter(HalideBufferAdapter&&) = delete;
    HalideBufferAdapter& operator=(HalideBufferAdapter&&) = delete;
    ~HalideBufferAdapter();

    [[nodiscard]] halide_buffer_t* get() noexcept;
    [[nodiscard]] const halide_buffer_t* get() const noexcept;

private:
    compute::IBuffer& buffer_;
    compute::IBuffer::CpuAccess access_;
    std::array<halide_dimension_t, 8> dimensions_{};
    halide_buffer_t halide_{};
};

}  // namespace cpipe::runtime
