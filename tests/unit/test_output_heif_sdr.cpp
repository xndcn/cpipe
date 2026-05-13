// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>
#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/HeifReader.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_output_heif_sdr();

namespace {

std::filesystem::path temp_heif_path() {
    return std::filesystem::temp_directory_path() / "cpipe_output_heif_sdr_test.heif";
}

std::string run_ldd_on_cli() {
    std::array<char, 512> buffer{};
    std::string output;
    const std::string command = std::string{"ldd \""} + CPIPE_CLI_PATH + "\" 2>&1";
    auto* pipe = popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    REQUIRE(pclose(pipe) == 0);
    return output;
}

void process_output_node(const cpipe_plugin_desc_t& desc,
                         const std::shared_ptr<cpipe::tests::CpuBuffer>& input,
                         const std::filesystem::path& path) {
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(nlohmann::json{{"path", path.string()}});
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_process_ctx process{
        .compute = nullptr,
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = nullptr,
        .n_out = 0,
        .out_metadata = nullptr,
    };

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    REQUIRE(status == CPIPE_OK);
}

}  // namespace

TEST_CASE("Bundled OCIO config round-trips sRGB gradient within one 8-bit code") {
    namespace OCIO = OCIO_NAMESPACE;

    const auto config_path =
        std::filesystem::path{CPIPE_SOURCE_DIR} / "share/cpipe/ocio/v0.1/config.ocio";
    const auto config = OCIO::Config::CreateFromFile(config_path.string().c_str());
    REQUIRE(config);

    const auto to_scene =
        config->getProcessor("output_srgb", "scene_linear_rec2020")->getDefaultCPUProcessor();
    const auto to_output =
        config->getProcessor("scene_linear_rec2020", "output_srgb")->getDefaultCPUProcessor();
    REQUIRE(to_scene);
    REQUIRE(to_output);

    for (int code = 0; code <= 255; code += 17) {
        float rgba[4] = {static_cast<float>(code) / 255.0F, static_cast<float>(code) / 255.0F,
                         static_cast<float>(code) / 255.0F, 1.0F};
        to_scene->applyRGBA(rgba);
        to_output->applyRGBA(rgba);
        for (int channel = 0; channel < 3; ++channel) {
            const auto rounded = static_cast<int>((rgba[channel] * 255.0F) + 0.5F);
            REQUIRE(std::abs(rounded - code) <= 1);
        }
    }
}

TEST_CASE("output.heif_sdr writes an 8-bit sRGB HEIF with ICC and NCLX metadata") {
    cpipe_link_builtin_output_heif_sdr();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.output.heif_sdr");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("caps").at("precision") ==
            nlohmann::json::array({"f16"}));
    REQUIRE(manifest.at("color").at("input_role") == "scene_linear_rec2020");

    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 64;
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic", "white_balance",
                               "color_matrix"};

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(width, height), cpipe::tests::BufferUsage::Input |
                                                        cpipe::tests::BufferUsage::CpuRead |
                                                        cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);

    std::vector<std::array<float, 4>> pixels;
    pixels.reserve(width * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto r = static_cast<float>(x) / static_cast<float>(width - 1U);
            const auto g = static_cast<float>(y) / static_cast<float>(height - 1U);
            const auto b =
                static_cast<float>(x + y) / static_cast<float>((width - 1U) + (height - 1U));
            pixels.push_back({r, g, b, 1.0F});
        }
    }
    cpipe::tests::write_rgba16(*input, pixels);

    const auto path = temp_heif_path();
    std::filesystem::remove(path);
    process_output_node(*desc, input, path);
    REQUIRE(std::filesystem::file_size(path) > 0);

    cpipe::color::HeifInfo info;
    std::string error;
    const auto read_status = cpipe::color::read_heif_sdr(path, &info, &error);
    INFO(error);
    REQUIRE(read_status == CPIPE_OK);
    REQUIRE(info.width == width);
    REQUIRE(info.height == height);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);
    REQUIRE(info.decoded_rgba.size() == static_cast<std::size_t>(width) * height * 4U);
}

TEST_CASE("Debug CLI linkage does not pull libx265") {
    const auto ldd_output = run_ldd_on_cli();
    REQUIRE(ldd_output.find("libx265") == std::string::npos);
    REQUIRE(ldd_output.find("x265") == std::string::npos);
}
