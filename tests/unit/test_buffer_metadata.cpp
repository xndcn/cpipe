// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/ByteBlob.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CaptureBlock.hpp>
#include <cpipe/core/MetadataBuilder.hpp>
#include <cpipe/core/TensorQuant.hpp>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using cpipe::compute::BufferMetadata;
using cpipe::compute::ByteBlob;
using cpipe::compute::CalibrationBlock;
using cpipe::compute::CaptureBlock;
using cpipe::compute::CFADescriptor;
using cpipe::compute::MetadataBuilder;
using cpipe::compute::TensorQuant;

}  // namespace

TEST_CASE("BufferMetadata default state is a stable empty snapshot") {
    const BufferMetadata metadata;

    REQUIRE(metadata.schema_version == 1);
    REQUIRE(metadata.calibration == nullptr);
    REQUIRE(metadata.capture.as_shot_neutral == std::array<float, 3>{1.0F, 1.0F, 1.0F});
    REQUIRE(metadata.capture.orientation == 1);
    REQUIRE(metadata.capture.burst_size == 1);
    REQUIRE(metadata.applied_steps.empty());
    REQUIRE(metadata.cs_role == "undefined");
    REQUIRE_FALSE(metadata.active_area.has_value());
    REQUIRE(metadata.tensor_quant.scheme == TensorQuant::Scheme::None);
    REQUIRE(metadata.ext_blobs.empty());
}

TEST_CASE("MetadataBuilder freezes an immutable metadata snapshot") {
    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->white_level = 4095;
    calibration->black_level = {64.0F, 65.0F, 66.0F, 67.0F};
    calibration->cfa = CFADescriptor{{0, 1, 1, 2}};

    ByteBlob icc_blob;
    icc_blob.bytes = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    auto icc = std::make_shared<const ByteBlob>(std::move(icc_blob));

    MetadataBuilder builder;
    builder.set_calibration(calibration);
    builder.set_cs_role("raw_camera");
    builder.set_icc_blob(icc);
    builder.add_applied_step("linearization");

    const auto frozen = builder.freeze();

    REQUIRE(frozen->calibration == calibration);
    REQUIRE(frozen->calibration->white_level == 4095);
    REQUIRE(frozen->cs_role == "raw_camera");
    REQUIRE(frozen->icc_blob == icc);
    REQUIRE(frozen->applied_steps == std::vector<std::string>{"linearization"});
    REQUIRE(builder.is_frozen());
    REQUIRE_THROWS_AS(builder.add_applied_step("after_freeze"), std::logic_error);
}

TEST_CASE("MetadataBuilder derives from an upstream snapshot without mutating it") {
    MetadataBuilder base_builder;
    base_builder.set_cs_role("raw_camera");
    base_builder.add_applied_step("linearization");
    CaptureBlock capture;
    capture.sensor_timestamp_ns = 42;
    capture.burst_size = 3;
    base_builder.set_capture(capture);
    const auto base = base_builder.freeze();

    MetadataBuilder derived{base};
    derived.set_cs_role("scene_linear_rec2020");
    derived.add_applied_step("color_matrix");
    const auto frozen = derived.freeze();

    REQUIRE(base->cs_role == "raw_camera");
    REQUIRE(base->applied_steps == std::vector<std::string>{"linearization"});
    REQUIRE(frozen->cs_role == "scene_linear_rec2020");
    REQUIRE(frozen->applied_steps == std::vector<std::string>{"linearization", "color_matrix"});
    REQUIRE(frozen->capture.sensor_timestamp_ns == 42);
    REQUIRE(frozen->capture.burst_size == 3);
}
