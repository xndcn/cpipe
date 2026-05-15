// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/ingest/dng_opcode/OpcodeList3.hpp>

#include "opcode_list_3_test_fixture.hpp"

TEST_CASE("OpcodeList3 parser reads the five P2 opcode parameter blocks") {
    const auto bytes = cpipe::tests::opcode_list_3_with({
        {1, cpipe::tests::warp_rectilinear_params(1.0)},
        {3, cpipe::tests::fix_vignette_radial_params(0.25)},
        {4, cpipe::tests::fix_bad_pixels_constant_params(0)},
        {5, cpipe::tests::fix_bad_pixels_list_params(1, 2)},
        {6, cpipe::tests::trim_bounds_params(1, 1, 3, 4)},
    });

    const auto parsed = cpipe::ingest::dng_opcode::OpcodeList3::parse(bytes);

    REQUIRE(parsed.status == CPIPE_OK);
    REQUIRE(parsed.opcodes.size() == 5);
    REQUIRE(parsed.opcodes[0].id ==
            cpipe::ingest::dng_opcode::OpcodeList3::OpcodeId::WarpRectilinear);
    REQUIRE(parsed.opcodes[0].warp_rectilinear.has_value());
    REQUIRE(parsed.opcodes[0].warp_rectilinear->coefficients.size() == 1);
    REQUIRE(parsed.opcodes[1].fix_vignette_radial.has_value());
    REQUIRE(parsed.opcodes[2].fix_bad_pixels_constant.has_value());
    REQUIRE(parsed.opcodes[3].fix_bad_pixels_list.has_value());
    REQUIRE(parsed.opcodes[3].fix_bad_pixels_list->bad_points.size() == 1);
    REQUIRE(parsed.opcodes[4].trim_bounds.has_value());
}

TEST_CASE("OpcodeList3 parser preserves optional unknown opcodes for graceful skip") {
    const auto bytes = cpipe::tests::opcode_list_3_with({{99, {std::byte{1}, std::byte{2}}}}, 1);

    const auto parsed = cpipe::ingest::dng_opcode::OpcodeList3::parse(bytes);

    REQUIRE(parsed.status == CPIPE_OK);
    REQUIRE(parsed.opcodes.size() == 1);
    REQUIRE(parsed.opcodes[0].id == cpipe::ingest::dng_opcode::OpcodeList3::OpcodeId::Unknown);
    REQUIRE(parsed.opcodes[0].optional);
}
