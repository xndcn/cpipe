// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>

namespace cpipe::compute {

enum class BufferUsage : std::uint32_t {
    None = 0,
    Input = 1u << 0u,
    Output = 1u << 1u,
    Intermediate = 1u << 2u,
    Scratch = 1u << 3u,
    CpuRead = 1u << 4u,
    CpuWrite = 1u << 5u,
    GpuSampled = 1u << 6u,
    GpuStorage = 1u << 7u,
    NpuInput = 1u << 8u,
    NpuOutput = 1u << 9u,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(lhs) |
                                    static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(lhs) &
                                    static_cast<std::uint32_t>(rhs));
}

constexpr BufferUsage& operator|=(BufferUsage& lhs, BufferUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool has_usage(BufferUsage usage, BufferUsage flag) noexcept {
    return (static_cast<std::uint32_t>(usage & flag) != 0u);
}

}  // namespace cpipe::compute
