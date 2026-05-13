// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cassert>
#include <cpipe/core/CpuBuffer.hpp>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace cpipe::compute {

CpuBuffer::CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role)
    : layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      size_bytes_(layout_.size_bytes()) {
    if (size_bytes_ == 0) {
        return;
    }

    void* allocated = nullptr;
    if (posix_memalign(&allocated, kAlignment, static_cast<std::size_t>(size_bytes_)) != 0) {
        std::abort();
    }
    data_ = allocated;
}

CpuBuffer::~CpuBuffer() {
    std::free(data_);
}

const BufferLayout& CpuBuffer::layout() const noexcept {
    return layout_;
}

std::uint64_t CpuBuffer::size_bytes() const noexcept {
    return size_bytes_;
}

std::string_view CpuBuffer::color_role() const noexcept {
    return color_role_;
}

void* CpuBuffer::lock_cpu(CpuAccess access) {
    assert(!locked_);

    if (access == CpuAccess::Read) {
        assert(has_usage(usage_, BufferUsage::CpuRead));
    } else if (access == CpuAccess::Write) {
        assert(has_usage(usage_, BufferUsage::CpuWrite));
    } else {
        assert(has_usage(usage_, BufferUsage::CpuRead));
        assert(has_usage(usage_, BufferUsage::CpuWrite));
    }

    locked_ = true;
    return data_;
}

void CpuBuffer::unlock_cpu() {
    assert(locked_);
    locked_ = false;
}

void CpuBuffer::flush_cpu_writes() {}

std::shared_ptr<IBuffer> CpuBuffer::sub_view(std::uint32_t x0, std::uint32_t y0, std::uint32_t w,
                                             std::uint32_t h) {
    (void)x0;
    (void)y0;
    (void)w;
    (void)h;
    std::clog << "warning: IBuffer::sub_view is not implemented in cpipe v1\n";
    return nullptr;
}

}  // namespace cpipe::compute
