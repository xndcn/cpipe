// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/ByteBlob.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/core/PixelFormat.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "opcode_list_3_test_fixture.hpp"

void cpipe_link_builtin_lens_dng_opcode_list_3();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::ByteBlob;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba16_layout(const std::uint32_t width, const std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R16G16B16A16_SFLOAT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
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

std::shared_ptr<CpuBuffer> make_rgba16(std::uint32_t width, std::uint32_t height,
                                       BufferUsage usage) {
    return std::make_shared<CpuBuffer>(rgba16_layout(width, height), usage);
}

void write_rgba(CpuBuffer& buffer, const std::vector<float>& pixels) {
    auto* out = static_cast<std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        out[i] = float_to_half(pixels[i]);
    }
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

std::vector<float> read_rgba(CpuBuffer& buffer, std::size_t count) {
    std::vector<float> pixels(count);
    const auto* in = static_cast<const std::uint16_t*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = half_to_float(in[i]);
    }
    buffer.unlock_cpu();
    return pixels;
}

std::shared_ptr<cpipe::compute::BufferMetadata> metadata_with_opcode_list_3(
    const std::vector<std::byte>& opcode_list_3) {
    auto blob = std::make_shared<ByteBlob>();
    blob->bytes = opcode_list_3;

    auto metadata = std::make_shared<cpipe::compute::BufferMetadata>();
    metadata->cs_role = "scene_linear_rec2020";
    metadata->applied_steps = {"demosaic"};
    metadata->ext_blobs["com.cpipe.dng.opcode_list_3_bytes"] = blob;
    return metadata;
}

std::vector<float> rgba_from_red(const std::vector<float>& red) {
    std::vector<float> rgba;
    rgba.reserve(red.size() * 4U);
    for (const auto value : red) {
        rgba.push_back(value);
        rgba.push_back(value);
        rgba.push_back(value);
        rgba.push_back(1.0F);
    }
    return rgba;
}

std::vector<float> run_opcode_list_3(std::uint32_t width, std::uint32_t height,
                                     const std::vector<float>& input_pixels,
                                     const std::vector<std::byte>& opcode_list_3) {
    cpipe_link_builtin_lens_dng_opcode_list_3();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.lens.dng_opcode_list_3");
    REQUIRE(desc != nullptr);

    auto input = make_rgba16(width, height,
                             BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata_with_opcode_list_3(opcode_list_3));
    write_rgba(*input, input_pixels);
    auto output = make_rgba16(width, height,
                              BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

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
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"demosaic", "opcode_list_3"});
    return read_rgba(*output, input_pixels.size());
}

float red_at(const std::vector<float>& rgba, std::uint32_t width, std::uint32_t x,
             std::uint32_t y) {
    return rgba[((static_cast<std::size_t>(y) * width) + x) * 4U];
}

}  // namespace

TEST_CASE("lens.dng_opcode_list_3 applies WarpRectilinear") {
    const auto input = rgba_from_red({0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F});
    const auto bytes =
        cpipe::tests::opcode_list_3_with({{1, cpipe::tests::warp_rectilinear_params(0.0)}});

    const auto out = run_opcode_list_3(3, 3, input, bytes);

    for (std::uint32_t y = 0; y < 3; ++y) {
        for (std::uint32_t x = 0; x < 3; ++x) {
            REQUIRE(red_at(out, 3, x, y) == Catch::Approx(0.4F).epsilon(0.001F));
        }
    }
}

TEST_CASE("lens.dng_opcode_list_3 applies FixVignetteRadial") {
    const auto input = rgba_from_red(std::vector<float>(9, 1.0F));
    const auto bytes =
        cpipe::tests::opcode_list_3_with({{3, cpipe::tests::fix_vignette_radial_params(1.0)}});

    const auto out = run_opcode_list_3(3, 3, input, bytes);

    REQUIRE(red_at(out, 3, 1, 1) == Catch::Approx(1.0F));
    REQUIRE(red_at(out, 3, 0, 0) == Catch::Approx(2.0F));
    REQUIRE(red_at(out, 3, 1, 0) == Catch::Approx(1.5F));
}

TEST_CASE("lens.dng_opcode_list_3 patches bad pixels") {
    auto input = rgba_from_red(std::vector<float>(9, 1.0F));
    input[4U * 4U] = 0.0F;
    const auto constant_bytes =
        cpipe::tests::opcode_list_3_with({{4, cpipe::tests::fix_bad_pixels_constant_params(0)}});
    auto out = run_opcode_list_3(3, 3, input, constant_bytes);
    REQUIRE(red_at(out, 3, 1, 1) == Catch::Approx(1.0F));

    input = rgba_from_red(std::vector<float>(9, 1.0F));
    input[4U * 4U] = 0.5F;
    const auto list_bytes =
        cpipe::tests::opcode_list_3_with({{5, cpipe::tests::fix_bad_pixels_list_params(1, 1)}});
    out = run_opcode_list_3(3, 3, input, list_bytes);
    REQUIRE(red_at(out, 3, 1, 1) == Catch::Approx(1.0F));
}

TEST_CASE("lens.dng_opcode_list_3 skips optional unknown opcodes and applies TrimBounds") {
    const auto input = rgba_from_red(std::vector<float>(9, 1.0F));
    const auto bytes = cpipe::tests::opcode_list_3_with(
        {{99, {std::byte{1}}}, {6, cpipe::tests::trim_bounds_params(1, 1, 3, 3)}}, 1);

    const auto out = run_opcode_list_3(3, 3, input, bytes);

    REQUIRE(red_at(out, 3, 0, 0) == Catch::Approx(0.0F));
    REQUIRE(red_at(out, 3, 1, 1) == Catch::Approx(1.0F));
    REQUIRE(red_at(out, 3, 2, 2) == Catch::Approx(1.0F));
}
