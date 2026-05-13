// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/MetadataBuilder.hpp>
#include <stdexcept>
#include <utility>

namespace cpipe::compute {

CalibrationBlock::~CalibrationBlock() = default;

CaptureBlock::~CaptureBlock() = default;

MetadataBuilder::MetadataBuilder() = default;

MetadataBuilder::MetadataBuilder(std::shared_ptr<const BufferMetadata> base) {
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

void MetadataBuilder::set_capture(CaptureBlock capture) {
    ensure_mutable();
    metadata_.capture = std::move(capture);
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

void MetadataBuilder::add_applied_step(std::string step) {
    ensure_mutable();
    metadata_.applied_steps.push_back(std::move(step));
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
