// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/BufferUsage.hpp>

#include <catch2/catch_test_macros.hpp>

using cpipe::compute::BufferUsage;
using cpipe::compute::has_usage;

TEST_CASE("BufferUsage flag combinations are bitwise") {
    const auto usage =
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::GpuStorage;

    CHECK(has_usage(usage, BufferUsage::Input));
    CHECK(has_usage(usage, BufferUsage::CpuRead));
    CHECK(has_usage(usage, BufferUsage::GpuStorage));
    CHECK_FALSE(has_usage(usage, BufferUsage::CpuWrite));
    CHECK_FALSE(has_usage(usage, BufferUsage::NpuOutput));
}
