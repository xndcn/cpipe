// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cpipe/sdk/sdk.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cpipe::nodes::wb_detail {

using Matrix3 = std::array<float, 9>;
using Vec3 = std::array<float, 3>;

constexpr const char* kCameraDiagBlob = "com.cpipe.wb.camera_diag_f32";
constexpr const char* kCameraToXyzBlob = "com.cpipe.wb.camera_to_xyz_d50_f32";
constexpr const char* kSceneCctBlob = "com.cpipe.wb.scene_cct_f32";
constexpr const char* kBlendWeightBlob = "com.cpipe.wb.dual_illuminant_weight_f32";
constexpr double kSearchMinCct = 1500.0;
constexpr double kSearchMaxCct = 10000.0;
constexpr double kBrentGoldenStep = 0.3819660112501051;
constexpr double kBrentTolerance = 1.0E-5;

struct BrentSearchState {
    double lower{kSearchMinCct};
    double upper{kSearchMaxCct};
    double best{0.5 * (kSearchMinCct + kSearchMaxCct)};
    double previous{best};
    double previous2{best};
    double best_error{0.0};
    double previous_error{0.0};
    double previous2_error{0.0};
    double step{0.0};
    double previous_step{0.0};
};

struct CalibrationResult {
    Matrix3 camera_to_xyz_d50{};
    double scene_cct{0.0};
    float blend_weight{0.0F};
};

inline sdk::Result<Matrix3> required_matrix(const std::optional<Matrix3>& matrix,
                                            std::string_view message) {
    if (!matrix.has_value()) {
        return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, std::string{message}});
    }
    return matrix.value_or(Matrix3{});
}

inline std::optional<double> calibration_illuminant_cct(std::uint16_t illuminant) {
    switch (illuminant) {
        case 17:
            return 2850.0;  // Standard Light A.
        case 20:
            return 5500.0;  // D55.
        case 21:
            return 6500.0;  // D65.
        case 22:
            return 7500.0;  // D75.
        case 23:
            return 5000.0;  // D50.
        case 24:
            return 3200.0;  // ISO studio tungsten.
        default:
            return std::nullopt;
    }
}

inline float reciprocal_cct_weight(double cct1, double cct2, double cct) {
    const auto denom = (1.0 / cct1) - (1.0 / cct2);
    if (std::abs(denom) <= std::numeric_limits<double>::epsilon()) {
        return 1.0F;
    }
    const auto weight = ((1.0 / cct) - (1.0 / cct2)) / denom;
    return static_cast<float>(std::clamp(weight, 0.0, 1.0));
}

inline Matrix3 lerp_matrix(const Matrix3& first, const Matrix3& second, float weight) {
    Matrix3 out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = (weight * first[i]) + ((1.0F - weight) * second[i]);
    }
    return out;
}

inline std::optional<Matrix3> inverse_matrix(const Matrix3& m) {
    const auto det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (!std::isfinite(det) || std::abs(det) <= std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }
    const auto inv_det = 1.0F / det;
    return Matrix3{
        (m[4] * m[8] - m[5] * m[7]) * inv_det, (m[2] * m[7] - m[1] * m[8]) * inv_det,
        (m[1] * m[5] - m[2] * m[4]) * inv_det, (m[5] * m[6] - m[3] * m[8]) * inv_det,
        (m[0] * m[8] - m[2] * m[6]) * inv_det, (m[2] * m[3] - m[0] * m[5]) * inv_det,
        (m[3] * m[7] - m[4] * m[6]) * inv_det, (m[1] * m[6] - m[0] * m[7]) * inv_det,
        (m[0] * m[4] - m[1] * m[3]) * inv_det,
    };
}

inline Vec3 mul_matrix_vec(const Matrix3& matrix, const Vec3& value) {
    return {matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
            matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
            matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2]};
}

inline std::array<double, 2> planckian_locus_xy(double cct) {
    const auto cct2 = cct * cct;
    const auto cct3 = cct2 * cct;
    auto x = (-3.0258469E9 / cct3) + (2.1070379E6 / cct2) + (0.2226347E3 / cct) + 0.240390;
    if (cct < 4000.0) {
        x = (-0.2661239E9 / cct3) - (0.2343580E6 / cct2) + (0.8776956E3 / cct) + 0.179910;
    }

    auto y = (3.0817580 * x * x * x) - (5.87338670 * x * x) + (3.75112997 * x) - 0.37001483;
    if (cct < 2222.0) {
        y = (-1.1063814 * x * x * x) - (1.34811020 * x * x) + (2.18555832 * x) - 0.20219683;
    } else if (cct < 4000.0) {
        y = (-0.9549476 * x * x * x) - (1.37418593 * x * x) + (2.09137015 * x) - 0.16748867;
    }
    return {x, y};
}

inline double cct_error(const Vec3& neutral, const Matrix3& color_matrix1,
                        const Matrix3& color_matrix2, double cct1, double cct2, double cct) {
    const auto weight = reciprocal_cct_weight(cct1, cct2, cct);
    const auto color_matrix = lerp_matrix(color_matrix1, color_matrix2, weight);
    const auto inverse = inverse_matrix(color_matrix);
    if (!inverse) {
        return std::numeric_limits<double>::infinity();
    }
    const auto xyz = mul_matrix_vec(*inverse, neutral);
    const auto sum = static_cast<double>(xyz[0] + xyz[1] + xyz[2]);
    if (!std::isfinite(sum) || sum <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }

    const auto xy = planckian_locus_xy(cct);
    const auto x = static_cast<double>(xyz[0]) / sum;
    const auto y = static_cast<double>(xyz[1]) / sum;
    const auto dx = x - xy[0];
    const auto dy = y - xy[1];
    return (dx * dx) + (dy * dy);
}

inline double interval_step(const BrentSearchState& state, double midpoint) {
    if (state.best < midpoint) {
        return state.upper - state.best;
    }
    return state.lower - state.best;
}

inline std::optional<double> parabolic_step(BrentSearchState& state, double midpoint,
                                            double tolerance) {
    const auto a = (state.best - state.previous) * (state.best_error - state.previous2_error);
    const auto b = (state.best - state.previous2) * (state.best_error - state.previous_error);
    auto numerator = ((state.best - state.previous2) * b) - ((state.best - state.previous) * a);
    auto denominator = 2.0 * (b - a);
    if (denominator <= 0.0) {
        return std::nullopt;
    }
    numerator = -numerator;
    const auto old_step = state.previous_step;
    state.previous_step = state.step;
    if (std::abs(numerator) >= std::abs(0.5 * denominator * old_step) ||
        numerator <= denominator * (state.lower - state.best) ||
        numerator >= denominator * (state.upper - state.best)) {
        return std::nullopt;
    }

    auto trial_step = numerator / denominator;
    const auto trial = state.best + trial_step;
    if ((trial - state.lower) < (2.0 * tolerance) || (state.upper - trial) < (2.0 * tolerance)) {
        trial_step = std::copysign(tolerance, midpoint - state.best);
    }
    return trial_step;
}

inline double next_brent_step(BrentSearchState& state, double midpoint, double tolerance) {
    if (std::abs(state.previous_step) > tolerance) {
        if (const auto trial_step = parabolic_step(state, midpoint, tolerance)) {
            return *trial_step;
        }
    }
    state.previous_step = interval_step(state, midpoint);
    return kBrentGoldenStep * state.previous_step;
}

inline void accept_trial(BrentSearchState& state, double trial, double trial_error) {
    if (trial < state.best) {
        state.upper = state.best;
    } else {
        state.lower = state.best;
    }
    state.previous2 = state.previous;
    state.previous2_error = state.previous_error;
    state.previous = state.best;
    state.previous_error = state.best_error;
    state.best = trial;
    state.best_error = trial_error;
}

inline void reject_trial(BrentSearchState& state, double trial, double trial_error) {
    if (trial < state.best) {
        state.lower = trial;
    } else {
        state.upper = trial;
    }
    if (trial_error <= state.previous_error || state.previous == state.best) {
        state.previous2 = state.previous;
        state.previous2_error = state.previous_error;
        state.previous = trial;
        state.previous_error = trial_error;
        return;
    }
    if (trial_error <= state.previous2_error || state.previous2 == state.best ||
        state.previous2 == state.previous) {
        state.previous2 = trial;
        state.previous2_error = trial_error;
    }
}

inline double estimate_scene_cct(const Vec3& neutral, const Matrix3& color_matrix1,
                                 const Matrix3& color_matrix2, double cct1, double cct2) {
    BrentSearchState state{};
    state.best_error = cct_error(neutral, color_matrix1, color_matrix2, cct1, cct2, state.best);
    state.previous_error = state.best_error;
    state.previous2_error = state.best_error;

    for (int i = 0; i < 80; ++i) {
        const auto midpoint = 0.5 * (state.lower + state.upper);
        const auto tolerance = (kBrentTolerance * std::abs(state.best)) + kBrentTolerance;
        if (std::abs(state.best - midpoint) <=
            (2.0 * tolerance) - (0.5 * (state.upper - state.lower))) {
            break;
        }

        state.step = next_brent_step(state, midpoint, tolerance);
        auto trial_step = std::copysign(tolerance, state.step);
        if (std::abs(state.step) >= tolerance) {
            trial_step = state.step;
        }
        const auto trial = state.best + trial_step;
        const auto trial_error =
            cct_error(neutral, color_matrix1, color_matrix2, cct1, cct2, trial);
        if (trial_error <= state.best_error) {
            accept_trial(state, trial, trial_error);
        } else {
            reject_trial(state, trial, trial_error);
        }
    }
    return state.best;
}

inline sdk::Result<CalibrationResult> interpolate_calibration(
    const sdk::CalibrationView& calibration, const Vec3& neutral) {
    const auto color_matrix1 =
        required_matrix(calibration.color_matrix1, "wb missing ColorMatrix1");
    if (!color_matrix1) {
        return tl::unexpected(color_matrix1.error());
    }
    const auto color_matrix2 =
        required_matrix(calibration.color_matrix2, "wb missing ColorMatrix2");
    if (!color_matrix2) {
        return tl::unexpected(color_matrix2.error());
    }
    const auto cct1 = calibration_illuminant_cct(calibration.calibration_illuminant1);
    const auto cct2 = calibration_illuminant_cct(calibration.calibration_illuminant2);
    if (!cct1.has_value() || !cct2.has_value()) {
        return tl::unexpected(
            sdk::Error{CPIPE_NEED_METADATA, "wb unsupported calibration illuminant"});
    }

    CalibrationResult out{};
    out.scene_cct = estimate_scene_cct(neutral, *color_matrix1, *color_matrix2, *cct1, *cct2);
    out.blend_weight = reciprocal_cct_weight(*cct1, *cct2, out.scene_cct);
    if (calibration.forward_matrix1.has_value() && calibration.forward_matrix2.has_value()) {
        out.camera_to_xyz_d50 =
            lerp_matrix(calibration.forward_matrix1.value_or(Matrix3{}),
                        calibration.forward_matrix2.value_or(Matrix3{}), out.blend_weight);
        return out;
    }

    const auto color_matrix = lerp_matrix(*color_matrix1, *color_matrix2, out.blend_weight);
    const auto inverse = inverse_matrix(color_matrix);
    if (!inverse) {
        return tl::unexpected(sdk::Error{CPIPE_NEED_METADATA, "wb singular ColorMatrix"});
    }
    out.camera_to_xyz_d50 = *inverse;
    return out;
}

inline sdk::Result<void> set_float_blob(sdk::MetadataBuilder& builder, std::string_view key,
                                        std::span<const float> values) {
    return builder.set_blob(key, std::as_bytes(values));
}

inline sdk::Result<void> emit_wb_blobs(sdk::MetadataBuilder& builder, const Vec3& neutral,
                                       const CalibrationResult& calibration) {
    const std::array<float, 3> camera_diag{1.0F / neutral[0], 1.0F / neutral[1], 1.0F / neutral[2]};
    const std::array<float, 1> cct_blob{static_cast<float>(calibration.scene_cct)};
    const std::array<float, 1> weight_blob{calibration.blend_weight};
    if (const auto set = set_float_blob(builder, kCameraDiagBlob, camera_diag); !set) {
        return tl::unexpected(set.error());
    }
    if (const auto set = set_float_blob(builder, kCameraToXyzBlob, calibration.camera_to_xyz_d50);
        !set) {
        return tl::unexpected(set.error());
    }
    if (const auto set = set_float_blob(builder, kSceneCctBlob, cct_blob); !set) {
        return tl::unexpected(set.error());
    }
    if (const auto set = set_float_blob(builder, kBlendWeightBlob, weight_blob); !set) {
        return tl::unexpected(set.error());
    }
    return {};
}

}  // namespace cpipe::nodes::wb_detail
