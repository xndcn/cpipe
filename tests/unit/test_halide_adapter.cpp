// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HalideBufferAdapter.hpp>
#include <cpipe/runtime/InferenceContext.hpp>
#include <cstdint>
#include <span>
#include <vector>

#include "test_halide_passthrough.h"

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::PixelFormat;

auto make_rgba_layout() -> BufferLayout {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 3;
    layout.dims[1] = 2;
    return layout;
}

auto fill_buffer(CpuBuffer& buffer) -> std::vector<std::byte> {
    auto expected = std::vector<std::byte>(buffer.size_bytes());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::byte>((index * 13U) & 0xFFU);
    }

    auto* ptr = static_cast<std::byte*>(buffer.lock_cpu(cpipe::compute::IBuffer::CpuAccess::Write));
    REQUIRE(ptr != nullptr);
    std::copy(expected.begin(), expected.end(), ptr);
    buffer.unlock_cpu();
    return expected;
}

auto read_buffer(CpuBuffer& buffer) -> std::vector<std::byte> {
    auto actual = std::vector<std::byte>(buffer.size_bytes());
    auto* ptr = static_cast<std::byte*>(buffer.lock_cpu(cpipe::compute::IBuffer::CpuAccess::Read));
    REQUIRE(ptr != nullptr);
    std::copy_n(ptr, actual.size(), actual.begin());
    buffer.unlock_cpu();
    return actual;
}

auto call_test_passthrough_copy(std::span<halide_buffer_t* const> inputs,
                                std::span<halide_buffer_t* const> outputs) -> int {
    if (inputs.size() != 1U || outputs.size() != 1U || inputs[0] == nullptr ||
        outputs[0] == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    return test_halide_passthrough(inputs[0], outputs[0]);
}

struct ParForState {
    std::atomic<int>* sum;
};

auto par_for_task(void*, int task_number, std::uint8_t* closure) -> int {
    auto* state = reinterpret_cast<ParForState*>(closure);  // NOLINT
    state->sum->fetch_add(task_number, std::memory_order_relaxed);
    return 0;
}

}  // namespace

TEST_CASE("ComputeContext submits a Halide AOT entry point over CpuBuffer handles") {
    auto input =
        CpuBuffer::create(make_rgba_layout(), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output =
        CpuBuffer::create(make_rgba_layout(), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);

    const auto expected = fill_buffer(*input);

    auto compute = cpipe::runtime::ComputeContext{};
    compute.register_halide("passthrough_copy", &call_test_passthrough_copy);

    const cpipe_buffer_t* inputs[] = {cpipe::runtime::as_cpipe_buffer(*input)};
    cpipe_buffer_t* outputs[] = {cpipe::runtime::as_cpipe_buffer(*output)};
    const auto& suite = cpipe::runtime::compute_suite_v1();

    CHECK(suite.submit_halide(compute.handle(), "passthrough_copy", inputs, 1, outputs, 1) ==
          CPIPE_OK);
    CHECK(read_buffer(*output) == expected);
}

TEST_CASE("ComputeContext installs the Halide custom parallel-for hook") {
    auto compute = cpipe::runtime::ComputeContext{};
    auto sum = std::atomic<int>{0};
    auto state = ParForState{&sum};

    const auto before = cpipe::runtime::halide_custom_parallel_for_calls();
    CHECK(halide_do_par_for(nullptr, &par_for_task, 0, 8,
                            reinterpret_cast<std::uint8_t*>(&state)) == 0);  // NOLINT

    CHECK(sum.load(std::memory_order_relaxed) == 28);
    CHECK(cpipe::runtime::halide_custom_parallel_for_calls() == before + 1U);
    CHECK(compute.executor_worker_count() >= 1U);
}

TEST_CASE("runtime inference suite reports unsupported submissions") {
    auto inference = cpipe::runtime::InferenceContext{};
    const auto& suite = cpipe::runtime::inference_suite_v1();

    CHECK(suite.submit_inference(inference.handle(), "model", nullptr, 0, nullptr, 0) ==
          CPIPE_UNSUPPORTED);
}
