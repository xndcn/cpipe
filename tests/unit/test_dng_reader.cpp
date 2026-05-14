// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/ingest/dng/DngReader.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "dng_test_fixture.hpp"

void cpipe_link_builtin_dng_input();
void cpipe_link_builtin_passthrough();

namespace {

std::filesystem::path source_path() {
    return std::filesystem::path{CPIPE_SOURCE_DIR};
}

cpipe::compute::BufferLayout raw_layout(std::uint32_t width, std::uint32_t height) {
    cpipe::compute::BufferLayout layout{};
    layout.kind = cpipe::compute::BufferKind::Image2D;
    layout.format = cpipe::compute::PixelFormat::R16_UINT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

std::filesystem::path write_source_binding_pipeline() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_dng_source_binding.json";
    std::ofstream out{path};
    out << R"({
  "$schema":"https://schemas.cpipe.dev/pipeline/v0.2.json",
  "version":"0.2",
  "id":"dng-source-binding",
  "inputs":[{"port":"raw","kind":"Image2D","format":"R16_UINT","width":4,"height":3}],
  "nodes":[{"id":"copy","type":"com.cpipe.builtin.passthrough","params":{}}],
  "edges":[]
})";
    return path;
}

}  // namespace

TEST_CASE("DngReader decodes a synthetic Bayer DNG into an R16 buffer with metadata") {
    cpipe::tests::SyntheticDngOptions options;
    options.pixels = {64, 73, 82, 91, 100, 109, 118, 127, 136, 145, 154, 163};
    const auto path = cpipe::tests::write_synthetic_dng("reader", options);

    const auto read = cpipe::ingest::dng::DngReader::read(path);
    INFO(read.message);
    REQUIRE(read.status == CPIPE_OK);
    REQUIRE(read.buffer != nullptr);
    REQUIRE(read.buffer->layout().kind == cpipe::compute::BufferKind::Image2D);
    REQUIRE(read.buffer->layout().format == cpipe::compute::PixelFormat::R16_UINT);
    REQUIRE(read.buffer->layout().dims[0] == 4);
    REQUIRE(read.buffer->layout().dims[1] == 3);

    const auto* pixels = static_cast<const std::uint16_t*>(
        read.buffer->lock_cpu(cpipe::compute::IBuffer::CpuAccess::Read));
    REQUIRE(std::memcmp(pixels, options.pixels.data(), options.pixels.size() * 2U) == 0);
    read.buffer->unlock_cpu();

    const auto metadata = read.buffer->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "raw_camera");
    REQUIRE(metadata->applied_steps.empty());
    REQUIRE(metadata->calibration != nullptr);
    REQUIRE(metadata->calibration->cfa.has_value());
    REQUIRE(metadata->calibration->linearization_table.has_value());
    REQUIRE(metadata->calibration->linearization_table->values ==
            std::vector<std::uint16_t>{0, 128, 1024, 4095});
    REQUIRE(metadata->calibration->color_matrix1.has_value());
    REQUIRE(metadata->calibration->color_matrix2.has_value());
    REQUIRE(metadata->calibration->forward_matrix1.has_value());
    REQUIRE(metadata->calibration->forward_matrix2.has_value());
    REQUIRE(metadata->capture.iso == 400);
    REQUIRE(metadata->capture.exposure_time_ns == 8'000'000);
    REQUIRE(metadata->capture.lens_aperture == Catch::Approx(1.7F));
    REQUIRE(metadata->capture.lens_focal_length_mm == Catch::Approx(4.3F));
    REQUIRE(metadata->capture.orientation == 1);
    REQUIRE(metadata->active_area.has_value());
    REQUIRE(metadata->exif_blob != nullptr);
    REQUIRE(metadata->xmp_blob != nullptr);
    REQUIRE(metadata->icc_blob != nullptr);
    REQUIRE(metadata->ext_blobs.contains("com.cpipe.dng.opcode_list_1_bytes"));
    REQUIRE(metadata->ext_blobs.contains("com.cpipe.dng.opcode_list_2_bytes"));
    REQUIRE(metadata->ext_blobs.contains("com.cpipe.dng.opcode_list_3_bytes"));
}

TEST_CASE("DngReader decodes the cropped Pixel 8 Pro corpus DNG") {
    const auto path = source_path() / "tests" / "corpus" / "pixel8pro.dng";
    REQUIRE(std::filesystem::exists(path));

    const auto read = cpipe::ingest::dng::DngReader::read(path);
    INFO(read.message);
    REQUIRE(read.status == CPIPE_OK);
    REQUIRE(read.buffer != nullptr);
    REQUIRE(read.buffer->layout().kind == cpipe::compute::BufferKind::Image2D);
    REQUIRE(read.buffer->layout().format == cpipe::compute::PixelFormat::R16_UINT);
    REQUIRE(read.buffer->layout().dims[0] == 1920);
    REQUIRE(read.buffer->layout().dims[1] == 1080);

    const auto metadata = read.buffer->metadata();
    REQUIRE(metadata != nullptr);
    REQUIRE(metadata->cs_role == "raw_camera");
    REQUIRE(metadata->applied_steps.empty());
    REQUIRE(metadata->calibration != nullptr);
    REQUIRE(metadata->calibration->cfa.has_value());
    REQUIRE(metadata->calibration->cfa->pattern == std::array<std::uint8_t, 4>{0, 1, 1, 2});
    REQUIRE(metadata->calibration->linearization_table.has_value());
    REQUIRE(metadata->calibration->linearization_table->values.size() == 16369);
    REQUIRE(metadata->calibration->black_level[0] > 1024.0F);
    REQUIRE(metadata->calibration->white_level == 16368);
    REQUIRE(metadata->calibration->color_matrix1.has_value());
    REQUIRE(metadata->calibration->forward_matrix2.has_value());
    REQUIRE(metadata->capture.camera_id == "Google");
    REQUIRE(metadata->capture.physical_camera_id == "Pixel 8 Pro");
    REQUIRE(metadata->capture.iso == 21);
    REQUIRE(metadata->capture.exposure_time_ns > 11'000'000);
    REQUIRE(metadata->capture.as_shot_neutral[0] > 0.0F);
    REQUIRE(metadata->capture.as_shot_neutral[1] > 0.0F);
    REQUIRE(metadata->capture.as_shot_neutral[2] > 0.0F);
    REQUIRE(metadata->active_area.has_value());
    REQUIRE(metadata->active_area->width == 1920);
    REQUIRE(metadata->active_area->height == 1080);
}

TEST_CASE("DngReader rejects non-Bayer DNG input") {
    cpipe::tests::SyntheticDngOptions options;
    options.include_cfa = false;
    const auto path = cpipe::tests::write_synthetic_dng("foveon_like", options);

    const auto read = cpipe::ingest::dng::DngReader::read(path);
    REQUIRE(read.status == CPIPE_FAILED);

    cpipe::tests::SyntheticDngOptions quad_options;
    quad_options.cfa_repeat = {4, 4};
    quad_options.cfa_pattern = {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2};
    const auto quad_path = cpipe::tests::write_synthetic_dng("quad_reader", quad_options);
    const auto quad_read = cpipe::ingest::dng::DngReader::read(quad_path);
    REQUIRE(quad_read.status == CPIPE_FAILED);
}

TEST_CASE("dng_input plugin is registered and reads its path parameter") {
    cpipe_link_builtin_dng_input();
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.builtin.dng_input");
    REQUIRE(desc != nullptr);
    REQUIRE(std::string_view{desc->manifest_json}.find("\"path\"") != std::string_view::npos);

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(write_source_binding_pipeline(), registry, &pipeline,
                                           &error) == CPIPE_OK);
    REQUIRE(pipeline.set_source("raw", "com.cpipe.builtin.dng_input",
                                nlohmann::json{{"path", "input.dng"}}) == CPIPE_OK);

    const auto path = cpipe::tests::write_synthetic_dng("dng_input", {});
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle(nlohmann::json{{"path", path.string()}});

    void* instance = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                             nullptr, &instance) == CPIPE_OK);

    auto dummy = std::make_shared<cpipe::compute::CpuBuffer>(
        raw_layout(1, 1), cpipe::compute::BufferUsage::Output |
                              cpipe::compute::BufferUsage::CpuRead |
                              cpipe::compute::BufferUsage::CpuWrite);
    auto output_handle = cpipe::runtime::make_buffer_handle(dummy);
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_process_ctx process{
        .compute = nullptr,
        .inference = nullptr,
        .inputs = nullptr,
        .n_in = 0,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = nullptr,
    };

    REQUIRE(desc->main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                             reinterpret_cast<cpipe_node_t*>(instance), params.get(), &process,
                             nullptr) == CPIPE_OK);
    REQUIRE(desc->main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                             nullptr) == CPIPE_OK);

    const auto output = cpipe::runtime::buffer_from_handle(output_handle.get());
    REQUIRE(output != nullptr);
    REQUIRE(output->layout().dims[0] == 4);
    REQUIRE(output->layout().dims[1] == 3);
    REQUIRE(output->metadata() != nullptr);
    REQUIRE(output->metadata()->cs_role == "raw_camera");
}
