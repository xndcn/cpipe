// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cstdint>

namespace {

halide_type_t halide_type_for(cpipe::compute::PixelFormat format) {
    using cpipe::compute::PixelFormat;

    switch (format) {
        case PixelFormat::R16G16B16A16_SFLOAT:
        case PixelFormat::R16G16B16_SFLOAT:
        case PixelFormat::F16:
            return halide_type_t{halide_type_float, 16};
        case PixelFormat::R32G32B32A32_SFLOAT:
        case PixelFormat::R32G32B32_SFLOAT:
        case PixelFormat::R32_SFLOAT:
        case PixelFormat::F32:
            return halide_type_t{halide_type_float, 32};
        case PixelFormat::I8:
            return halide_type_t{halide_type_int, 8};
        case PixelFormat::R16G16B16A16_UNORM:
        case PixelFormat::R16_UINT:
            return halide_type_t{halide_type_uint, 16};
        default:
            return halide_type_t{halide_type_uint, 8};
    }
}

}  // namespace

namespace cpipe::runtime {

HalideBufferAdapter::HalideBufferAdapter(compute::IBuffer& buffer,
                                         compute::IBuffer::CpuAccess access)
    : buffer_(buffer), access_(access) {
    const auto& layout = buffer_.layout();
    auto* host = static_cast<std::uint8_t*>(buffer_.lock_cpu(access_));
    const auto channels = compute::channel_count(layout.format);
    const auto channel_bytes = compute::bytes_per_channel(layout.format);

    halide_.host = host;
    halide_.type = halide_type_for(layout.format);
    halide_.flags = access_ == compute::IBuffer::CpuAccess::Write
                        ? static_cast<std::uint64_t>(halide_buffer_flag_host_dirty)
                        : 0U;
    halide_.dim = dimensions_.data();

    if (layout.kind == compute::BufferKind::Image2D && channels > 1) {
        halide_.dimensions = 3;
        const auto pixel_stride =
            layout.stride[0] != 0 ? layout.stride[0] : channel_bytes * channels;
        const auto row_stride =
            layout.stride[1] != 0 ? layout.stride[1] : pixel_stride * layout.dims[0];
        dimensions_[0] =
            halide_dimension_t{0, static_cast<std::int32_t>(layout.dims[0]),
                               static_cast<std::int32_t>(pixel_stride / channel_bytes)};
        dimensions_[1] = halide_dimension_t{0, static_cast<std::int32_t>(layout.dims[1]),
                                            static_cast<std::int32_t>(row_stride / channel_bytes)};
        dimensions_[2] = halide_dimension_t{0, static_cast<std::int32_t>(channels), 1};
        return;
    }

    halide_.dimensions = static_cast<std::int32_t>(layout.ndim);
    std::int32_t dense_stride = 1;
    for (std::uint8_t i = 0; i < layout.ndim; ++i) {
        const auto stride = layout.stride[i] != 0
                                ? static_cast<std::int32_t>(layout.stride[i] / channel_bytes)
                                : dense_stride;
        dimensions_[i] = halide_dimension_t{0, static_cast<std::int32_t>(layout.dims[i]), stride};
        dense_stride = stride * static_cast<std::int32_t>(layout.dims[i]);
    }
}

HalideBufferAdapter::~HalideBufferAdapter() {
    buffer_.unlock_cpu();
    if (access_ == compute::IBuffer::CpuAccess::Write ||
        access_ == compute::IBuffer::CpuAccess::ReadWrite) {
        buffer_.flush_cpu_writes();
    }
}

halide_buffer_t* HalideBufferAdapter::get() noexcept {
    return &halide_;
}

const halide_buffer_t* HalideBufferAdapter::get() const noexcept {
    return &halide_;
}

}  // namespace cpipe::runtime
