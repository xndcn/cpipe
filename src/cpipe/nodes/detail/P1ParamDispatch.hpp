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

struct WbParams {
    float as_shot_neutral[3];
};

struct ColormatrixParams {
    float transform[9];
};

}  // namespace cpipe::nodes::detail
