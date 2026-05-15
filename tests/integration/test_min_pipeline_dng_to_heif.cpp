// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/color/HeifReader.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/ingest/dng/DngReader.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../unit/dng_test_fixture.hpp"

void cpipe_link_all_builtin_nodes();

namespace {

using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

constexpr double kMinIntegrationPsnrDb = 37.0;

struct FloatImage {
    int width{0};
    int height{0};
    int channels{0};
    std::vector<float> pixels;
};

std::filesystem::path source_path() {
    return std::filesystem::path{CPIPE_SOURCE_DIR};
}

std::string shell_quote(const std::filesystem::path& path) {
    return "'" + path.string() + "'";
}

float half_to_float(std::uint16_t bits) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    _Float16 half = 0.0F;
    std::memcpy(&half, &bits, sizeof(bits));
    return static_cast<float>(half);
}

BufferLayout next_layout(const std::string& node_id, const BufferLayout& input) {
    BufferLayout output = input;
    if (node_id == "com.cpipe.linearize.dng_lut" || node_id == "com.cpipe.blacklevel.dng_levels") {
        output.format = PixelFormat::R32_SFLOAT;
    } else {
        output.format = PixelFormat::R16G16B16A16_SFLOAT;
    }
    return output;
}

void process_single_output_node(const cpipe_plugin_desc_t& desc,
                                const std::shared_ptr<IBuffer>& input,
                                const std::shared_ptr<IBuffer>& output,
                                cpipe::runtime::ComputeContext* compute) {
    cpipe::runtime::HostContext host_context;
    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                            &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder =
        cpipe::runtime::make_metadata_builder_handle(input->metadata(), {input->metadata()});
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status = static_cast<cpipe_status_t>(
        desc.main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                        reinterpret_cast<cpipe_node_t*>(instance), nullptr, &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    const auto destroy_status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance, nullptr));
    CAPTURE(desc.node_id, status);
    REQUIRE(status == CPIPE_OK);
    REQUIRE(destroy_status == CPIPE_OK);
}

FloatImage rgb_from_rgba16(const std::shared_ptr<IBuffer>& buffer) {
    const auto& layout = buffer->layout();
    REQUIRE(layout.format == PixelFormat::R16G16B16A16_SFLOAT);
    const auto width = static_cast<int>(layout.dims[0]);
    const auto height = static_cast<int>(layout.dims[1]);
    FloatImage image{width, height, 3, {}};
    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.pixels.resize(pixel_count * 3U);

    const auto* input =
        static_cast<const std::uint16_t*>(buffer->lock_cpu(IBuffer::CpuAccess::Read));
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        image.pixels[(pixel * 3U) + 0U] = half_to_float(input[(pixel * 4U) + 0U]);
        image.pixels[(pixel * 3U) + 1U] = half_to_float(input[(pixel * 4U) + 1U]);
        image.pixels[(pixel * 3U) + 2U] = half_to_float(input[(pixel * 4U) + 2U]);
    }
    buffer->unlock_cpu();
    return image;
}

FloatImage rgb_from_rgba32(const cpipe::color::HeifInfo& info) {
    FloatImage image{static_cast<int>(info.width), static_cast<int>(info.height), 3, {}};
    image.pixels.resize(static_cast<std::size_t>(info.width) * info.height * 3U);
    REQUIRE(info.scene_linear_rec2020_rgba.size() ==
            static_cast<std::size_t>(info.width) * info.height * 4U);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(info.width) * info.height;
         ++pixel) {
        image.pixels[(pixel * 3U) + 0U] = info.scene_linear_rec2020_rgba[(pixel * 4U) + 0U];
        image.pixels[(pixel * 3U) + 1U] = info.scene_linear_rec2020_rgba[(pixel * 4U) + 1U];
        image.pixels[(pixel * 3U) + 2U] = info.scene_linear_rec2020_rgba[(pixel * 4U) + 2U];
    }
    return image;
}

FloatImage run_isp_reference(const std::filesystem::path& input_path) {
    cpipe::runtime::Registry registry;
    cpipe_link_all_builtin_nodes();
    registry.load_builtin_nodes();

    auto read = cpipe::ingest::dng::DngReader::read(input_path);
    INFO(read.message);
    REQUIRE(read.status == CPIPE_OK);
    REQUIRE(read.buffer != nullptr);

    cpipe::runtime::ComputeContext compute;

    std::shared_ptr<IBuffer> current = read.buffer;
    const std::vector<std::string> node_ids{
        "com.cpipe.linearize.dng_lut", "com.cpipe.blacklevel.dng_levels",
        "com.cpipe.demosaic.bilinear", "com.cpipe.wb.dual_illuminant",
        "com.cpipe.colormatrix.dng_to_working"};
    for (const auto& node_id : node_ids) {
        const auto* desc = registry.find(node_id.c_str());
        REQUIRE(desc != nullptr);
        auto output = std::make_shared<CpuBuffer>(
            next_layout(node_id, current->layout()),
            BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);
        process_single_output_node(*desc, current, output, &compute);
        current = std::move(output);
    }

    return rgb_from_rgba16(current);
}

OIIO::ImageBuf image_buf_from_pixels(const std::string& name, const FloatImage& image) {
    OIIO::ImageSpec spec{image.width, image.height, image.channels, OIIO::TypeDesc::FLOAT};
    OIIO::ImageBuf buffer{name, spec};
    const OIIO::ROI roi{0, image.width, 0, image.height, 0, 1, 0, image.channels};
    REQUIRE(buffer.set_pixels(roi, OIIO::TypeDesc::FLOAT, image.pixels.data()));
    return buffer;
}

void require_psnr_at_least(const FloatImage& expected, const FloatImage& actual,
                           double min_psnr_db) {
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    REQUIRE(actual.channels == expected.channels);

    auto actual_buf = image_buf_from_pixels("min_pipeline.actual", actual);
    auto expected_buf = image_buf_from_pixels("min_pipeline.expected", expected);
    const auto result = OIIO::ImageBufAlgo::compare(actual_buf, expected_buf, 0.0F, 0.0F);
    CAPTURE(result.PSNR, result.rms_error, result.maxerror, result.nfail);
    REQUIRE((std::isinf(result.PSNR) || result.PSNR >= min_psnr_db));
}

}  // namespace

TEST_CASE("min pipeline converts a synthetic Bayer DNG to decodable SDR HEIF") {
    cpipe_link_all_builtin_nodes();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    const auto pipeline_path = source_path() / "examples" / "pipelines" / "min-pipeline.cpipe.json";
    REQUIRE(cpipe::runtime::Pipeline::load(pipeline_path, registry, &pipeline, &error) == CPIPE_OK);

    cpipe::tests::SyntheticDngOptions options;
    options.width = 16;
    options.height = 16;
    const auto input_path = cpipe::tests::write_synthetic_dng("min_pipeline", options);
    REQUIRE(pipeline.set_source("raw", "com.cpipe.builtin.dng_input",
                                nlohmann::json{{"path", input_path.string()}}) == CPIPE_OK);

    const auto output_path =
        std::filesystem::temp_directory_path() / "cpipe_min_pipeline_output.heif";
    std::filesystem::remove(output_path);
    REQUIRE(pipeline.run_to_file(output_path, &error) == CPIPE_OK);
    REQUIRE(std::filesystem::file_size(output_path) > 0);

    cpipe::color::HeifInfo info;
    const auto read_status = cpipe::color::read_heif_sdr(output_path, &info, &error);
    INFO(error);
    REQUIRE(read_status == CPIPE_OK);
    REQUIRE(info.width == 16);
    REQUIRE(info.height == 16);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);

    const auto cli_output_path =
        std::filesystem::temp_directory_path() / "cpipe_min_pipeline_cli_output.heif";
    std::filesystem::remove(cli_output_path);
    const auto command = shell_quote(CPIPE_CLI_PATH) + " run " + shell_quote(input_path) + " -p " +
                         shell_quote(pipeline_path) + " -o " + shell_quote(cli_output_path);
    REQUIRE(std::system(command.c_str()) == 0);
    REQUIRE(std::filesystem::file_size(cli_output_path) > 0);

    cpipe::color::HeifInfo cli_info;
    const auto cli_read_status = cpipe::color::read_heif_sdr(cli_output_path, &cli_info, &error);
    INFO(error);
    REQUIRE(cli_read_status == CPIPE_OK);
    REQUIRE(cli_info.width == 16);
    REQUIRE(cli_info.height == 16);
}

TEST_CASE("min pipeline converts the Pixel 8 Pro corpus DNG to decodable SDR HEIF") {
    const auto input_path = source_path() / "tests" / "corpus" / "pixel8pro.dng";
    const auto pipeline_path = source_path() / "examples" / "pipelines" / "min-pipeline.cpipe.json";
    REQUIRE(std::filesystem::exists(input_path));

    const auto cli_output_path =
        std::filesystem::temp_directory_path() / "cpipe_pixel8pro_min_pipeline.heif";
    std::filesystem::remove(cli_output_path);
    const auto command = shell_quote(CPIPE_CLI_PATH) + " run " + shell_quote(input_path) + " -p " +
                         shell_quote(pipeline_path) + " -o " + shell_quote(cli_output_path);
    REQUIRE(std::system(command.c_str()) == 0);
    REQUIRE(std::filesystem::file_size(cli_output_path) > 0);

    std::string error;
    cpipe::color::HeifInfo info;
    const cpipe::color::HeifReadOptions read_options{
        .ocio_config_path = source_path() / "share/cpipe/ocio/v0.1/config.ocio",
    };
    const auto read_status = cpipe::color::read_heif_sdr(cli_output_path, &info, &error);
    INFO(error);
    REQUIRE(read_status == CPIPE_OK);
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.luma_bits_per_pixel == 8);
    REQUIRE(info.icc_profile_bytes > 0);
    REQUIRE(info.nclx_color_primaries == 1);
    REQUIRE(info.nclx_transfer_characteristics == 13);
    REQUIRE(info.nclx_matrix_coefficients == 1);

    cpipe::color::HeifInfo linear_info;
    const auto linear_read_status =
        cpipe::color::read_heif_sdr(cli_output_path, read_options, &linear_info, &error);
    INFO(error);
    REQUIRE(linear_read_status == CPIPE_OK);
    const auto reference = run_isp_reference(input_path);
    require_psnr_at_least(reference, rgb_from_rgba32(linear_info), kMinIntegrationPsnrDb);
}
