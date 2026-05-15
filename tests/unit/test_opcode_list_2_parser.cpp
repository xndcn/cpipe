// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/ingest/dng_opcode/OpcodeList2.hpp>

#include "gainmap_test_fixture.hpp"

TEST_CASE("OpcodeList2 parser reads one-plane GainMap") {
    const auto params =
        cpipe::tests::gain_map_params(2, 2, 0, 1, 2.0, 2.0, {1.0F, 2.0F, 3.0F, 4.0F});
    const auto bytes = cpipe::tests::opcode_list_2_with_gain_maps({params});

    const auto parsed = cpipe::ingest::dng_opcode::OpcodeList2::parse_gain_maps(bytes);

    REQUIRE(parsed.status == CPIPE_OK);
    REQUIRE(parsed.gain_maps.size() == 1);
    const auto& map = parsed.gain_maps.front();
    REQUIRE(map.plane == 0);
    REQUIRE(map.planes == 1);
    REQUIRE(map.map_points_v == 2);
    REQUIRE(map.map_points_h == 2);
    REQUIRE(map.map_spacing_v == 2.0);
    REQUIRE(map.map_spacing_h == 2.0);
    REQUIRE(map.gain == std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F});
}

TEST_CASE("OpcodeList2 parser reads four-plane GainMap set") {
    std::vector<std::vector<std::byte>> opcodes;
    for (std::uint32_t plane = 0; plane < 4; ++plane) {
        opcodes.push_back(cpipe::tests::gain_map_params(1, 1, plane, 4, 1.0, 1.0,
                                                        {1.0F + static_cast<float>(plane)}));
    }
    const auto bytes = cpipe::tests::opcode_list_2_with_gain_maps(opcodes);

    const auto parsed = cpipe::ingest::dng_opcode::OpcodeList2::parse_gain_maps(bytes);

    REQUIRE(parsed.status == CPIPE_OK);
    REQUIRE(parsed.gain_maps.size() == 4);
    for (std::uint32_t plane = 0; plane < 4; ++plane) {
        REQUIRE(parsed.gain_maps[plane].plane == plane);
        REQUIRE(parsed.gain_maps[plane].planes == 4);
        REQUIRE(parsed.gain_maps[plane].gain ==
                std::vector<float>{1.0F + static_cast<float>(plane)});
    }
}
