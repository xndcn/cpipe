// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cpipe::tests {

inline void append_be32(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
    out.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::byte>(value & 0xffU));
}

inline void append_be64(std::vector<std::byte>& out, std::uint64_t value) {
    append_be32(out, static_cast<std::uint32_t>(value >> 32U));
    append_be32(out, static_cast<std::uint32_t>(value & 0xffffffffU));
}

inline void append_be_double(std::vector<std::byte>& out, double value) {
    append_be64(out, std::bit_cast<std::uint64_t>(value));
}

inline void append_be_float(std::vector<std::byte>& out, float value) {
    append_be32(out, std::bit_cast<std::uint32_t>(value));
}

inline std::vector<std::byte> gain_map_params(std::uint32_t width, std::uint32_t height,
                                              std::uint32_t plane, std::uint32_t planes,
                                              double spacing_v, double spacing_h,
                                              const std::vector<float>& gains) {
    std::vector<std::byte> out;
    append_be32(out, 0);
    append_be32(out, 0);
    append_be32(out, height);
    append_be32(out, width);
    append_be32(out, plane);
    append_be32(out, planes);
    append_be32(out, 1);
    append_be32(out, 1);
    append_be32(out, height);
    append_be32(out, width);
    append_be_double(out, spacing_v);
    append_be_double(out, spacing_h);
    append_be_double(out, 0.0);
    append_be_double(out, 0.0);
    append_be32(out, 1);
    for (const auto gain : gains) {
        append_be_float(out, gain);
    }
    return out;
}

inline std::vector<std::byte> opcode_list_2_with_gain_maps(
    const std::vector<std::vector<std::byte>>& params) {
    std::vector<std::byte> out;
    append_be32(out, static_cast<std::uint32_t>(params.size()));
    for (const auto& param : params) {
        append_be32(out, 9);
        append_be32(out, 0x00010003U);
        append_be32(out, 0);
        append_be32(out, static_cast<std::uint32_t>(param.size()));
        out.insert(out.end(), param.begin(), param.end());
    }
    return out;
}

}  // namespace cpipe::tests
