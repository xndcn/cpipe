// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_wb_dual_illuminant();

namespace {

constexpr std::array<float, 3> kSyntheticNeutral{0.9520738F, 1.0F, 0.8886605F};
constexpr float kExpectedCct = 5000.0F;
constexpr float kExpectedWeight = 0.23424658F;
constexpr const char* kDiagBlob = "com.cpipe.wb.camera_diag_f32";
constexpr const char* kMatrixBlob = "com.cpipe.wb.camera_to_xyz_d50_f32";
constexpr const char* kCctBlob = "com.cpipe.wb.scene_cct_f32";
constexpr const char* kWeightBlob = "com.cpipe.wb.dual_illuminant_weight_f32";

std::shared_ptr<cpipe::tests::BufferMetadata> synthetic_dual_illuminant_metadata() {
    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->calibration_illuminant1 = 17;
    calibration->calibration_illuminant2 = 21;
    calibration->color_matrix1 =
        cpipe::tests::Matrix3{{1.2F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.8F}};
    calibration->color_matrix2 =
        cpipe::tests::Matrix3{{0.9F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.1F}};
    calibration->forward_matrix1 =
        cpipe::tests::Matrix3{{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F}};
    calibration->forward_matrix2 =
        cpipe::tests::Matrix3{{9.0F, 8.0F, 7.0F, 6.0F, 5.0F, 4.0F, 3.0F, 2.0F, 1.0F}};

    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic"};
    metadata->capture.as_shot_neutral = kSyntheticNeutral;
    return metadata;
}

std::vector<float> blob_as_floats(const cpipe::tests::BufferMetadata& metadata,
                                  const std::string& key, std::size_t count) {
    const auto found = metadata.ext_blobs.find(key);
    REQUIRE(found != metadata.ext_blobs.end());
    REQUIRE(found->second != nullptr);
    REQUIRE(found->second->bytes.size() == count * sizeof(float));

    std::vector<float> values(count);
    std::memcpy(values.data(), found->second->bytes.data(), found->second->bytes.size());
    return values;
}

}  // namespace

TEST_CASE("wb.dual_illuminant applies inverse AsShotNeutral gains") {
    cpipe_link_builtin_wb_dual_illuminant();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.wb.dual_illuminant");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("metadata").at("requires_steps_applied") ==
            nlohmann::json::array({"demosaic"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"white_balance"}));

    auto metadata = synthetic_dual_illuminant_metadata();

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(2, 1),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    cpipe::tests::write_rgba16(
        *input, {{{kSyntheticNeutral[0] * 0.25F, 0.25F, kSyntheticNeutral[2] * 0.25F, 0.5F},
                  {kSyntheticNeutral[0] * 0.5F, 0.5F, kSyntheticNeutral[2] * 0.5F, 1.0F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(2, 1), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto pixels = cpipe::tests::read_rgba16(*output, 2);
    REQUIRE(pixels[0][0] == Catch::Approx(0.25F));
    REQUIRE(pixels[0][1] == Catch::Approx(0.25F));
    REQUIRE(pixels[0][2] == Catch::Approx(0.25F));
    REQUIRE(pixels[0][3] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][0] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][1] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][2] == Catch::Approx(0.5F));
    REQUIRE(pixels[1][3] == Catch::Approx(1.0F));

    REQUIRE(output->metadata()->cs_role == "raw_camera");
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"linearization", "black_white_scaling", "demosaic",
                                     "white_balance"});

    const auto diag = blob_as_floats(*output->metadata(), kDiagBlob, 3);
    REQUIRE(diag[0] == Catch::Approx(1.0F / kSyntheticNeutral[0]));
    REQUIRE(diag[1] == Catch::Approx(1.0F / kSyntheticNeutral[1]));
    REQUIRE(diag[2] == Catch::Approx(1.0F / kSyntheticNeutral[2]));
}

TEST_CASE("wb.dual_illuminant interpolates dual illuminant calibration metadata") {
    cpipe_link_builtin_wb_dual_illuminant();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.wb.dual_illuminant");
    REQUIRE(desc != nullptr);

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(1, 1),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(synthetic_dual_illuminant_metadata());
    cpipe::tests::write_rgba16(*input,
                               {{{kSyntheticNeutral[0], 1.0F, kSyntheticNeutral[2], 1.0F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(1, 1), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto cct = blob_as_floats(*output->metadata(), kCctBlob, 1);
    REQUIRE(cct[0] == Catch::Approx(kExpectedCct).margin(0.5F));

    const auto weight = blob_as_floats(*output->metadata(), kWeightBlob, 1);
    REQUIRE(weight[0] == Catch::Approx(kExpectedWeight).epsilon(0.001F));

    const auto matrix = blob_as_floats(*output->metadata(), kMatrixBlob, 9);
    const std::array<float, 9> expected_matrix{7.1260276F, 6.5945206F, 6.0630136F, 5.5315065F, 5.0F,
                                               4.4684930F, 3.9369864F, 3.4054794F, 2.8739727F};
    for (std::size_t i = 0; i < expected_matrix.size(); ++i) {
        REQUIRE(matrix[i] == Catch::Approx(expected_matrix[i]).epsilon(0.001F));
    }
}
