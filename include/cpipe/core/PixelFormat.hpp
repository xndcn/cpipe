// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cpipe::compute {

enum class PixelFormat : uint16_t {
    UNDEFINED = 0,
    R16_UINT,
    R10_PACKED,
    R8G8B8A8_UNORM,
    R8G8B8_UNORM,
    R10G10B10A2_UNORM,
    R16G16B16A16_SFLOAT,
    R16G16B16_SFLOAT,
    R32G32B32A32_SFLOAT,
    R32G32B32_SFLOAT,
    R32_SFLOAT,
    F16,
    F32,
    I8,
    U8,
    BLOB,
};

constexpr std::size_t kPixelFormatCount = 16;

std::string_view to_string(PixelFormat format) noexcept;
std::size_t bytes_per_pixel(PixelFormat format) noexcept;

}  // namespace cpipe::compute
