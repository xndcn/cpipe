// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferUsage.hpp>

using cpipe::compute::BufferUsage;

TEST_CASE("BufferUsage supports bitwise combinations") {
    const auto usage = BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite;

    REQUIRE(cpipe::compute::has_usage(usage, BufferUsage::Input));
    REQUIRE(cpipe::compute::has_usage(usage, BufferUsage::CpuRead));
    REQUIRE(cpipe::compute::has_usage(usage, BufferUsage::CpuWrite));
    REQUIRE_FALSE(cpipe::compute::has_usage(usage, BufferUsage::GpuStorage));
}
