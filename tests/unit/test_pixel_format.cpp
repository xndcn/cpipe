// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/PixelFormat.hpp>

namespace {

using cpipe::compute::PixelFormat;

}  // namespace

TEST_CASE("test_pixel_format: reports byte-addressable pixel sizes") {
    CHECK(bytes_per_pixel(PixelFormat::UNDEFINED) == 0);
    CHECK(bytes_per_pixel(PixelFormat::R16_UINT) == 2);
    CHECK(bytes_per_pixel(PixelFormat::R10_PACKED) == 0);
    CHECK(bytes_per_pixel(PixelFormat::R8G8B8A8_UNORM) == 4);
    CHECK(bytes_per_pixel(PixelFormat::R8G8B8_UNORM) == 3);
    CHECK(bytes_per_pixel(PixelFormat::R10G10B10A2_UNORM) == 4);
    CHECK(bytes_per_pixel(PixelFormat::R16G16B16A16_SFLOAT) == 8);
    CHECK(bytes_per_pixel(PixelFormat::R16G16B16_SFLOAT) == 6);
    CHECK(bytes_per_pixel(PixelFormat::R16_SFLOAT) == 2);
    CHECK(bytes_per_pixel(PixelFormat::R32G32B32A32_SFLOAT) == 16);
    CHECK(bytes_per_pixel(PixelFormat::R32G32B32_SFLOAT) == 12);
    CHECK(bytes_per_pixel(PixelFormat::R32_SFLOAT) == 4);
    CHECK(bytes_per_pixel(PixelFormat::F16) == 2);
    CHECK(bytes_per_pixel(PixelFormat::F32) == 4);
    CHECK(bytes_per_pixel(PixelFormat::I8) == 1);
    CHECK(bytes_per_pixel(PixelFormat::U8) == 1);
    CHECK(bytes_per_pixel(PixelFormat::BLOB) == 1);
}

TEST_CASE("test_pixel_format: has stable strings") {
    CHECK(to_string(PixelFormat::R16_UINT) == "R16_UINT");
    CHECK(to_string(PixelFormat::R10_PACKED) == "R10_PACKED");
    CHECK(to_string(PixelFormat::R8G8B8A8_UNORM) == "R8G8B8A8_UNORM");
    CHECK(to_string(PixelFormat::R8G8B8_UNORM) == "R8G8B8_UNORM");
    CHECK(to_string(PixelFormat::R10G10B10A2_UNORM) == "R10G10B10A2_UNORM");
    CHECK(to_string(PixelFormat::R16G16B16A16_SFLOAT) == "R16G16B16A16_SFLOAT");
    CHECK(to_string(PixelFormat::R16G16B16_SFLOAT) == "R16G16B16_SFLOAT");
    CHECK(to_string(PixelFormat::R16_SFLOAT) == "R16_SFLOAT");
    CHECK(to_string(PixelFormat::R32G32B32A32_SFLOAT) == "R32G32B32A32_SFLOAT");
    CHECK(to_string(PixelFormat::R32G32B32_SFLOAT) == "R32G32B32_SFLOAT");
    CHECK(to_string(PixelFormat::R32_SFLOAT) == "R32_SFLOAT");
    CHECK(to_string(PixelFormat::F16) == "F16");
    CHECK(to_string(PixelFormat::F32) == "F32");
    CHECK(to_string(PixelFormat::I8) == "I8");
    CHECK(to_string(PixelFormat::U8) == "U8");
    CHECK(to_string(PixelFormat::BLOB) == "BLOB");
}
