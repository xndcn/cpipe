// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cpipe::tests {

inline void append_opcode3_be32(std::vector<std::byte>* out, std::uint32_t value) {
    out->push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
    out->push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    out->push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    out->push_back(static_cast<std::byte>(value & 0xffU));
}

inline void append_opcode3_be64(std::vector<std::byte>* out, std::uint64_t value) {
    append_opcode3_be32(out, static_cast<std::uint32_t>(value >> 32U));
    append_opcode3_be32(out, static_cast<std::uint32_t>(value & 0xffffffffU));
}

inline void append_opcode3_double(std::vector<std::byte>* out, double value) {
    append_opcode3_be64(out, std::bit_cast<std::uint64_t>(value));
}

inline std::vector<std::byte> warp_rectilinear_params(double kr0) {
    std::vector<std::byte> out;
    append_opcode3_be32(&out, 1);
    append_opcode3_double(&out, kr0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.5);
    append_opcode3_double(&out, 0.5);
    return out;
}

inline std::vector<std::byte> fix_vignette_radial_params(double k0) {
    std::vector<std::byte> out;
    append_opcode3_double(&out, k0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.0);
    append_opcode3_double(&out, 0.5);
    append_opcode3_double(&out, 0.5);
    return out;
}

inline std::vector<std::byte> fix_bad_pixels_constant_params(std::uint32_t constant) {
    std::vector<std::byte> out;
    append_opcode3_be32(&out, constant);
    append_opcode3_be32(&out, 0);
    return out;
}

inline std::vector<std::byte> fix_bad_pixels_list_params(std::uint32_t row, std::uint32_t column) {
    std::vector<std::byte> out;
    append_opcode3_be32(&out, 0);
    append_opcode3_be32(&out, 1);
    append_opcode3_be32(&out, 0);
    append_opcode3_be32(&out, row);
    append_opcode3_be32(&out, column);
    return out;
}

inline std::vector<std::byte> trim_bounds_params(std::uint32_t top, std::uint32_t left,
                                                 std::uint32_t bottom, std::uint32_t right) {
    std::vector<std::byte> out;
    append_opcode3_be32(&out, top);
    append_opcode3_be32(&out, left);
    append_opcode3_be32(&out, bottom);
    append_opcode3_be32(&out, right);
    return out;
}

inline std::vector<std::byte> opcode_list_3_with(
    const std::vector<std::pair<std::uint32_t, std::vector<std::byte>>>& opcodes,
    std::uint32_t flags = 0) {
    std::vector<std::byte> out;
    append_opcode3_be32(&out, static_cast<std::uint32_t>(opcodes.size()));
    for (const auto& [id, params] : opcodes) {
        append_opcode3_be32(&out, id);
        append_opcode3_be32(&out, 0x00010003U);
        append_opcode3_be32(&out, flags);
        append_opcode3_be32(&out, static_cast<std::uint32_t>(params.size()));
        out.insert(out.end(), params.begin(), params.end());
    }
    return out;
}

}  // namespace cpipe::tests
