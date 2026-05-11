// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <mutex>
#include <string>

namespace cpipe::compute {

class CpuBuffer final : public IBuffer {
public:
    explicit CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role = {});
    ~CpuBuffer() override;

    CpuBuffer(const CpuBuffer&) = delete;
    CpuBuffer& operator=(const CpuBuffer&) = delete;
    CpuBuffer(CpuBuffer&&) = delete;
    CpuBuffer& operator=(CpuBuffer&&) = delete;

    [[nodiscard]] const BufferLayout& layout() const noexcept override;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept override;
    [[nodiscard]] std::string_view color_role() const noexcept override;

    void* lock_cpu(CpuAccess access) override;
    void unlock_cpu() override;
    void flush_cpu_writes() override;

    std::shared_ptr<IBuffer> sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t width,
                                      std::uint32_t height) override;

private:
    [[nodiscard]] bool permits(CpuAccess access) const noexcept;

    BufferLayout layout_;
    BufferUsage usage_;
    std::string color_role_;
    std::uint64_t size_bytes_ = 0;
    void* data_ = nullptr;
    bool locked_ = false;
    std::mutex mutex_;
};

}  // namespace cpipe::compute
