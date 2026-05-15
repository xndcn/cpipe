// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cstdint>
#include <vector>

namespace cpipe::color {

class IccV4_4Writer {
public:
    // ICC v4.4 output profile for BT.2020 PQ HEIF signalling; see
    // docs/research/13-color-management.md §3.1/§3.10 and P2-PD-29.
    [[nodiscard]] static std::vector<std::uint8_t> bt2020_pq();
};

}  // namespace cpipe::color
