// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cassert>
#include <cpipe/core/CpuBuffer.hpp>
#include <cstdlib>
#include <iostream>
#include <new>
#include <utility>

namespace cpipe::compute {
namespace {

[[nodiscard]] auto access_allowed(BufferUsage usage, IBuffer::CpuAccess access) noexcept -> bool {
    switch (access) {
        case IBuffer::CpuAccess::Read:
            return has_usage(usage, BufferUsage::CpuRead);
        case IBuffer::CpuAccess::Write:
            return has_usage(usage, BufferUsage::CpuWrite);
        case IBuffer::CpuAccess::ReadWrite:
            return has_usage(usage, BufferUsage::CpuRead) &&
                   has_usage(usage, BufferUsage::CpuWrite);
    }
    return false;
}

}  // namespace

auto CpuBuffer::create(BufferLayout layout, BufferUsage usage, std::string color_role)
    -> std::unique_ptr<CpuBuffer> {
    const auto size = layout.size_bytes();
    if (size == 0U) {
        return nullptr;
    }

    void* data = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    if (posix_memalign(&data, kAlignment, size) != 0) {
        return nullptr;
    }

    auto* buffer = new (std::nothrow) CpuBuffer(layout, usage, std::move(color_role), data, size);
    if (buffer == nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
        std::free(data);
        return nullptr;
    }
    return std::unique_ptr<CpuBuffer>{buffer};
}

CpuBuffer::CpuBuffer(BufferLayout layout, BufferUsage usage, std::string color_role, void* data,
                     std::uint64_t size_bytes) noexcept
    : layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      data_(data),
      size_bytes_(size_bytes) {}

CpuBuffer::~CpuBuffer() {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
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

auto CpuBuffer::usage() const noexcept -> BufferUsage {
    return usage_;
}

auto CpuBuffer::data() noexcept -> void* {
    return data_;
}

auto CpuBuffer::data() const noexcept -> const void* {
    return data_;
}

auto CpuBuffer::lock_cpu(CpuAccess access) -> void* {
    if (locked_ || !access_allowed(usage_, access)) {
        return nullptr;
    }
    locked_ = true;
    return data_;
}

auto CpuBuffer::unlock_cpu() -> void {
    assert(locked_);
    locked_ = false;
}

auto CpuBuffer::flush_cpu_writes() -> void {}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto CpuBuffer::sub_view(std::uint32_t origin_x, std::uint32_t origin_y, std::uint32_t width,
                         std::uint32_t height) -> std::shared_ptr<IBuffer> {
    (void)origin_x;
    (void)origin_y;
    (void)width;
    (void)height;
    std::clog << "cpipe::compute::CpuBuffer::sub_view is not implemented in v1\n";
    return nullptr;
}

}  // namespace cpipe::compute
