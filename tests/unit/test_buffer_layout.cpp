// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "cpipe/core/BufferLayout.hpp"
#include "cpipe/core/PixelFormat.hpp"

namespace {
using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::PixelFormat;

auto image_layout(std::uint32_t width, std::uint32_t height,
                  std::uint64_t row_stride) -> BufferLayout {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    layout.stride[0] = 4;
    layout.stride[1] = row_stride;
    return layout;
}
}  // namespace

TEST_CASE("BufferLayout computes tightly packed image size") {
    BufferLayout layout = image_layout(5, 3, 0);

    CHECK(layout.size_bytes() == 60);
}

TEST_CASE("BufferLayout respects padded row stride for Image2D") {
    BufferLayout layout = image_layout(5, 3, 64);

    CHECK(layout.size_bytes() == 192);
}

TEST_CASE("BufferLayout computes Volume3D size") {
    BufferLayout layout{};
    layout.kind = BufferKind::Volume3D;
    layout.format = PixelFormat::R16G16B16A16_SFLOAT;
    layout.ndim = 3;
    layout.dims[0] = 17;
    layout.dims[1] = 17;
    layout.dims[2] = 17;

    CHECK(layout.size_bytes() == 17ULL * 17ULL * 17ULL * 8ULL);
}

TEST_CASE("BufferLayout computes TensorND size") {
    BufferLayout layout{};
    layout.kind = BufferKind::TensorND;
    layout.format = PixelFormat::F16;
    layout.ndim = 4;
    layout.dims[0] = 1;
    layout.dims[1] = 3;
    layout.dims[2] = 8;
    layout.dims[3] = 8;

    CHECK(layout.size_bytes() == 1ULL * 3ULL * 8ULL * 8ULL * 2ULL);
}

TEST_CASE("BufferLayout computes Blob size from byte dimension") {
    BufferLayout layout{};
    layout.kind = BufferKind::Blob;
    layout.format = PixelFormat::BLOB;
    layout.ndim = 1;
    layout.dims[0] = 4096;

    CHECK(layout.size_bytes() == 4096);
}
