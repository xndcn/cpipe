// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/HalideBufferAdapter.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/PixelFormat.hpp"

namespace cpipe::runtime {
namespace {

struct HalideFormat {
    halide_type_t type;
    std::uint32_t component_bytes;
    std::uint32_t channels;
};

[[nodiscard]] auto format_info(compute::PixelFormat format) noexcept
    -> std::optional<HalideFormat> {
    switch (format) {
        case compute::PixelFormat::R16_UINT:
            return HalideFormat{halide_type_t(halide_type_uint, 16), 2U, 1U};
        case compute::PixelFormat::R8G8B8A8_UNORM:
            return HalideFormat{halide_type_t(halide_type_uint, 8), 1U, 4U};
        case compute::PixelFormat::R8G8B8_UNORM:
            return HalideFormat{halide_type_t(halide_type_uint, 8), 1U, 3U};
        case compute::PixelFormat::R16G16B16A16_SFLOAT:
            return HalideFormat{halide_type_t(halide_type_float, 16), 2U, 4U};
        case compute::PixelFormat::R16G16B16_SFLOAT:
            return HalideFormat{halide_type_t(halide_type_float, 16), 2U, 3U};
        case compute::PixelFormat::R32G32B32A32_SFLOAT:
            return HalideFormat{halide_type_t(halide_type_float, 32), 4U, 4U};
        case compute::PixelFormat::R32G32B32_SFLOAT:
            return HalideFormat{halide_type_t(halide_type_float, 32), 4U, 3U};
        case compute::PixelFormat::R32_SFLOAT:
        case compute::PixelFormat::F32:
            return HalideFormat{halide_type_t(halide_type_float, 32), 4U, 1U};
        case compute::PixelFormat::F16:
            return HalideFormat{halide_type_t(halide_type_float, 16), 2U, 1U};
        case compute::PixelFormat::I8:
            return HalideFormat{halide_type_t(halide_type_int, 8), 1U, 1U};
        case compute::PixelFormat::U8:
        case compute::PixelFormat::BLOB:
            return HalideFormat{halide_type_t(halide_type_uint, 8), 1U, 1U};
        case compute::PixelFormat::UNDEFINED:
        case compute::PixelFormat::R10_PACKED:
        case compute::PixelFormat::R10G10B10A2_UNORM:
            return std::nullopt;
    }

    return std::nullopt;
}

[[nodiscard]] auto to_i32(std::uint64_t value) noexcept -> std::optional<std::int32_t> {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(value);
}

}  // namespace

HalideBufferAdapter::HalideBufferAdapter(compute::IBuffer& buffer,
                                         compute::IBuffer::CpuAccess access)
    : access_(access) {
    const auto& layout = buffer.layout();
    const auto maybe_format = format_info(layout.format);
    const auto maybe_pixel_bytes = compute::bytes_per_pixel(layout.format);
    if (!maybe_format.has_value() || !maybe_pixel_bytes.has_value() || layout.ndim == 0 ||
        layout.ndim > layout.dims.size()) {
        status_ = CPIPE_BAD_PRECISION;
        return;
    }

    const auto& halide_format = maybe_format.value();
    const auto halide_dims = layout.ndim + (halide_format.channels > 1U ? 1U : 0U);
    if (halide_dims > dims_.size()) {
        status_ = CPIPE_BAD_INDEX;
        return;
    }

    std::array<std::uint64_t, 8> byte_stride{};
    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        if (layout.dims[index] == 0) {
            status_ = CPIPE_BAD_INDEX;
            return;
        }
        if (layout.stride[index] != 0) {
            byte_stride[index] = layout.stride[index];
        } else if (index == 0) {
            byte_stride[index] = maybe_pixel_bytes.value();
        } else {
            byte_stride[index] = byte_stride[index - 1U] * layout.dims[index - 1U];
        }
        if (byte_stride[index] % halide_format.component_bytes != 0U) {
            status_ = CPIPE_BAD_PRECISION;
            return;
        }
    }

    auto* host = static_cast<std::uint8_t*>(buffer.lock_cpu(access));
    if (host == nullptr) {
        status_ = CPIPE_FAILED;
        return;
    }
    locked_buffer_ = &buffer;

    auto dim_index = std::size_t{0};
    if (halide_format.channels > 1U) {
        const auto maybe_channels = to_i32(halide_format.channels);
        if (!maybe_channels.has_value()) {
            status_ = CPIPE_BAD_INDEX;
            return;
        }
        dims_[dim_index] = halide_dimension_t{0, maybe_channels.value(), 1};
        ++dim_index;
    }

    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        const auto maybe_extent = to_i32(layout.dims[index]);
        const auto maybe_stride = to_i32(byte_stride[index] / halide_format.component_bytes);
        if (!maybe_extent.has_value() || !maybe_stride.has_value()) {
            status_ = CPIPE_BAD_INDEX;
            return;
        }
        dims_[dim_index] = halide_dimension_t{0, maybe_extent.value(), maybe_stride.value()};
        ++dim_index;
    }

    buffer_.device = 0;
    buffer_.device_interface = nullptr;
    buffer_.host = host;
    buffer_.flags = 0;
    buffer_.type = halide_format.type;
    buffer_.dimensions = static_cast<std::int32_t>(halide_dims);
    buffer_.dim = dims_.data();
    buffer_.padding = nullptr;
}

HalideBufferAdapter::~HalideBufferAdapter() {
    if (locked_buffer_ != nullptr) {
        if (access_ != compute::IBuffer::CpuAccess::Read) {
            locked_buffer_->flush_cpu_writes();
        }
        locked_buffer_->unlock_cpu();
    }
}

auto HalideBufferAdapter::status() const noexcept -> int {
    return status_;
}

auto HalideBufferAdapter::get() noexcept -> halide_buffer_t* {
    return status_ == CPIPE_OK ? &buffer_ : nullptr;
}

}  // namespace cpipe::runtime
