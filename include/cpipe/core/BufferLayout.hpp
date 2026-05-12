// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <cstdint>

#include "cpipe/core/PixelFormat.hpp"

namespace cpipe::compute {

enum class BufferKind : std::uint8_t {
    Image2D = 0,
    Volume3D = 1,
    TensorND = 2,
    Blob = 3,
};

struct BufferLayout {
    BufferKind kind = BufferKind::Blob;
    PixelFormat format = PixelFormat::UNDEFINED;
    std::uint8_t ndim = 0;
    std::array<std::uint32_t, 8> dims{};
    std::array<std::uint64_t, 8> stride{};

    [[nodiscard]] auto size_bytes() const noexcept -> std::uint64_t;
};

}  // namespace cpipe::compute
