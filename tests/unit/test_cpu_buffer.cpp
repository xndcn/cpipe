// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/BufferUsage.hpp"
#include "cpipe/core/IBuffer.hpp"
#include "cpipe/core/PixelFormat.hpp"

namespace {
auto make_layout() -> cpipe::compute::BufferLayout {
    cpipe::compute::BufferLayout layout{};
    layout.kind = cpipe::compute::BufferKind::Image2D;
    layout.format = cpipe::compute::PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 16;
    layout.dims[1] = 16;
    return layout;
}
}  // namespace

TEST_CASE("CpuBuffer allocates aligned memory and survives repeated lock cycles") {
    using cpipe::compute::BufferUsage;
    using cpipe::compute::CpuBuffer;
    using cpipe::compute::IBuffer;

    CpuBuffer buffer(make_layout(),
                     BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    CHECK(buffer.size_bytes() == 16ULL * 16ULL * 4ULL);

    for (std::uint8_t fill : {std::uint8_t{0x11}, std::uint8_t{0x27}}) {
        auto* ptr = static_cast<std::uint8_t*>(buffer.lock_cpu(IBuffer::CpuAccess::ReadWrite));
        REQUIRE(ptr != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(ptr) % 64U == 0U);
        std::memset(ptr, fill, static_cast<std::size_t>(buffer.size_bytes()));
        buffer.unlock_cpu();
        buffer.flush_cpu_writes();

        const auto* read_ptr =
            static_cast<const std::uint8_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
        REQUIRE(read_ptr != nullptr);
        CHECK(read_ptr[0] == fill);
        CHECK(read_ptr[buffer.size_bytes() - 1U] == fill);
        buffer.unlock_cpu();
    }
}

TEST_CASE("IBuffer sub_view is reserved and returns null in v1") {
    using cpipe::compute::BufferUsage;
    using cpipe::compute::CpuBuffer;

    CpuBuffer buffer(make_layout(), BufferUsage::Input | BufferUsage::CpuRead);

    CHECK(buffer.sub_view(0, 0, 4, 4) == nullptr);
}
