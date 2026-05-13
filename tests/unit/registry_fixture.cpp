// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>

namespace {

class RegistryTestNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.registry";
    static constexpr const char* VERSION = "1.0.0";

    cpipe::sdk::Result<void> process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                                     const cpipe::sdk::ParamView&,
                                     std::span<const cpipe::sdk::Buffer*>,
                                     std::span<cpipe::sdk::Buffer*>) override {
        return {};
    }
};

constexpr char kRegistryTestManifest[] = R"({"id":"com.cpipe.test.registry","version":"1.0.0"})";

}  // namespace

CPIPE_REGISTER_NODE(RegistryTestNode, kRegistryTestManifest)

void force_registry_fixture() {}
