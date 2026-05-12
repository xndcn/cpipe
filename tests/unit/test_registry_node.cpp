// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <span>

#include "cpipe/sdk/registry.hpp"
#include "cpipe/sdk/sdk.hpp"

namespace {

class RegistryFixtureNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.registry";
    static constexpr const char* VERSION = "1.0.0";

    auto process(cpipe::sdk::ComputeContext& compute, cpipe::sdk::InferenceContext* inference,
                 const cpipe::sdk::ParamView& params, std::span<const cpipe::sdk::Buffer*> inputs,
                 std::span<cpipe::sdk::Buffer*> outputs) -> cpipe::sdk::Result<void> override {
        (void)compute;
        (void)inference;
        (void)params;
        (void)inputs;
        (void)outputs;
        return {};
    }
};

constexpr const char* kRegistryFixtureManifest =
    R"({"id":"com.cpipe.test.registry","version":"1.0.0"})";

}  // namespace

CPIPE_REGISTER_NODE(RegistryFixtureNode, kRegistryFixtureManifest)

extern "C" void cpipe_test_registry_anchor() {}

extern "C" auto cpipe_test_registry_node_id() -> const char* {
    return RegistryFixtureNode::ID;
}
