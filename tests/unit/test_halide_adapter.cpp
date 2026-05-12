// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "RuntimeHandles.hpp"
#include "cpipe/core/CpuBuffer.hpp"
#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/HalideBufferAdapter.hpp"

using namespace cpipe::compute;

TEST_CASE("Halide adapter exposes RGBA8 interleaved dims and strides") {
    auto buffer =
        CpuBuffer::create(make_rgba8_layout(4, 3), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(buffer.has_value());

    auto adapter = cpipe::runtime::HalideBufferAdapter::from_buffer(**buffer);
    REQUIRE(adapter.has_value());
    REQUIRE(adapter->get()->dimensions == 2);
    REQUIRE(adapter->get()->dim[0].extent == 4);
    REQUIRE(adapter->get()->dim[0].stride == 1);
    REQUIRE(adapter->get()->dim[1].extent == 3);
    REQUIRE(adapter->get()->dim[1].stride == 4);
}

TEST_CASE("ComputeContext invokes passthrough Halide AOT") {
    auto input =
        CpuBuffer::create(make_rgba8_layout(4, 4), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output =
        CpuBuffer::create(make_rgba8_layout(4, 4), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(input.has_value());
    REQUIRE(output.has_value());

    auto* input_ptr = (*input)->lock_cpu(IBuffer::CpuAccess::Write);
    for (uint64_t i = 0; i < (*input)->size_bytes(); ++i) {
        static_cast<uint8_t*>(input_ptr)[i] = static_cast<uint8_t>(i);
    }
    (*input)->unlock_cpu();

    cpipe_buffer_t in{*input};
    cpipe_buffer_t out{*output};
    const cpipe_buffer_t* inputs[] = {&in};
    cpipe_buffer_t* outputs[] = {&out};

    cpipe::runtime::ComputeContext compute;
    REQUIRE(compute.submit_halide("passthrough_copy", inputs, 1, outputs, 1) == CPIPE_OK);
    REQUIRE(std::memcmp((*input)->data(), (*output)->data(),
                        static_cast<std::size_t>((*input)->size_bytes())) == 0);
}
