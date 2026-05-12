// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/core/BufferLayout.hpp"

#include <limits>

namespace cpipe::compute {

namespace {

uint64_t multiply_checked(uint64_t lhs, uint64_t rhs) noexcept {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    if (lhs > std::numeric_limits<uint64_t>::max() / rhs) {
        return 0;
    }
    return lhs * rhs;
}

uint64_t packed_r10_row_bytes(uint32_t width) noexcept {
    return (static_cast<uint64_t>(width) * 10u + 7u) / 8u;
}

}  // namespace

std::string_view to_string(BufferKind kind) noexcept {
    switch (kind) {
        case BufferKind::Image2D:
            return "Image2D";
        case BufferKind::Volume3D:
            return "Volume3D";
        case BufferKind::TensorND:
            return "TensorND";
        case BufferKind::Blob:
            return "Blob";
    }
    return "Unknown";
}

uint64_t BufferLayout::size_bytes() const noexcept {
    if (!is_valid()) {
        return 0;
    }

    if (kind == BufferKind::Blob) {
        return dims[0];
    }

    const auto bpp = bytes_per_pixel(format);
    if (kind == BufferKind::Image2D && format == PixelFormat::R10_PACKED) {
        const uint64_t row = stride[1] != 0 ? stride[1] : packed_r10_row_bytes(dims[0]);
        return multiply_checked(row, dims[1]);
    }

    if (bpp == 0) {
        return 0;
    }

    const uint64_t outer_stride = stride[ndim - 1u];
    if (outer_stride != 0) {
        return outer_stride * dims[ndim - 1u];
    }

    uint64_t elements = 1;
    for (uint8_t index = 0; index < ndim; ++index) {
        elements = multiply_checked(elements, dims[index]);
    }
    return multiply_checked(elements, bpp);
}

bool BufferLayout::is_valid() const noexcept {
    if (ndim == 0 || ndim > dims.size()) {
        return false;
    }
    for (uint8_t index = 0; index < ndim; ++index) {
        if (dims[index] == 0) {
            return false;
        }
    }
    if (kind == BufferKind::Image2D && ndim != 2) {
        return false;
    }
    if (kind == BufferKind::Volume3D && ndim != 3) {
        return false;
    }
    if (kind == BufferKind::Blob && ndim != 1) {
        return false;
    }
    return true;
}

BufferLayout make_rgba8_layout(uint32_t width, uint32_t height) noexcept {
    BufferLayout layout;
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    layout.stride[0] = 4;
    layout.stride[1] = static_cast<uint64_t>(width) * 4u;
    return layout;
}

BufferLayout make_blob_layout(uint64_t bytes) noexcept {
    BufferLayout layout;
    layout.kind = BufferKind::Blob;
    layout.format = PixelFormat::BLOB;
    layout.ndim = 1;
    layout.dims[0] = static_cast<uint32_t>(bytes);
    layout.stride[0] = 1;
    return layout;
}

BufferLayout make_default_strides(BufferLayout layout) noexcept {
    const auto bpp = bytes_per_pixel(layout.format);
    if (bpp == 0 || !layout.is_valid()) {
        return layout;
    }

    uint64_t stride = bpp;
    for (uint8_t index = 0; index < layout.ndim; ++index) {
        layout.stride[index] = stride;
        stride *= layout.dims[index];
    }
    return layout;
}

}  // namespace cpipe::compute
