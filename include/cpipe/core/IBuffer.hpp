// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <memory>
#include <string_view>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/BufferUsage.hpp"

namespace cpipe::compute {

class IBuffer {
public:
    enum class CpuAccess { Read = 0, Write = 1, ReadWrite = 2 };

    virtual ~IBuffer() = default;

    [[nodiscard]] virtual const BufferLayout& layout() const noexcept = 0;
    [[nodiscard]] virtual uint64_t size_bytes() const noexcept = 0;
    [[nodiscard]] virtual std::string_view color_role() const noexcept = 0;
    [[nodiscard]] virtual BufferUsage usage() const noexcept = 0;

    virtual void* lock_cpu(CpuAccess access) = 0;
    virtual void unlock_cpu() = 0;
    virtual void flush_cpu_writes() = 0;

    virtual std::shared_ptr<IBuffer> sub_view(uint32_t x0, uint32_t y0, uint32_t width,
                                              uint32_t height) = 0;
};

}  // namespace cpipe::compute
