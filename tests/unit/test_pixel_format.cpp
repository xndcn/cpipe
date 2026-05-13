// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/PixelFormat.hpp>

namespace {
using cpipe::compute::PixelFormat;
}

TEST_CASE("PixelFormat exposes v1 formats") {
    REQUIRE(cpipe::compute::to_string(PixelFormat::UNDEFINED) == "UNDEFINED");
    REQUIRE(cpipe::compute::to_string(PixelFormat::R16_UINT) == "R16_UINT");
    REQUIRE(cpipe::compute::to_string(PixelFormat::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM");
    REQUIRE(cpipe::compute::to_string(PixelFormat::R32_SFLOAT) == "R32_SFLOAT");
    REQUIRE(cpipe::compute::to_string(PixelFormat::BLOB) == "BLOB");
}

TEST_CASE("PixelFormat byte sizes match buffer.md") {
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R16_UINT) == 2);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R10_PACKED) == 0);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R8G8B8A8_UNORM) == 4);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R8G8B8_UNORM) == 3);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R10G10B10A2_UNORM) == 4);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R16G16B16A16_SFLOAT) == 8);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R16G16B16_SFLOAT) == 6);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R32G32B32A32_SFLOAT) == 16);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R32G32B32_SFLOAT) == 12);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::R32_SFLOAT) == 4);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::F16) == 2);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::F32) == 4);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::I8) == 1);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::U8) == 1);
    REQUIRE(cpipe::compute::bytes_per_pixel(PixelFormat::BLOB) == 1);
}
