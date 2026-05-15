// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "full_classic_pipeline_test_utils.hpp"

TEST_CASE("full classic HDR pipeline converts Pixel 8 Pro DNG to PQ HEIF") {
    const auto root = cpipe::tests::source_path();
    const auto input_path = root / "tests" / "corpus" / "pixel8pro.dng";
    const auto pipeline_path =
        root / "examples" / "pipelines" / "full-classic-pipeline-hdr.cpipe.json";
    const auto output_path = std::filesystem::temp_directory_path() / "cpipe_full_classic_hdr.heif";

    cpipe::tests::require_cli_pipeline_run(input_path, pipeline_path, output_path);

    const auto info = cpipe::tests::read_heif(output_path);
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.luma_bits_per_pixel == 10);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 9);
    REQUIRE(info.nclx_transfer_characteristics == 16);
    REQUIRE(info.nclx_matrix_coefficients == 9);
    REQUIRE(info.nclx_full_range);
    REQUIRE(info.has_mastering_display);
    REQUIRE(info.has_content_light_level);
    REQUIRE(info.max_content_light_level > 0);
    REQUIRE(info.max_pic_average_light_level > 0);
}
