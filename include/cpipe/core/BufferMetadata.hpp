// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <array>
#include <cpipe/core/ByteBlob.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CaptureBlock.hpp>
#include <cpipe/core/TensorQuant.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cpipe::compute {

struct Rect2u {
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct MasteringDisplay {
    std::array<float, 2> red_primary{};
    std::array<float, 2> green_primary{};
    std::array<float, 2> blue_primary{};
    std::array<float, 2> white_point{};
    float min_luminance{0.0F};
    float max_luminance{0.0F};
};

struct ContentLightLevel {
    std::uint32_t max_cll{0};
    std::uint32_t max_fall{0};
};

struct UltraHdrGainMapMeta {
    float min_content_boost{1.0F};
    float max_content_boost{1.0F};
    float gamma{1.0F};
    float offset_sdr{0.0F};
    float offset_hdr{0.0F};
};

struct BufferMetadata {
    std::uint64_t schema_version{1};

    std::shared_ptr<const CalibrationBlock> calibration;
    CaptureBlock capture;

    std::vector<std::string> applied_steps;
    std::string cs_role{"undefined"};
    std::optional<Rect2u> active_area;

    std::shared_ptr<const ByteBlob> exif_blob;
    std::shared_ptr<const ByteBlob> xmp_blob;
    std::shared_ptr<const ByteBlob> icc_blob;
    std::optional<MasteringDisplay> mdcv;
    std::optional<ContentLightLevel> clli;
    std::optional<UltraHdrGainMapMeta> ultrahdr;

    TensorQuant tensor_quant;
    std::map<std::string, std::shared_ptr<const ByteBlob>> ext_blobs;
};

}  // namespace cpipe::compute
