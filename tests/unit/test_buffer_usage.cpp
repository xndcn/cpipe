// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>

#include "cpipe/core/BufferUsage.hpp"

TEST_CASE("BufferUsage flags compose and test bitwise") {
    using cpipe::compute::BufferUsage;

    const BufferUsage usage = BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::GpuStorage;

    CHECK(cpipe::compute::has_usage(usage, BufferUsage::Input));
    CHECK(cpipe::compute::has_usage(usage, BufferUsage::CpuRead));
    CHECK(cpipe::compute::has_usage(usage, BufferUsage::GpuStorage));
    CHECK_FALSE(cpipe::compute::has_usage(usage, BufferUsage::Output));
}
