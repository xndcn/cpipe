// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <cpipe/core/IBuffer.hpp>
#include <cstdint>

namespace cpipe::runtime {

struct HalideDimension {
    std::int32_t min = 0;
    std::int32_t extent = 0;
    std::int32_t stride = 0;
    std::uint32_t flags = 0;
};

struct HalideBufferView {
    std::uint8_t* host = nullptr;
    compute::PixelFormat format = compute::PixelFormat::UNDEFINED;
    std::uint8_t ndim = 0;
    std::array<HalideDimension, 8> dim{};
    std::uint64_t size_bytes = 0;
};

class HalideBufferAdapter {
public:
    HalideBufferAdapter(compute::IBuffer& buffer, compute::IBuffer::CpuAccess access);
    ~HalideBufferAdapter();

    HalideBufferAdapter(const HalideBufferAdapter&) = delete;
    HalideBufferAdapter& operator=(const HalideBufferAdapter&) = delete;
    HalideBufferAdapter(HalideBufferAdapter&&) = delete;
    HalideBufferAdapter& operator=(HalideBufferAdapter&&) = delete;

    [[nodiscard]] HalideBufferView& view() noexcept;
    [[nodiscard]] const HalideBufferView& view() const noexcept;

private:
    compute::IBuffer& buffer_;
    compute::IBuffer::CpuAccess access_;
    bool locked_ = false;
    HalideBufferView view_{};
};

using HalideFilterEntry = int (*)(const HalideBufferView* const* inputs, std::size_t n_in,
                                  HalideBufferView* const* outputs, std::size_t n_out);

}  // namespace cpipe::runtime
