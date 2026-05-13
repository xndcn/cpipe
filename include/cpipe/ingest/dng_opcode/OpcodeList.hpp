// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/BufferMetadata.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cpipe::ingest::dng_opcode {

struct ParsedDngMetadata {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint16_t bits_per_sample{0};
    std::uint32_t strip_offset{0};
    std::uint32_t strip_byte_count{0};
    compute::CalibrationBlock calibration;
    compute::CaptureBlock capture;
    std::optional<compute::Rect2u> active_area;
    std::string make;
    std::string model;
    std::vector<std::uint16_t> linearization_table;
    std::vector<std::byte> exif_blob;
    std::vector<std::byte> xmp_blob;
    std::vector<std::byte> icc_blob;
    std::vector<std::byte> opcode_list_1;
    std::vector<std::byte> opcode_list_2;
    std::vector<std::byte> opcode_list_3;
};

struct ParseResult {
    cpipe_status_t status{CPIPE_FAILED};
    ParsedDngMetadata metadata;
    std::string message;
};

class OpcodeListParser {
public:
    [[nodiscard]] static ParseResult parse(const std::filesystem::path& path);
};

}  // namespace cpipe::ingest::dng_opcode
