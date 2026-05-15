// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/color/HeifReader.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

cpipe::tests::BufferLayout rgba8_layout(std::uint32_t width, std::uint32_t height) {
    cpipe::tests::BufferLayout layout{};
    layout.kind = cpipe::tests::BufferKind::Image2D;
    layout.format = cpipe::tests::PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
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

TEST_CASE("output.heif_sdr writes an 8-bit sRGB HEIF with ICC and NCLX metadata") {
    cpipe_link_builtin_output_heif_sdr();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.output.heif_sdr");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("caps").at("precision") == nlohmann::json::array({"u8"}));
    REQUIRE(manifest.at("color").at("input_role") == "output_srgb");

    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 64;
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "output_srgb";
    metadata->applied_steps = {"linearization", "black_white_scaling",
                               "demosaic",      "white_balance",
                               "color_matrix",  "color.scene_linear_to_display"};

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(rgba8_layout(width, height),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);

    auto* pixels =
        static_cast<std::uint8_t*>(input->lock_cpu(cpipe::tests::IBuffer::CpuAccess::Write));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto base = (static_cast<std::size_t>(y) * width + x) * 4U;
            pixels[base + 0U] = static_cast<std::uint8_t>((x * 255U) / (width - 1U));
            pixels[base + 1U] = static_cast<std::uint8_t>((y * 255U) / (height - 1U));
            pixels[base + 2U] =
                static_cast<std::uint8_t>(((x + y) * 255U) / ((width - 1U) + (height - 1U)));
            pixels[base + 3U] = 255;
        }
    }
    input->unlock_cpu();
    input->flush_cpu_writes();

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

TEST_CASE("min pipeline inserts the display transform before output.heif_sdr") {
    const auto path =
        std::filesystem::path{CPIPE_SOURCE_DIR} / "examples/pipelines/min-pipeline.cpipe.json";
    std::ifstream file{path};
    REQUIRE(file.good());
    const auto doc = nlohmann::json::parse(file);

    bool saw_display = false;
    for (const auto& node : doc.at("nodes")) {
        if (node.at("type") == "com.cpipe.color.scene_linear_to_display") {
            saw_display = true;
            REQUIRE(node.at("params").at("target") == "sRGB");
        }
    }
    REQUIRE(saw_display);

    bool display_feeds_output = false;
    for (const auto& edge : doc.at("edges")) {
        if (edge.at("from") == "display.display" && edge.at("to") == "out.rgb") {
            display_feeds_output = true;
        }
    }
    REQUIRE(display_feeds_output);
}

TEST_CASE("Debug CLI linkage does not pull libx265") {
    const auto ldd_output = run_ldd_on_cli();
    REQUIRE(ldd_output.find("libx265") == std::string::npos);
    REQUIRE(ldd_output.find("x265") == std::string::npos);
}
