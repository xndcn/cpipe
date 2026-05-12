// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::PixelFormat;

TEST_CASE("CpuBuffer allocates aligned memory and survives repeated lock cycles") {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 8;
    layout.dims[1] = 4;

    const auto buffer = CpuBuffer::create(
        layout, BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->size_bytes() == 128U);

    for (int cycle = 0; cycle < 2; ++cycle) {
        auto* ptr = buffer->lock_cpu(cpipe::compute::IBuffer::CpuAccess::ReadWrite);
        REQUIRE(ptr != nullptr);
        const auto address = reinterpret_cast<std::uintptr_t>(ptr);
        REQUIRE(address % CpuBuffer::kAlignment == 0U);
        buffer->unlock_cpu();
    }
}

TEST_CASE("CpuBuffer sub_view is reserved and logs a v1 warning") {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 8;
    layout.dims[1] = 4;

    const auto buffer = CpuBuffer::create(layout, BufferUsage::Input | BufferUsage::CpuRead);
    REQUIRE(buffer != nullptr);

    std::ostringstream captured;
    auto* const old = std::clog.rdbuf(captured.rdbuf());
    const auto sub = buffer->sub_view(0, 0, 4, 4);
    std::clog.rdbuf(old);

    REQUIRE(sub == nullptr);
    REQUIRE(captured.str().find("sub_view is not implemented in v1") != std::string::npos);
}
