// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cpipe::color {

struct Cube3dLut {
    std::uint32_t size{0};
    std::vector<float> values;

    [[nodiscard]] std::array<float, 3> at(std::uint32_t r, std::uint32_t g, std::uint32_t b) const;
};

class Cube3dLutLoader {
public:
    [[nodiscard]] static cpipe_status_t load(const std::filesystem::path& path, Cube3dLut* out,
                                             std::string* error);
};

}  // namespace cpipe::color
