// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/BufferUsage.hpp"
#include "cpipe/core/IBuffer.hpp"
#include "cpipe/core/PixelFormat.hpp"
#include "cpipe/runtime/BufferHandle.hpp"
#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/HalideBufferAdapter.hpp"
#include "cpipe/runtime/InferenceContext.hpp"
#include "cpipe/runtime/Registry.hpp"
#include "cpipe/runtime/TaskExecutor.hpp"

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output);

namespace {
auto make_rgba_layout() -> cpipe::compute::BufferLayout {
    cpipe::compute::BufferLayout layout{};
    layout.kind = cpipe::compute::BufferKind::Image2D;
    layout.format = cpipe::compute::PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 3;
    layout.dims[1] = 2;
    return layout;
}

void fill_ramp(cpipe::compute::CpuBuffer& buffer) {
    auto* ptr =
        static_cast<std::uint8_t*>(buffer.lock_cpu(cpipe::compute::IBuffer::CpuAccess::ReadWrite));
    REQUIRE(ptr != nullptr);
    for (std::uint64_t index = 0; index < buffer.size_bytes(); ++index) {
        ptr[index] = static_cast<std::uint8_t>(index + 17U);
    }
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

}  // namespace

TEST_CASE("HalideBufferAdapter exposes CpuBuffer RGBA layout as Halide dimensions") {
    using cpipe::compute::BufferUsage;
    using cpipe::compute::CpuBuffer;
    using cpipe::compute::IBuffer;

    CpuBuffer input(make_rgba_layout(),
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    fill_ramp(input);

    cpipe::runtime::HalideBufferAdapter adapter{input, IBuffer::CpuAccess::Read};

    REQUIRE(adapter.status() == CPIPE_OK);
    auto* halide = adapter.get();
    REQUIRE(halide != nullptr);
    CHECK(halide->type == halide_type_t(halide_type_uint, 8));
    REQUIRE(halide->dimensions == 3);
    CHECK(halide->dim[0].extent == 4);
    CHECK(halide->dim[0].stride == 1);
    CHECK(halide->dim[1].extent == 3);
    CHECK(halide->dim[1].stride == 4);
    CHECK(halide->dim[2].extent == 2);
    CHECK(halide->dim[2].stride == 12);
}

TEST_CASE("ComputeContext submits passthrough_copy and inference remains unsupported") {
    using cpipe::compute::BufferUsage;
    using cpipe::compute::CpuBuffer;
    using cpipe::compute::IBuffer;

    CpuBuffer input(make_rgba_layout(),
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    CpuBuffer output(make_rgba_layout(),
                     BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    fill_ramp(input);

    cpipe::runtime::BufferHandle input_handle{input};
    cpipe::runtime::BufferHandle output_handle{output};
    std::array<const cpipe_buffer_t*, 1> inputs{input_handle.native()};
    std::array<cpipe_buffer_t*, 1> outputs{output_handle.native()};

    const std::array<cpipe::runtime::HalideFilter, 1> filters{
        {{"passthrough_copy", &passthrough_copy}}};
    cpipe::runtime::TaskExecutor executor{2};
    cpipe::runtime::ComputeContext compute{executor, filters};
    cpipe::runtime::InferenceContext inference;
    auto host = cpipe::runtime::make_host();

    const auto* compute_suite =
        static_cast<const cpipe_compute_suite_v1*>(host.get_suite(&host, "compute", 1));
    REQUIRE(compute_suite != nullptr);
    const auto before_tasks = executor.halide_tasks_dispatched();
    CHECK(compute_suite->submit_halide(compute.native(), "passthrough_copy", inputs.data(),
                                       inputs.size(), outputs.data(), outputs.size()) == CPIPE_OK);
    CHECK(executor.halide_tasks_dispatched() >= before_tasks + make_rgba_layout().dims[1]);

    const auto* input_ptr =
        static_cast<const std::uint8_t*>(input.lock_cpu(IBuffer::CpuAccess::Read));
    const auto* output_ptr =
        static_cast<const std::uint8_t*>(output.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(input_ptr != nullptr);
    REQUIRE(output_ptr != nullptr);
    CHECK(std::memcmp(input_ptr, output_ptr, static_cast<std::size_t>(input.size_bytes())) == 0);
    output.unlock_cpu();
    input.unlock_cpu();

    const auto* inference_suite =
        static_cast<const cpipe_inference_suite_v1*>(host.get_suite(&host, "inference", 1));
    REQUIRE(inference_suite != nullptr);
    CHECK(inference_suite->submit_inference(inference.native(), "missing", nullptr, 0, nullptr,
                                            0) == CPIPE_UNSUPPORTED);
}
