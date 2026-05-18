// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

void cpipe_link_builtin_blacklevel_dng_levels();
void cpipe_link_builtin_demosaic_quad_bayer_remosaic();
void cpipe_link_builtin_demosaic_rcd();
void cpipe_link_builtin_linearize_dng_lut();
void cpipe_link_builtin_precision_convert();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

constexpr std::array<std::uint8_t, 16> kSonyQbc{0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2};

BufferLayout raw16_layout(const std::uint32_t width, const std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R16_UINT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

std::shared_ptr<CpuBuffer> make_raw16(const std::uint32_t width, const std::uint32_t height,
                                      BufferUsage usage) {
    return std::make_shared<CpuBuffer>(raw16_layout(width, height), usage);
}

std::vector<std::uint16_t> synthetic_qbc(const std::uint32_t width, const std::uint32_t height) {
    std::vector<std::uint16_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            pixels.push_back(static_cast<std::uint16_t>(100U + (y * 23U) + (x * 11U)));
        }
    }
    return pixels;
}

void write_raw16(CpuBuffer& buffer, const std::vector<std::uint16_t>& pixels) {
    auto* out = static_cast<std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(pixels.begin(), pixels.end(), out);
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

std::vector<std::uint16_t> read_raw16(CpuBuffer& buffer, const std::size_t count) {
    std::vector<std::uint16_t> pixels(count);
    const auto* in = static_cast<const std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    std::copy(in, in + count, pixels.begin());
    buffer.unlock_cpu();
    return pixels;
}

std::uint16_t sample_clamped(const std::vector<std::uint16_t>& pixels, const std::uint32_t width,
                             const std::uint32_t height, int x, int y) {
    x = std::clamp(x, 0, static_cast<int>(width) - 1);
    y = std::clamp(y, 0, static_cast<int>(height) - 1);
    return pixels[(static_cast<std::uint32_t>(y) * width) + static_cast<std::uint32_t>(x)];
}

int qbc_color(const int x, const int y) {
    const auto mx = ((x % 4) + 4) % 4;
    const auto my = ((y % 4) + 4) % 4;
    if (my < 2 && mx < 2) {
        return 0;
    }
    if (my >= 2 && mx >= 2) {
        return 2;
    }
    return 1;
}

int rggb_color(const int x, const int y) {
    if ((y & 1) == 0 && (x & 1) == 0) {
        return 0;
    }
    if ((y & 1) != 0 && (x & 1) != 0) {
        return 2;
    }
    return 1;
}

std::uint16_t expected_remosaic_sample(const std::vector<std::uint16_t>& pixels,
                                       const std::uint32_t width, const std::uint32_t height,
                                       const std::uint32_t x, const std::uint32_t y) {
    const auto target = rggb_color(static_cast<int>(x), static_cast<int>(y));
    std::uint32_t weighted_sum = 0;
    std::uint32_t weight_sum = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const auto sx = std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1);
            const auto sy = std::clamp(static_cast<int>(y) + dy, 0, static_cast<int>(height) - 1);
            if (qbc_color(sx, sy) != target) {
                continue;
            }
            const auto grad_x =
                std::abs(static_cast<int>(sample_clamped(pixels, width, height, sx - 1, sy)) -
                         static_cast<int>(sample_clamped(pixels, width, height, sx + 1, sy)));
            const auto grad_y =
                std::abs(static_cast<int>(sample_clamped(pixels, width, height, sx, sy - 1)) -
                         static_cast<int>(sample_clamped(pixels, width, height, sx, sy + 1)));
            const auto penalty = 64 + grad_x + grad_y + ((std::abs(dx) + std::abs(dy)) * 16);
            const auto weight = static_cast<std::uint32_t>(std::max(1, 65536 / penalty));
            weighted_sum += weight * sample_clamped(pixels, width, height, sx, sy);
            weight_sum += weight;
        }
    }
    return static_cast<std::uint16_t>((weighted_sum + (weight_sum / 2U)) / weight_sum);
}

std::filesystem::path write_remosaic_pipeline() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_quad_remosaic_pipeline.json";
    std::ofstream out{path};
    out << R"({
  "$schema":"https://schemas.cpipe.dev/pipeline/v0.4.json",
  "version":"0.4",
  "id":"quad-remosaic-pipeline",
  "inputs":[{"port":"raw","kind":"Image2D","format":"R16_UINT","width":8,"height":8}],
  "nodes":[
    {"id":"remosaic","type":"com.cpipe.demosaic.quad_bayer_remosaic","params":{}},
    {"id":"linearize","type":"com.cpipe.linearize.dng_lut","params":{}},
    {"id":"black","type":"com.cpipe.blacklevel.dng_levels","params":{}},
    {"id":"demosaic","type":"com.cpipe.demosaic.rcd","params":{}}
  ],
  "edges":[
    {"from":"remosaic.bayer","to":"linearize.raw"},
    {"from":"linearize.bayer","to":"black.bayer"},
    {"from":"black.bayer","to":"demosaic.bayer"}
  ]
})";
    return path;
}

}  // namespace

TEST_CASE("demosaic.quad_bayer_remosaic remosaics Sony QBC to RGGB Bayer metadata") {
    cpipe_link_builtin_demosaic_quad_bayer_remosaic();

    constexpr std::uint32_t kWidth = 8;
    constexpr std::uint32_t kHeight = 8;
    const auto pixels = synthetic_qbc(kWidth, kHeight);

    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->cfa = cpipe::compute::CFADescriptor{{4, 4}, kSonyQbc};

    auto metadata = std::make_shared<cpipe::compute::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";

    auto input = make_raw16(kWidth, kHeight,
                            BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    write_raw16(*input, pixels);
    auto output = make_raw16(kWidth, kHeight,
                             BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.demosaic.quad_bayer_remosaic");
    REQUIRE(desc != nullptr);

    cpipe::runtime::HostContext host_context;
    cpipe::runtime::ComputeContext compute;
    void* instance = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                             &instance) == CPIPE_OK);

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

    REQUIRE(desc->main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                             reinterpret_cast<cpipe_node_t*>(instance), nullptr, &process,
                             nullptr) == CPIPE_OK);
    REQUIRE(desc->main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                             nullptr) == CPIPE_OK);
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));

    const auto actual = read_raw16(*output, pixels.size());
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            REQUIRE(actual[(y * kWidth) + x] ==
                    expected_remosaic_sample(pixels, kWidth, kHeight, x, y));
        }
    }

    const auto out_metadata_frozen = output->metadata();
    REQUIRE(out_metadata_frozen != nullptr);
    REQUIRE(out_metadata_frozen->calibration != nullptr);
    REQUIRE(out_metadata_frozen->calibration->cfa.has_value());
    REQUIRE(out_metadata_frozen->calibration->cfa->repeat == std::array<std::uint8_t, 2>{2, 2});
    REQUIRE(out_metadata_frozen->calibration->cfa->pattern[0] == 0);
    REQUIRE(out_metadata_frozen->calibration->cfa->pattern[1] == 1);
    REQUIRE(out_metadata_frozen->calibration->cfa->pattern[2] == 1);
    REQUIRE(out_metadata_frozen->calibration->cfa->pattern[3] == 2);
    REQUIRE(out_metadata_frozen->applied_steps == std::vector<std::string>{"quad_bayer_remosaic"});
}

TEST_CASE("Pipeline load accepts quad remosaic before regular Bayer demosaic") {
    cpipe_link_builtin_blacklevel_dng_levels();
    cpipe_link_builtin_demosaic_quad_bayer_remosaic();
    cpipe_link_builtin_demosaic_rcd();
    cpipe_link_builtin_linearize_dng_lut();
    cpipe_link_builtin_precision_convert();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(write_remosaic_pipeline(), registry, &pipeline,
                                           &error) == CPIPE_OK);
}
