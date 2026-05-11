// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <spdlog/spdlog.h>

#include <cpipe/core/CpuBuffer.hpp>
#include <cstdlib>
#include <new>

namespace cpipe::compute {

CpuBuffer::CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role)
    : layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      size_bytes_(layout_.size_bytes()) {
    if (size_bytes_ == 0) {
        return;
    }

    void* data = nullptr;
    if (posix_memalign(&data, 64, static_cast<std::size_t>(size_bytes_)) != 0) {
        throw std::bad_alloc();
    }
    data_ = data;
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (locked_ || !permits(access)) {
        return nullptr;
    }
    locked_ = true;
    return data_;
}

void CpuBuffer::unlock_cpu() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!locked_) {
        spdlog::warn("CpuBuffer::unlock_cpu called without a matching lock_cpu");
        return;
    }
    locked_ = false;
}

void CpuBuffer::flush_cpu_writes() {}

std::shared_ptr<IBuffer> CpuBuffer::sub_view(std::uint32_t, std::uint32_t, std::uint32_t,
                                             std::uint32_t) {
    spdlog::warn("IBuffer::sub_view is not implemented in v1");
    return nullptr;
}

bool CpuBuffer::permits(CpuAccess access) const noexcept {
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
