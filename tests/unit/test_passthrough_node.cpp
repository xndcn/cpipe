// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <HalideRuntime.h>
#include <cpipe/sdk/cpipe_node.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/ComputeContext.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "passthrough_copy.h"

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::PixelFormat;

constexpr char kPassthroughId[] = "com.cpipe.builtin.passthrough";
constexpr char kPassthroughVersion[] = "1.0.0";

auto make_rgba_layout(std::uint32_t width, std::uint32_t height) -> BufferLayout {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

auto fill_gradient(CpuBuffer& buffer) -> std::vector<std::byte> {
    auto expected = std::vector<std::byte>(buffer.size_bytes());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::byte>((index * 17U + 3U) & 0xFFU);
    }

    auto* ptr = static_cast<std::byte*>(buffer.lock_cpu(cpipe::compute::IBuffer::CpuAccess::Write));
    REQUIRE(ptr != nullptr);
    std::copy(expected.begin(), expected.end(), ptr);
    buffer.unlock_cpu();
    return expected;
}

auto read_bytes(CpuBuffer& buffer) -> std::vector<std::byte> {
    auto actual = std::vector<std::byte>(buffer.size_bytes());
    auto* ptr = static_cast<std::byte*>(buffer.lock_cpu(cpipe::compute::IBuffer::CpuAccess::Read));
    REQUIRE(ptr != nullptr);
    std::copy_n(ptr, actual.size(), actual.begin());
    buffer.unlock_cpu();
    return actual;
}

auto read_manifest_source() -> std::string {
    auto input = std::ifstream{CPIPE_PASSTHROUGH_MANIFEST_PATH, std::ios::binary};
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

auto find_passthrough_descriptor() -> const cpipe_plugin_desc_t* {
    auto registry = cpipe::runtime::Registry{};
    registry.load_builtin_nodes();
    return registry.find(kPassthroughId);
}

auto call_passthrough_copy(std::span<halide_buffer_t* const> inputs,
                           std::span<halide_buffer_t* const> outputs) -> int {
    if (inputs.size() != 1U || outputs.size() != 1U || inputs[0] == nullptr ||
        outputs[0] == nullptr) {
        return CPIPE_INTERNAL_ERROR;
    }
    return passthrough_copy(inputs[0], outputs[0]);
}

}  // namespace

TEST_CASE("builtin passthrough descriptor embeds the source manifest") {
    const auto* descriptor = find_passthrough_descriptor();
    REQUIRE(descriptor != nullptr);

    REQUIRE(descriptor->manifest_json != nullptr);
    CHECK(std::string_view{descriptor->node_id} == kPassthroughId);
    CHECK(std::string_view{descriptor->node_version} == kPassthroughVersion);
    CHECK(std::string_view{descriptor->manifest_json} == read_manifest_source());

    const auto manifest = nlohmann::json::parse(descriptor->manifest_json);
    CHECK(manifest.at("id") == kPassthroughId);
    CHECK(manifest.at("version") == kPassthroughVersion);
    CHECK(manifest.at("compute").at("engine") == "Halide");
    CHECK(manifest.at("compute").at("halide_aot").at(0) == "passthrough_copy");
}

TEST_CASE("builtin passthrough process copies RGBA8 Image2D buffers through Halide AOT") {
    const auto* descriptor = find_passthrough_descriptor();
    REQUIRE(descriptor != nullptr);

    auto host = cpipe::runtime::make_default_host();
    void* node_state = nullptr;
    CHECK(descriptor->main_entry(CPIPE_ACTION_CREATE, &host, nullptr, nullptr, nullptr,
                                 &node_state) == CPIPE_OK);
    REQUIRE(node_state != nullptr);

    auto input =
        CpuBuffer::create(make_rgba_layout(16, 16), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output =
        CpuBuffer::create(make_rgba_layout(16, 16), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    const auto expected = fill_gradient(*input);

    auto compute = cpipe::runtime::ComputeContext{};
    compute.register_halide("passthrough_copy", &call_passthrough_copy);

    const cpipe_buffer_t* inputs[] = {cpipe::runtime::as_cpipe_buffer(*input)};
    cpipe_buffer_t* outputs[] = {cpipe::runtime::as_cpipe_buffer(*output)};
    auto process_ctx = cpipe_process_ctx{compute.handle(), nullptr, inputs, 1, outputs, 1};

    auto* node_handle = reinterpret_cast<cpipe_node_t*>(node_state);  // NOLINT
    CHECK(descriptor->main_entry(CPIPE_ACTION_PROCESS, &host, node_handle, nullptr, &process_ctx,
                                 nullptr) == CPIPE_OK);
    CHECK(read_bytes(*output) == expected);
    CHECK(descriptor->main_entry(CPIPE_ACTION_DESTROY, &host, nullptr, nullptr, node_state,
                                 nullptr) == CPIPE_OK);
}
