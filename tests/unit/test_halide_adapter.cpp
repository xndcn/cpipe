// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cpipe/runtime/InferenceContext.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba_layout() {
    return BufferLayout{
        .kind = BufferKind::Image2D,
        .format = PixelFormat::R8G8B8A8_UNORM,
        .ndim = 2,
        .dims = {8, 4},
        .stride = {},
    };
}

int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output) {
    if (input == nullptr || output == nullptr || input->dimensions != 1 ||
        output->dimensions != 1) {
        return CPIPE_FAILED;
    }

    const auto input_size = input->dim[0].extent;
    const auto output_size = output->dim[0].extent;
    if (input_size != output_size) {
        return CPIPE_FAILED;
    }

    std::memcpy(output->host, input->host, static_cast<std::size_t>(input_size));
    return CPIPE_OK;
}

}  // namespace

TEST_CASE("test_halide_adapter: maps CpuBuffer to byte-addressed Halide view") {
    CpuBuffer buffer(rgba_layout(),
                     BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::HalideBufferAdapter adapter(buffer, IBuffer::CpuAccess::ReadWrite);
    const auto& view = adapter.buffer();

    REQUIRE(view.host != nullptr);
    CHECK(view.dimensions == 1);
    CHECK(view.type == halide_type_of<std::uint8_t>());
    CHECK(view.dim[0].extent == 8 * 4 * 4);
    CHECK(view.dim[0].stride == 1);
}

TEST_CASE("test_halide_adapter: ComputeContext submit_halide invokes registered AOT entry") {
    CpuBuffer input(rgba_layout(),
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    CpuBuffer output(rgba_layout(),
                     BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* in_bytes = static_cast<std::uint8_t*>(input.lock_cpu(IBuffer::CpuAccess::ReadWrite));
    REQUIRE(in_bytes != nullptr);
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(input.size_bytes()));
    for (std::uint64_t i = 0; i < input.size_bytes(); ++i) {
        expected[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i & 0xffU);
        in_bytes[static_cast<std::size_t>(i)] = expected[static_cast<std::size_t>(i)];
    }
    input.unlock_cpu();

    cpipe::runtime::ComputeContext context;
    context.register_halide("passthrough_copy", &passthrough_copy);

    std::vector<const IBuffer*> inputs{&input};
    std::vector<IBuffer*> outputs{&output};
    REQUIRE(context.submit_halide("passthrough_copy", inputs, outputs) == CPIPE_OK);

    const auto* copied =
        static_cast<const std::uint8_t*>(output.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(copied != nullptr);
    CHECK(std::equal(copied, copied + static_cast<std::size_t>(output.size_bytes()),
                     expected.begin()));
    output.unlock_cpu();
}

TEST_CASE("test_halide_adapter: InferenceContext reports unsupported in P0") {
    cpipe::runtime::InferenceContext inference;
    CHECK(inference.submit("model", {}, {}) == CPIPE_UNSUPPORTED);
}
