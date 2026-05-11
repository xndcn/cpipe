// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cstddef>

namespace cpipe::runtime {
namespace {

[[nodiscard]] std::uint64_t row_stride_bytes(const compute::BufferLayout& layout) noexcept {
    if (layout.ndim >= 2 && layout.stride[1] != 0) {
        return layout.stride[1];
    }
    return static_cast<std::uint64_t>(layout.dims[0]) * compute::bytes_per_pixel(layout.format);
}

[[nodiscard]] std::int32_t to_i32(std::uint64_t value) noexcept {
    return static_cast<std::int32_t>(value);
}

}  // namespace

HalideBufferAdapter::HalideBufferAdapter(compute::IBuffer& buffer,
                                         compute::IBuffer::CpuAccess access)
    : buffer_(buffer), access_(access) {
    view_.host = static_cast<std::uint8_t*>(buffer_.lock_cpu(access_));
    locked_ = view_.host != nullptr;
    view_.format = buffer_.layout().format;
    view_.size_bytes = buffer_.size_bytes();

    const auto& layout = buffer_.layout();
    if (layout.kind == compute::BufferKind::Image2D && layout.ndim == 2) {
        const auto row_bytes = row_stride_bytes(layout);
        view_.ndim = 2;
        view_.dim[0].extent = to_i32(static_cast<std::uint64_t>(layout.dims[0]) *
                                     compute::bytes_per_pixel(layout.format));
        view_.dim[0].stride = 1;
        view_.dim[1].extent = to_i32(layout.dims[1]);
        view_.dim[1].stride = to_i32(row_bytes);
        return;
    }

    view_.ndim = 1;
    view_.dim[0].extent = to_i32(view_.size_bytes);
    view_.dim[0].stride = 1;
}

HalideBufferAdapter::~HalideBufferAdapter() {
    if (!locked_) {
        return;
    }
    buffer_.unlock_cpu();
    if (access_ != compute::IBuffer::CpuAccess::Read) {
        buffer_.flush_cpu_writes();
    }
}

HalideBufferView& HalideBufferAdapter::view() noexcept {
    return view_;
}

const HalideBufferView& HalideBufferAdapter::view() const noexcept {
    return view_;
}

}  // namespace cpipe::runtime
