// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferLayout.hpp>

namespace {
using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::PixelFormat;
}  // namespace

TEST_CASE("BufferLayout sizes Image2D with row stride") {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 64;
    layout.dims[1] = 32;
    layout.stride[0] = 4;
    layout.stride[1] = 288;

    REQUIRE(layout.size_bytes() == 288ULL * 32ULL);
}

TEST_CASE("BufferLayout sizes Volume3D and TensorND contiguous layouts") {
    BufferLayout volume{};
    volume.kind = BufferKind::Volume3D;
    volume.format = PixelFormat::R16G16B16A16_SFLOAT;
    volume.ndim = 3;
    volume.dims[0] = 17;
    volume.dims[1] = 17;
    volume.dims[2] = 17;

    REQUIRE(volume.size_bytes() == 17ULL * 17ULL * 17ULL * 8ULL);

    BufferLayout tensor{};
    tensor.kind = BufferKind::TensorND;
    tensor.format = PixelFormat::F32;
    tensor.ndim = 4;
    tensor.dims[0] = 1;
    tensor.dims[1] = 3;
    tensor.dims[2] = 8;
    tensor.dims[3] = 16;

    REQUIRE(tensor.size_bytes() == 1ULL * 3ULL * 8ULL * 16ULL * 4ULL);
}

TEST_CASE("BufferLayout sizes blobs as byte streams") {
    BufferLayout blob{};
    blob.kind = BufferKind::Blob;
    blob.format = PixelFormat::BLOB;
    blob.ndim = 1;
    blob.dims[0] = 4097;

    REQUIRE(blob.size_bytes() == 4097);
}
