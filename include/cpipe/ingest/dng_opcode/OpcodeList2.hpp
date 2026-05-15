// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cpipe::ingest::dng_opcode {

struct GainMap {
    std::uint32_t top{0};
    std::uint32_t left{0};
    std::uint32_t bottom{0};
    std::uint32_t right{0};
    std::uint32_t plane{0};
    std::uint32_t planes{0};
    std::uint32_t row_pitch{0};
    std::uint32_t col_pitch{0};
    std::uint32_t map_points_v{0};
    std::uint32_t map_points_h{0};
    double map_spacing_v{0.0};
    double map_spacing_h{0.0};
    double map_origin_v{0.0};
    double map_origin_h{0.0};
    std::uint32_t map_planes{0};
    std::vector<float> gain;
};

struct GainMapParseResult {
    cpipe_status_t status{CPIPE_FAILED};
    std::vector<GainMap> gain_maps;
    std::string message;
};

class OpcodeList2 {
public:
    [[nodiscard]] static GainMapParseResult parse_gain_maps(std::span<const std::byte> bytes);
};

}  // namespace cpipe::ingest::dng_opcode
