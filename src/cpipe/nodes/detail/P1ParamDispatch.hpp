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

struct Opcode3DispatchHeader {
    std::uint32_t opcode_count;
    std::uint32_t reserved;
};

struct Opcode3WarpCoefficient {
    double kr0;
    double kr1;
    double kr2;
    double kr3;
    double kt0;
    double kt1;
};

struct Opcode3DispatchRecord {
    std::uint32_t id;
    std::uint32_t optional;
    std::uint32_t coefficient_count;
    Opcode3WarpCoefficient coefficients[4];
    double cx_hat;
    double cy_hat;
    double vignette_k[5];
    double vignette_cx_hat;
    double vignette_cy_hat;
    std::uint32_t constant;
    std::uint32_t bayer_phase;
    std::uint32_t point_offset;
    std::uint32_t point_count;
    std::uint32_t rect_offset;
    std::uint32_t rect_count;
    std::uint32_t top;
    std::uint32_t left;
    std::uint32_t bottom;
    std::uint32_t right;
};

struct Opcode3BadPoint {
    std::uint32_t row;
    std::uint32_t column;
};

struct Opcode3BadRect {
    std::uint32_t top;
    std::uint32_t left;
    std::uint32_t bottom;
    std::uint32_t right;
};

}  // namespace cpipe::nodes::detail
