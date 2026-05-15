// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <string_view>

namespace cpipe::compute {

enum class PixelFormat : std::uint16_t {
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
    R16G16B16A16_UNORM,
};

[[nodiscard]] constexpr std::uint64_t bytes_per_pixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::R16_UINT:
            return 2;
        case PixelFormat::R10_PACKED:
            return 0;
        case PixelFormat::R8G8B8A8_UNORM:
            return 4;
        case PixelFormat::R8G8B8_UNORM:
            return 3;
        case PixelFormat::R10G10B10A2_UNORM:
            return 4;
        case PixelFormat::R16G16B16A16_SFLOAT:
        case PixelFormat::R16G16B16A16_UNORM:
            return 8;
        case PixelFormat::R16G16B16_SFLOAT:
            return 6;
        case PixelFormat::R32G32B32A32_SFLOAT:
            return 16;
        case PixelFormat::R32G32B32_SFLOAT:
            return 12;
        case PixelFormat::R32_SFLOAT:
        case PixelFormat::F32:
            return 4;
        case PixelFormat::F16:
            return 2;
        case PixelFormat::I8:
        case PixelFormat::U8:
        case PixelFormat::BLOB:
            return 1;
        case PixelFormat::UNDEFINED:
            return 0;
    }
    return 0;
}

[[nodiscard]] constexpr std::uint64_t channel_count(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::R8G8B8A8_UNORM:
        case PixelFormat::R10G10B10A2_UNORM:
        case PixelFormat::R16G16B16A16_SFLOAT:
        case PixelFormat::R16G16B16A16_UNORM:
        case PixelFormat::R32G32B32A32_SFLOAT:
            return 4;
        case PixelFormat::R8G8B8_UNORM:
        case PixelFormat::R16G16B16_SFLOAT:
        case PixelFormat::R32G32B32_SFLOAT:
            return 3;
        case PixelFormat::UNDEFINED:
            return 0;
        default:
            return 1;
    }
}

[[nodiscard]] constexpr std::uint64_t bytes_per_channel(PixelFormat format) noexcept {
    const auto channels = channel_count(format);
    if (channels == 0) {
        return 0;
    }
    return bytes_per_pixel(format) / channels;
}

[[nodiscard]] constexpr std::string_view to_string(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::UNDEFINED:
            return "UNDEFINED";
        case PixelFormat::R16_UINT:
            return "R16_UINT";
        case PixelFormat::R10_PACKED:
            return "R10_PACKED";
        case PixelFormat::R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case PixelFormat::R8G8B8_UNORM:
            return "R8G8B8_UNORM";
        case PixelFormat::R10G10B10A2_UNORM:
            return "R10G10B10A2_UNORM";
        case PixelFormat::R16G16B16A16_SFLOAT:
            return "R16G16B16A16_SFLOAT";
        case PixelFormat::R16G16B16_SFLOAT:
            return "R16G16B16_SFLOAT";
        case PixelFormat::R32G32B32A32_SFLOAT:
            return "R32G32B32A32_SFLOAT";
        case PixelFormat::R32G32B32_SFLOAT:
            return "R32G32B32_SFLOAT";
        case PixelFormat::R32_SFLOAT:
            return "R32_SFLOAT";
        case PixelFormat::F16:
            return "F16";
        case PixelFormat::F32:
            return "F32";
        case PixelFormat::I8:
            return "I8";
        case PixelFormat::U8:
            return "U8";
        case PixelFormat::BLOB:
            return "BLOB";
        case PixelFormat::R16G16B16A16_UNORM:
            return "R16G16B16A16_UNORM";
    }
    return "UNKNOWN";
}

}  // namespace cpipe::compute
