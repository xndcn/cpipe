// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_denoise_bm3d();

namespace {

constexpr std::uint32_t kWidth = 16;
constexpr std::uint32_t kHeight = 16;

std::shared_ptr<cpipe::tests::BufferMetadata> scene_metadata_with_noise_profile() {
    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->noise_profile = {{0.0009F, 0.0F}, {0.0009F, 0.0F}, {0.0009F, 0.0F}};

    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"color_matrix"};
    return metadata;
}

std::array<float, 4> clean_pixel(std::uint32_t x, std::uint32_t y) {
    const auto base = 0.18F + (0.006F * static_cast<float>(x)) + (0.004F * static_cast<float>(y));
    return {base, base + 0.015F, base - 0.01F, 1.0F};
}

std::array<float, 4> noisy_pixel(std::uint32_t x, std::uint32_t y) {
    auto pixel = clean_pixel(x, y);
    const auto noise = ((x + y) & 1U) == 0U ? 0.055F : -0.055F;
    pixel[0] += noise;
    pixel[1] += noise * 0.75F;
    pixel[2] += noise;
    return pixel;
}

std::vector<std::array<float, 4>> make_clean_patch() {
    std::vector<std::array<float, 4>> pixels;
    pixels.reserve(kWidth * kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            pixels.push_back(clean_pixel(x, y));
        }
    }
    return pixels;
}

std::vector<std::array<float, 4>> make_noisy_patch() {
    std::vector<std::array<float, 4>> pixels;
    pixels.reserve(kWidth * kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            pixels.push_back(noisy_pixel(x, y));
        }
    }
    return pixels;
}

float rmse_rgb(const std::vector<std::array<float, 4>>& lhs,
               const std::vector<std::array<float, 4>>& rhs) {
    REQUIRE(lhs.size() == rhs.size());
    double squared = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            const auto delta = static_cast<double>(lhs[i][c] - rhs[i][c]);
            squared += delta * delta;
            ++count;
        }
    }
    return static_cast<float>(std::sqrt(squared / static_cast<double>(count)));
}

cpipe_status_t process_bm3d(const cpipe_plugin_desc_t& desc,
                            const std::shared_ptr<cpipe::tests::CpuBuffer>& input,
                            const std::shared_ptr<cpipe::tests::CpuBuffer>& output,
                            nlohmann::json params_json) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(std::move(params_json));

    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(input->metadata(), {input->metadata()});
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

}  // namespace

TEST_CASE("denoise.bm3d reduces synthetic noise and honors sigma override") {
    cpipe_link_builtin_denoise_bm3d();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.denoise.bm3d");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") ==
            nlohmann::json::array({"denoise_bm3d_step1", "denoise_bm3d_step2"}));
    REQUIRE(manifest.at("params").at(0).at("name") == "sigma");
    REQUIRE(manifest.at("params").at(1).at("name") == "sigma_override");
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"denoise.bm3d"}));

    const auto clean = make_clean_patch();
    const auto noisy = make_noisy_patch();
    auto input = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(kWidth, kHeight), cpipe::tests::BufferUsage::Input |
                                                          cpipe::tests::BufferUsage::CpuRead |
                                                          cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata_with_noise_profile());
    cpipe::tests::write_rgba16(*input, noisy);

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(kWidth, kHeight), cpipe::tests::BufferUsage::Output |
                                                          cpipe::tests::BufferUsage::CpuRead |
                                                          cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(process_bm3d(*desc, input, output, nlohmann::json::object()) == CPIPE_OK);

    const auto denoised = cpipe::tests::read_rgba16(*output, clean.size());
    const auto noisy_rmse = rmse_rgb(noisy, clean);
    const auto denoised_rmse = rmse_rgb(denoised, clean);
    REQUIRE(denoised_rmse <= noisy_rmse * 0.20F);
    REQUIRE(denoised[0][3] == Catch::Approx(1.0F).margin(0.001F));

    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "scene_linear_rec2020");
    REQUIRE(metadata->applied_steps == std::vector<std::string>{"color_matrix", "denoise.bm3d"});

    auto override_output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(kWidth, kHeight), cpipe::tests::BufferUsage::Output |
                                                          cpipe::tests::BufferUsage::CpuRead |
                                                          cpipe::tests::BufferUsage::CpuWrite);
    REQUIRE(process_bm3d(*desc, input, override_output, nlohmann::json{{"sigma_override", 0.09}}) ==
            CPIPE_OK);
    const auto override_pixels = cpipe::tests::read_rgba16(*override_output, clean.size());
    REQUIRE(rmse_rgb(override_pixels, clean) <= denoised_rmse);
}
