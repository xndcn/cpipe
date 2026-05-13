// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/sdk/cpipe_node.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace cpipe::tests {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferMetadata;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::Matrix3;
using cpipe::compute::PixelFormat;

inline BufferLayout rgba16_layout(std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R16G16B16A16_SFLOAT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

inline std::uint16_t float_to_half(float value) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    const auto half = static_cast<_Float16>(value);
    std::uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

inline float half_to_float(std::uint16_t bits) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    _Float16 half = 0.0F;
    std::memcpy(&half, &bits, sizeof(bits));
    return static_cast<float>(half);
}

inline void write_rgba16(CpuBuffer& buffer, const std::vector<std::array<float, 4>>& pixels) {
    auto* out = static_cast<std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        for (std::size_t c = 0; c < pixels[i].size(); ++c) {
            out[(i * 4U) + c] = float_to_half(pixels[i][c]);
        }
    }
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

inline std::vector<std::array<float, 4>> read_rgba16(CpuBuffer& buffer, std::size_t pixel_count) {
    const auto* in = static_cast<const std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    std::vector<std::array<float, 4>> pixels(pixel_count);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        for (std::size_t c = 0; c < pixels[i].size(); ++c) {
            pixels[i][c] = half_to_float(in[(i * 4U) + c]);
        }
    }
    buffer.unlock_cpu();
    return pixels;
}

inline cpipe_status_t process_single_input_node(const cpipe_plugin_desc_t& desc,
                                                const std::shared_ptr<CpuBuffer>& input,
                                                const std::shared_ptr<CpuBuffer>& output) {
    cpipe::runtime::HostContext host_context;
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                            &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(input->metadata(), {input->metadata()});
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = nullptr,
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        nullptr, &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

}  // namespace cpipe::tests
