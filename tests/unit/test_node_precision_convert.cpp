// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/ParamHandle.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "color_node_fixture.hpp"

void cpipe_link_builtin_precision_convert();

namespace {

cpipe::tests::BufferLayout rgba8_layout(std::uint32_t width, std::uint32_t height) {
    cpipe::tests::BufferLayout layout{};
    layout.kind = cpipe::tests::BufferKind::Image2D;
    layout.format = cpipe::tests::PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

std::vector<std::uint8_t> read_bytes(cpipe::tests::CpuBuffer& buffer) {
    const auto* data =
        static_cast<const std::uint8_t*>(buffer.lock_cpu(cpipe::tests::IBuffer::CpuAccess::Read));
    std::vector<std::uint8_t> out(data, data + buffer.size_bytes());
    buffer.unlock_cpu();
    return out;
}

cpipe_status_t process_precision_convert(const cpipe_plugin_desc_t& desc,
                                         const std::shared_ptr<cpipe::tests::CpuBuffer>& input,
                                         const std::shared_ptr<cpipe::tests::CpuBuffer>& output) {
    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::HostContext host_context;
    auto params = cpipe::runtime::make_param_handle({{"target_format", "R8G8B8A8_UNORM"}});

    void* instance = nullptr;
    REQUIRE(desc.main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, params.get(),
                            nullptr, &instance) == CPIPE_OK);

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

    const auto status = static_cast<cpipe_status_t>(desc.main_entry(
        CPIPE_ACTION_PROCESS, host_context.host(), reinterpret_cast<cpipe_node_t*>(instance),
        params.get(), &process, nullptr));
    output->set_metadata(cpipe::runtime::freeze_metadata_builder(builder.get()));
    REQUIRE(desc.main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                            nullptr) == CPIPE_OK);
    return status;
}

}  // namespace

TEST_CASE("precision_convert maps rgba16f to rgba8 within one LSB") {
    cpipe_link_builtin_precision_convert();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.precision_convert");
    REQUIRE(desc != nullptr);

    auto metadata = std::make_shared<cpipe::tests::BufferMetadata>();
    metadata->cs_role = "display";

    auto input = std::make_shared<cpipe::tests::CpuBuffer>(cpipe::tests::rgba16_layout(2, 1),
                                                           cpipe::tests::BufferUsage::Input |
                                                               cpipe::tests::BufferUsage::CpuRead |
                                                               cpipe::tests::BufferUsage::CpuWrite);
    input->set_metadata(metadata);
    cpipe::tests::write_rgba16(*input, {{{0.0F, 0.5F, 1.0F, 1.0F}, {0.25F, 0.75F, 1.25F, -0.5F}}});

    auto output = std::make_shared<cpipe::tests::CpuBuffer>(
        rgba8_layout(2, 1), cpipe::tests::BufferUsage::Output | cpipe::tests::BufferUsage::CpuRead |
                                cpipe::tests::BufferUsage::CpuWrite);

    REQUIRE(process_precision_convert(*desc, input, output) == CPIPE_OK);

    const auto bytes = read_bytes(*output);
    REQUIRE(bytes.size() == 8);
    REQUIRE(bytes[0] == 0);
    REQUIRE(std::abs(static_cast<int>(bytes[1]) - 128) <= 1);
    REQUIRE(bytes[2] == 255);
    REQUIRE(bytes[3] == 255);
    REQUIRE(std::abs(static_cast<int>(bytes[4]) - 64) <= 1);
    REQUIRE(std::abs(static_cast<int>(bytes[5]) - 191) <= 1);
    REQUIRE(bytes[6] == 255);
    REQUIRE(bytes[7] == 0);
}
