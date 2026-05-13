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
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

void cpipe_link_builtin_blacklevel_dng_levels();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferMetadata;
using cpipe::compute::BufferUsage;
using cpipe::compute::CalibrationBlock;
using cpipe::compute::CFADescriptor;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout r32_layout() {
    BufferLayout out{};
    out.kind = BufferKind::Image2D;
    out.format = PixelFormat::R32_SFLOAT;
    out.ndim = 2;
    out.dims[0] = 4;
    out.dims[1] = 2;
    return out;
}

}  // namespace

TEST_CASE("blacklevel.dng_levels subtracts per-CFA black and normalizes by white") {
    cpipe_link_builtin_blacklevel_dng_levels();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.blacklevel.dng_levels");
    REQUIRE(desc != nullptr);

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    REQUIRE(manifest.at("ports").at(1).at("metadata").at("sets_steps_applied") ==
            nlohmann::json::array({"black_white_scaling"}));

    auto calibration = std::make_shared<CalibrationBlock>();
    calibration->cfa = CFADescriptor{{0, 1, 1, 2}};
    calibration->black_level = {10.0F, 20.0F, 30.0F, 30.0F};
    calibration->white_level = 110;

    auto metadata = std::make_shared<BufferMetadata>();
    metadata->calibration = calibration;
    metadata->cs_role = "raw_camera";
    metadata->applied_steps.push_back("linearization");

    auto input = std::make_shared<CpuBuffer>(
        r32_layout(), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    auto output = std::make_shared<CpuBuffer>(
        r32_layout(), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    const float raw[] = {10.0F, 65.0F, 110.0F, 200.0F, 20.0F, 70.0F, 0.0F, 110.0F};
    auto* in = static_cast<float*>(input->lock_cpu(IBuffer::CpuAccess::Write));
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
    REQUIRE(output->metadata()->applied_steps ==
            std::vector<std::string>{"linearization", "black_white_scaling"});

    const auto* out = static_cast<const float*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    const float expected[] = {0.0F, 0.5F, 1.0F, 1.0F, 0.0F, 0.5F, 0.0F, 1.0F};
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        REQUIRE(out[i] == Catch::Approx(expected[i]));
    }
    output->unlock_cpu();
}
