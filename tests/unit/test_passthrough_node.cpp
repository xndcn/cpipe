// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

extern "C" int passthrough_copy(halide_buffer_t* input, halide_buffer_t* output);
void cpipe_link_builtin_passthrough();

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba_layout(std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

}  // namespace

TEST_CASE("Passthrough node dispatches Halide AOT copy") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.builtin.passthrough");
    REQUIRE(desc != nullptr);

    std::ifstream manifest_file{std::string{CPIPE_SOURCE_DIR} +
                                "/src/cpipe/nodes/passthrough.json"};
    REQUIRE(manifest_file.good());
    std::ostringstream manifest_stream;
    manifest_stream << manifest_file.rdbuf();
    REQUIRE(std::string{desc->manifest_json} == manifest_stream.str());

    const auto manifest = nlohmann::json::parse(desc->manifest_json);
    std::ifstream schema_file{std::string{CPIPE_SOURCE_DIR} + "/schemas/node-v0.1.json"};
    REQUIRE(schema_file.good());
    const auto schema = nlohmann::json::parse(schema_file);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    REQUIRE_NOTHROW(validator.validate(manifest));

    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(16, 16), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(16, 16), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* input_bytes = static_cast<std::byte*>(input->lock_cpu(IBuffer::CpuAccess::Write));
    for (std::uint64_t i = 0; i < input->size_bytes(); ++i) {
        input_bytes[i] = static_cast<std::byte>((i * 17U) & 0xffU);
    }
    input->unlock_cpu();
    input->flush_cpu_writes();

    cpipe::runtime::ComputeContext compute;
    compute.register_halide_filter("passthrough_copy", &passthrough_copy);
    cpipe::runtime::HostContext host_context;

    void* instance = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                             &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    const cpipe_buffer_t* inputs[] = {input_handle.get()};
    cpipe_buffer_t* outputs[] = {output_handle.get()};
    cpipe_process_ctx process{
        .compute = reinterpret_cast<cpipe_compute_t*>(&compute),
        .inference = nullptr,
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
    };

    REQUIRE(desc->main_entry(CPIPE_ACTION_PROCESS, host_context.host(),
                             reinterpret_cast<cpipe_node_t*>(instance), nullptr, &process,
                             nullptr) == CPIPE_OK);
    REQUIRE(desc->main_entry(CPIPE_ACTION_DESTROY, host_context.host(), nullptr, nullptr, instance,
                             nullptr) == CPIPE_OK);

    const auto* in = static_cast<const std::byte*>(input->lock_cpu(IBuffer::CpuAccess::Read));
    const auto* out = static_cast<const std::byte*>(output->lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::memcmp(in, out, static_cast<std::size_t>(input->size_bytes())) == 0);
    output->unlock_cpu();
    input->unlock_cpu();
}
