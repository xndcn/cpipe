// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <algorithm>
#include <array>
#include <cpipe/sdk/sdk.hpp>
#include <cstdint>

#include "../detail/P1ParamDispatch.hpp"

namespace cpipe::nodes::detail {

inline bool is_supported_bayer_cfa(const sdk::CalibrationView& calibration) {
    if (!calibration.has_cfa || calibration.cfa_repeat[0] != 2 || calibration.cfa_repeat[1] != 2) {
        return false;
    }

    static constexpr std::array<std::array<std::uint8_t, 4>, 4> kSupported{{
        {0, 1, 1, 2},
        {2, 1, 1, 0},
        {1, 0, 2, 1},
        {1, 2, 0, 1},
    }};
    return std::ranges::any_of(kSupported, [&](const auto& pattern) {
        return std::equal(pattern.begin(), pattern.end(), calibration.cfa_pattern.begin());
    });
}

inline DemosaicParams make_demosaic_params(const sdk::CalibrationView& calibration) {
    DemosaicParams params{};
    std::copy_n(calibration.cfa_pattern.begin(), 4U, params.cfa_pattern);
    return params;
}

}  // namespace cpipe::nodes::detail
