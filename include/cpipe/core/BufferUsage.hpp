// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <type_traits>

namespace cpipe::compute {

enum class BufferUsage : std::uint32_t {
    None = 0,
    Input = 1u << 0,
    Output = 1u << 1,
    Intermediate = 1u << 2,
    Scratch = 1u << 3,
    CpuRead = 1u << 4,
    CpuWrite = 1u << 5,
    GpuSampled = 1u << 6,
    GpuStorage = 1u << 7,
    NpuInput = 1u << 8,
    NpuOutput = 1u << 9,
};

[[nodiscard]] constexpr auto to_underlying(BufferUsage usage) noexcept -> std::uint32_t {
    return static_cast<std::underlying_type_t<BufferUsage>>(usage);
}

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(to_underlying(lhs) | to_underlying(rhs));
}

[[nodiscard]] constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(to_underlying(lhs) & to_underlying(rhs));
}

constexpr BufferUsage& operator|=(BufferUsage& lhs, BufferUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool has_usage(BufferUsage usage, BufferUsage flag) noexcept {
    return (to_underlying(usage & flag) == to_underlying(flag)) && flag != BufferUsage::None;
}

}  // namespace cpipe::compute
