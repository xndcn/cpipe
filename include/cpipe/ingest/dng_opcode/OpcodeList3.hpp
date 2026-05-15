// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cpipe::ingest::dng_opcode {

class OpcodeList3 {
public:
    enum class OpcodeId : std::uint32_t {
        Unknown = 0,
        WarpRectilinear = 1,
        FixVignetteRadial = 3,
        FixBadPixelsConstant = 4,
        FixBadPixelsList = 5,
        TrimBounds = 6,
    };

    struct WarpCoefficient {
        double kr0{0.0};
        double kr1{0.0};
        double kr2{0.0};
        double kr3{0.0};
        double kt0{0.0};
        double kt1{0.0};
    };

    struct WarpRectilinear {
        std::vector<WarpCoefficient> coefficients;
        double cx_hat{0.0};
        double cy_hat{0.0};
    };

    struct FixVignetteRadial {
        std::array<double, 5> k{};
        double cx_hat{0.0};
        double cy_hat{0.0};
    };

    struct FixBadPixelsConstant {
        std::uint32_t constant{0};
        std::uint32_t bayer_phase{0};
    };

    struct BadPoint {
        std::uint32_t row{0};
        std::uint32_t column{0};
    };

    struct BadRect {
        std::uint32_t top{0};
        std::uint32_t left{0};
        std::uint32_t bottom{0};
        std::uint32_t right{0};
    };

    struct FixBadPixelsList {
        std::uint32_t bayer_phase{0};
        std::vector<BadPoint> bad_points;
        std::vector<BadRect> bad_rects;
    };

    struct TrimBounds {
        std::uint32_t top{0};
        std::uint32_t left{0};
        std::uint32_t bottom{0};
        std::uint32_t right{0};
    };

    struct Opcode {
        OpcodeId id{OpcodeId::Unknown};
        std::uint32_t raw_id{0};
        std::uint32_t dng_version{0};
        std::uint32_t flags{0};
        bool optional{false};
        std::vector<std::byte> raw_params;
        std::optional<WarpRectilinear> warp_rectilinear;
        std::optional<FixVignetteRadial> fix_vignette_radial;
        std::optional<FixBadPixelsConstant> fix_bad_pixels_constant;
        std::optional<FixBadPixelsList> fix_bad_pixels_list;
        std::optional<TrimBounds> trim_bounds;
    };

    struct ParseResult {
        cpipe_status_t status{CPIPE_FAILED};
        std::vector<Opcode> opcodes;
        std::string message;
    };

    [[nodiscard]] static ParseResult parse(std::span<const std::byte> bytes);
};

}  // namespace cpipe::ingest::dng_opcode
