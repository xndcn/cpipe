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

int passthrough_copy(const cpipe::runtime::HalideBufferView* const* inputs, std::size_t n_in,
                     cpipe::runtime::HalideBufferView* const* outputs, std::size_t n_out) {
    if (n_in != 1 || n_out != 1) {
        return CPIPE_BAD_INDEX;
    }

    const auto* in = inputs[0];
    auto* out = outputs[0];
    if (in == nullptr || out == nullptr || in->size_bytes != out->size_bytes) {
        return CPIPE_FAILED;
    }

    std::memcpy(out->host, in->host, static_cast<std::size_t>(in->size_bytes));
    return CPIPE_OK;
}

}  // namespace

TEST_CASE("test_halide_adapter: maps CpuBuffer to byte-addressed Halide view") {
    CpuBuffer buffer(rgba_layout(),
                     BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::HalideBufferAdapter adapter(buffer, IBuffer::CpuAccess::ReadWrite);
    const auto& view = adapter.view();

    REQUIRE(view.host != nullptr);
    CHECK(view.ndim == 2);
    CHECK(view.dim[0].extent == 8 * 4);
    CHECK(view.dim[0].stride == 1);
    CHECK(view.dim[1].extent == 4);
    CHECK(view.dim[1].stride == 8 * 4);
    CHECK(view.size_bytes == 8 * 4 * 4);
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
