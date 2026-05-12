// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>

#include "cpipe/core/BufferUsage.hpp"

using namespace cpipe::compute;

TEST_CASE("BufferUsage flags combine bitwise") {
    auto usage = BufferUsage::CpuRead | BufferUsage::CpuWrite;
    REQUIRE(has_usage(usage, BufferUsage::CpuRead));
    REQUIRE(has_usage(usage, BufferUsage::CpuWrite));
    REQUIRE_FALSE(has_usage(usage, BufferUsage::GpuStorage));

    usage |= BufferUsage::GpuStorage;
    REQUIRE(has_usage(usage, BufferUsage::GpuStorage));
}
