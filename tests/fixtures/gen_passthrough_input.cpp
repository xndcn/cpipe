// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gen_passthrough_input <output>\n";
        return 2;
    }

    constexpr uint32_t kWidth = 64;
    constexpr uint32_t kHeight = 64;
    std::array<uint8_t, kWidth * kHeight * 4> bytes{};
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const auto offset = static_cast<std::size_t>((y * kWidth + x) * 4);
            bytes[offset + 0] = static_cast<uint8_t>(x);
            bytes[offset + 1] = static_cast<uint8_t>(y);
            bytes[offset + 2] = static_cast<uint8_t>((x + y) & 0xffu);
            bytes[offset + 3] = 255;
        }
    }

    std::ofstream out(argv[1], std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out ? 0 : 1;
}
