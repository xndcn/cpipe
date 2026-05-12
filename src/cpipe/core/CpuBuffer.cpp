// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/core/CpuBuffer.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>

namespace cpipe::compute {

Result<std::shared_ptr<CpuBuffer>> CpuBuffer::create(BufferLayout layout, BufferUsage usage,
                                                     std::string color_role) {
    layout = make_default_strides(layout);
    if (!layout.is_valid()) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, "invalid buffer layout"));
    }

    const uint64_t size = layout.size_bytes();
    if (size == 0) {
        return tl::unexpected(make_error(StatusCode::InvalidArgument, "buffer has zero byte size"));
    }

    void* data = nullptr;
    const auto aligned_size =
        static_cast<std::size_t>((size + kAlignment - 1u) & ~(kAlignment - 1u));
    if (posix_memalign(&data, kAlignment, aligned_size) != 0) {
        return tl::unexpected(make_error(StatusCode::OutOfMemory, "posix_memalign failed"));
    }
    std::memset(data, 0, aligned_size);

    return std::shared_ptr<CpuBuffer>(
        new CpuBuffer(layout, usage, std::move(color_role), data, size));
}

CpuBuffer::CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role, void* data,
                     uint64_t size)
    : layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      data_(data),
      size_(size) {}

CpuBuffer::~CpuBuffer() {
    std::free(data_);
}

const BufferLayout& CpuBuffer::layout() const noexcept {
    return layout_;
}

uint64_t CpuBuffer::size_bytes() const noexcept {
    return size_;
}

std::string_view CpuBuffer::color_role() const noexcept {
    return color_role_;
}

BufferUsage CpuBuffer::usage() const noexcept {
    return usage_;
}

bool CpuBuffer::is_locked() const noexcept {
    return locked_;
}

void* CpuBuffer::data() noexcept {
    return data_;
}

const void* CpuBuffer::data() const noexcept {
    return data_;
}

void* CpuBuffer::lock_cpu(CpuAccess access) {
    const bool wants_read = access == CpuAccess::Read || access == CpuAccess::ReadWrite;
    const bool wants_write = access == CpuAccess::Write || access == CpuAccess::ReadWrite;
    if ((wants_read && !has_usage(usage_, BufferUsage::CpuRead)) ||
        (wants_write && !has_usage(usage_, BufferUsage::CpuWrite))) {
        spdlog::warn("CpuBuffer lock_cpu rejected incompatible access");
        return nullptr;
    }
    if (locked_) {
        spdlog::warn("CpuBuffer lock_cpu called while already locked");
        return nullptr;
    }
    locked_ = true;
    return data_;
}

void CpuBuffer::unlock_cpu() {
    if (!locked_) {
        spdlog::warn("CpuBuffer unlock_cpu called without a matching lock");
        return;
    }
    locked_ = false;
}

void CpuBuffer::flush_cpu_writes() {}

std::shared_ptr<IBuffer> CpuBuffer::sub_view(uint32_t, uint32_t, uint32_t, uint32_t) {
    spdlog::warn("IBuffer::sub_view is not implemented in v1");
    return nullptr;
}

}  // namespace cpipe::compute
