// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cpipe::compute {

inline constexpr std::uint32_t kBitsPerByte = 8;
inline constexpr std::uint32_t kBitsPerPackedRaw10Pixel = 10;
inline constexpr std::uint32_t kBitsPerUint16Pixel = 16;
inline constexpr std::uint32_t kBitsPerRgb8Pixel = 24;
inline constexpr std::uint32_t kBitsPerRgba8Pixel = 32;
inline constexpr std::uint32_t kBitsPerRgb16FloatPixel = 48;
inline constexpr std::uint32_t kBitsPerRgba16FloatPixel = 64;
inline constexpr std::uint32_t kBitsPerRgb32FloatPixel = 96;
inline constexpr std::uint32_t kBitsPerRgba32FloatPixel = 128;

enum class PixelFormat : std::uint16_t {  // NOLINT(performance-enum-size)
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

[[nodiscard]] constexpr auto bits_per_pixel(PixelFormat format) noexcept -> std::uint32_t {
    switch (format) {
        case PixelFormat::R16_UINT:
        case PixelFormat::F16:
            return kBitsPerUint16Pixel;
        case PixelFormat::R10_PACKED:
            return kBitsPerPackedRaw10Pixel;
        case PixelFormat::R8G8B8A8_UNORM:
        case PixelFormat::R10G10B10A2_UNORM:
        case PixelFormat::F32:
        case PixelFormat::R32_SFLOAT:
            return kBitsPerRgba8Pixel;
        case PixelFormat::R8G8B8_UNORM:
            return kBitsPerRgb8Pixel;
        case PixelFormat::R16G16B16A16_SFLOAT:
            return kBitsPerRgba16FloatPixel;
        case PixelFormat::R16G16B16_SFLOAT:
            return kBitsPerRgb16FloatPixel;
        case PixelFormat::R32G32B32A32_SFLOAT:
            return kBitsPerRgba32FloatPixel;
        case PixelFormat::R32G32B32_SFLOAT:
            return kBitsPerRgb32FloatPixel;
        case PixelFormat::I8:
        case PixelFormat::U8:
        case PixelFormat::BLOB:
            return kBitsPerByte;
        case PixelFormat::UNDEFINED:
            return 0;
    }

    return 0;
}

[[nodiscard]] constexpr auto bytes_per_pixel(PixelFormat format) noexcept
    -> std::optional<std::uint32_t> {
    const auto bits = bits_per_pixel(format);
    if (bits == 0 || bits % kBitsPerByte != 0) {
        return std::nullopt;
    }
    return bits / kBitsPerByte;
}

[[nodiscard]] constexpr auto to_string(PixelFormat format) noexcept -> std::string_view {
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
    }

    return "UNKNOWN";
}

}  // namespace cpipe::compute
