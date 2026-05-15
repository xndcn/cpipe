// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>

namespace cpipe::nodes::detail {

struct LinearizeParamsHeader {
    std::uint32_t table_size;
};

struct BlacklevelParams {
    float black_level[4];
    std::uint32_t white_level;
    std::uint8_t cfa_pattern[4];
};

struct DemosaicParams {
    std::uint8_t cfa_pattern[4];
};

struct WbParams {
    float as_shot_neutral[3];
};

struct ColormatrixParams {
    float transform[9];
};

struct GainMapDispatchHeader {
    std::uint32_t map_count;
    std::uint8_t cfa_repeat[2];
    std::uint8_t cfa_pattern[16];
};

struct GainMapDispatchPlane {
    std::uint32_t top;
    std::uint32_t left;
    std::uint32_t bottom;
    std::uint32_t right;
    std::uint32_t plane;
    std::uint32_t planes;
    std::uint32_t map_points_v;
    std::uint32_t map_points_h;
    double map_spacing_v;
    double map_spacing_h;
    double map_origin_v;
    double map_origin_h;
    std::uint32_t gain_offset;
    std::uint32_t gain_count;
};

}  // namespace cpipe::nodes::detail
