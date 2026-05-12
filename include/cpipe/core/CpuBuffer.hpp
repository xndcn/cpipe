// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "cpipe/core/IBuffer.hpp"
#include "cpipe/core/Status.hpp"

namespace cpipe::compute {

class CpuBuffer final : public IBuffer {
public:
    static constexpr std::size_t kAlignment = 64;

    static cpipe::Result<std::shared_ptr<CpuBuffer>> create(BufferLayout layout, BufferUsage usage,
                                                            std::string color_role = {});

    CpuBuffer(const CpuBuffer&) = delete;
    CpuBuffer& operator=(const CpuBuffer&) = delete;
    CpuBuffer(CpuBuffer&&) = delete;
    CpuBuffer& operator=(CpuBuffer&&) = delete;
    ~CpuBuffer() override;

    [[nodiscard]] const BufferLayout& layout() const noexcept override;
    [[nodiscard]] uint64_t size_bytes() const noexcept override;
    [[nodiscard]] std::string_view color_role() const noexcept override;
    [[nodiscard]] BufferUsage usage() const noexcept override;
    [[nodiscard]] bool is_locked() const noexcept;
    [[nodiscard]] void* data() noexcept;
    [[nodiscard]] const void* data() const noexcept;

    void* lock_cpu(CpuAccess access) override;
    void unlock_cpu() override;
    void flush_cpu_writes() override;

    std::shared_ptr<IBuffer> sub_view(uint32_t x0, uint32_t y0, uint32_t width,
                                      uint32_t height) override;

private:
    CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role, void* data,
              uint64_t size);

    BufferLayout layout_;
    BufferUsage usage_;
    std::string color_role_;
    void* data_ = nullptr;
    uint64_t size_ = 0;
    bool locked_ = false;
};

}  // namespace cpipe::compute
