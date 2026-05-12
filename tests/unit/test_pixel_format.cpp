// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "cpipe/core/PixelFormat.hpp"

using cpipe::compute::PixelFormat;

TEST_CASE("PixelFormat values expose stable names and byte sizes") {
    const std::array formats{
        PixelFormat::UNDEFINED,
        PixelFormat::R16_UINT,
        PixelFormat::R10_PACKED,
        PixelFormat::R8G8B8A8_UNORM,
        PixelFormat::R8G8B8_UNORM,
        PixelFormat::R10G10B10A2_UNORM,
        PixelFormat::R16G16B16A16_SFLOAT,
        PixelFormat::R16G16B16_SFLOAT,
        PixelFormat::R32G32B32A32_SFLOAT,
        PixelFormat::R32G32B32_SFLOAT,
        PixelFormat::R32_SFLOAT,
        PixelFormat::F16,
        PixelFormat::F32,
        PixelFormat::I8,
        PixelFormat::U8,
        PixelFormat::BLOB,
    };

    REQUIRE(formats.size() == cpipe::compute::kPixelFormatCount);
    REQUIRE(cpipe::compute::to_string(PixelFormat::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM");
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R16_UINT) == 2);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R8G8B8_UNORM) == 3);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R16G16B16A16_SFLOAT) == 8);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R32G32B32A32_SFLOAT) == 16);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::BLOB) == 1);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R10_PACKED) == 0);
}
