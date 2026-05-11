// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/BufferLayout.hpp>
#include <cstddef>

namespace cpipe::compute {
namespace {

[[nodiscard]] constexpr std::uint64_t ceil_div(std::uint64_t numerator,
                                               std::uint64_t denominator) noexcept {
    return (numerator + denominator - 1) / denominator;
}

[[nodiscard]] std::uint64_t contiguous_size(const BufferLayout& layout) noexcept {
    if (layout.kind == BufferKind::Blob) {
        return layout.dims[0];
    }

    if (layout.ndim == 0 || layout.ndim > 8) {
        return 0;
    }

    if (layout.kind == BufferKind::Image2D && layout.format == PixelFormat::R10_PACKED) {
        const auto row_bytes =
            ceil_div(static_cast<std::uint64_t>(layout.dims[0]) * bits_per_pixel(layout.format), 8);
        return row_bytes * static_cast<std::uint64_t>(layout.dims[1]);
    }

    const auto bytes = bytes_per_pixel(layout.format);
    if (bytes == 0) {
        return 0;
    }

    std::uint64_t elements = 1;
    for (std::uint8_t i = 0; i < layout.ndim; ++i) {
        elements *= static_cast<std::uint64_t>(layout.dims[i]);
    }
    return elements * bytes;
}

[[nodiscard]] bool has_explicit_stride(const BufferLayout& layout) noexcept {
    for (std::uint8_t i = 0; i < layout.ndim && i < 8; ++i) {
        if (layout.stride[i] != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::uint64_t BufferLayout::size_bytes() const noexcept {
    if (kind == BufferKind::Blob) {
        return dims[0];
    }

    if (ndim == 0 || ndim > 8) {
        return 0;
    }

    if (has_explicit_stride(*this)) {
        const auto last = static_cast<std::uint8_t>(ndim - 1);
        if (stride[last] != 0) {
            return stride[last] * static_cast<std::uint64_t>(dims[last]);
        }
    }

    return contiguous_size(*this);
}

}  // namespace cpipe::compute
