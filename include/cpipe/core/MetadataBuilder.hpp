// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/BufferMetadata.hpp>
#include <memory>
#include <optional>
#include <string>

namespace cpipe::compute {

class MetadataBuilder {
public:
    MetadataBuilder();
    explicit MetadataBuilder(std::shared_ptr<const BufferMetadata> base);

    [[nodiscard]] bool is_frozen() const noexcept;
    [[nodiscard]] std::shared_ptr<const BufferMetadata> freeze();

    void set_calibration(std::shared_ptr<const CalibrationBlock> calibration);
    void set_capture(CaptureBlock capture);
    void set_cs_role(std::string role);
    void set_active_area(std::optional<Rect2u> area);
    void clear_cfa();
    void add_applied_step(std::string step);
    void set_exif_blob(std::shared_ptr<const ByteBlob> blob);
    void set_xmp_blob(std::shared_ptr<const ByteBlob> blob);
    void set_icc_blob(std::shared_ptr<const ByteBlob> blob);
    void set_blob(std::string key, std::shared_ptr<const ByteBlob> blob);
    void set_tensor_quant(TensorQuant quant);

private:
    void ensure_mutable() const;

    BufferMetadata metadata_{};
    std::shared_ptr<const BufferMetadata> frozen_;
};

}  // namespace cpipe::compute
