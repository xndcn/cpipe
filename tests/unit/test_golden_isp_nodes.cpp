// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/ByteBlob.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gainmap_test_fixture.hpp"
#include "opcode_list_3_test_fixture.hpp"

void cpipe_link_builtin_blacklevel_dng_levels();
void cpipe_link_builtin_colormatrix_dng_to_working();
void cpipe_link_builtin_demosaic_amaze();
void cpipe_link_builtin_demosaic_bilinear();
void cpipe_link_builtin_demosaic_quad_bayer_remosaic();
void cpipe_link_builtin_demosaic_rcd();
void cpipe_link_builtin_lens_dng_opcode_list_3();
void cpipe_link_builtin_lens_shading_gainmap();
void cpipe_link_builtin_linearize_dng_lut();
void cpipe_link_builtin_wb_dual_illuminant();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferMetadata;
using cpipe::compute::BufferUsage;
using cpipe::compute::ByteBlob;
using cpipe::compute::CalibrationBlock;
using cpipe::compute::CFADescriptor;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::LinearizationTable;
using cpipe::compute::Matrix3;
using cpipe::compute::PixelFormat;

constexpr double kMinPsnrDb = 40.0;

struct FloatImage {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> pixels;
};

std::filesystem::path golden_path(const std::string& node, const std::string& filename) {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "tests" / "golden" / node / filename;
}

FloatImage read_fixture(const std::string& node, const std::string& filename,
                        const int expected_channels) {
    const auto path = golden_path(node, filename);
    INFO("fixture: " << path);
    REQUIRE(std::filesystem::exists(path));

    OIIO::ImageBuf buffer{path.string()};
    REQUIRE(buffer.read(0, 0, true, OIIO::TypeDesc::FLOAT));
    const auto& spec = buffer.spec();
    REQUIRE(spec.width > 0);
    REQUIRE(spec.height > 0);
    REQUIRE(spec.nchannels == expected_channels);

    FloatImage image{spec.width, spec.height, spec.nchannels, {}};
    image.pixels.resize(static_cast<std::size_t>(image.width) *
                        static_cast<std::size_t>(image.height) *
                        static_cast<std::size_t>(image.channels));
    const OIIO::ROI roi{0, image.width, 0, image.height, 0, 1, 0, image.channels};
    REQUIRE(buffer.get_pixels(roi, OIIO::TypeDesc::FLOAT, image.pixels.data()));
    return image;
}

OIIO::ImageBuf image_buf_from_pixels(const std::string& name, const FloatImage& image) {
    OIIO::ImageSpec spec{image.width, image.height, image.channels, OIIO::TypeDesc::FLOAT};
    OIIO::ImageBuf buffer{name, spec};
    const OIIO::ROI roi{0, image.width, 0, image.height, 0, 1, 0, image.channels};
    REQUIRE(buffer.set_pixels(roi, OIIO::TypeDesc::FLOAT, image.pixels.data()));
    return buffer;
}

void require_psnr_at_least(const std::string& fixture, const FloatImage& actual) {
    const auto expected = read_fixture(fixture, "out.exr", actual.channels);
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);

    auto actual_buf = image_buf_from_pixels(fixture + ".actual", actual);
    auto expected_buf = image_buf_from_pixels(fixture + ".expected", expected);
    const auto result = OIIO::ImageBufAlgo::compare(actual_buf, expected_buf, 0.0F, 0.0F);
    CAPTURE(fixture, result.PSNR, result.rms_error, result.maxerror, result.nfail);
    REQUIRE_FALSE(result.error);
    REQUIRE((std::isinf(result.PSNR) || result.PSNR >= kMinPsnrDb));
}

BufferLayout image2d_layout(PixelFormat format, const int width, const int height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = format;
    layout.ndim = 2;
    layout.dims[0] = static_cast<std::uint32_t>(width);
    layout.dims[1] = static_cast<std::uint32_t>(height);
    return layout;
}

std::shared_ptr<CpuBuffer> make_buffer(PixelFormat format, const int width, const int height,
                                       BufferUsage usage) {
    return std::make_shared<CpuBuffer>(image2d_layout(format, width, height), usage);
}

std::uint16_t float_to_half(float value) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    const auto half = static_cast<_Float16>(value);
    std::uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

float half_to_float(std::uint16_t bits) {
    static_assert(sizeof(_Float16) == sizeof(std::uint16_t));
    _Float16 half = 0.0F;
    std::memcpy(&half, &bits, sizeof(bits));
    return static_cast<float>(half);
}

void write_r16(CpuBuffer& buffer, const FloatImage& image) {
    auto* out = static_cast<std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        out[i] = static_cast<std::uint16_t>(std::clamp(image.pixels[i], 0.0F, 65535.0F));
    }
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

FloatImage read_r16(CpuBuffer& buffer, const int width, const int height) {
    FloatImage image{width, height, 1, {}};
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const auto* in = static_cast<const std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = static_cast<float>(in[i]);
    }
    buffer.unlock_cpu();
    return image;
}

void write_f32(CpuBuffer& buffer, const FloatImage& image) {
    auto* out = static_cast<float*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(image.pixels.begin(), image.pixels.end(), out);
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

void write_rgba16(CpuBuffer& buffer, const FloatImage& image) {
    auto* out = static_cast<std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        out[i] = float_to_half(image.pixels[i]);
    }
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

FloatImage read_f32(CpuBuffer& buffer, const int width, const int height) {
    FloatImage image{width, height, 1, {}};
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const auto* in = static_cast<const float*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    std::copy(in, in + image.pixels.size(), image.pixels.begin());
    buffer.unlock_cpu();
    return image;
}

FloatImage read_rgba16(CpuBuffer& buffer, const int width, const int height) {
    FloatImage image{width, height, 4, {}};
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    const auto* in = static_cast<const std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = half_to_float(in[i]);
    }
    buffer.unlock_cpu();
    return image;
}

const cpipe_plugin_desc_t& require_node(cpipe::runtime::Registry& registry,
                                        const std::string& node_id) {
    const auto* desc = registry.find(node_id.c_str());
    REQUIRE(desc != nullptr);
    return *desc;
}

void process_single_input_node(const cpipe_plugin_desc_t& desc,
                               const std::shared_ptr<CpuBuffer>& input,
                               const std::shared_ptr<CpuBuffer>& output,
                               cpipe::runtime::ComputeContext* compute = nullptr) {
    cpipe::runtime::HostContext host_context;
    cpipe::runtime::ComputeContext local_compute;
    auto* active_compute = compute == nullptr ? &local_compute : compute;
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
        .compute = reinterpret_cast<cpipe_compute_t*>(active_compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
        .out_metadata = out_metadata,
    };

    const auto status =
        desc.main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                        reinterpret_cast<cpipe_node_t*>(instance), nullptr, &process, nullptr);
    CAPTURE(desc.node_id, status);
    REQUIRE(status == CPIPE_OK);
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
}

std::shared_ptr<BufferMetadata> metadata_with_cfa() {
    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->cfa = CFADescriptor{{0, 1, 1, 2}};

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    return metadata;
}

std::shared_ptr<BufferMetadata> metadata_with_quad_bayer_cfa() {
    constexpr std::array<std::uint8_t, 16> kSonyQbc{
        0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2,
    };
    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->cfa = CFADescriptor{{4, 4}, kSonyQbc};

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    return metadata;
}

void register_builtin_nodes(cpipe::runtime::Registry& registry) {
    cpipe_link_builtin_linearize_dng_lut();
    cpipe_link_builtin_blacklevel_dng_levels();
    cpipe_link_builtin_demosaic_amaze();
    cpipe_link_builtin_demosaic_bilinear();
    cpipe_link_builtin_demosaic_quad_bayer_remosaic();
    cpipe_link_builtin_demosaic_rcd();
    cpipe_link_builtin_lens_shading_gainmap();
    cpipe_link_builtin_lens_dng_opcode_list_3();
    cpipe_link_builtin_wb_dual_illuminant();
    cpipe_link_builtin_colormatrix_dng_to_working();
    registry.load_builtin_nodes();
}

void assert_linearize_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.linearize.dng_lut";
    constexpr auto kFixture = "linearize.dng_lut";
    const auto input_image = read_fixture(kFixture, "in.exr", 1);

    auto metadata = std::make_shared<BufferMetadata>();
    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->linearization_table =
        LinearizationTable{{0, 11, 23, 36, 50, 65, 81, 98, 116, 135, 155, 176, 198, 221, 245, 270}};
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";

    auto input = make_buffer(PixelFormat::R16_UINT, input_image.width, input_image.height,
                             BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_r16(*input, input_image);
    auto output = make_buffer(PixelFormat::R32_SFLOAT, input_image.width, input_image.height,
                              BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    process_single_input_node(require_node(registry, kNode), input, output);
    require_psnr_at_least(kFixture, read_f32(*output, input_image.width, input_image.height));
}

void assert_blacklevel_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.blacklevel.dng_levels";
    constexpr auto kFixture = "blacklevel.dng_levels";
    const auto input_image = read_fixture(kFixture, "in.exr", 1);

    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->cfa = CFADescriptor{{0, 1, 1, 2}};
    calibration->black_level = {64.0F, 96.0F, 128.0F, 128.0F};
    calibration->white_level = 1024;

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization"};

    auto input = make_buffer(PixelFormat::R32_SFLOAT, input_image.width, input_image.height,
                             BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_f32(*input, input_image);
    auto output = make_buffer(PixelFormat::R32_SFLOAT, input_image.width, input_image.height,
                              BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    process_single_input_node(require_node(registry, kNode), input, output);
    require_psnr_at_least(kFixture, read_f32(*output, input_image.width, input_image.height));
}

void assert_gainmap_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.lens.shading_gainmap";
    constexpr auto kFixture = "lens.shading_gainmap";
    const auto input_image = read_fixture(kFixture, "in.exr", 1);

    auto metadata = metadata_with_cfa();
    metadata->applied_steps = {"linearization"};
    auto opcode_blob = std::make_shared<ByteBlob>();
    const auto params =
        cpipe::tests::gain_map_params(2, 2, 0, 1, 7.0, 7.0, {1.0F, 1.2F, 1.4F, 1.8F});
    opcode_blob->bytes = cpipe::tests::opcode_list_2_with_gain_maps({params});
    metadata->ext_blobs["com.cpipe.dng.opcode_list_2_bytes"] = opcode_blob;

    auto input = make_buffer(PixelFormat::R32_SFLOAT, input_image.width, input_image.height,
                             BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_f32(*input, input_image);
    auto output = make_buffer(PixelFormat::R32_SFLOAT, input_image.width, input_image.height,
                              BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    process_single_input_node(require_node(registry, kNode), input, output);
    require_psnr_at_least(kFixture, read_f32(*output, input_image.width, input_image.height));
}

void assert_demosaic_golden(cpipe::runtime::Registry& registry, const char* node,
                            const char* fixture) {
    const auto input_image = read_fixture(fixture, "in.exr", 1);

    auto metadata = metadata_with_cfa();
    metadata->applied_steps = {"linearization", "black_white_scaling"};

    auto input = make_buffer(PixelFormat::R32_SFLOAT, input_image.width, input_image.height,
                             BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_f32(*input, input_image);
    auto output =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::ComputeContext compute;
    process_single_input_node(require_node(registry, node), input, output, &compute);
    require_psnr_at_least(fixture, read_rgba16(*output, input_image.width, input_image.height));
}

void assert_quad_bayer_remosaic_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.demosaic.quad_bayer_remosaic";
    constexpr auto kFixture = "demosaic.quad_bayer_remosaic";
    const auto input_image = read_fixture(kFixture, "in.exr", 1);

    auto metadata = metadata_with_quad_bayer_cfa();
    auto input = make_buffer(PixelFormat::R16_UINT, input_image.width, input_image.height,
                             BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_r16(*input, input_image);
    auto output = make_buffer(PixelFormat::R16_UINT, input_image.width, input_image.height,
                              BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::ComputeContext compute;
    process_single_input_node(require_node(registry, kNode), input, output, &compute);
    require_psnr_at_least(kFixture, read_r16(*output, input_image.width, input_image.height));
}

void assert_opcode_list_3_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.lens.dng_opcode_list_3";
    constexpr auto kFixture = "lens.dng_opcode_list_3";
    const auto input_image = read_fixture(kFixture, "in.exr", 4);

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"demosaic"};
    auto opcode_blob = std::make_shared<ByteBlob>();
    opcode_blob->bytes =
        cpipe::tests::opcode_list_3_with({{3, cpipe::tests::fix_vignette_radial_params(0.5)}});
    metadata->ext_blobs["com.cpipe.dng.opcode_list_3_bytes"] = opcode_blob;

    auto input =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_rgba16(*input, input_image);
    auto output =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    process_single_input_node(require_node(registry, kNode), input, output);
    require_psnr_at_least(kFixture, read_rgba16(*output, input_image.width, input_image.height));
}

void assert_wb_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.wb.dual_illuminant";
    constexpr auto kFixture = "wb.dual_illuminant";
    const auto input_image = read_fixture(kFixture, "in.exr", 4);

    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->calibration_illuminant1 = 17;
    calibration->calibration_illuminant2 = 21;
    calibration->color_matrix1 = Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    calibration->color_matrix2 = Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    calibration->forward_matrix1 = Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
    calibration->forward_matrix2 = Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic"};
    metadata->capture.as_shot_neutral = {0.5F, 1.0F, 0.25F};

    auto input =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_rgba16(*input, input_image);
    auto output =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    process_single_input_node(require_node(registry, kNode), input, output);
    require_psnr_at_least(kFixture, read_rgba16(*output, input_image.width, input_image.height));
}

void assert_colormatrix_golden(cpipe::runtime::Registry& registry) {
    constexpr auto kNode = "com.cpipe.colormatrix.dng_to_working";
    constexpr auto kFixture = "colormatrix.dng_to_working";
    const auto input_image = read_fixture(kFixture, "in.exr", 4);

    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->color_matrix1 = Matrix3{{2.0F, 0.0F, 0.0F, 0.0F, 4.0F, 0.0F, 0.0F, 0.0F, 0.5F}};

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling", "demosaic", "white_balance"};

    auto input =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_rgba16(*input, input_image);
    auto output =
        make_buffer(PixelFormat::R16G16B16A16_SFLOAT, input_image.width, input_image.height,
                    BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    process_single_input_node(require_node(registry, kNode), input, output);
    require_psnr_at_least(kFixture, read_rgba16(*output, input_image.width, input_image.height));
}

}  // namespace

TEST_CASE("P1 ISP node EXR goldens meet PSNR threshold") {
    cpipe::runtime::Registry registry;
    register_builtin_nodes(registry);

    SECTION("linearize.dng_lut") {
        assert_linearize_golden(registry);
    }
    SECTION("blacklevel.dng_levels") {
        assert_blacklevel_golden(registry);
    }
    SECTION("lens.shading_gainmap") {
        assert_gainmap_golden(registry);
    }
    SECTION("demosaic.bilinear") {
        assert_demosaic_golden(registry, "com.cpipe.demosaic.bilinear", "demosaic.bilinear");
    }
    SECTION("demosaic.rcd") {
        assert_demosaic_golden(registry, "com.cpipe.demosaic.rcd", "demosaic.rcd");
    }
    SECTION("demosaic.amaze") {
        assert_demosaic_golden(registry, "com.cpipe.demosaic.amaze", "demosaic.amaze");
    }
    SECTION("demosaic.quad_bayer_remosaic") {
        assert_quad_bayer_remosaic_golden(registry);
    }
    SECTION("lens.dng_opcode_list_3") {
        assert_opcode_list_3_golden(registry);
    }
    SECTION("wb.dual_illuminant") {
        assert_wb_golden(registry);
    }
    SECTION("colormatrix.dng_to_working") {
        assert_colormatrix_golden(registry);
    }
}
