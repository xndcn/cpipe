// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include "cpipe/runtime/HalideBufferAdapter.hpp"

#include "cpipe/core/CpuBuffer.hpp"

namespace cpipe::runtime {

Result<HalideBufferAdapter> HalideBufferAdapter::from_buffer(compute::IBuffer& buffer) {
    auto* cpu = dynamic_cast<compute::CpuBuffer*>(&buffer);
    if (cpu == nullptr) {
        return tl::unexpected(make_error(StatusCode::Unsupported, "only CpuBuffer is supported"));
    }

    const auto& layout = cpu->layout();
    if (layout.kind != compute::BufferKind::Image2D ||
        layout.format != compute::PixelFormat::R8G8B8A8_UNORM) {
        return tl::unexpected(
            make_error(StatusCode::Unsupported, "only R8G8B8A8 Image2D is supported"));
    }

    HalideBufferAdapter adapter;
    adapter.dims_.resize(2);
    adapter.dims_[0] = halide_dimension_t{0, static_cast<int32_t>(layout.dims[0]), 1, 0};
    adapter.dims_[1] = halide_dimension_t{0, static_cast<int32_t>(layout.dims[1]),
                                          static_cast<int32_t>(layout.stride[1] / 4u), 0};
    adapter.buffer_.device = 0;
    adapter.buffer_.device_interface = nullptr;
    adapter.buffer_.host = static_cast<uint8_t*>(cpu->data());
    adapter.buffer_.flags = 0;
    adapter.buffer_.type = halide_type_t{halide_type_uint, 32, 1};
    adapter.buffer_.dimensions = static_cast<int32_t>(adapter.dims_.size());
    adapter.buffer_.dim = adapter.dims_.data();
    adapter.buffer_.padding = nullptr;
    return adapter;
}

halide_buffer_t* HalideBufferAdapter::get() noexcept {
    return &buffer_;
}

const halide_buffer_t* HalideBufferAdapter::get() const noexcept {
    return &buffer_;
}

}  // namespace cpipe::runtime
