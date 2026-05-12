// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "cpipe/core/PixelFormat.hpp"

namespace cpipe::compute {

enum class BufferKind : uint8_t {
    Image2D = 0,
    Volume3D = 1,
    TensorND = 2,
    Blob = 3,
};

std::string_view to_string(BufferKind kind) noexcept;

struct BufferLayout {
    BufferKind kind = BufferKind::Blob;
    PixelFormat format = PixelFormat::BLOB;
    uint8_t ndim = 1;
    std::array<uint32_t, 8> dims{};
    std::array<uint64_t, 8> stride{};

    [[nodiscard]] uint64_t size_bytes() const noexcept;
    [[nodiscard]] bool is_valid() const noexcept;
};

BufferLayout make_rgba8_layout(uint32_t width, uint32_t height) noexcept;
BufferLayout make_blob_layout(uint64_t bytes) noexcept;
BufferLayout make_default_strides(BufferLayout layout) noexcept;

}  // namespace cpipe::compute
