// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cpipe/runtime/InferenceContext.hpp>
#include <cstddef>
#include <cstring>
#include <memory>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

int test_halide_copy(halide_buffer_t* input, halide_buffer_t* output) {
    std::memcpy(output->host, input->host,
                static_cast<std::size_t>(input->dim[1].extent) *
                    static_cast<std::size_t>(input->dim[1].stride));
    return 0;
}

BufferLayout rgba_layout(std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

}  // namespace

TEST_CASE("HalideBufferAdapter exposes interleaved RGBA image shape") {
    CpuBuffer buffer{rgba_layout(8, 4), BufferUsage::Input | BufferUsage::CpuRead};

    cpipe::runtime::HalideBufferAdapter adapter{buffer, IBuffer::CpuAccess::Read};
    const auto* halide = adapter.get();

    REQUIRE(halide->host != nullptr);
    REQUIRE(halide->type.code == halide_type_uint);
    REQUIRE(halide->type.bits == 8);
    REQUIRE(halide->dimensions == 3);
    REQUIRE(halide->dim[0].extent == 8);
    REQUIRE(halide->dim[0].stride == 4);
    REQUIRE(halide->dim[1].extent == 4);
    REQUIRE(halide->dim[1].stride == 32);
    REQUIRE(halide->dim[2].extent == 4);
    REQUIRE(halide->dim[2].stride == 1);
}

TEST_CASE("ComputeContext submit_halide copies CpuBuffer bytes") {
    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(8, 4), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(8, 4), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* input_bytes = static_cast<std::byte*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    for (std::uint64_t i = 0; i < input->size_bytes(); ++i) {
        input_bytes[i] = static_cast<std::byte>(i & 0xffU);
    }
    input->unlock_cpu();
    input->flush_cpu_writes();

    cpipe::runtime::ComputeContext compute;
    compute.register_halide_filter("passthrough_copy", &test_halide_copy);

    REQUIRE(compute.submit_halide("passthrough_copy", {input}, {output}) == CPIPE_OK);

    const auto* in = static_cast<const std::byte*>(input->lock_cpu(IBuffer::CpuAccess::Read));
    const auto* out = static_cast<const std::byte*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::memcmp(in, out, static_cast<std::size_t>(input->size_bytes())) == 0);
    output->unlock_cpu();
    input->unlock_cpu();
}

TEST_CASE("InferenceContext returns unsupported in P0") {
    cpipe::runtime::InferenceContext inference;
    REQUIRE(inference.submit("model", {}, {}) == CPIPE_UNSUPPORTED);
}
