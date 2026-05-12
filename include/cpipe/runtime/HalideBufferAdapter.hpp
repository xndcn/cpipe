// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/IBuffer.hpp>

namespace cpipe::runtime {

class HalideBufferAdapter {
public:
    HalideBufferAdapter(cpipe_buffer_t* buffer, compute::IBuffer::CpuAccess access) noexcept;
    HalideBufferAdapter(const HalideBufferAdapter&) = delete;
    auto operator=(const HalideBufferAdapter&) -> HalideBufferAdapter& = delete;
    HalideBufferAdapter(HalideBufferAdapter&&) = delete;
    auto operator=(HalideBufferAdapter&&) -> HalideBufferAdapter& = delete;
    ~HalideBufferAdapter();

    [[nodiscard]] auto status() const noexcept -> int;
    [[nodiscard]] auto get() noexcept -> halide_buffer_t*;

private:
    static constexpr auto kAdapterMaxDimensions = compute::kMaxBufferDimensions + 1U;

    cpipe_buffer_t* handle_ = nullptr;
    compute::IBuffer* buffer_owner_ = nullptr;
    compute::IBuffer::CpuAccess access_ = compute::IBuffer::CpuAccess::Read;
    std::array<halide_dimension_t, kAdapterMaxDimensions> dimensions_{};
    halide_buffer_t buffer_{};
    int status_ = CPIPE_INTERNAL_ERROR;
    bool locked_ = false;
};

}  // namespace cpipe::runtime
