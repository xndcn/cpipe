// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <filesystem>
#include <string>

void cpipe_link_builtin_passthrough();

namespace {

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "tests" / "fixtures" / name;
}

class PrecisionProducerNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.precision_producer";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

class PrecisionConsumerNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.precision_consumer";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

class ParamEnumNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.param_enum";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

constexpr char kPrecisionProducerManifest[] = R"({
  "id":"com.cpipe.test.precision_producer",
  "version":"1.0.0",
  "ports":[
    {"name":"out","kind":"out","caps":{"precision":["fp16"]}}
  ],
  "compute":{"device":"CPU","engine":"Halide","out_pixel_bytes":8},
  "color":{"input_role":"any","output_role":"any"}
})";

constexpr char kPrecisionConsumerManifest[] = R"({
  "id":"com.cpipe.test.precision_consumer",
  "version":"1.0.0",
  "ports":[
    {"name":"in","kind":"in","caps":{"precision":["fp32"]}}
  ],
  "compute":{"device":"CPU","engine":"Halide","out_pixel_bytes":8},
  "color":{"input_role":"any","output_role":"any"}
})";

constexpr char kParamEnumManifest[] = R"({
  "id":"com.cpipe.test.param_enum",
  "version":"1.0.0",
  "ports":[],
  "params":[
    {
      "name":"target",
      "type":"enum",
      "enum_values":["sRGB","BT2020-PQ"],
      "default":"sRGB"
    }
  ],
  "compute":{"device":"CPU","engine":"Host","out_pixel_bytes":0},
  "color":{"input_role":"any","output_role":"any"}
})";

}  // namespace

CPIPE_REGISTER_NODE(PrecisionProducerNode, kPrecisionProducerManifest)
CPIPE_REGISTER_NODE(PrecisionConsumerNode, kPrecisionConsumerManifest)
CPIPE_REGISTER_NODE(ParamEnumNode, kParamEnumManifest)

TEST_CASE("Pipeline loads valid passthrough graph") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough-v0.4.json"), registry,
                                           &pipeline, &error) == CPIPE_OK);
    REQUIRE(pipeline.node_count() == 1);
    REQUIRE(pipeline.layout().size_bytes() == 64ULL * 64ULL * 4ULL);
    REQUIRE(pipeline.memory_peak_bytes() >= pipeline.layout().size_bytes());
}

TEST_CASE("Pipeline rejects pre-v0.4 schemas after migration") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough.json"), registry, &pipeline,
                                           &error) == CPIPE_FAILED);
    REQUIRE(error.find("schema version mismatch") != std::string::npos);

    error.clear();
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough-v0.2.json"), registry,
                                           &pipeline, &error) == CPIPE_FAILED);
    REQUIRE(error.find("schema version mismatch") != std::string::npos);

    error.clear();
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough-v0.3.json"), registry,
                                           &pipeline, &error) == CPIPE_FAILED);
    REQUIRE(error.find("tools/migrate/v03_to_v04.py") != std::string::npos);
}

TEST_CASE("Pipeline enforces source binding for v0.4 inputs") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough-v0.4.json"), registry,
                                           &pipeline, &error) == CPIPE_OK);
    REQUIRE(pipeline.run(&error) == CPIPE_FAILED);
    REQUIRE(error.find("source") != std::string::npos);
    REQUIRE(pipeline.set_source("raw", "com.cpipe.builtin.passthrough", {}) == CPIPE_OK);
}

TEST_CASE("Pipeline rejects unknown node type") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("invalid_pipeline-v0.4.json"), registry,
                                           &pipeline, &error) == CPIPE_BAD_INDEX);
    REQUIRE(error.find("unknown node type") != std::string::npos);
}

TEST_CASE("Pipeline rejects dangling edge") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("dangling_pipeline-v0.4.json"), registry,
                                           &pipeline, &error) == CPIPE_BAD_INDEX);
    REQUIRE(error.find("dangling edge") != std::string::npos);
}

TEST_CASE("Pipeline memory cap rejects plans above device budget") {
    cpipe_link_builtin_passthrough();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    pipeline.set_device_memory_cap(1);
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("passthrough-v0.4.json"), registry,
                                           &pipeline, &error) == CPIPE_OOM);
    REQUIRE(error.find("memory") != std::string::npos);
}

TEST_CASE("Pipeline precision planner rejects disjoint edge precision") {
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("precision_mismatch-v0.4.json"), registry,
                                           &pipeline, &error) == CPIPE_BAD_PRECISION);
    REQUIRE(error.find("precision") != std::string::npos);
}

TEST_CASE("Pipeline rejects enum params outside the node manifest") {
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("params_invalid_target-v0.4.json"),
                                           registry, &pipeline, &error) == CPIPE_BAD_INDEX);
    REQUIRE(error.find("target") != std::string::npos);
}
