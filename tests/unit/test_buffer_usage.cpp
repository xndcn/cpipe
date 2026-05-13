// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferUsage.hpp>

TEST_CASE("BufferUsage flags compose and query") {
    using cpipe::compute::BufferUsage;

    const auto usage = BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::GpuStorage;

    REQUIRE(cpipe::compute::has_usage(usage, BufferUsage::Input));
    REQUIRE(cpipe::compute::has_usage(usage, BufferUsage::CpuRead));
    REQUIRE(cpipe::compute::has_usage(usage, BufferUsage::GpuStorage));
    REQUIRE_FALSE(cpipe::compute::has_usage(usage, BufferUsage::Output));
}
