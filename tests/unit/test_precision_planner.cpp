// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Pipeline.hpp>
#include <cpipe/runtime/PrecisionPlanner.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

void cpipe_link_builtin_precision_convert();

namespace {

constexpr char kRgba16ProducerManifest[] = R"({
  "id":"com.cpipe.test.rgba16_producer",
  "version":"1.0.0",
  "ports":[
    {"name":"out","kind":"out","caps":{"channels":["rgba"],"precision":["f16"]}}
  ],
  "compute":{"device":"CPU","engine":"Host","out_pixel_bytes":8},
  "color":{"input_role":"any","output_role":"any"}
})";

constexpr char kRgba8ConsumerManifest[] = R"({
  "id":"com.cpipe.test.rgba8_consumer",
  "version":"1.0.0",
  "ports":[
    {"name":"in","kind":"in","caps":{"channels":["rgba"],"precision":["u8"]}}
  ],
  "compute":{"device":"CPU","engine":"Host","out_pixel_bytes":0},
  "color":{"input_role":"any","output_role":"any"}
})";

constexpr char kRaw16ProducerManifest[] = R"({
  "id":"com.cpipe.test.raw16_producer",
  "version":"1.0.0",
  "ports":[
    {"name":"out","kind":"out","caps":{"channels":["r"],"precision":["u16"]}}
  ],
  "compute":{"device":"CPU","engine":"Host","out_pixel_bytes":2},
  "color":{"input_role":"raw_camera","output_role":"raw_camera"}
})";

class NoopNode : public cpipe::sdk::Node {
public:
    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::MetadataBuilder*>) override {
        return {};
    }
};

class Rgba16ProducerNode final : public NoopNode {
public:
    static constexpr const char* ID = "com.cpipe.test.rgba16_producer";
    static constexpr const char* VERSION = "1.0.0";
};

class Rgba8ConsumerNode final : public NoopNode {
public:
    static constexpr const char* ID = "com.cpipe.test.rgba8_consumer";
    static constexpr const char* VERSION = "1.0.0";
};

class Raw16ProducerNode final : public NoopNode {
public:
    static constexpr const char* ID = "com.cpipe.test.raw16_producer";
    static constexpr const char* VERSION = "1.0.0";
};

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{CPIPE_SOURCE_DIR} / "tests" / "fixtures" / name;
}

}  // namespace

CPIPE_REGISTER_NODE(Rgba16ProducerNode, kRgba16ProducerManifest)
CPIPE_REGISTER_NODE(Rgba8ConsumerNode, kRgba8ConsumerManifest)
CPIPE_REGISTER_NODE(Raw16ProducerNode, kRaw16ProducerManifest)

TEST_CASE("PrecisionPlanner auto-inserts precision_convert for adjacent compatible mismatch") {
    cpipe_link_builtin_precision_convert();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    const auto* producer = registry.find("com.cpipe.test.rgba16_producer");
    const auto* consumer = registry.find("com.cpipe.test.rgba8_consumer");
    const auto* precision_convert = registry.find("com.cpipe.precision_convert");
    REQUIRE(producer != nullptr);
    REQUIRE(consumer != nullptr);
    REQUIRE(precision_convert != nullptr);

    const std::vector<cpipe::runtime::PrecisionGraphNode> nodes{
        {.id = "producer", .descriptor = producer, .params = nlohmann::json::object()},
        {.id = "consumer", .descriptor = consumer, .params = nlohmann::json::object()},
    };
    const std::vector<cpipe::runtime::PrecisionGraphEdge> edges{
        {.from = 0, .to = 1, .from_port = "out", .to_port = "in"},
    };

    cpipe::runtime::PrecisionPlan plan;
    std::string error;
    REQUIRE(cpipe::runtime::PrecisionPlanner::auto_insert(nodes, edges, precision_convert, &plan,
                                                          &error) == CPIPE_OK);

    REQUIRE(plan.nodes.size() == 3);
    REQUIRE(plan.edges.size() == 2);
    REQUIRE(plan.inserted_node_indices.size() == 1);
    const auto inserted = plan.inserted_node_indices.front();
    REQUIRE(plan.nodes[inserted].descriptor == precision_convert);
    REQUIRE(plan.nodes[inserted].params.at("target_format") == "R8G8B8A8_UNORM");
}

TEST_CASE("Pipeline load includes implicit precision_convert node") {
    cpipe_link_builtin_precision_convert();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("precision_auto_insert-v0.3.json"),
                                           registry, &pipeline, &error) == CPIPE_OK);
    REQUIRE(pipeline.node_count() == 3);
}

TEST_CASE("PrecisionPlanner rejects raw-to-rgba precision conversion without demosaic") {
    cpipe_link_builtin_precision_convert();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    const auto* producer = registry.find("com.cpipe.test.raw16_producer");
    const auto* consumer = registry.find("com.cpipe.test.rgba8_consumer");
    const auto* precision_convert = registry.find("com.cpipe.precision_convert");
    REQUIRE(producer != nullptr);
    REQUIRE(consumer != nullptr);
    REQUIRE(precision_convert != nullptr);

    const std::vector<cpipe::runtime::PrecisionGraphNode> nodes{
        {.id = "producer", .descriptor = producer, .params = nlohmann::json::object()},
        {.id = "consumer", .descriptor = consumer, .params = nlohmann::json::object()},
    };
    const std::vector<cpipe::runtime::PrecisionGraphEdge> edges{
        {.from = 0, .to = 1, .from_port = "out", .to_port = "in"},
    };

    cpipe::runtime::PrecisionPlan plan;
    std::string error;
    REQUIRE(cpipe::runtime::PrecisionPlanner::auto_insert(nodes, edges, precision_convert, &plan,
                                                          &error) == CPIPE_BAD_PRECISION);
    REQUIRE(error.find("precision") != std::string::npos);
}

TEST_CASE("Pipeline rejects user-authored precision_convert nodes") {
    cpipe_link_builtin_precision_convert();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    cpipe::runtime::Pipeline pipeline;
    std::string error;
    REQUIRE(cpipe::runtime::Pipeline::load(fixture_path("precision_convert_user-v0.3.json"),
                                           registry, &pipeline, &error) == CPIPE_BAD_INDEX);
    REQUIRE(error.find("reserved") != std::string::npos);
}
