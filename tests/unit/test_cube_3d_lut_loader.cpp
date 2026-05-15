// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/Cube3dLutLoader.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path temp_lut_path(std::string_view name) {
    return std::filesystem::temp_directory_path() /
           (std::string{"cpipe_test_"} + std::string{name});
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out{path};
    REQUIRE(out.good());
    out << text;
}

void require_identity_lut(const cpipe::color::Cube3dLut& lut) {
    REQUIRE(lut.size == 2);
    REQUIRE(lut.values.size() == 24);
    constexpr float kOneLsb = 1.0F / 65535.0F;
    for (std::uint32_t r = 0; r < lut.size; ++r) {
        for (std::uint32_t g = 0; g < lut.size; ++g) {
            for (std::uint32_t b = 0; b < lut.size; ++b) {
                const auto rgb = lut.at(r, g, b);
                REQUIRE(rgb[0] == Catch::Approx(static_cast<float>(r)).margin(kOneLsb));
                REQUIRE(rgb[1] == Catch::Approx(static_cast<float>(g)).margin(kOneLsb));
                REQUIRE(rgb[2] == Catch::Approx(static_cast<float>(b)).margin(kOneLsb));
            }
        }
    }
}

}  // namespace

TEST_CASE("Cube3dLutLoader parses standard .cube identity LUT") {
    const auto path = temp_lut_path("identity.cube");
    write_text_file(path, R"cube(# cpipe identity fixture
TITLE "identity"
LUT_3D_SIZE 2
DOMAIN_MIN 0 0 0
DOMAIN_MAX 1 1 1
0 0 0
0 0 1
0 1 0
0 1 1
1 0 0
1 0 1
1 1 0
1 1 1
)cube");

    cpipe::color::Cube3dLut lut;
    std::string error;
    REQUIRE(cpipe::color::Cube3dLutLoader::load(path, &lut, &error) == CPIPE_OK);
    INFO(error);
    require_identity_lut(lut);
}

TEST_CASE("Cube3dLutLoader parses standard .spi3d identity LUT") {
    const auto path = temp_lut_path("identity.spi3d");
    write_text_file(path, R"spi3d(SPILUT 1.0
3 3
2 2 2
0 0 0 0 0 0
0 0 1 0 0 1
0 1 0 0 1 0
0 1 1 0 1 1
1 0 0 1 0 0
1 0 1 1 0 1
1 1 0 1 1 0
1 1 1 1 1 1
)spi3d");

    cpipe::color::Cube3dLut lut;
    std::string error;
    REQUIRE(cpipe::color::Cube3dLutLoader::load(path, &lut, &error) == CPIPE_OK);
    INFO(error);
    require_identity_lut(lut);
}
