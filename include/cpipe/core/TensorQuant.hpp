// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace cpipe::compute {

struct TensorQuant {
    enum class Scheme : std::uint8_t { None = 0, Symmetric = 1, Asymmetric = 2 };

    Scheme scheme{Scheme::None};
    std::optional<std::int8_t> axis;
    std::vector<float> scales;
    std::vector<std::int32_t> zero_points;
};

}  // namespace cpipe::compute
