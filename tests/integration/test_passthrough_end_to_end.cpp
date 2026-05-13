// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

void cpipe_link_builtin_passthrough();

namespace {

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "tests" / "fixtures" / name;
}

std::filesystem::path binary_path(std::string_view name) {
    auto dir = std::filesystem::path{CPIPE_BINARY_DIR} / "tests" / "integration";
    std::filesystem::create_directories(dir);
    return dir / name;
}

void write_gradient(const std::filesystem::path& path) {
    std::ofstream out{path, std::ios::binary};
    REQUIRE(out.good());

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
    REQUIRE(out.good());
}

std::vector<char> read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("Passthrough pipeline runs end-to-end") {
    cpipe_link_builtin_passthrough();

    const auto input_path = binary_path("passthrough_input.bin");
    const auto output_path = binary_path("passthrough_output.bin");
    write_gradient(input_path);

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough-v0.2.json"), registry,
                                           &pipeline, &error) == CPIPE_OK);
    REQUIRE(pipeline.run_file(input_path, output_path, &error) == CPIPE_OK);

    REQUIRE(read_file(output_path) == read_file(input_path));
}
