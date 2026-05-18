// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>

namespace cpipe::nodes::detail {

struct Lut3dParamHeader {
    std::uint32_t size{0};
    std::uint32_t value_count{0};
    std::uint32_t interpolation{0};
};

}  // namespace cpipe::nodes::detail
