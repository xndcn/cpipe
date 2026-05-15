// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_colormatrix_dng_to_working();
void cpipe_link_builtin_demosaic_bilinear();

namespace {

class Rec2020ProducerNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.rec2020_producer";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

constexpr char kRec2020ProducerManifest[] = R"({
  "id":"com.cpipe.test.rec2020_producer",
  "version":"1.0.0",
  "ports":[
    {
      "name":"rgb",
      "kind":"out",
      "caps":{"precision":["f16"]},
      "metadata":{"sets_steps_applied":["white_balance"]}
    }
  ],
  "compute":{"device":"CPU","engine":"Host","out_pixel_bytes":8},
  "color":{"input_role":"any","output_role":"scene_linear_rec2020","respects_chromaticity":true}
})";

constexpr std::array<float, 9> kD50ToD65{
    0.9555766F, -0.0230393F, 0.0631636F,  -0.0282895F, 1.0099416F,
    0.0210077F, 0.0122982F,  -0.0204830F, 1.3299098F,
};

constexpr std::array<float, 9> kXyzD65ToRec2020{
    1.7166512F, -0.3556708F, -0.2533663F, -0.6666844F, 1.6164812F,
    0.0157685F, 0.0176399F,  -0.0427706F, 0.9421031F,
};

std::array<float, 3> mul3(const std::array<float, 9>& matrix, const std::array<float, 3>& value) {
    return {matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
            matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
            matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2]};
}

std::filesystem::path write_colormatrix_without_wb_pipeline() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_colormatrix_without_wb.json";
    std::ofstream out{path};
    out << R"({
  "$schema":"https://schemas.cpipe.dev/pipeline/v0.3.json",
  "version":"0.3",
  "id":"colormatrix-without-wb",
  "inputs":[{"port":"raw","kind":"Image2D","format":"R16_UINT","width":4,"height":4}],
  "nodes":[
    {"id":"dem","type":"com.cpipe.demosaic.bilinear","params":{}},
    {"id":"cm","type":"com.cpipe.colormatrix.dng_to_working","params":{}}
  ],
  "edges":[{"from":"dem.rgba","to":"cm.rgb"}]
})";
    return path;
}

std::filesystem::path write_colormatrix_wrong_role_pipeline() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_colormatrix_wrong_role.json";
    std::ofstream out{path};
    out << R"({
  "$schema":"https://schemas.cpipe.dev/pipeline/v0.3.json",
  "version":"0.3",
  "id":"colormatrix-wrong-role",
  "inputs":[{"port":"raw","kind":"Image2D","format":"R16_UINT","width":4,"height":4}],
  "nodes":[
    {"id":"prod","type":"com.cpipe.test.rec2020_producer","params":{}},
    {"id":"cm","type":"com.cpipe.colormatrix.dng_to_working","params":{}}
  ],
  "edges":[{"from":"prod.rgb","to":"cm.rgb"}]
})";
    return path;
}

}  // namespace

CPIPE_REGISTER_NODE(Rec2020ProducerNode, kRec2020ProducerManifest)

TEST_CASE("colormatrix.dng_to_working maps camera RGB to linear Rec.2020 D65") {
    cpipe_link_builtin_colormatrix_dng_to_working();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.colormatrix.dng_to_working");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("metadata").at("requires_steps_applied") ==
            nlohmann::json::array({"white_balance"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"color_matrix"}));
    REQUIRE(manifest.at("color").at("output_role") == "scene_linear_rec2020");

    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->color_matrix1 =
        cpipe::tests::Matrix3{{2.0F, 0.0F, 0.0F, 0.0F, 4.0F, 0.0F, 0.0F, 0.0F, 0.5F}};

    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic", "white_balance"};

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(1, 1),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    cpipe::tests::write_rgba16(*input, {{{0.5F, 0.25F, 0.125F, 0.75F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(1, 1), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const std::array<float, 3> xyz_d50{0.25F, 0.0625F, 0.25F};
    const auto xyz_d65 = mul3(kD50ToD65, xyz_d50);
    const auto rec2020 = mul3(kXyzD65ToRec2020, xyz_d65);
    const auto pixels = cpipe::tests::read_rgba16(*output, 1);
    REQUIRE(pixels[0][0] == Catch::Approx(rec2020[0]).margin(0.001F));
    REQUIRE(pixels[0][1] == Catch::Approx(rec2020[1]).margin(0.001F));
    REQUIRE(pixels[0][2] == Catch::Approx(rec2020[2]).margin(0.001F));
    REQUIRE(pixels[0][3] == Catch::Approx(0.75F));

    REQUIRE(output->metadata()->cs_role == "scene_linear_rec2020");
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"linearization", "black_white_scaling", "demosaic",
                                     "white_balance", "color_matrix"});
}

TEST_CASE("colormatrix.dng_to_working rejects non raw_camera inputs at process time") {
    cpipe_link_builtin_colormatrix_dng_to_working();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.colormatrix.dng_to_working");
    REQUIRE(desc != nullptr);

    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->color_matrix1 =
        cpipe::tests::Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"white_balance"};

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(1, 1),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    cpipe::tests::write_rgba16(*input, {{{1.0F, 1.0F, 1.0F, 1.0F}}});
    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(1, 1), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_NEED_METADATA);
}

TEST_CASE("Pipeline load rejects colormatrix before white balance") {
    cpipe_link_builtin_demosaic_bilinear();
    cpipe_link_builtin_colormatrix_dng_to_working();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(write_colormatrix_without_wb_pipeline(), registry,
                                           &pipeline, &error) == CPIPE_NEED_METADATA);
    REQUIRE(error.find("white_balance") != std::string::npos);
}

TEST_CASE("Pipeline load rejects colormatrix color role mismatch") {
    cpipe_link_builtin_colormatrix_dng_to_working();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(write_colormatrix_wrong_role_pipeline(), registry,
                                           &pipeline, &error) == CPIPE_NEED_METADATA);
    REQUIRE(error.find("color role") != std::string::npos);
}
