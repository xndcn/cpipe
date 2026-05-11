// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/CpuBuffer.hpp>

#include <cstdint>
#include <cstring>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba_layout() {
    return BufferLayout{
        .kind = BufferKind::Image2D,
        .format = PixelFormat::R8G8B8A8_UNORM,
        .ndim = 2,
        .dims = {16, 16},
        .stride = {},
    };
}

}  // namespace

TEST_CASE("CpuBuffer locks, unlocks, and preserves bytes across cycles") {
    CpuBuffer buffer(rgba_layout(), BufferUsage::Input | BufferUsage::CpuRead |
                                        BufferUsage::CpuWrite);

    auto* first = static_cast<std::uint8_t*>(buffer.lock_cpu(IBuffer::CpuAccess::ReadWrite));
    REQUIRE(first != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(first) % 64 == 0);
    std::memset(first, 0x2a, static_cast<std::size_t>(buffer.size_bytes()));
    buffer.unlock_cpu();

    const auto* second = static_cast<const std::uint8_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(second != nullptr);
    CHECK(second[0] == 0x2a);
    CHECK(second[static_cast<std::size_t>(buffer.size_bytes()) - 1] == 0x2a);
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

TEST_CASE("CpuBuffer reports layout and reserves sub_view for v2") {
    auto buffer = std::make_shared<CpuBuffer>(
        rgba_layout(), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    CHECK(buffer->layout().kind == BufferKind::Image2D);
    CHECK(buffer->size_bytes() == 16 * 16 * 4);
    CHECK(buffer->color_role().empty());
    CHECK(buffer->sub_view(0, 0, 4, 4) == nullptr);
}
