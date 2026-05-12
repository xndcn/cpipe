// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <cpipe/core/BufferLayout.hpp>
#include <cstdint>
#include <limits>
#include <optional>

namespace cpipe::compute {
namespace {

[[nodiscard]] constexpr auto ceil_div(std::uint64_t value, std::uint64_t divisor) noexcept
    -> std::uint64_t {
    return (value + divisor - 1U) / divisor;
}

[[nodiscard]] auto checked_mul(std::uint64_t lhs, std::uint64_t rhs) noexcept
    -> std::optional<std::uint64_t> {
    if (rhs != 0U && lhs > std::numeric_limits<std::uint64_t>::max() / rhs) {
        return std::nullopt;
    }
    return lhs * rhs;
}

[[nodiscard]] auto packed_row_bytes(const BufferLayout& layout) noexcept -> std::uint64_t {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    return ceil_div(static_cast<std::uint64_t>(layout.dims[0]) * bits_per_pixel(layout.format),
                    kBitsPerByte);
}

[[nodiscard]] auto tight_size_bytes(const BufferLayout& layout,
                                    std::uint64_t element_bytes) noexcept -> std::uint64_t {
    auto total = element_bytes;
    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto next = checked_mul(total, layout.dims[index]);
        if (!next.has_value()) {
            return 0;
        }
        total = *next;
    }
    return total;
}

[[nodiscard]] auto strided_size_bytes(const BufferLayout& layout,
                                      std::uint64_t element_bytes) noexcept -> std::uint64_t {
    std::array<std::uint64_t, kMaxBufferDimensions> effective_stride{};
    effective_stride[0] = element_bytes;
    for (std::uint8_t index = 1; index < layout.ndim; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto next = checked_mul(effective_stride[index - 1U], layout.dims[index - 1U]);
        if (!next.has_value()) {
            return 0;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        effective_stride[index] = *next;
    }

    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        if (layout.stride[index] != 0U) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            effective_stride[index] = layout.stride[index];
        }
    }

    std::uint64_t end_offset = 0;
    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        if (layout.dims[index] == 0U) {
            return 0;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto stride_bytes = effective_stride[index];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto dim_extent = layout.dims[index];
        const auto extent = checked_mul(static_cast<std::uint64_t>(dim_extent - 1U), stride_bytes);
        if (!extent.has_value() ||
            end_offset > std::numeric_limits<std::uint64_t>::max() - *extent) {
            return 0;
        }
        end_offset += *extent;
    }

    if (end_offset > std::numeric_limits<std::uint64_t>::max() - element_bytes) {
        return 0;
    }
    return end_offset + element_bytes;
}

[[nodiscard]] auto has_any_explicit_stride(const BufferLayout& layout) noexcept -> bool {
    for (std::uint8_t index = 0; index < layout.ndim; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        if (layout.stride[index] != 0U) {
            return true;
        }
    }
    return false;
}

}  // namespace

auto BufferLayout::size_bytes() const noexcept -> std::uint64_t {
    if (kind == BufferKind::Blob) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return ndim == 1U ? dims[0] : 0U;
    }
    if (ndim == 0U || ndim > kMaxBufferDimensions) {
        return 0;
    }
    if (format == PixelFormat::R10_PACKED && kind == BufferKind::Image2D && ndim == 2U) {
        const auto row_bytes = packed_row_bytes(*this);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto row_stride = stride[1] == 0U ? row_bytes : stride[1];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return dims[1] == 0U
                   ? 0U
                   : ((static_cast<std::uint64_t>(dims[1] - 1U) * row_stride) + row_bytes);
    }

    const auto element_bytes = bytes_per_pixel(format);
    if (!element_bytes.has_value()) {
        return 0;
    }
    if (has_any_explicit_stride(*this)) {
        return strided_size_bytes(*this, *element_bytes);
    }
    return tight_size_bytes(*this, *element_bytes);
}

}  // namespace cpipe::compute
