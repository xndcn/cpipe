// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/HeifReader.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace cpipe::tests {

inline std::filesystem::path source_path() {
    return std::filesystem::path{CPIPE_SOURCE_DIR};
}

inline std::string shell_quote(const std::filesystem::path& path) {
    return "'" + path.string() + "'";
}

inline void require_cli_pipeline_run(const std::filesystem::path& input_path,
                                     const std::filesystem::path& pipeline_path,
                                     const std::filesystem::path& output_path) {
    REQUIRE(std::filesystem::exists(input_path));
    REQUIRE(std::filesystem::exists(pipeline_path));
    std::filesystem::remove(output_path);

    const auto command = shell_quote(CPIPE_CLI_PATH) + " run " + shell_quote(input_path) + " -p " +
                         shell_quote(pipeline_path) + " -o " + shell_quote(output_path);
    REQUIRE(std::system(command.c_str()) == 0);
    REQUIRE(std::filesystem::file_size(output_path) > 0);
}

inline cpipe::color::HeifInfo read_heif(const std::filesystem::path& output_path) {
    std::string error;
    cpipe::color::HeifInfo info;
    const auto status = cpipe::color::read_heif_sdr(output_path, &info, &error);
    INFO(error);
    REQUIRE(status == CPIPE_OK);
    return info;
}

}  // namespace cpipe::tests
