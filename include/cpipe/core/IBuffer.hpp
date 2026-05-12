// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferLayout.hpp>
#include <cstdint>
#include <memory>
#include <string_view>

namespace cpipe::compute {

class IBuffer {
public:
    enum class CpuAccess : std::uint8_t { Read, Write, ReadWrite };

    IBuffer() = default;
    IBuffer(const IBuffer&) = delete;
    auto operator=(const IBuffer&) -> IBuffer& = delete;
    IBuffer(IBuffer&&) = delete;
    auto operator=(IBuffer&&) -> IBuffer& = delete;

    virtual ~IBuffer() = default;

    [[nodiscard]] virtual auto layout() const noexcept -> const BufferLayout& = 0;
    [[nodiscard]] virtual auto size_bytes() const noexcept -> std::uint64_t = 0;
    [[nodiscard]] virtual auto color_role() const noexcept -> std::string_view = 0;

    [[nodiscard]] virtual auto lock_cpu(CpuAccess access) -> void* = 0;
    virtual auto unlock_cpu() -> void = 0;
    virtual auto flush_cpu_writes() -> void = 0;

    [[nodiscard]] virtual auto sub_view(std::uint32_t origin_x, std::uint32_t origin_y,
                                        std::uint32_t width,
                                        std::uint32_t height)
        -> std::shared_ptr<IBuffer> = 0;  // NOLINT(bugprone-easily-swappable-parameters)
};

}  // namespace cpipe::compute
