// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/BufferUsage.hpp"

namespace cpipe::compute {

class IBuffer {
public:
    enum class CpuAccess { Read, Write, ReadWrite };  // NOLINT(performance-enum-size)

    virtual ~IBuffer() = default;

    [[nodiscard]] virtual auto layout() const noexcept -> const BufferLayout& = 0;
    [[nodiscard]] virtual auto size_bytes() const noexcept -> std::uint64_t = 0;
    [[nodiscard]] virtual auto color_role() const noexcept -> std::string_view = 0;

    virtual auto lock_cpu(CpuAccess access) -> void* = 0;
    virtual void unlock_cpu() = 0;
    virtual void flush_cpu_writes() = 0;

    virtual auto sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t width,
                          std::uint32_t height) -> std::shared_ptr<IBuffer> = 0;
};

class CpuBuffer final : public IBuffer {
public:
    explicit CpuBuffer(BufferLayout layout, BufferUsage usage,
                       std::string color_role = std::string{});
    ~CpuBuffer() override;

    CpuBuffer(const CpuBuffer&) = delete;
    auto operator=(const CpuBuffer&) -> CpuBuffer& = delete;

    CpuBuffer(CpuBuffer&&) = delete;
    auto operator=(CpuBuffer&&) -> CpuBuffer& = delete;

    [[nodiscard]] auto layout() const noexcept -> const BufferLayout& override;
    [[nodiscard]] auto size_bytes() const noexcept -> std::uint64_t override;
    [[nodiscard]] auto color_role() const noexcept -> std::string_view override;

    auto lock_cpu(CpuAccess access) -> void* override;
    void unlock_cpu() override;
    void flush_cpu_writes() override;

    auto sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t width,
                  std::uint32_t height) -> std::shared_ptr<IBuffer> override;

private:
    [[nodiscard]] auto allows_access(CpuAccess access) const noexcept -> bool;

    BufferLayout layout_{};
    BufferUsage usage_ = BufferUsage::None;
    std::string color_role_;
    void* data_ = nullptr;
    std::uint64_t size_bytes_ = 0;
    bool locked_ = false;
};

}  // namespace cpipe::compute
