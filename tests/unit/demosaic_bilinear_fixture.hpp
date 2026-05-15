// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

void cpipe_link_builtin_demosaic_bilinear();

namespace cpipe::tests {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferMetadata;
using cpipe::compute::BufferUsage;
using cpipe::compute::CalibrationBlock;
using cpipe::compute::CFADescriptor;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

inline BufferLayout demosaic_layout(PixelFormat format) {
    BufferLayout out{};
    out.kind = BufferKind::Image2D;
    out.format = format;
    out.ndim = 2;
    out.dims[0] = 16;
    out.dims[1] = 16;
    return out;
}

struct DemosaicRun {
    std::shared_ptr<const BufferMetadata> metadata;
    std::vector<float> pixels;
};

inline const std::array<std::array<std::uint8_t, 4>, 4>& cfa_patterns() {
    static constexpr std::array<std::array<std::uint8_t, 4>, 4> patterns{{
        {0, 1, 1, 2},
        {2, 1, 1, 0},
        {1, 0, 2, 1},
        {1, 2, 0, 1},
    }};
    return patterns;
}

inline std::uint8_t cfa_at(const std::array<std::uint8_t, 4>& pattern, std::int32_t x,
                           std::int32_t y) {
    return pattern[((static_cast<std::uint32_t>(y) & 1U) * 2U) +
                   (static_cast<std::uint32_t>(x) & 1U)];
}

inline float sample(const std::vector<float>& bayer, std::uint32_t width, std::uint32_t height,
                    std::int32_t x, std::int32_t y) {
    x = std::clamp(x, 0, static_cast<std::int32_t>(width) - 1);
    y = std::clamp(y, 0, static_cast<std::int32_t>(height) - 1);
    return bayer[(static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x)];
}

inline float half_to_float(std::uint16_t bits) {
    const auto sign = static_cast<std::uint32_t>(bits & 0x8000U) << 16U;
    auto exponent = static_cast<std::uint32_t>((bits >> 10U) & 0x1fU);
    auto mantissa = static_cast<std::uint32_t>(bits & 0x03ffU);
    std::uint32_t out = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            out = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x0400U) == 0U) {
                mantissa <<= 1U;
                --exponent;
            }
            mantissa &= 0x03ffU;
            out = sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
        }
    } else if (exponent == 31U) {
        out = sign | 0x7f800000U | (mantissa << 13U);
    } else {
        out = sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
    }
    float value = 0.0F;
    std::memcpy(&value, &out, sizeof(value));
    return value;
}

inline std::array<float, 4> expected_rgba(const std::vector<float>& bayer, std::uint32_t width,
                                          std::uint32_t height,
                                          const std::array<std::uint8_t, 4>& pattern,
                                          std::uint32_t x, std::uint32_t y) {
    const auto sx = static_cast<std::int32_t>(x);
    const auto sy = static_cast<std::int32_t>(y);
    const auto center = sample(bayer, width, height, sx, sy);
    const auto horizontal = 0.5F * (sample(bayer, width, height, sx - 1, sy) +
                                    sample(bayer, width, height, sx + 1, sy));
    const auto vertical = 0.5F * (sample(bayer, width, height, sx, sy - 1) +
                                  sample(bayer, width, height, sx, sy + 1));
    const auto cross =
        0.25F *
        (sample(bayer, width, height, sx - 1, sy) + sample(bayer, width, height, sx + 1, sy) +
         sample(bayer, width, height, sx, sy - 1) + sample(bayer, width, height, sx, sy + 1));
    const auto diagonal = 0.25F * (sample(bayer, width, height, sx - 1, sy - 1) +
                                   sample(bayer, width, height, sx + 1, sy - 1) +
                                   sample(bayer, width, height, sx - 1, sy + 1) +
                                   sample(bayer, width, height, sx + 1, sy + 1));

    switch (cfa_at(pattern, sx, sy)) {
        case 0:
            return {center, cross, diagonal, 1.0F};
        case 2:
            return {diagonal, cross, center, 1.0F};
        default:
            if (cfa_at(pattern, sx - 1, sy) == 0) {
                return {horizontal, center, vertical, 1.0F};
            }
            return {vertical, center, horizontal, 1.0F};
    }
}

inline DemosaicRun run_demosaic_node(const char* node_id, void (*link_builtin)(),
                                     const std::array<std::uint8_t, 4>& pattern,
                                     const std::vector<float>& raw) {
    link_builtin();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find(node_id);
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"demosaic"}));
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("clears_fields") ==
            nlohmann::json::array({"calibration.cfa"}));

    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->cfa = CFADescriptor{pattern};

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps = {"linearization", "black_white_scaling"};

    auto input = std::make_shared<CpuBuffer>(
        demosaic_layout(PixelFormat::R32_SFLOAT),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    auto output = std::make_shared<CpuBuffer>(
        demosaic_layout(PixelFormat::R16G16B16A16_SFLOAT),
        BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* in = static_cast<float*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(raw.begin(), raw.end(), in);
    input->unlock_cpu();
    input->flush_cpu_writes();

    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
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
    REQUIRE(output->metadata()->calibration != nullptr);
    REQUIRE_FALSE(output->metadata()->calibration->cfa.has_value());
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"linearization", "black_white_scaling", "demosaic"});

    const auto* out = static_cast<const std::uint16_t*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    std::vector<float> pixels(raw.size() * 4U);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = half_to_float(out[i]);
    }
    output->unlock_cpu();
    return DemosaicRun{output->metadata(), std::move(pixels)};
}

inline std::vector<float> synthetic_bayer() {
    std::vector<float> raw(16U * 16U);
    for (std::uint32_t y = 0; y < 16U; ++y) {
        for (std::uint32_t x = 0; x < 16U; ++x) {
            raw[(static_cast<std::size_t>(y) * 16U) + x] =
                0.02F + (0.031F * static_cast<float>(x)) + (0.017F * static_cast<float>(y));
        }
    }
    return raw;
}

inline void assert_demosaic_node() {
    const auto raw = synthetic_bayer();
    for (const auto& pattern : cfa_patterns()) {
        const auto run = run_demosaic_node("com.cpipe.demosaic.bilinear",
                                           &cpipe_link_builtin_demosaic_bilinear, pattern, raw);

        for (std::uint32_t y = 0; y < 16U; ++y) {
            for (std::uint32_t x = 0; x < 16U; ++x) {
                const auto expected = expected_rgba(raw, 16U, 16U, pattern, x, y);
                const auto base = (static_cast<std::size_t>(y) * 16U + x) * 4U;
                for (std::size_t c = 0; c < expected.size(); ++c) {
                    REQUIRE(run.pixels[base + c] == Catch::Approx(expected[c]).margin(0.001F));
                }
            }
        }
    }
}

}  // namespace cpipe::tests
