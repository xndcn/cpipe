// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/BufferLayout.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::PixelFormat;

}  // namespace

TEST_CASE("BufferLayout computes contiguous Image2D byte size") {
    BufferLayout layout{
        .kind = BufferKind::Image2D,
        .format = PixelFormat::R8G8B8A8_UNORM,
        .ndim = 2,
        .dims = {64, 32},
        .stride = {},
    };

    CHECK(layout.size_bytes() == 64 * 32 * 4);
}

TEST_CASE("BufferLayout respects explicit row stride") {
    BufferLayout layout{
        .kind = BufferKind::Image2D,
        .format = PixelFormat::R8G8B8A8_UNORM,
        .ndim = 2,
        .dims = {64, 32},
        .stride = {4, 320},
    };

    CHECK(layout.size_bytes() == 320 * 32);
}

TEST_CASE("BufferLayout computes RAW10 packed rows") {
    BufferLayout layout{
        .kind = BufferKind::Image2D,
        .format = PixelFormat::R10_PACKED,
        .ndim = 2,
        .dims = {13, 7},
        .stride = {},
    };

    CHECK(layout.size_bytes() == 17 * 7);
}

TEST_CASE("BufferLayout computes Volume3D, TensorND, and Blob sizes") {
    BufferLayout lut{
        .kind = BufferKind::Volume3D,
        .format = PixelFormat::R16G16B16A16_SFLOAT,
        .ndim = 3,
        .dims = {17, 17, 17},
        .stride = {},
    };
    CHECK(lut.size_bytes() == 17 * 17 * 17 * 8);

    BufferLayout tensor{
        .kind = BufferKind::TensorND,
        .format = PixelFormat::F32,
        .ndim = 4,
        .dims = {1, 3, 16, 16},
        .stride = {},
    };
    CHECK(tensor.size_bytes() == 1 * 3 * 16 * 16 * 4);

    BufferLayout blob{
        .kind = BufferKind::Blob,
        .format = PixelFormat::BLOB,
        .ndim = 1,
        .dims = {4096},
        .stride = {},
    };
    CHECK(blob.size_bytes() == 4096);
}
