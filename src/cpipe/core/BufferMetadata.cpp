// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <cpipe/core/MetadataBuilder.hpp>
#include <stdexcept>
#include <utility>

namespace cpipe::compute {

CalibrationBlock::~CalibrationBlock() = default;

CaptureBlock::~CaptureBlock() = default;

MetadataBuilder::MetadataBuilder() = default;

MetadataBuilder::MetadataBuilder(const std::shared_ptr<const BufferMetadata>& base) {
    if (base) {
        metadata_ = *base;
    }
}

bool MetadataBuilder::is_frozen() const noexcept {
    return frozen_ != nullptr;
}

std::shared_ptr<const BufferMetadata> MetadataBuilder::freeze() {
    if (!frozen_) {
        frozen_ = std::make_shared<const BufferMetadata>(metadata_);
    }
    return frozen_;
}

void MetadataBuilder::set_calibration(std::shared_ptr<const CalibrationBlock> calibration) {
    ensure_mutable();
    metadata_.calibration = std::move(calibration);
}

void MetadataBuilder::clear_calibration() {
    ensure_mutable();
    metadata_.calibration.reset();
}

void MetadataBuilder::set_capture(const CaptureBlock& capture) {
    ensure_mutable();
    metadata_.capture = capture;
}

void MetadataBuilder::set_as_shot_neutral(std::array<float, 3> as_shot_neutral) {
    ensure_mutable();
    metadata_.capture.as_shot_neutral = as_shot_neutral;
}

void MetadataBuilder::set_orientation(std::uint8_t orientation) {
    ensure_mutable();
    metadata_.capture.orientation = orientation;
}

void MetadataBuilder::set_cs_role(std::string role) {
    ensure_mutable();
    metadata_.cs_role = std::move(role);
}

void MetadataBuilder::set_active_area(std::optional<Rect2u> area) {
    ensure_mutable();
    metadata_.active_area = area;
}

void MetadataBuilder::clear_cfa() {
    ensure_mutable();
    if (!metadata_.calibration) {
        return;
    }
    auto mutable_calibration = std::make_shared<CalibrationBlock>(*metadata_.calibration);
    mutable_calibration->cfa.reset();
    metadata_.calibration = std::move(mutable_calibration);
}

void MetadataBuilder::set_cfa(CFADescriptor cfa) {
    ensure_mutable();
    auto mutable_calibration = metadata_.calibration
                                   ? std::make_shared<CalibrationBlock>(*metadata_.calibration)
                                   : std::make_shared<CalibrationBlock>();
    mutable_calibration->cfa = cfa;
    metadata_.calibration = std::move(mutable_calibration);
}

void MetadataBuilder::add_applied_step(std::string step) {
    ensure_mutable();
    metadata_.applied_steps.push_back(std::move(step));
}

void MetadataBuilder::remove_applied_step(std::string_view step) {
    ensure_mutable();
    std::erase(metadata_.applied_steps, step);
}

void MetadataBuilder::set_exif_blob(std::shared_ptr<const ByteBlob> blob) {
    ensure_mutable();
    metadata_.exif_blob = std::move(blob);
}

void MetadataBuilder::set_xmp_blob(std::shared_ptr<const ByteBlob> blob) {
    ensure_mutable();
    metadata_.xmp_blob = std::move(blob);
}

void MetadataBuilder::set_icc_blob(std::shared_ptr<const ByteBlob> blob) {
    ensure_mutable();
    metadata_.icc_blob = std::move(blob);
}

void MetadataBuilder::set_blob(std::string key, std::shared_ptr<const ByteBlob> blob) {
    ensure_mutable();
    if (!blob) {
        metadata_.ext_blobs.erase(key);
        return;
    }
    metadata_.ext_blobs[std::move(key)] = std::move(blob);
}

void MetadataBuilder::set_tensor_quant(TensorQuant quant) {
    ensure_mutable();
    metadata_.tensor_quant = std::move(quant);
}

void MetadataBuilder::ensure_mutable() const {
    if (frozen_) {
        throw std::logic_error{"MetadataBuilder is frozen"};
    }
}

}  // namespace cpipe::compute
