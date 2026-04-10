// src/common/types.hpp -- C++ type wrappers for C types in cpipe/types.h
#pragma once
#include <cpipe/types.h>
#include <cstdint>

namespace cpipe {

enum class PixelFormat : int {
    BAYER_RGGB_16 = CPIPE_PIXEL_FORMAT_BAYER_RGGB_16,
    BAYER_BGGR_16 = CPIPE_PIXEL_FORMAT_BAYER_BGGR_16,
    BAYER_GRBG_16 = CPIPE_PIXEL_FORMAT_BAYER_GRBG_16,
    BAYER_GBRG_16 = CPIPE_PIXEL_FORMAT_BAYER_GBRG_16,
    RGB_16        = CPIPE_PIXEL_FORMAT_RGB_16,
    RGB_8         = CPIPE_PIXEL_FORMAT_RGB_8,
    RGBA_8        = CPIPE_PIXEL_FORMAT_RGBA_8,
    RGB_FLOAT32   = CPIPE_PIXEL_FORMAT_RGB_FLOAT32,
};

enum class DeviceType : int {
    CPU = CPIPE_DEVICE_CPU,
    GPU = CPIPE_DEVICE_GPU,
    NPU = CPIPE_DEVICE_NPU,
};

inline cpipe_pixel_format_t to_c(PixelFormat f) noexcept {
    return static_cast<cpipe_pixel_format_t>(f);
}
inline PixelFormat from_c(cpipe_pixel_format_t f) noexcept {
    return static_cast<PixelFormat>(f);
}
inline cpipe_device_type_t to_c(DeviceType d) noexcept {
    return static_cast<cpipe_device_type_t>(d);
}
inline DeviceType from_c(cpipe_device_type_t d) noexcept {
    return static_cast<DeviceType>(d);
}

/// Returns the number of bytes per pixel for the given format.
inline uint32_t bytes_per_pixel(PixelFormat fmt) noexcept {
    switch (fmt) {
    case PixelFormat::BAYER_RGGB_16:
    case PixelFormat::BAYER_BGGR_16:
    case PixelFormat::BAYER_GRBG_16:
    case PixelFormat::BAYER_GBRG_16: return 2;
    case PixelFormat::RGB_16:        return 6;
    case PixelFormat::RGB_8:         return 3;
    case PixelFormat::RGBA_8:        return 4;
    case PixelFormat::RGB_FLOAT32:   return 12;
    }
    return 0; // unreachable; silence compiler warning
}

} // namespace cpipe
