// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/PixelFormat.hpp>
#include <cstdint>

namespace cpipe::compute {

enum class BufferKind : std::uint8_t {
    Image2D = 0,
    Volume3D = 1,
    TensorND = 2,
    Blob = 3,
};

struct BufferLayout {
    BufferKind kind{BufferKind::Image2D};
    PixelFormat format{PixelFormat::UNDEFINED};
    std::uint8_t ndim{0};
    std::uint32_t dims[8]{};
    std::uint64_t stride[8]{};

    [[nodiscard]] std::uint64_t size_bytes() const noexcept;
};

}  // namespace cpipe::compute
