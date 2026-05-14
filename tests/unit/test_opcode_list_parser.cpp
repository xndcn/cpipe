// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/ingest/dng_opcode/OpcodeList.hpp>
#include <cstddef>
#include <vector>

#include "dng_test_fixture.hpp"

TEST_CASE("OpcodeListParser reads DNG metadata and preserves opcode bytes") {
    const auto path = cpipe::tests::write_synthetic_dng("opcode_parser", {});

    const auto parsed = cpipe::ingest::dng_opcode::OpcodeListParser::parse(path);
    INFO(parsed.message);
    REQUIRE(parsed.status == CPIPE_OK);
    REQUIRE(parsed.metadata.width == 4);
    REQUIRE(parsed.metadata.height == 3);
    REQUIRE(parsed.metadata.bits_per_sample == 16);

    REQUIRE(parsed.metadata.calibration.cfa.has_value());
    REQUIRE(parsed.metadata.calibration.cfa->pattern == std::array<std::uint8_t, 4>{0, 1, 1, 2});
    REQUIRE(parsed.metadata.linearization_table == std::vector<std::uint16_t>{0, 128, 1024, 4095});
    REQUIRE(parsed.metadata.calibration.linearization_table.has_value());
    REQUIRE(parsed.metadata.calibration.black_level[0] == Catch::Approx(64.0F));
    REQUIRE(parsed.metadata.calibration.black_level[3] == Catch::Approx(67.0F));
    REQUIRE(parsed.metadata.calibration.white_level == 4095);
    REQUIRE(parsed.metadata.calibration.color_matrix1.has_value());
    REQUIRE(parsed.metadata.calibration.color_matrix2.has_value());
    REQUIRE(parsed.metadata.calibration.forward_matrix1.has_value());
    REQUIRE(parsed.metadata.calibration.forward_matrix2.has_value());
    REQUIRE(parsed.metadata.capture.as_shot_neutral[1] == Catch::Approx(2.0F));
    REQUIRE(parsed.metadata.capture.iso == 400);
    REQUIRE(parsed.metadata.capture.exposure_time_ns == 8'000'000);
    REQUIRE(parsed.metadata.capture.lens_aperture == Catch::Approx(1.7F));
    REQUIRE(parsed.metadata.capture.lens_focal_length_mm == Catch::Approx(4.3F));
    REQUIRE(parsed.metadata.capture.orientation == 1);

    REQUIRE(parsed.metadata.active_area.has_value());
    REQUIRE(parsed.metadata.active_area->width == 4);
    REQUIRE(parsed.metadata.active_area->height == 3);
    REQUIRE(parsed.metadata.exif_blob.size() == 6);
    REQUIRE(!parsed.metadata.xmp_blob.empty());
    REQUIRE(parsed.metadata.icc_blob ==
            std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    REQUIRE(parsed.metadata.opcode_list_1 ==
            std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    REQUIRE(parsed.metadata.opcode_list_2 ==
            std::vector<std::byte>{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}});
    REQUIRE(parsed.metadata.opcode_list_3 ==
            std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1},
                                   std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}});
}

TEST_CASE("OpcodeListParser rejects non-2x2 Bayer CFA") {
    cpipe::tests::SyntheticDngOptions options;
    options.cfa_repeat = {4, 4};
    options.cfa_pattern = {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2};
    const auto path = cpipe::tests::write_synthetic_dng("quad_bayer", options);

    const auto parsed = cpipe::ingest::dng_opcode::OpcodeListParser::parse(path);
    REQUIRE(parsed.status == CPIPE_UNSUPPORTED);
}
