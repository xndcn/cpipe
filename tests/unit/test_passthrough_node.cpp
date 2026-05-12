// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "HostSuites.hpp"
#include "RuntimeHandles.hpp"
#include "cpipe/core/CpuBuffer.hpp"
#include "cpipe/runtime/ComputeContext.hpp"
#include "cpipe/runtime/InferenceContext.hpp"
#include "cpipe/runtime/Registry.hpp"

using namespace cpipe::compute;

TEST_CASE("Passthrough node produces byte-identical output") {
    cpipe::runtime::Registry registry;
    const auto* desc = registry.find("com.cpipe.builtin.passthrough");
    REQUIRE(desc != nullptr);

    auto input =
        CpuBuffer::create(make_rgba8_layout(16, 16), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    auto output =
        CpuBuffer::create(make_rgba8_layout(16, 16), BufferUsage::CpuRead | BufferUsage::CpuWrite);
    REQUIRE(input.has_value());
    REQUIRE(output.has_value());

    auto* input_ptr = (*input)->lock_cpu(IBuffer::CpuAccess::Write);
    for (uint64_t i = 0; i < (*input)->size_bytes(); ++i) {
        static_cast<uint8_t*>(input_ptr)[i] = static_cast<uint8_t>((i * 3u) & 0xffu);
    }
    (*input)->unlock_cpu();

    cpipe::runtime::ComputeContext compute;
    cpipe::runtime::InferenceContext inference;
    cpipe_compute_t compute_handle{&compute};
    cpipe_inference_t inference_handle{&inference};
    cpipe_buffer_t in{*input};
    cpipe_buffer_t out{*output};
    const cpipe_buffer_t* inputs[] = {&in};
    cpipe_buffer_t* outputs[] = {&out};
    cpipe_process_ctx process{&compute_handle, &inference_handle, inputs, 1, outputs, 1};
    cpipe_node_t node{};
    cpipe_props_t props{nlohmann::json::object()};
    auto host = cpipe::runtime::make_host();

    REQUIRE(desc->main_entry(CPIPE_ACTION_PROCESS, &host, &node, &props, &process, nullptr) ==
            CPIPE_OK);
    REQUIRE(std::memcmp((*input)->data(), (*output)->data(),
                        static_cast<std::size_t>((*input)->size_bytes())) == 0);
}

TEST_CASE("Passthrough manifest validates against node schema") {
    cpipe::runtime::Registry registry;
    const auto* desc = registry.find("com.cpipe.builtin.passthrough");
    REQUIRE(desc != nullptr);

    std::ifstream schema_file("schemas/node-v0.1.json");
    std::ifstream manifest_file("src/cpipe/nodes/passthrough.json");
    REQUIRE(schema_file.good());
    REQUIRE(manifest_file.good());

    const auto schema = nlohmann::json::parse(schema_file);
    const auto manifest = nlohmann::json::parse(manifest_file);
    const auto embedded_manifest = nlohmann::json::parse(desc->manifest_json);

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema);
    REQUIRE_NOTHROW(validator.validate(manifest));
    REQUIRE(embedded_manifest == manifest);
}
