// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/PixelFormat.hpp>
#include <cstdint>

namespace cpipe::compute {

inline constexpr std::uint8_t kMaxBufferDimensions = 8;

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
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    std::uint32_t dims[kMaxBufferDimensions] = {};
    std::uint64_t stride[kMaxBufferDimensions] = {};
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

    [[nodiscard]] auto size_bytes() const noexcept -> std::uint64_t;
};

}  // namespace cpipe::compute
