// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cpipe::compute {

class CpuBuffer final : public IBuffer {
public:
    static constexpr std::uint64_t kAlignment = 64;

    [[nodiscard]] static auto create(BufferLayout layout, BufferUsage usage,
                                     std::string color_role = {}) -> std::unique_ptr<CpuBuffer>;

    CpuBuffer(const CpuBuffer&) = delete;
    auto operator=(const CpuBuffer&) -> CpuBuffer& = delete;
    CpuBuffer(CpuBuffer&&) = delete;
    auto operator=(CpuBuffer&&) -> CpuBuffer& = delete;
    ~CpuBuffer() override;

    [[nodiscard]] auto layout() const noexcept -> const BufferLayout& override;
    [[nodiscard]] auto size_bytes() const noexcept -> std::uint64_t override;
    [[nodiscard]] auto color_role() const noexcept -> std::string_view override;
    [[nodiscard]] auto usage() const noexcept -> BufferUsage;
    [[nodiscard]] auto data() noexcept -> void*;
    [[nodiscard]] auto data() const noexcept -> const void*;

    [[nodiscard]] auto lock_cpu(CpuAccess access) -> void* override;
    auto unlock_cpu() -> void override;
    auto flush_cpu_writes() -> void override;
    [[nodiscard]] auto sub_view(std::uint32_t origin_x, std::uint32_t origin_y, std::uint32_t width,
                                std::uint32_t height)
        -> std::shared_ptr<IBuffer> override;  // NOLINT(bugprone-easily-swappable-parameters)

private:
    CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role, void* data,
              std::uint64_t size_bytes) noexcept;

    BufferLayout layout_{};
    BufferUsage usage_ = BufferUsage::None;
    std::string color_role_;
    void* data_ = nullptr;
    std::uint64_t size_bytes_ = 0;
    bool locked_ = false;
};

}  // namespace cpipe::compute
