// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
#include <memory>
#include <string>
#include <vector>

#include "gainmap_test_fixture.hpp"

void cpipe_link_builtin_lens_shading_gainmap();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::ByteBlob;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

constexpr std::array<std::uint8_t, 16> kSonyQbc{0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2};

BufferLayout r32_layout(const std::uint32_t width, const std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R32_SFLOAT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

std::shared_ptr<CpuBuffer> make_r32(const std::uint32_t width, const std::uint32_t height,
                                    BufferUsage usage) {
    return std::make_shared<CpuBuffer>(r32_layout(width, height), usage);
}

void write_f32(CpuBuffer& buffer, const std::vector<float>& pixels) {
    auto* out = static_cast<float*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(pixels.begin(), pixels.end(), out);
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();
}

std::vector<float> read_f32(CpuBuffer& buffer, const std::size_t count) {
    std::vector<float> pixels(count);
    const auto* in = static_cast<const float*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    std::copy(in, in + count, pixels.begin());
    buffer.unlock_cpu();
    return pixels;
}

std::shared_ptr<cpipe::compute::BufferMetadata> metadata_with_gainmap(
    cpipe::compute::CFADescriptor cfa, const std::vector<std::byte>& opcode_list_2) {
    auto calibration = std::make_shared<cpipe::compute::CalibrationBlock>();
    calibration->cfa = cfa;

    auto blob = std::make_shared<ByteBlob>();
    blob->bytes = opcode_list_2;

    auto metadata = std::make_shared<cpipe::compute::BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->ext_blobs["com.cpipe.dng.opcode_list_2_bytes"] = blob;
    return metadata;
}

std::vector<float> run_gainmap(const std::uint32_t width, const std::uint32_t height,
                               const std::vector<float>& input_pixels,
                               std::shared_ptr<cpipe::compute::BufferMetadata> metadata) {
    cpipe_link_builtin_lens_shading_gainmap();
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.lens.shading_gainmap");
    REQUIRE(desc != nullptr);

    auto input =
        make_r32(width, height, BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(std::move(metadata));
    write_f32(*input, input_pixels);
    auto output =
        make_r32(width, height, BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

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
    REQUIRE(output->metadata()->applied_steps == std::vector<std::string>{"lens_shading_gainmap"});
    return read_f32(*output, input_pixels.size());
}

}  // namespace

TEST_CASE("lens.shading_gainmap bilinearly resamples a one-plane GainMap") {
    const auto params =
        cpipe::tests::gain_map_params(2, 2, 0, 1, 2.0, 2.0, {1.0F, 2.0F, 3.0F, 5.0F});
    const auto bytes = cpipe::tests::opcode_list_2_with_gain_maps({params});
    const auto metadata = metadata_with_gainmap(cpipe::compute::CFADescriptor{{0, 1, 1, 2}}, bytes);

    const auto out = run_gainmap(3, 3, std::vector<float>(9, 10.0F), metadata);

    REQUIRE(out[0] == Catch::Approx(10.0F));
    REQUIRE(out[1] == Catch::Approx(15.0F));
    REQUIRE(out[2] == Catch::Approx(20.0F));
    REQUIRE(out[4] == Catch::Approx(27.5F));
    REQUIRE(out[8] == Catch::Approx(50.0F));
}

TEST_CASE("lens.shading_gainmap routes four GainMap planes on Sony QBC") {
    std::vector<std::vector<std::byte>> opcodes;
    for (std::uint32_t plane = 0; plane < 4; ++plane) {
        opcodes.push_back(cpipe::tests::gain_map_params(1, 1, plane, 4, 1.0, 1.0,
                                                        {1.0F + static_cast<float>(plane)}));
    }
    const auto bytes = cpipe::tests::opcode_list_2_with_gain_maps(opcodes);
    const auto metadata =
        metadata_with_gainmap(cpipe::compute::CFADescriptor{{4, 4}, kSonyQbc}, bytes);

    const auto out = run_gainmap(4, 4, std::vector<float>(16, 1.0F), metadata);

    REQUIRE(out[0] == Catch::Approx(1.0F));
    REQUIRE(out[1] == Catch::Approx(1.0F));
    REQUIRE(out[2] == Catch::Approx(2.0F));
    REQUIRE(out[6] == Catch::Approx(2.0F));
    REQUIRE(out[8] == Catch::Approx(3.0F));
    REQUIRE(out[13] == Catch::Approx(3.0F));
    REQUIRE(out[10] == Catch::Approx(4.0F));
    REQUIRE(out[15] == Catch::Approx(4.0F));
}
