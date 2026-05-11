// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cstdint>

namespace cpipe::runtime {
namespace {

[[nodiscard]] std::int32_t to_i32(std::uint64_t value) noexcept {
    return static_cast<std::int32_t>(value);
}

}  // namespace

HalideBufferAdapter::HalideBufferAdapter(compute::IBuffer& buffer,
                                         compute::IBuffer::CpuAccess access)
    : buffer_(buffer), access_(access) {
    auto* host = static_cast<std::uint8_t*>(buffer_.lock_cpu(access_));
    locked_ = host != nullptr;

    dims_[0] = halide_dimension_t{0, to_i32(buffer_.size_bytes()), 1};
    halide_buffer_.device = 0;
    halide_buffer_.device_interface = nullptr;
    halide_buffer_.host = host;
    halide_buffer_.flags = 0;
    halide_buffer_.type = halide_type_of<std::uint8_t>();
    halide_buffer_.dimensions = 1;
    halide_buffer_.dim = dims_.data();
    halide_buffer_.padding = nullptr;
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

halide_buffer_t& HalideBufferAdapter::buffer() noexcept {
    return halide_buffer_;
}

const halide_buffer_t& HalideBufferAdapter::buffer() const noexcept {
    return halide_buffer_;
}

}  // namespace cpipe::runtime
