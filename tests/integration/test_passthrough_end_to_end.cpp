// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "cpipe/core/CpuBuffer.hpp"
#include "cpipe/runtime/Pipeline.hpp"

using namespace cpipe::compute;

TEST_CASE("Passthrough pipeline runs registry to Halide end-to-end") {
    auto pipeline = cpipe::runtime::Pipeline::load("tests/fixtures/passthrough.json");
    REQUIRE(pipeline.has_value());

    auto input =
        CpuBuffer::create(make_rgba8_layout(64, 64), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(input.has_value());

    auto* input_ptr = (*input)->lock_cpu(IBuffer::CpuAccess::Write);
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            const auto offset = static_cast<std::size_t>((y * 64 + x) * 4);
            static_cast<uint8_t*>(input_ptr)[offset + 0] = static_cast<uint8_t>(x);
            static_cast<uint8_t*>(input_ptr)[offset + 1] = static_cast<uint8_t>(y);
            static_cast<uint8_t*>(input_ptr)[offset + 2] = static_cast<uint8_t>((x + y) & 0xffu);
            static_cast<uint8_t*>(input_ptr)[offset + 3] = 255;
        }
    }
    (*input)->unlock_cpu();
    (*input)->flush_cpu_writes();

    auto output = pipeline->run(*input);
    REQUIRE(output.has_value());
    REQUIRE((*output)->size_bytes() == (*input)->size_bytes());
    REQUIRE(std::memcmp((*input)->data(), (*output)->data(),
                        static_cast<std::size_t>((*input)->size_bytes())) == 0);
}
