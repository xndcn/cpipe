// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "full_classic_pipeline_test_utils.hpp"

TEST_CASE("full classic SDR pipeline converts Pixel 8 Pro DNG to SDR HEIF") {
    const auto root = cpipe::tests::source_path();
    const auto input_path = root / "tests" / "corpus" / "pixel8pro.dng";
    const auto pipeline_path = root / "examples" / "pipelines" / "full-classic-pipeline.cpipe.json";
    const auto output_path = std::filesystem::temp_directory_path() / "cpipe_full_classic_sdr.heif";

    cpipe::tests::require_cli_pipeline_run(input_path, pipeline_path, output_path);

    const auto info = cpipe::tests::read_heif(output_path);
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);
}

TEST_CASE("full classic SDR pipeline converts synthetic Quad Bayer DNG to SDR HEIF") {
    const auto root = cpipe::tests::source_path();
    const auto input_path = root / "tests" / "corpus" / "pixel8pro-qbc.dng";
    const auto pipeline_path = root / "examples" / "pipelines" / "full-classic-pipeline.cpipe.json";
    const auto output_path =
        std::filesystem::temp_directory_path() / "cpipe_full_classic_qbc_sdr.heif";

    cpipe::tests::require_cli_pipeline_run(input_path, pipeline_path, output_path);

    const auto info = cpipe::tests::read_heif(output_path);
    REQUIRE(info.width == 32);
    REQUIRE(info.height == 32);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);
}
