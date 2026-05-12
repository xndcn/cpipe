// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cpipe/core/PixelFormat.hpp"

namespace {
using cpipe::compute::PixelFormat;

struct PixelFormatCase {
    PixelFormat format;
    std::string_view name;
    std::optional<std::uint32_t> bytes_per_pixel;
};
}  // namespace

TEST_CASE("PixelFormat names and element sizes match the v1 buffer design") {
    const std::array<PixelFormatCase, 16> cases = {{
        {PixelFormat::UNDEFINED, "UNDEFINED", std::nullopt},
        {PixelFormat::R16_UINT, "R16_UINT", 2U},
        {PixelFormat::R10_PACKED, "R10_PACKED", std::nullopt},
        {PixelFormat::R8G8B8A8_UNORM, "R8G8B8A8_UNORM", 4U},
        {PixelFormat::R8G8B8_UNORM, "R8G8B8_UNORM", 3U},
        {PixelFormat::R10G10B10A2_UNORM, "R10G10B10A2_UNORM", 4U},
        {PixelFormat::R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT", 8U},
        {PixelFormat::R16G16B16_SFLOAT, "R16G16B16_SFLOAT", 6U},
        {PixelFormat::R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT", 16U},
        {PixelFormat::R32G32B32_SFLOAT, "R32G32B32_SFLOAT", 12U},
        {PixelFormat::R32_SFLOAT, "R32_SFLOAT", 4U},
        {PixelFormat::F16, "F16", 2U},
        {PixelFormat::F32, "F32", 4U},
        {PixelFormat::I8, "I8", 1U},
        {PixelFormat::U8, "U8", 1U},
        {PixelFormat::BLOB, "BLOB", 1U},
    }};

    for (const auto& test_case : cases) {
        CAPTURE(test_case.name);
        CHECK(cpipe::compute::to_string(test_case.format) == test_case.name);
        CHECK(cpipe::compute::bytes_per_pixel(test_case.format) == test_case.bytes_per_pixel);
    }
}
