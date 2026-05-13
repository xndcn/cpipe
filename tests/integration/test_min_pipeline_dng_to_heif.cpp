// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/HeifReader.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

#include "../unit/dng_test_fixture.hpp"

void cpipe_link_all_builtin_nodes();

namespace {

std::filesystem::path source_path() {
    return std::filesystem::path{CPIPE_SOURCE_DIR};
}

std::string shell_quote(const std::filesystem::path& path) {
    return "'" + path.string() + "'";
}

}  // namespace

TEST_CASE("min pipeline converts a synthetic Bayer DNG to decodable SDR HEIF") {
    cpipe_link_all_builtin_nodes();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    const auto pipeline_path = source_path() / "examples" / "pipelines" / "min-pipeline.cpipe.json";
    REQUIRE(cpipe::runtime::Pipeline::load(pipeline_path, registry, &pipeline, &error) == CPIPE_OK);

    cpipe::tests::SyntheticDngOptions options;
    options.width = 16;
    options.height = 16;
    const auto input_path = cpipe::tests::write_synthetic_dng("min_pipeline", options);
    REQUIRE(pipeline.set_source("raw", "com.cpipe.builtin.dng_input",
                                nlohmann::json{{"path", input_path.string()}}) == CPIPE_OK);

    const auto output_path =
        std::filesystem::temp_directory_path() / "cpipe_min_pipeline_output.heif";
    std::filesystem::remove(output_path);
    REQUIRE(pipeline.run_to_file(output_path, &error) == CPIPE_OK);
    REQUIRE(std::filesystem::file_size(output_path) > 0);

    cpipe::color::HeifInfo info;
    const auto read_status = cpipe::color::read_heif_sdr(output_path, &info, &error);
    INFO(error);
    REQUIRE(read_status == CPIPE_OK);
    REQUIRE(info.width == 16);
    REQUIRE(info.height == 16);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);

    const auto cli_output_path =
        std::filesystem::temp_directory_path() / "cpipe_min_pipeline_cli_output.heif";
    std::filesystem::remove(cli_output_path);
    const auto command = shell_quote(CPIPE_CLI_PATH) + " run " + shell_quote(input_path) + " -p " +
                         shell_quote(pipeline_path) + " -o " + shell_quote(cli_output_path);
    REQUIRE(std::system(command.c_str()) == 0);
    REQUIRE(std::filesystem::file_size(cli_output_path) > 0);

    cpipe::color::HeifInfo cli_info;
    const auto cli_read_status = cpipe::color::read_heif_sdr(cli_output_path, &cli_info, &error);
    INFO(error);
    REQUIRE(cli_read_status == CPIPE_OK);
    REQUIRE(cli_info.width == 16);
    REQUIRE(cli_info.height == 16);
}

TEST_CASE("min pipeline converts the Pixel 8 Pro corpus DNG to decodable SDR HEIF") {
    const auto input_path = source_path() / "tests" / "corpus" / "pixel8pro.dng";
    const auto pipeline_path = source_path() / "examples" / "pipelines" / "min-pipeline.cpipe.json";
    REQUIRE(std::filesystem::exists(input_path));

    const auto cli_output_path =
        std::filesystem::temp_directory_path() / "cpipe_pixel8pro_min_pipeline.heif";
    std::filesystem::remove(cli_output_path);
    const auto command = shell_quote(CPIPE_CLI_PATH) + " run " + shell_quote(input_path) + " -p " +
                         shell_quote(pipeline_path) + " -o " + shell_quote(cli_output_path);
    REQUIRE(std::system(command.c_str()) == 0);
    REQUIRE(std::filesystem::file_size(cli_output_path) > 0);

    std::string error;
    cpipe::color::HeifInfo info;
    const auto read_status = cpipe::color::read_heif_sdr(cli_output_path, &info, &error);
    INFO(error);
    REQUIRE(read_status == CPIPE_OK);
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);
}
