// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cstdint>
#include <limits>
#include <optional>

namespace cpipe::runtime {
namespace {

inline constexpr auto kUint8Bits = static_cast<std::uint8_t>(compute::kBitsPerByte);
inline constexpr auto kUint16Bits = static_cast<std::uint8_t>(compute::kBitsPerUint16Pixel);
inline constexpr auto kFloat32Bits = static_cast<std::uint8_t>(compute::kBitsPerRgba8Pixel);
inline constexpr auto kUint8Bytes = std::uint32_t{1};
inline constexpr auto kUint16Bytes = std::uint32_t{2};
inline constexpr auto kFloat32Bytes = std::uint32_t{4};
inline constexpr auto kScalarChannels = std::int32_t{1};
inline constexpr auto kRgbChannels = std::int32_t{3};
inline constexpr auto kRgbaChannels = std::int32_t{4};
inline constexpr auto kImage2DDimensions = std::uint8_t{2};
inline constexpr auto kHalideInterleavedDimensions = std::int32_t{3};
inline constexpr auto kXDimension = std::size_t{0};
inline constexpr auto kYDimension = std::size_t{1};
inline constexpr auto kChannelDimension = std::size_t{2};
inline constexpr auto kHalideMin = std::int32_t{0};
inline constexpr auto kNoHalideDimensionFlags = std::uint32_t{0};

struct ElementInfo {
    halide_type_t type;
    std::uint32_t bytes = 0;
    std::int32_t channels = kScalarChannels;
    bool expand_channels = false;
};

[[nodiscard]] auto make_type(halide_type_code_t code, std::uint8_t bits) noexcept -> halide_type_t {
    halide_type_t type{};
    type.code = code;
    type.bits = bits;
    type.lanes = static_cast<std::uint16_t>(kScalarChannels);
    return type;
}

[[nodiscard]] auto element_info(compute::PixelFormat format) noexcept
    -> std::optional<ElementInfo> {
    using cpipe::compute::PixelFormat;
    switch (format) {
        case PixelFormat::R8G8B8A8_UNORM:
            return ElementInfo{make_type(halide_type_uint, kUint8Bits), kUint8Bytes, kRgbaChannels,
                               true};
        case PixelFormat::R8G8B8_UNORM:
            return ElementInfo{make_type(halide_type_uint, kUint8Bits), kUint8Bytes, kRgbChannels,
                               true};
        case PixelFormat::R16G16B16A16_SFLOAT:
            return ElementInfo{make_type(halide_type_float, kUint16Bits), kUint16Bytes,
                               kRgbaChannels, true};
        case PixelFormat::R16G16B16_SFLOAT:
            return ElementInfo{make_type(halide_type_float, kUint16Bits), kUint16Bytes,
                               kRgbChannels, true};
        case PixelFormat::R32G32B32A32_SFLOAT:
            return ElementInfo{make_type(halide_type_float, kFloat32Bits), kFloat32Bytes,
                               kRgbaChannels, true};
        case PixelFormat::R32G32B32_SFLOAT:
            return ElementInfo{make_type(halide_type_float, kFloat32Bits), kFloat32Bytes,
                               kRgbChannels, true};
        case PixelFormat::R16_UINT:
        case PixelFormat::F16:
            return ElementInfo{make_type(halide_type_uint, kUint16Bits), kUint16Bytes,
                               kScalarChannels, false};
        case PixelFormat::R32_SFLOAT:
        case PixelFormat::F32:
            return ElementInfo{make_type(halide_type_float, kFloat32Bits), kFloat32Bytes,
                               kScalarChannels, false};
        case PixelFormat::I8:
            return ElementInfo{make_type(halide_type_int, kUint8Bits), kUint8Bytes, kScalarChannels,
                               false};
        case PixelFormat::U8:
        case PixelFormat::BLOB:
            return ElementInfo{make_type(halide_type_uint, kUint8Bits), kUint8Bytes,
                               kScalarChannels, false};
        case PixelFormat::UNDEFINED:
        case PixelFormat::R10_PACKED:
        case PixelFormat::R10G10B10A2_UNORM:
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] auto checked_i32(std::uint64_t value) noexcept -> std::optional<std::int32_t> {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(value);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto tight_stride(const compute::BufferLayout& layout, std::uint8_t index,
                                std::uint32_t element_bytes) noexcept -> std::uint64_t {
    auto stride = static_cast<std::uint64_t>(element_bytes);
    for (std::uint8_t current = 0; current < index; ++current) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        stride *= layout.dims[current];
    }
    return stride;
}

[[nodiscard]] auto stride_elements(std::uint64_t stride_bytes, std::uint32_t element_bytes) noexcept
    -> std::optional<std::int32_t> {
    if (element_bytes == 0U || stride_bytes % element_bytes != 0U) {
        return std::nullopt;
    }
    return checked_i32(stride_bytes / element_bytes);
}

}  // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
HalideBufferAdapter::HalideBufferAdapter(cpipe_buffer_t* buffer,
                                         compute::IBuffer::CpuAccess access) noexcept
    : handle_(buffer), access_(access) {
    if (handle_ == nullptr) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    buffer_owner_ = reinterpret_cast<compute::IBuffer*>(handle_);
    if (buffer_owner_ == nullptr) {
        return;
    }

    const auto& layout = buffer_owner_->layout();
    const auto info = element_info(layout.format);
    if (!info.has_value() || layout.ndim == 0U || layout.ndim > compute::kMaxBufferDimensions) {
        status_ = CPIPE_UNSUPPORTED;
        return;
    }

    auto* host_ptr = buffer_owner_->lock_cpu(access_);
    if (host_ptr == nullptr) {
        status_ = CPIPE_FAILED;
        return;
    }
    locked_ = true;

    buffer_ = halide_buffer_t{};
    buffer_.device = 0;
    buffer_.device_interface = nullptr;
    buffer_.host = static_cast<std::uint8_t*>(host_ptr);
    buffer_.flags = 0;
    buffer_.type = info->type;
    buffer_.dim = dimensions_.data();
    buffer_.padding = nullptr;

    if (info->expand_channels && layout.kind == compute::BufferKind::Image2D &&
        layout.ndim == kImage2DDimensions) {
        const auto pixel_bytes =
            static_cast<std::uint64_t>(info->bytes) * static_cast<std::uint64_t>(info->channels);
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto x_stride =
            layout.stride[kXDimension] == 0U ? pixel_bytes : layout.stride[kXDimension];
        const auto y_stride =
            layout.stride[kYDimension] == 0U
                ? static_cast<std::uint64_t>(layout.dims[kXDimension]) * pixel_bytes
                : layout.stride[kYDimension];
        const auto x_stride_elements = stride_elements(x_stride, info->bytes);
        const auto y_stride_elements = stride_elements(y_stride, info->bytes);
        const auto width = checked_i32(layout.dims[kXDimension]);
        const auto height = checked_i32(layout.dims[kYDimension]);
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
        if (!x_stride_elements.has_value() || !y_stride_elements.has_value() ||
            !width.has_value() || !height.has_value()) {
            status_ = CPIPE_UNSUPPORTED;
            return;
        }

        dimensions_[kXDimension] =
            halide_dimension_t{kHalideMin, *width, *x_stride_elements, kNoHalideDimensionFlags};
        dimensions_[kYDimension] =
            halide_dimension_t{kHalideMin, *height, *y_stride_elements, kNoHalideDimensionFlags};
        dimensions_[kChannelDimension] = halide_dimension_t{
            kHalideMin, info->channels, kScalarChannels, kNoHalideDimensionFlags};
        buffer_.dimensions = kHalideInterleavedDimensions;
        status_ = CPIPE_OK;
        return;
    }

    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto extent = checked_i32(layout.dims[index]);
        const auto stride_bytes = layout.stride[index] == 0U
                                      ? tight_stride(layout, index, info->bytes)
                                      : layout.stride[index];
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto stride = stride_elements(stride_bytes, info->bytes);
        if (!extent.has_value() || !stride.has_value()) {
            status_ = CPIPE_UNSUPPORTED;
            return;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        dimensions_[index] =
            halide_dimension_t{kHalideMin, *extent, *stride, kNoHalideDimensionFlags};
    }
    buffer_.dimensions = layout.ndim;
    status_ = CPIPE_OK;
}
// NOLINTEND(readability-function-cognitive-complexity)

HalideBufferAdapter::~HalideBufferAdapter() {
    if (!locked_ || buffer_owner_ == nullptr) {
        return;
    }
    if (access_ != compute::IBuffer::CpuAccess::Read) {
        buffer_owner_->flush_cpu_writes();
    }
    buffer_owner_->unlock_cpu();
}

auto HalideBufferAdapter::status() const noexcept -> int {
    return status_;
}

auto HalideBufferAdapter::get() noexcept -> halide_buffer_t* {
    return &buffer_;
}

}  // namespace cpipe::runtime
