// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cpipe::compute {

struct CFADescriptor {
    std::array<std::uint8_t, 2> repeat{2, 2};
    std::array<std::uint8_t, 16> pattern{};

    constexpr CFADescriptor() = default;

    constexpr explicit CFADescriptor(const std::array<std::uint8_t, 4>& bayer_pattern) {
        std::copy(bayer_pattern.begin(), bayer_pattern.end(), pattern.begin());
    }

    constexpr CFADescriptor(std::array<std::uint8_t, 2> repeat_dims,
                            std::array<std::uint8_t, 16> cfa_pattern)
        : repeat(repeat_dims), pattern(cfa_pattern) {}
};

struct LinearizationTable {
    std::vector<std::uint16_t> values;
};

struct Matrix3 {
    std::array<float, 9> values{};
};

struct CalibrationBlock {
    std::optional<CFADescriptor> cfa;

    std::array<float, 4> black_level{};
    std::uint32_t white_level{0};
    std::optional<LinearizationTable> linearization_table;

    std::optional<Matrix3> color_matrix1;
    std::optional<Matrix3> color_matrix2;
    std::optional<Matrix3> forward_matrix1;
    std::optional<Matrix3> forward_matrix2;
    std::optional<Matrix3> camera_calibration1;
    std::optional<Matrix3> camera_calibration2;
    std::uint16_t calibration_illuminant1{0};
    std::uint16_t calibration_illuminant2{0};

    std::vector<std::pair<float, float>> noise_profile;
    std::array<float, 5> lens_distortion{};
    std::array<float, 5> lens_intrinsic{};

    ~CalibrationBlock();
};

}  // namespace cpipe::compute
