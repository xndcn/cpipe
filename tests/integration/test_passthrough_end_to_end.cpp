// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/nodes/Passthrough.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::vector<std::uint8_t> make_gradient() {
    constexpr std::size_t width = 64;
    constexpr std::size_t height = 64;
    std::vector<std::uint8_t> bytes(width * height * 4);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto offset = (y * width + x) * 4;
            bytes[offset + 0] = static_cast<std::uint8_t>(x);
            bytes[offset + 1] = static_cast<std::uint8_t>(y);
            bytes[offset + 2] = static_cast<std::uint8_t>((x + y) & 0xffU);
            bytes[offset + 3] = 255;
        }
    }
    return bytes;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    const auto size = std::filesystem::file_size(path);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
    return bytes;
}

}  // namespace

TEST_CASE("test_passthrough_end_to_end: registry load scheduler compute and output match") {
    const auto temp = std::filesystem::temp_directory_path();
    const auto input_path = temp / "cpipe_passthrough_gradient.bin";
    const auto output_path = temp / "cpipe_passthrough_gradient_out.bin";
    const auto pipeline_path =
        std::filesystem::path(CPIPE_SOURCE_DIR) / "tests/fixtures/passthrough.json";

    const auto input = make_gradient();
    write_file(input_path, input);

    std::string error;
    auto pipeline = cpipe::runtime::Pipeline::load_file(pipeline_path, &error);
    REQUIRE(pipeline.has_value());

    cpipe::runtime::ComputeContext compute;
    cpipe::nodes::register_passthrough_halide(compute);
    REQUIRE(pipeline->run_file(input_path, output_path, compute, &error) == CPIPE_OK);

    CHECK(read_file(output_path) == input);
}
