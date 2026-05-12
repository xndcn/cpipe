// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

using cpipe::compute::PixelFormat;

TEST_CASE("PixelFormat exposes the v1 format set and metadata") {
    constexpr PixelFormat kFormats[] = {
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

    REQUIRE(std::size(kFormats) == 15U);
    REQUIRE(cpipe::compute::bits_per_pixel(PixelFormat::R16_UINT) == 16U);
    REQUIRE(cpipe::compute::bits_per_pixel(PixelFormat::R10_PACKED) == 10U);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R8G8B8A8_UNORM) == 4U);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R10_PACKED) == std::nullopt);
    REQUIRE(cpipe::compute::to_string(PixelFormat::R16G16B16A16_SFLOAT) ==
            std::string_view{"R16G16B16A16_SFLOAT"});
    REQUIRE(cpipe::compute::to_string(PixelFormat::UNDEFINED) == std::string_view{"UNDEFINED"});
}
