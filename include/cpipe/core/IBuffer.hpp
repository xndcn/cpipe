// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cstdint>
#include <memory>
#include <string_view>

namespace cpipe::compute {

class IBuffer {
public:
    enum class CpuAccess { Read, Write, ReadWrite };

    virtual ~IBuffer() = default;

    [[nodiscard]] virtual const BufferLayout& layout() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t size_bytes() const noexcept = 0;
    [[nodiscard]] virtual std::string_view color_role() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const BufferMetadata> metadata() const noexcept = 0;
    virtual void set_metadata(std::shared_ptr<const BufferMetadata> metadata) = 0;

    virtual void* lock_cpu(CpuAccess access) = 0;
    virtual void unlock_cpu() = 0;
    virtual void flush_cpu_writes() = 0;

    virtual std::shared_ptr<IBuffer> sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t w,
                                              std::uint32_t h) = 0;
};

}  // namespace cpipe::compute
