// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <new>

#include "cpipe/core/IBuffer.hpp"

namespace cpipe::compute {
namespace {
constexpr std::size_t kCpuBufferAlignment = 64;
}

CpuBuffer::CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role)
    : layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      size_bytes_(layout_.size_bytes()) {
    const auto allocation_size = static_cast<std::size_t>(size_bytes_ == 0 ? 1 : size_bytes_);
    if (posix_memalign(&data_, kCpuBufferAlignment, allocation_size) != 0) {
        data_ = nullptr;
        size_bytes_ = 0;
    }
}

CpuBuffer::~CpuBuffer() {
    std::free(data_);
}

auto CpuBuffer::layout() const noexcept -> const BufferLayout& {
    return layout_;
}

auto CpuBuffer::size_bytes() const noexcept -> std::uint64_t {
    return size_bytes_;
}

auto CpuBuffer::color_role() const noexcept -> std::string_view {
    return color_role_;
}

auto CpuBuffer::lock_cpu(CpuAccess access) -> void* {
    if (locked_) {
        spdlog::warn("CpuBuffer::lock_cpu called while already locked");
        return nullptr;
    }
    if (!allows_access(access)) {
        spdlog::warn("CpuBuffer::lock_cpu called without compatible CPU usage flags");
        return nullptr;
    }

    locked_ = true;
    return data_;
}

void CpuBuffer::unlock_cpu() {
    if (!locked_) {
        spdlog::warn("CpuBuffer::unlock_cpu called without a matching lock");
        return;
    }

    locked_ = false;
}

void CpuBuffer::flush_cpu_writes() {}

auto CpuBuffer::sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t width,
                         std::uint32_t height) -> std::shared_ptr<IBuffer> {
    (void)x0;
    (void)y0;
    (void)width;
    (void)height;
    spdlog::warn("IBuffer::sub_view is reserved but not implemented in v1");
    return nullptr;
}

auto CpuBuffer::allows_access(CpuAccess access) const noexcept -> bool {
    switch (access) {
        case CpuAccess::Read:
            return has_usage(usage_, BufferUsage::CpuRead);
        case CpuAccess::Write:
            return has_usage(usage_, BufferUsage::CpuWrite);
        case CpuAccess::ReadWrite:
            return has_usage(usage_, BufferUsage::CpuRead) &&
                   has_usage(usage_, BufferUsage::CpuWrite);
    }

    return false;
}

}  // namespace cpipe::compute
