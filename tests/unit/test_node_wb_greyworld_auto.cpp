// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_approx.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstddef>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_wb_greyworld_auto();

namespace {

constexpr const char* kDiagBlob = "com.cpipe.wb.camera_diag_f32";
constexpr const char* kMatrixBlob = "com.cpipe.wb.camera_to_xyz_d50_f32";
constexpr const char* kCctBlob = "com.cpipe.wb.scene_cct_f32";
constexpr const char* kWeightBlob = "com.cpipe.wb.dual_illuminant_weight_f32";

std::shared_ptr<cpipe::tests::BufferMetadata> greyworld_metadata() {
    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->calibration_illuminant1 = 17;
    calibration->calibration_illuminant2 = 21;
    calibration->color_matrix1 =
        cpipe::tests::Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    calibration->color_matrix2 =
        cpipe::tests::Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    calibration->forward_matrix1 =
        cpipe::tests::Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    calibration->forward_matrix2 =
        cpipe::tests::Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};

    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic"};
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

TEST_CASE("wb.greyworld_auto estimates neutral from a synthetic gray-mean fixture") {
    cpipe_link_builtin_wb_greyworld_auto();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.wb.greyworld_auto");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(0).at("metadata").at("requires_steps_applied") ==
            nlohmann::json::array({"demosaic"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"white_balance"}));

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(2, 2),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(greyworld_metadata());
    cpipe::tests::write_rgba16(*input, {{{0.10F, 0.20F, 0.05F, 1.0F},
                                         {0.20F, 0.40F, 0.10F, 1.0F},
                                         {0.30F, 0.60F, 0.15F, 1.0F},
                                         {0.20F, 0.40F, 0.10F, 1.0F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        cpipe::tests::rgba16_layout(2, 2), cpipe::tests::BufferUsage::Output |
                                               cpipe::tests::BufferUsage::CpuRead |
                                               cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(cpipe::tests::process_single_input_node(*desc, input, output) == CPIPE_OK);

    const auto metadata = output->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->applied_steps == std::vector<std::string>{"linearization",
                                                                "black_white_scaling", "demosaic",
                                                                "white_balance"});
    REQUIRE(metadata->capture.as_shot_neutral[0] == Catch::Approx(0.5F).epsilon(0.05F));
    REQUIRE(metadata->capture.as_shot_neutral[1] == Catch::Approx(1.0F).epsilon(0.05F));
    REQUIRE(metadata->capture.as_shot_neutral[2] == Catch::Approx(0.25F).epsilon(0.05F));

    const auto diag = blob_as_floats(*metadata, kDiagBlob, 3);
    REQUIRE(diag[0] == Catch::Approx(2.0F).epsilon(0.05F));
    REQUIRE(diag[1] == Catch::Approx(1.0F).epsilon(0.05F));
    REQUIRE(diag[2] == Catch::Approx(4.0F).epsilon(0.05F));
    REQUIRE_FALSE(blob_as_floats(*metadata, kMatrixBlob, 9).empty());
    REQUIRE_FALSE(blob_as_floats(*metadata, kCctBlob, 1).empty());
    REQUIRE_FALSE(blob_as_floats(*metadata, kWeightBlob, 1).empty());

    const auto pixels = cpipe::tests::read_rgba16(*output, 4);
    for (const auto& pixel : pixels) {
        REQUIRE(pixel[0] == Catch::Approx(pixel[1]).epsilon(0.01F));
        REQUIRE(pixel[2] == Catch::Approx(pixel[1]).epsilon(0.01F));
        REQUIRE(pixel[3] == Catch::Approx(1.0F));
    }
}
