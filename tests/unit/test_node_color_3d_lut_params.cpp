// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

#include "node_param_test_utils.hpp"

void cpipe_link_builtin_color_3d_lut();

namespace {

std::filesystem::path write_interpolation_lut() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_t12_interp_2.cube";
    std::ofstream out{path};
    REQUIRE(out.good());
    out << "TITLE \"cpipe t12 interpolation\"\n";
    out << "LUT_3D_SIZE 2\n";
    out << "DOMAIN_MIN 0 0 0\n";
    out << "DOMAIN_MAX 1 1 1\n";
    for (int r = 0; r < 2; ++r) {
        for (int g = 0; g < 2; ++g) {
            for (int b = 0; b < 2; ++b) {
                const auto value = static_cast<float>(r * g);
                out << value << ' ' << value << ' ' << value << '\n';
            }
        }
    }
    return path;
}

}  // namespace

TEST_CASE("color.3d_lut interpolation live param selects tetrahedral or trilinear") {
    cpipe_link_builtin_color_3d_lut();
    cpipe::runtime::Registry registry;
    const auto& desc = cpipe::tests::require_builtin_node(registry, "com.cpipe.color.3d_lut");

    const auto manifest = nlohmann::json::parse(desc.manifest_json);
    REQUIRE(manifest.at("params").at(0).at("name") == "lut_path");
    REQUIRE(manifest.at("params").at(1).at("name") == "interpolation");

    const auto lut_path = write_interpolation_lut();
    const std::vector<std::array<float, 4>> input{{0.5F, 0.5F, 0.5F, 1.0F}};
    const auto tetra = cpipe::tests::run_single_input_node_with_params(
        desc, input, 1, 1, {{"lut_path", lut_path.string()}, {"interpolation", "tetrahedral"}});
    const auto trilinear = cpipe::tests::run_single_input_node_with_params(
        desc, input, 1, 1, {{"lut_path", lut_path.string()}, {"interpolation", "trilinear"}});

    REQUIRE(cpipe::tests::max_channel_delta(tetra, trilinear) > 0.1F);
}
