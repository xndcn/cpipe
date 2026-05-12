// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>

namespace cpipe::compute {

enum class BufferUsage : uint32_t {
    None = 0,
    CpuRead = 1u << 0u,
    CpuWrite = 1u << 1u,
    GpuSampled = 1u << 2u,
    GpuStorage = 1u << 3u,
};

constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr BufferUsage& operator|=(BufferUsage& lhs, BufferUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_usage(BufferUsage value, BufferUsage flag) noexcept {
    return (static_cast<uint32_t>(value & flag) != 0u);
}

}  // namespace cpipe::compute
