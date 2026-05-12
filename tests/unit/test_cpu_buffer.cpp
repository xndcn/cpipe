// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "cpipe/core/CpuBuffer.hpp"

using namespace cpipe::compute;

TEST_CASE("CpuBuffer locks aligned CPU memory across cycles") {
    auto buffer =
        CpuBuffer::create(make_rgba8_layout(16, 16), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(buffer.has_value());
    REQUIRE((*buffer)->size_bytes() == 16 * 16 * 4);

    for (int cycle = 0; cycle < 2; ++cycle) {
        auto* ptr = (*buffer)->lock_cpu(IBuffer::CpuAccess::ReadWrite);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % CpuBuffer::kAlignment == 0);
        REQUIRE((*buffer)->is_locked());
        (*buffer)->unlock_cpu();
        REQUIRE_FALSE((*buffer)->is_locked());
    }

    (*buffer)->unlock_cpu();
    REQUIRE_FALSE((*buffer)->is_locked());
}

TEST_CASE("CpuBuffer rejects incompatible CPU access and v1 subviews") {
    auto buffer = CpuBuffer::create(make_rgba8_layout(8, 8), BufferUsage::CpuRead);
    REQUIRE(buffer.has_value());
    REQUIRE((*buffer)->lock_cpu(IBuffer::CpuAccess::Write) == nullptr);
    REQUIRE((*buffer)->sub_view(0, 0, 4, 4) == nullptr);
}
