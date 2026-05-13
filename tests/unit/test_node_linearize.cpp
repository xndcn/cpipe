// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CalibrationBlock.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

void cpipe_link_builtin_linearize_dng_lut();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferMetadata;
using cpipe::compute::BufferUsage;
using cpipe::compute::CalibrationBlock;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::LinearizationTable;
using cpipe::compute::PixelFormat;

BufferLayout layout(PixelFormat format) {
    BufferLayout out{};
    out.kind = BufferKind::Image2D;
    out.format = format;
    out.ndim = 2;
    out.dims[0] = 4;
    out.dims[1] = 2;
    return out;
}

}  // namespace

TEST_CASE("linearize.dng_lut applies DNG LinearizationTable and records metadata") {
    cpipe_link_builtin_linearize_dng_lut();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.linearize.dng_lut");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"linearization"}));

    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->linearization_table = LinearizationTable{{0, 17, 34, 68, 136, 272}};

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";

    auto input = std::make_shared<CpuBuffer>(
        layout(PixelFormat::R16_UINT), BufferUsage::Input | BufferUsage::CpuRead |
                                          BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    auto output = std::make_shared<CpuBuffer>(
        layout(PixelFormat::R32_SFLOAT), BufferUsage::Output | BufferUsage::CpuRead |
                                           BufferUsage::CpuWrite);

    const std::uint16_t raw[] = {0, 1, 2, 3, 4, 5, 2, 0};
    auto* in = static_cast<std::uint16_t*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    std::copy(std::begin(raw), std::end(raw), in);
    input->unlock_cpu();
    input->flush_cpu_writes();

    cpipe::runtime::HostContext host_context;
    void* instance = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                             &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder = cpipe::runtime::make_metadata_builder_handle(input->metadata(),
                                                                {input->metadata()});
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_metadata_builder_t* out_metadata[] = {builder.get()};
    cpipe_process_ctx process{
        .compute = nullptr,
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
    REQUIRE(output->metadata()->applied_steps == std::vector<std::string>{"linearization"});

    const auto* out = static_cast<const float*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    const float expected[] = {0.0F, 17.0F, 34.0F, 68.0F, 136.0F, 272.0F, 34.0F, 0.0F};
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        REQUIRE(out[i] == Catch::Approx(expected[i]));
    }
    output->unlock_cpu();
}
