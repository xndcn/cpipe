// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cpipe::compute {

class CpuBuffer final : public IBuffer {
public:
    static constexpr std::size_t kAlignment = 64;

    CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role = {});
    CpuBuffer(const CpuBuffer&) = delete;
    CpuBuffer& operator=(const CpuBuffer&) = delete;
    CpuBuffer(CpuBuffer&&) = delete;
    CpuBuffer& operator=(CpuBuffer&&) = delete;
    ~CpuBuffer() override;

    [[nodiscard]] const BufferLayout& layout() const noexcept override;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept override;
    [[nodiscard]] std::string_view color_role() const noexcept override;
    [[nodiscard]] BufferUsage usage() const noexcept {
        return usage_;
    }

    void* lock_cpu(CpuAccess access) override;
    void unlock_cpu() override;
    void flush_cpu_writes() override;
    std::shared_ptr<IBuffer> sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t w,
                                      std::uint32_t h) override;

private:
    BufferLayout layout_{};
    BufferUsage usage_{BufferUsage::None};
    std::string color_role_;
    void* data_{nullptr};
    std::uint64_t size_bytes_{0};
    bool locked_{false};
};

}  // namespace cpipe::compute
