// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/nodes/Passthrough.hpp>
#include <cpipe/runtime/AbiBridge.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <vector>

extern const char PASSTHROUGH_MANIFEST_JSON[];

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba_layout() {
    return BufferLayout{
        .kind = BufferKind::Image2D,
        .format = PixelFormat::R8G8B8A8_UNORM,
        .ndim = 2,
        .dims = {16, 16},
        .stride = {},
    };
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    REQUIRE(file.is_open());
    return nlohmann::json::parse(file);
}

}  // namespace

TEST_CASE("test_passthrough_node: manifest validates against node schema") {
    const auto root = std::filesystem::path(CPIPE_SOURCE_DIR);
    const auto schema = read_json(root / "schemas/node-v0.1.json");
    const auto manifest = read_json(root / "src/cpipe/nodes/passthrough.json");

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    CHECK_NOTHROW(validator.validate(manifest));
    CHECK(nlohmann::json::parse(PASSTHROUGH_MANIFEST_JSON) == manifest);
    CHECK(manifest.at("id") == "com.cpipe.builtin.passthrough");
}

TEST_CASE("test_passthrough_node: descriptor is registered") {
    const auto registry = cpipe::runtime::Registry::load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.builtin.passthrough");

    REQUIRE(desc != nullptr);
    CHECK(desc->abi_major == CPIPE_ABI_MAJOR);
    CHECK(desc->manifest_json != nullptr);
}

TEST_CASE("test_passthrough_node: process copies RGBA8 bytes through compute suite") {
    const auto* desc =
        cpipe::runtime::Registry::load_builtin_nodes().find("com.cpipe.builtin.passthrough");
    REQUIRE(desc != nullptr);

    CpuBuffer input(rgba_layout(),
                    BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    CpuBuffer output(rgba_layout(),
                     BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    auto* input_bytes = static_cast<std::uint8_t*>(input.lock_cpu(IBuffer::CpuAccess::ReadWrite));
    REQUIRE(input_bytes != nullptr);
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(input.size_bytes()));
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<std::uint8_t>((i * 13U) & 0xffU);
        input_bytes[i] = expected[i];
    }
    input.unlock_cpu();

    cpipe::runtime::ComputeContext compute;
    cpipe::nodes::register_passthrough_halide(compute);
    cpipe::runtime::InferenceContext inference;
    cpipe::runtime::HostContext host_context;
    cpipe::runtime::ComputeHandle compute_handle(compute);
    cpipe::runtime::InferenceHandle inference_handle(inference);
    cpipe::runtime::BufferHandle input_handle(input);
    cpipe::runtime::BufferHandle output_handle(output);

    void* state = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.c_host(), nullptr, nullptr, nullptr,
                             &state) == CPIPE_OK);
    REQUIRE(state != nullptr);

    const cpipe_buffer_t* inputs[] = {input_handle.c_buffer()};
    cpipe_buffer_t* outputs[] = {output_handle.c_buffer()};
    cpipe_process_ctx process_ctx{
        .compute = compute_handle.c_compute(),
        .inference = inference_handle.c_inference(),
        .inputs = inputs,
        .n_in = 1,
        .outputs = outputs,
        .n_out = 1,
    };

    CHECK(desc->main_entry(CPIPE_ACTION_PROCESS, host_context.c_host(),
                           reinterpret_cast<cpipe_node_t*>(state), nullptr, &process_ctx,
                           nullptr) == CPIPE_OK);
    CHECK(desc->main_entry(CPIPE_ACTION_DESTROY, host_context.c_host(), nullptr, nullptr, state,
                           nullptr) == CPIPE_OK);

    const auto* output_bytes =
        static_cast<const std::uint8_t*>(output.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(output_bytes != nullptr);
    CHECK(std::equal(output_bytes, output_bytes + expected.size(), expected.begin()));
    output.unlock_cpu();
}
