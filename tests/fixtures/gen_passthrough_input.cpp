// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cstdint>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }

    std::ofstream out{std::string{argv[1]}, std::ios::binary};
    if (!out) {
        return 1;
    }

    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            const unsigned char pixel[4] = {
                static_cast<unsigned char>(x & 0xffU),
                static_cast<unsigned char>(y & 0xffU),
                static_cast<unsigned char>((x + y) & 0xffU),
                255U,
            };
            out.write(reinterpret_cast<const char*>(pixel), sizeof(pixel));
        }
    }
    return out.good() ? 0 : 1;
}
