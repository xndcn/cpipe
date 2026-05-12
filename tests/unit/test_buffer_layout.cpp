// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>

#include "cpipe/core/BufferLayout.hpp"

using namespace cpipe::compute;

TEST_CASE("BufferLayout computes Image2D byte sizes") {
    auto layout = make_rgba8_layout(64, 32);
    REQUIRE(layout.size_bytes() == 64 * 32 * 4);

    layout.stride[1] = 320;
    REQUIRE(layout.size_bytes() == 320 * 32);
}

TEST_CASE("BufferLayout computes non-image byte sizes") {
    BufferLayout volume;
    volume.kind = BufferKind::Volume3D;
    volume.format = PixelFormat::F32;
    volume.ndim = 3;
    volume.dims = {4, 4, 4, 0, 0, 0, 0, 0};
    REQUIRE(make_default_strides(volume).size_bytes() == 4 * 4 * 4 * 4);

    BufferLayout tensor;
    tensor.kind = BufferKind::TensorND;
    tensor.format = PixelFormat::F16;
    tensor.ndim = 4;
    tensor.dims = {1, 3, 2, 2, 0, 0, 0, 0};
    REQUIRE(make_default_strides(tensor).size_bytes() == 1 * 3 * 2 * 2 * 2);

    REQUIRE(make_blob_layout(4096).size_bytes() == 4096);
}

TEST_CASE("BufferLayout handles R10 packed rows") {
    BufferLayout layout;
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R10_PACKED;
    layout.ndim = 2;
    layout.dims = {4, 2, 0, 0, 0, 0, 0, 0};
    REQUIRE(layout.size_bytes() == 10);
}
