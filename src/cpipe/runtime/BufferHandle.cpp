// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/BufferHandle.hpp"

#include <algorithm>
#include <cstdint>

#include "RuntimeHandles.hpp"
#include "RuntimeSuites.hpp"

namespace cpipe::runtime {

BufferHandle::BufferHandle(compute::IBuffer& buffer) : native_(new cpipe_buffer_t{&buffer}) {}

BufferHandle::~BufferHandle() {
    delete native_;
}

auto BufferHandle::native() noexcept -> cpipe_buffer_t* {
    return native_;
}

auto BufferHandle::native() const noexcept -> const cpipe_buffer_t* {
    return native_;
}

namespace detail {

auto unwrap(cpipe_buffer_t* handle) noexcept -> compute::IBuffer* {
    return handle == nullptr ? nullptr : handle->buffer;
}

auto unwrap(const cpipe_buffer_t* handle) noexcept -> compute::IBuffer* {
    return handle == nullptr ? nullptr : handle->buffer;
}

namespace {

auto get_dims(const cpipe_buffer_t* buffer, std::uint8_t* ndim, std::uint32_t* out_dims) -> int {
    const auto* native = unwrap(buffer);
    if (native == nullptr || ndim == nullptr || out_dims == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    const auto& layout = native->layout();
    *ndim = layout.ndim;
    std::copy(layout.dims.begin(), layout.dims.end(), out_dims);
    return CPIPE_OK;
}

auto get_format(const cpipe_buffer_t* buffer, int* out) -> int {
    const auto* native = unwrap(buffer);
    if (native == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    *out = static_cast<int>(native->layout().format);
    return CPIPE_OK;
}

auto get_kind(const cpipe_buffer_t* buffer, int* out) -> int {
    const auto* native = unwrap(buffer);
    if (native == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    *out = static_cast<int>(native->layout().kind);
    return CPIPE_OK;
}

auto get_stride(const cpipe_buffer_t* buffer, std::uint64_t* out_stride) -> int {
    const auto* native = unwrap(buffer);
    if (native == nullptr || out_stride == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    const auto& layout = native->layout();
    std::copy(layout.stride.begin(), layout.stride.end(), out_stride);
    return CPIPE_OK;
}

auto get_color_role(const cpipe_buffer_t* buffer, const char** out) -> int {
    const auto* native = unwrap(buffer);
    if (native == nullptr || out == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    *out = native->color_role().data();
    return CPIPE_OK;
}

[[nodiscard]] auto to_cpu_access(int access) noexcept -> compute::IBuffer::CpuAccess {
    switch (access) {
        case 0:
            return compute::IBuffer::CpuAccess::Read;
        case 1:
            return compute::IBuffer::CpuAccess::Write;
        case 2:
            return compute::IBuffer::CpuAccess::ReadWrite;
        default:
            return compute::IBuffer::CpuAccess::Read;
    }
}

auto lock_cpu(cpipe_buffer_t* buffer, int access, void** ptr) -> int {
    auto* native = unwrap(buffer);
    if (native == nullptr || ptr == nullptr || access < 0 || access > 2) {
        return CPIPE_BAD_INDEX;
    }

    *ptr = native->lock_cpu(to_cpu_access(access));
    return *ptr == nullptr ? CPIPE_FAILED : CPIPE_OK;
}

auto unlock_cpu(cpipe_buffer_t* buffer) -> int {
    auto* native = unwrap(buffer);
    if (native == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    native->unlock_cpu();
    return CPIPE_OK;
}

auto flush_cpu_writes(cpipe_buffer_t* buffer) -> int {
    auto* native = unwrap(buffer);
    if (native == nullptr) {
        return CPIPE_BAD_INDEX;
    }

    native->flush_cpu_writes();
    return CPIPE_OK;
}

constexpr cpipe_buffer_suite_v1 kBufferSuite{&get_dims,   &get_format,      &get_kind,
                                             &get_stride, &get_color_role,  &lock_cpu,
                                             &unlock_cpu, &flush_cpu_writes};

}  // namespace

auto buffer_suite() noexcept -> const cpipe_buffer_suite_v1* {
    return &kBufferSuite;
}

}  // namespace detail

}  // namespace cpipe::runtime
