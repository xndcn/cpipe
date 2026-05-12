// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferLayout.hpp>

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::PixelFormat;

TEST_CASE("BufferLayout computes image sizes with explicit row stride") {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 3;
    layout.dims[1] = 2;
    layout.stride[0] = 4;
    layout.stride[1] = 16;

    REQUIRE(layout.size_bytes() == 28U);
}

TEST_CASE("BufferLayout computes tight volume, tensor, blob, and RAW10 sizes") {
    BufferLayout volume{};
    volume.kind = BufferKind::Volume3D;
    volume.format = PixelFormat::R16G16B16A16_SFLOAT;
    volume.ndim = 3;
    volume.dims[0] = 2;
    volume.dims[1] = 3;
    volume.dims[2] = 4;
    REQUIRE(volume.size_bytes() == 192U);

    BufferLayout tensor{};
    tensor.kind = BufferKind::TensorND;
    tensor.format = PixelFormat::F32;
    tensor.ndim = 4;
    tensor.dims[0] = 1;
    tensor.dims[1] = 3;
    tensor.dims[2] = 5;
    tensor.dims[3] = 7;
    REQUIRE(tensor.size_bytes() == 420U);

    BufferLayout blob{};
    blob.kind = BufferKind::Blob;
    blob.format = PixelFormat::BLOB;
    blob.ndim = 1;
    blob.dims[0] = 123;
    REQUIRE(blob.size_bytes() == 123U);

    BufferLayout raw10{};
    raw10.kind = BufferKind::Image2D;
    raw10.format = PixelFormat::R10_PACKED;
    raw10.ndim = 2;
    raw10.dims[0] = 5;
    raw10.dims[1] = 2;
    REQUIRE(raw10.size_bytes() == 14U);
}
