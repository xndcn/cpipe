// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <OpenColorIO/OpenColorIO.h>

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cpipe/color/OcioVulkanProcessor.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cpipe/runtime/VulkanImage.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

constexpr std::uint32_t kWidth = 4;
constexpr std::uint32_t kHeight = 2;

BufferLayout rgba_layout(PixelFormat format) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = format;
    layout.ndim = 2;
    layout.dims[0] = kWidth;
    layout.dims[1] = kHeight;
    return layout;
}

std::filesystem::path config_path() {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "share" / "cpipe" / "ocio" / "v0.2" /
           "config.ocio";
}

std::vector<std::uint16_t> scene_rgba16() {
    return {
        0x2e14, 0x3266, 0x350a, 0x3c00, 0x3733, 0x34cd, 0x2fb8, 0x3800, 0x3a66, 0x36cd, 0x3333,
        0x3c00, 0x2a66, 0x31eb, 0x38cd, 0x3400, 0x3b33, 0x3a66, 0x3800, 0x3c00, 0x34cd, 0x399a,
        0x2e14, 0x3800, 0x30a4, 0x2e14, 0x3a66, 0x3c00, 0x37ae, 0x3333, 0x2ccc, 0x3400,
    };
}

std::uint32_t half_to_bits(std::uint16_t bits) {
    const auto sign = static_cast<std::uint32_t>(bits & 0x8000U) << 16U;
    auto exponent = static_cast<std::uint32_t>((bits >> 10U) & 0x1fU);
    auto mantissa = static_cast<std::uint32_t>(bits & 0x03ffU);
    if (exponent == 0) {
        if (mantissa == 0) {
            return sign;
        }
        exponent = 1;
        while ((mantissa & 0x0400U) == 0U) {
            mantissa <<= 1U;
            --exponent;
        }
        mantissa &= 0x03ffU;
        return sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
    }
    if (exponent == 31U) {
        return sign | 0x7f800000U | (mantissa << 13U);
    }
    return sign | ((exponent + 112U) << 23U) | (mantissa << 13U);
}

float half_to_float(std::uint16_t bits) {
    const auto out = half_to_bits(bits);
    float value = 0.0F;
    std::memcpy(&value, &out, sizeof(value));
    return value;
}

void apply_cpu_ocio(std::vector<float>& rgba) {
    const auto config = OCIO::Config::CreateFromFile(config_path().string().c_str());
    const auto cpu =
        config->getProcessor("scene_linear_rec2020", "output_srgb")->getDefaultCPUProcessor();
    OCIO::PackedImageDesc desc{rgba.data(), kWidth, kHeight, 4};
    cpu->apply(desc);
}

}  // namespace

TEST_CASE("OcioVulkanProcessor compute pass matches CPU OCIO for sRGB") {
    const auto* enabled = std::getenv("CPIPE_VULKAN_AVAILABLE");
    if (enabled == nullptr || std::string_view{enabled} != "ON") {
        SUCCEED("CPIPE_VULKAN_AVAILABLE is not ON; skipping OCIO Vulkan processor check");
        return;
    }

    const auto created = cpipe::runtime::VulkanDevicePlane::create();
    REQUIRE(created.status == cpipe::compute::StatusCode::Ok);
    REQUIRE(created.plane != nullptr);

    cpipe::runtime::VulkanImage input{
        created.plane, rgba_layout(PixelFormat::R16G16B16A16_SFLOAT),
        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite | BufferUsage::GpuStorage,
        "scene_linear_rec2020"};
    cpipe::runtime::VulkanImage output{created.plane, rgba_layout(PixelFormat::R8G8B8A8_UNORM),
                                       BufferUsage::Output | BufferUsage::CpuRead |
                                           BufferUsage::CpuWrite | BufferUsage::GpuStorage,
                                       "output_srgb"};

    const auto input_values = scene_rgba16();
    auto* input_ptr = static_cast<std::uint16_t*>(input.lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(input_values.begin(), input_values.end(), input_ptr);
    input.unlock_cpu();
    input.flush_cpu_writes();

    cpipe::color::OcioVulkanProcessor processor{config_path(), "scene_linear_rec2020",
                                                "output_srgb"};
    REQUIRE(processor.compute_pass(created.plane, input, output) == CPIPE_OK);

    std::vector<float> cpu_rgba(input_values.size());
    for (std::size_t i = 0; i < input_values.size(); ++i) {
        cpu_rgba[i] = half_to_float(input_values[i]);
    }
    apply_cpu_ocio(cpu_rgba);

    const auto* gpu = static_cast<const std::uint8_t*>(output.lock_cpu(IBuffer::CpuAccess::Read));
    for (std::size_t i = 0; i < cpu_rgba.size(); ++i) {
        const auto expected =
            static_cast<int>(std::lround(std::clamp(cpu_rgba[i], 0.0F, 1.0F) * 255.0F));
        REQUIRE(static_cast<int>(gpu[i]) == Catch::Approx(expected).margin(0.5));
    }
    output.unlock_cpu();
}
