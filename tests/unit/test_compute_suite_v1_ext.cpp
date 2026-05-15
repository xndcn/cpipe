// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
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

int test_scale_by_param(halide_buffer_t* const* inputs, std::size_t n_inputs,
                        halide_buffer_t* const* outputs, std::size_t n_outputs,
                        const void* param_blob, std::size_t param_blob_size) {
    if (inputs == nullptr || outputs == nullptr || n_inputs != 1 || n_outputs != 1 ||
        inputs[0] == nullptr || outputs[0] == nullptr || param_blob == nullptr ||
        param_blob_size != sizeof(float)) {
        return CPIPE_BAD_INDEX;
    }
    const auto scale = *static_cast<const float*>(param_blob);
    const auto* input = inputs[0];
    auto* output = outputs[0];
    const auto* in = reinterpret_cast<const float*>(input->host);
    auto* out = reinterpret_cast<float*>(output->host);
    for (int y = 0; y < input->dim[1].extent; ++y) {
        for (int x = 0; x < input->dim[0].extent; ++x) {
            const auto offset = (y * input->dim[1].stride) + (x * input->dim[0].stride);
            out[offset] = in[offset] * scale;
        }
    }
    return CPIPE_OK;
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

BufferLayout r32_layout(std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R32_SFLOAT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

BufferLayout rgba32_layout(std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R32G32B32A32_SFLOAT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

}  // namespace

TEST_CASE("ComputeContext keeps imperative Halide registration as a test seam") {
    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(4, 4), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(4, 4), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* input_bytes = static_cast<std::byte*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    for (std::uint64_t i = 0; i < input->size_bytes(); ++i) {
        input_bytes[i] = static_cast<std::byte>((i * 13U) & 0xffU);
    }
    input->unlock_cpu();
    input->flush_cpu_writes();

    cpipe::runtime::ComputeContext compute;
    compute.register_halide_filter("test_halide_copy", &test_halide_copy);

    REQUIRE(compute.submit_halide("test_halide_copy", {input}, {output}) == CPIPE_OK);

    const auto* in = static_cast<const std::byte*>(input->lock_cpu(IBuffer::CpuAccess::Read));
    const auto* out = static_cast<const std::byte*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::memcmp(in, out, static_cast<std::size_t>(input->size_bytes())) == 0);
    output->unlock_cpu();
    input->unlock_cpu();
}

TEST_CASE("Host exposes compute suite v1 tail extensions") {
    cpipe::runtime::HostContext host_context;
    auto* host = host_context.host();
    REQUIRE(host->abi_minor == CPIPE_ABI_MINOR);
    REQUIRE(host->abi_minor == 3);

    const auto* suite =
        static_cast<const cpipe_compute_suite_v1*>(host->get_suite(host, "compute", 1));
    REQUIRE(suite != nullptr);
    REQUIRE(suite->submit_halide != nullptr);
    REQUIRE(suite->submit_halide_with_params != nullptr);
    REQUIRE(suite->submit_ocio_processor != nullptr);
    REQUIRE(host->get_ocio_processor != nullptr);
}

TEST_CASE("Compute suite v1 submits parameterized Halide filters") {
    auto input = std::make_shared<CpuBuffer>(
        r32_layout(2, 2), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output = std::make_shared<CpuBuffer>(
        r32_layout(2, 2), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* input_values = static_cast<float*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    input_values[0] = 1.0F;
    input_values[1] = 2.0F;
    input_values[2] = 3.0F;
    input_values[3] = 4.0F;
    input->unlock_cpu();
    input->flush_cpu_writes();

    cpipe::runtime::ComputeContext compute;
    compute.register_halide_param_filter("test_scale_by_param", &test_scale_by_param);
    cpipe::runtime::HostContext host_context;
    auto* host = host_context.host();
    const auto* suite =
        static_cast<const cpipe_compute_suite_v1*>(host->get_suite(host, "compute", 1));
    REQUIRE(suite != nullptr);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    const float scale = 2.5F;

    REQUIRE(suite->submit_halide_with_params(reinterpret_cast<cpipe_compute_t*>(&compute),
                                             "test_scale_by_param", inputs, 1, outputs, 1, &scale,
                                             sizeof(scale)) == CPIPE_OK);

    const auto* out = static_cast<const float*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(out[0] == Catch::Approx(2.5F));
    REQUIRE(out[1] == Catch::Approx(5.0F));
    REQUIRE(out[2] == Catch::Approx(7.5F));
    REQUIRE(out[3] == Catch::Approx(10.0F));
    output->unlock_cpu();
}

TEST_CASE("Host OCIO accessor returns a processor for the bundled v0.1 config") {
    cpipe::runtime::HostContext host_context;
    auto* host = host_context.host();
    const auto* suite =
        static_cast<const cpipe_compute_suite_v1*>(host->get_suite(host, "compute", 1));
    REQUIRE(suite != nullptr);

    const auto config_path =
        std::filesystem::path{CPIPE_SOURCE_DIR} / "share/cpipe/ocio/v0.1/config.ocio";
    auto* processor = host->get_ocio_processor(host, config_path.string().c_str(),
                                               "scene_linear_rec2020", "output_srgb");
    REQUIRE(processor != nullptr);

    auto input = std::make_shared<CpuBuffer>(
        rgba32_layout(1, 1), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output = std::make_shared<CpuBuffer>(
        rgba32_layout(1, 1), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto* input_values = static_cast<float*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    input_values[0] = 0.18F;
    input_values[1] = 0.20F;
    input_values[2] = 0.22F;
    input_values[3] = 1.0F;
    input->unlock_cpu();
    input->flush_cpu_writes();

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    REQUIRE(suite->submit_ocio_processor(nullptr, processor, input_handle.get(),
                                         output_handle.get()) == CPIPE_OK);

    const auto* out = static_cast<const float*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::isfinite(out[0]));
    REQUIRE(std::isfinite(out[1]));
    REQUIRE(std::isfinite(out[2]));
    REQUIRE(out[3] == Catch::Approx(1.0F));
    output->unlock_cpu();
}
