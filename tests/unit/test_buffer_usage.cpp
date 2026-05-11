// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferUsage.hpp>

using cpipe::compute::BufferUsage;
using cpipe::compute::has_usage;

TEST_CASE("test_buffer_usage: flag combinations are bitwise") {
    const auto usage = BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::GpuStorage;

    CHECK(has_usage(usage, BufferUsage::Input));
    CHECK(has_usage(usage, BufferUsage::CpuRead));
    CHECK(has_usage(usage, BufferUsage::GpuStorage));
    CHECK_FALSE(has_usage(usage, BufferUsage::CpuWrite));
    CHECK_FALSE(has_usage(usage, BufferUsage::NpuOutput));
}
