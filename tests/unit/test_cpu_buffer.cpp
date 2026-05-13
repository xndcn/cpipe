// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cstdint>

namespace {
using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;
}  // namespace

TEST_CASE("CpuBuffer allocates aligned storage and survives lock cycles") {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 16;
    layout.dims[1] = 16;

    CpuBuffer buffer{layout, BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite};
    REQUIRE(buffer.size_bytes() == 16ULL * 16ULL * 4ULL);

    for (int i = 0; i < 2; ++i) {
        void* ptr = buffer.lock_cpu(IBuffer::CpuAccess::ReadWrite);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % CpuBuffer::kAlignment == 0);
        buffer.unlock_cpu();
        buffer.flush_cpu_writes();
    }
}

TEST_CASE("CpuBuffer sub_view is reserved in v1") {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 4;
    layout.dims[1] = 4;

    CpuBuffer buffer{layout, BufferUsage::Input | BufferUsage::CpuRead};
    REQUIRE(buffer.sub_view(0, 0, 2, 2) == nullptr);
}
