// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/core/PixelFormat.hpp"

namespace cpipe::compute {

std::string_view to_string(PixelFormat format) noexcept {
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

std::size_t bytes_per_pixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::R16_UINT:
        case PixelFormat::F16:
            return 2;
        case PixelFormat::R8G8B8A8_UNORM:
        case PixelFormat::R10G10B10A2_UNORM:
        case PixelFormat::R32_SFLOAT:
        case PixelFormat::F32:
            return 4;
        case PixelFormat::R8G8B8_UNORM:
            return 3;
        case PixelFormat::R16G16B16A16_SFLOAT:
            return 8;
        case PixelFormat::R16G16B16_SFLOAT:
            return 6;
        case PixelFormat::R32G32B32A32_SFLOAT:
            return 16;
        case PixelFormat::R32G32B32_SFLOAT:
            return 12;
        case PixelFormat::I8:
        case PixelFormat::U8:
        case PixelFormat::BLOB:
            return 1;
        case PixelFormat::UNDEFINED:
        case PixelFormat::R10_PACKED:
            return 0;
    }
    return 0;
}

}  // namespace cpipe::compute
