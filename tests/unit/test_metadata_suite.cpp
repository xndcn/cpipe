// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferMetadata.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/CpuBuffer.hpp>
#include <cpipe/runtime/BufferHandle.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/MetadataHandle.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::CpuBuffer;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

BufferLayout rgba_layout() {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R8G8B8A8_UNORM;
    layout.ndim = 2;
    layout.dims[0] = 4;
    layout.dims[1] = 4;
    return layout;
}

class MetadataEchoNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.metadata_echo";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(
        cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*, const cpipe::sdk::ParamView&,
        std::span<const cpipe::sdk::Buffer*> inputs, std::span<cpipe::sdk::Buffer*> outputs,
        std::span<cpipe::sdk::MetadataBuilder*> out_metadata) override {
        (void)outputs;
        if (inputs.empty() || out_metadata.empty()) {
            return tl::unexpected(
                cpipe::sdk::Error{CPIPE_BAD_INDEX, "metadata echo missing buffers"});
        }

        const auto* metadata = inputs[0]->metadata();
        if (metadata == nullptr || metadata->cs_role() != "raw_camera") {
            return tl::unexpected(
                cpipe::sdk::Error{CPIPE_NEED_METADATA, "input metadata role mismatch"});
        }
        return out_metadata[0]->add_applied_step("test");
    }
};

class MetadataProducerNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.metadata_producer";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

class MetadataConsumerNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.metadata_consumer";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

constexpr char kMetadataEchoManifest[] = R"({
  "id":"com.cpipe.test.metadata_echo",
  "version":"1.0.0",
  "ports":[],
  "compute":{"device":"CPU","engine":"Halide"},
  "color":{"input_role":"any","output_role":"any"}
})";

constexpr char kMetadataProducerManifest[] = R"({
  "id":"com.cpipe.test.metadata_producer",
  "version":"1.0.0",
  "ports":[
    {"name":"out","kind":"out","metadata":{"sets_steps_applied":["com.cpipe.test.produced"]}}
  ],
  "compute":{"device":"CPU","engine":"Halide"},
  "color":{"input_role":"any","output_role":"any"}
})";

constexpr char kMetadataConsumerManifest[] = R"({
  "id":"com.cpipe.test.metadata_consumer",
  "version":"1.0.0",
  "ports":[
    {"name":"in","kind":"in","metadata":{"requires_steps_applied":["com.cpipe.test.required"]}}
  ],
  "compute":{"device":"CPU","engine":"Halide"},
  "color":{"input_role":"any","output_role":"any"}
})";

std::filesystem::path write_bad_metadata_pipeline() {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_bad_metadata_steps.json";
    std::ofstream out{path};
    out << R"({
  "$schema":"https://schemas.cpipe.dev/pipeline/v0.2.json",
  "version":"0.2",
  "id":"bad-metadata-steps",
  "inputs":[{"port":"raw","kind":"Image2D","format":"R8G8B8A8_UNORM","width":4,"height":4}],
  "nodes":[
    {"id":"producer","type":"com.cpipe.test.metadata_producer","params":{}},
    {"id":"consumer","type":"com.cpipe.test.metadata_consumer","params":{}}
  ],
  "edges":[{"from":"producer.out","to":"consumer.in"}]
})";
    return path;
}

}  // namespace

CPIPE_REGISTER_NODE(MetadataEchoNode, kMetadataEchoManifest)
CPIPE_REGISTER_NODE(MetadataProducerNode, kMetadataProducerManifest)
CPIPE_REGISTER_NODE(MetadataConsumerNode, kMetadataConsumerManifest)

TEST_CASE("Host exposes metadata ABI suites") {
    cpipe::runtime::HostContext host_context;
    auto* host = host_context.host();

    REQUIRE(host->abi_minor == 2);
    REQUIRE(host->get_suite(host, "metadata", 1) != nullptr);
    REQUIRE(host->get_suite(host, "metadata_builder", 1) != nullptr);
}

TEST_CASE("SDK metadata wrappers read input metadata and freeze output metadata") {
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.test.metadata_echo");
    REQUIRE(desc != nullptr);

    auto input_metadata = std::make_shared<cpipe::compute::BufferMetadata>();
    input_metadata->cs_role = "raw_camera";
    auto input = std::make_shared<CpuBuffer>(
        rgba_layout(), BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite);
    input->set_metadata(input_metadata);
    auto output = std::make_shared<CpuBuffer>(
        rgba_layout(), BufferUsage::Output | BufferUsage::CpuRead | BufferUsage::CpuWrite);

    cpipe::runtime::HostContext host_context;
    void* instance = nullptr;
    REQUIRE(desc->main_entry(CPIPE_ACTION_CREATE, host_context.host(), nullptr, nullptr, nullptr,
                             &instance) == CPIPE_OK);

    auto input_handle = cpipe::runtime::make_buffer_handle(input);
    auto output_handle = cpipe::runtime::make_buffer_handle(output);
    auto builder = cpipe::runtime::make_metadata_builder_handle(input->metadata());

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

    const auto frozen = cpipe::runtime::freeze_metadata_builder(builder.get());
    output->set_metadata(frozen);
    REQUIRE(output->metadata()->cs_role == "raw_camera");
    REQUIRE(output->metadata()->applied_steps == std::vector<std::string>{"test"});
}

TEST_CASE("Pipeline load rejects unsatisfied manifest metadata steps") {
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    REQUIRE(registry.find("com.cpipe.test.metadata_producer") != nullptr);
    REQUIRE(registry.find("com.cpipe.test.metadata_consumer") != nullptr);

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(write_bad_metadata_pipeline(), registry, &pipeline,
                                           &error) == CPIPE_NEED_METADATA);
    REQUIRE(error.find("com.cpipe.test.required") != std::string::npos);
}
