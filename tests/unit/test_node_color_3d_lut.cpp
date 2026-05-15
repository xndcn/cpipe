// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_color_3d_lut();

namespace {

constexpr std::uint32_t kLutSize = 33;

std::filesystem::path temp_lut_path(std::string_view name) {
    return std::filesystem::temp_directory_path() /
           (std::string{"cpipe_test_color_3d_lut_"} + std::string{name});
}

std::array<float, 3> lut_function(float r, float g, float b) {
    return {
        std::clamp(0.02F + (0.65F * r) + (0.15F * g * g) + (0.08F * b), 0.0F, 1.0F),
        std::clamp(0.05F + (0.70F * g) + (0.10F * r * b) + (0.04F * r), 0.0F, 1.0F),
        std::clamp(0.03F + (0.75F * b) + (0.12F * r * g) + (0.03F * g), 0.0F, 1.0F),
    };
}

std::size_t lut_offset(std::uint32_t size, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    return ((static_cast<std::size_t>(r) * size * size) + (static_cast<std::size_t>(g) * size) +
            b) *
           3U;
}

std::array<float, 3> lut_at(const std::vector<float>& values, std::uint32_t size, std::uint32_t r,
                            std::uint32_t g, std::uint32_t b) {
    const auto offset = lut_offset(size, r, g, b);
    return {values[offset], values[offset + 1U], values[offset + 2U]};
}

std::array<float, 3> add_delta(const std::array<float, 3>& base, const std::array<float, 3>& from,
                               const std::array<float, 3>& to, float weight) {
    return {base[0] + ((to[0] - from[0]) * weight), base[1] + ((to[1] - from[1]) * weight),
            base[2] + ((to[2] - from[2]) * weight)};
}

std::array<float, 3> tetrahedral_reference(const std::vector<float>& values, std::uint32_t size,
                                           const std::array<float, 3>& rgb) {
    const auto max_index = size - 1U;
    const auto rx = std::clamp(rgb[0], 0.0F, 1.0F) * static_cast<float>(max_index);
    const auto gx = std::clamp(rgb[1], 0.0F, 1.0F) * static_cast<float>(max_index);
    const auto bx = std::clamp(rgb[2], 0.0F, 1.0F) * static_cast<float>(max_index);
    const auto r0 = std::min(static_cast<std::uint32_t>(std::floor(rx)), max_index - 1U);
    const auto g0 = std::min(static_cast<std::uint32_t>(std::floor(gx)), max_index - 1U);
    const auto b0 = std::min(static_cast<std::uint32_t>(std::floor(bx)), max_index - 1U);
    const auto dr = rx - static_cast<float>(r0);
    const auto dg = gx - static_cast<float>(g0);
    const auto db = bx - static_cast<float>(b0);
    const auto c000 = lut_at(values, size, r0, g0, b0);
    const auto c100 = lut_at(values, size, r0 + 1U, g0, b0);
    const auto c010 = lut_at(values, size, r0, g0 + 1U, b0);
    const auto c001 = lut_at(values, size, r0, g0, b0 + 1U);
    const auto c110 = lut_at(values, size, r0 + 1U, g0 + 1U, b0);
    const auto c101 = lut_at(values, size, r0 + 1U, g0, b0 + 1U);
    const auto c011 = lut_at(values, size, r0, g0 + 1U, b0 + 1U);
    const auto c111 = lut_at(values, size, r0 + 1U, g0 + 1U, b0 + 1U);

    if (dr >= dg && dg >= db) {
        return add_delta(add_delta(add_delta(c000, c000, c100, dr), c100, c110, dg), c110, c111,
                         db);
    }
    if (dr >= db && db >= dg) {
        return add_delta(add_delta(add_delta(c000, c000, c100, dr), c100, c101, db), c101, c111,
                         dg);
    }
    if (db >= dr && dr >= dg) {
        return add_delta(add_delta(add_delta(c000, c000, c001, db), c001, c101, dr), c101, c111,
                         dg);
    }
    if (dg >= dr && dr >= db) {
        return add_delta(add_delta(add_delta(c000, c000, c010, dg), c010, c110, dr), c110, c111,
                         db);
    }
    if (dg >= db && db >= dr) {
        return add_delta(add_delta(add_delta(c000, c000, c010, dg), c010, c011, db), c011, c111,
                         dr);
    }
    return add_delta(add_delta(add_delta(c000, c000, c001, db), c001, c011, dg), c011, c111, dr);
}

std::vector<float> write_synthetic_cube(const std::filesystem::path& path) {
    std::ofstream out{path};
    REQUIRE(out.good());
    out << "TITLE \"cpipe synthetic 33\"\n";
    out << "LUT_3D_SIZE " << kLutSize << "\n";
    out << "DOMAIN_MIN 0 0 0\n";
    out << "DOMAIN_MAX 1 1 1\n";

    std::vector<float> values(static_cast<std::size_t>(kLutSize) * kLutSize * kLutSize * 3U);
    for (std::uint32_t r = 0; r < kLutSize; ++r) {
        for (std::uint32_t g = 0; g < kLutSize; ++g) {
            for (std::uint32_t b = 0; b < kLutSize; ++b) {
                const auto rgb = lut_function(static_cast<float>(r) / (kLutSize - 1U),
                                              static_cast<float>(g) / (kLutSize - 1U),
                                              static_cast<float>(b) / (kLutSize - 1U));
                const auto offset = lut_offset(kLutSize, r, g, b);
                values[offset] = rgb[0];
                values[offset + 1U] = rgb[1];
                values[offset + 2U] = rgb[2];
                out << rgb[0] << ' ' << rgb[1] << ' ' << rgb[2] << '\n';
            }
        }
    }
    return values;
}

std::shared_ptr<cpipe::tests::BufferMetadata> scene_metadata() {
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"tone.filmic_rgb"};
    return metadata;
}

cpipe_status_t process_lut(const cpipe_plugin_desc_t& desc,
                           const std::shared_ptr<cpipe::tests::CpuBuffer>& input,
                           const std::shared_ptr<cpipe::tests::CpuBuffer>& output,
                           const std::filesystem::path& lut_path) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    auto params =
        cpipe::runtime::make_param_handle(nlohmann::json{{"lut_path", lut_path.string()}});

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

nlohmann::json pipeline_doc(nlohmann::json params) {
    return {
        {"$schema", "https://schemas.cpipe.dev/pipeline/v0.3.json"},
        {"version", "0.3"},
        {"id", "color-lut-schema"},
        {"inputs", nlohmann::json::array({{{"port", "raw"},
                                           {"kind", "Image2D"},
                                           {"format", "R16G16B16A16_SFLOAT"},
                                           {"width", 1},
                                           {"height", 1}}})},
        {"nodes",
         nlohmann::json::array(
             {{{"id", "lut"}, {"type", "com.cpipe.color.3d_lut"}, {"params", std::move(params)}}})},
        {"edges", nlohmann::json::array()},
    };
}

}  // namespace

TEST_CASE("color.3d_lut manifest and pipeline schema require lut_path") {
    cpipe_link_builtin_color_3d_lut();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.3d_lut");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("compute").at("engine") == "Halide");
    REQUIRE(manifest.at("compute").at("halide_aot") == nlohmann::json::array({"color_3d_lut"}));
    REQUIRE(manifest.at("params").at(0).at("name") == "lut_path");
    REQUIRE(manifest.at("params").at(0).at("type") == "string");
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"color.3d_lut"}));

    const auto schema_path =
        std::filesystem::path{CPIPE_SOURCE_DIR} / "schemas" / "pipeline-v0.3.json";
    std::ifstream schema_file{schema_path};
    REQUIRE(schema_file.good());
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schema_file));

    REQUIRE_NOTHROW(validator.validate(pipeline_doc({{"lut_path", "look.cube"}})));
    REQUIRE_THROWS(validator.validate(pipeline_doc(nlohmann::json::object())));
    REQUIRE_THROWS(validator.validate(pipeline_doc({{"lut_path", 12}})));
}

TEST_CASE("color.3d_lut applies 33^3 tetrahedral interpolation") {
    cpipe_link_builtin_color_3d_lut();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.color.3d_lut");
    REQUIRE(desc != nullptr);

    const auto lut_path = temp_lut_path("synthetic_33.cube");
    const auto lut_values = write_synthetic_cube(lut_path);
    const std::vector<std::array<float, 4>> input_pixels{
        {0.10F, 0.20F, 0.30F, 1.0F},
        {0.42F, 0.18F, 0.73F, 0.75F},
        {0.83F, 0.66F, 0.11F, 0.5F},
        {1.0F, 0.0F, 0.55F, 0.25F},
    };

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(static_cast<std::uint32_t>(input_pixels.size()), 1),
        cpipe::tests::BufferUsage::Input | cpipe::tests::BufferUsage::CpuRead |
            cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(scene_metadata());
    cpipe::tests::write_rgba16(*input, input_pixels);

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(static_cast<std::uint32_t>(input_pixels.size()), 1),
        cpipe::tests::BufferUsage::Output | cpipe::tests::BufferUsage::CpuRead |
            cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(process_lut(*desc, input, output, lut_path) == CPIPE_OK);

    const auto actual = cpipe::tests::read_rgba16(*output, input_pixels.size());
    for (std::size_t i = 0; i < input_pixels.size(); ++i) {
        const auto expected = tetrahedral_reference(
            lut_values, kLutSize, {input_pixels[i][0], input_pixels[i][1], input_pixels[i][2]});
        REQUIRE(actual[i][0] == Catch::Approx(expected[0]).margin(0.003F));
        REQUIRE(actual[i][1] == Catch::Approx(expected[1]).margin(0.003F));
        REQUIRE(actual[i][2] == Catch::Approx(expected[2]).margin(0.003F));
        REQUIRE(actual[i][3] == Catch::Approx(input_pixels[i][3]).margin(0.001F));
    }

    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "scene_linear_rec2020");
    REQUIRE(metadata->applied_steps == std::vector<std::string>{"tone.filmic_rgb", "color.3d_lut"});
}
