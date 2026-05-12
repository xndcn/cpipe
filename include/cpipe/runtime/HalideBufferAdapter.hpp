// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>

#include <array>

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/sdk/cpipe_node.h"

namespace cpipe::runtime {

class HalideBufferAdapter final {
public:
    HalideBufferAdapter(compute::IBuffer& buffer, compute::IBuffer::CpuAccess access);
    ~HalideBufferAdapter();

    HalideBufferAdapter(const HalideBufferAdapter&) = delete;
    auto operator=(const HalideBufferAdapter&) -> HalideBufferAdapter& = delete;

    HalideBufferAdapter(HalideBufferAdapter&&) = delete;
    auto operator=(HalideBufferAdapter&&) -> HalideBufferAdapter& = delete;

    [[nodiscard]] auto status() const noexcept -> int;
    [[nodiscard]] auto get() noexcept -> halide_buffer_t*;

private:
    static constexpr auto kMaxHalideDims = 9U;

    compute::IBuffer* locked_buffer_ = nullptr;
    compute::IBuffer::CpuAccess access_ = compute::IBuffer::CpuAccess::Read;
    std::array<halide_dimension_t, kMaxHalideDims> dims_{};
    halide_buffer_t buffer_{};
    int status_ = CPIPE_OK;
};

}  // namespace cpipe::runtime
