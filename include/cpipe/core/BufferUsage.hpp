// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <type_traits>

namespace cpipe::compute {

// buffer.md fixes BufferUsage at uint32_t for usage bit flags.
enum class BufferUsage : std::uint32_t {  // NOLINT(performance-enum-size)
    None = 0U,
    Input = 1U << 0U,
    Output = 1U << 1U,
    Intermediate = 1U << 2U,
    Scratch = 1U << 3U,
    CpuRead = 1U << 4U,
    CpuWrite = 1U << 5U,
    GpuSampled = 1U << 6U,
    GpuStorage = 1U << 7U,
    NpuInput = 1U << 8U,
    NpuOutput = 1U << 9U,
};

constexpr auto to_underlying(BufferUsage usage) noexcept -> std::uint32_t {
    return static_cast<std::underlying_type_t<BufferUsage>>(usage);
}

constexpr auto operator|(BufferUsage lhs, BufferUsage rhs) noexcept -> BufferUsage {
    return static_cast<BufferUsage>(to_underlying(lhs) | to_underlying(rhs));
}

constexpr auto operator&(BufferUsage lhs, BufferUsage rhs) noexcept -> BufferUsage {
    return static_cast<BufferUsage>(to_underlying(lhs) & to_underlying(rhs));
}

constexpr auto operator|=(BufferUsage& lhs, BufferUsage rhs) noexcept -> BufferUsage& {
    lhs = lhs | rhs;
    return lhs;
}

constexpr auto has_usage(BufferUsage usage, BufferUsage flag) noexcept -> bool {
    return (to_underlying(usage & flag) != 0U);
}

}  // namespace cpipe::compute
