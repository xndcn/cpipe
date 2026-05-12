// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/sdk/cpipe_node.h>

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>
#include <string_view>

namespace {

class RegistryTestNode final : public cpipe::sdk::Node {
public:
    static constexpr const char* ID = "com.cpipe.test.registry";
    static constexpr const char* VERSION = "1.0.0";

    auto process(cpipe::sdk::ComputeContext&, cpipe::sdk::InferenceContext*,
                 const cpipe::sdk::ParamView&, std::span<const cpipe::sdk::Buffer*>,
                 std::span<cpipe::sdk::Buffer*>) -> cpipe::sdk::Result<void> override {
        return {};
    }
};

constexpr char kRegistryTestManifest[] =
    R"json({"id":"com.cpipe.test.registry","version":"1.0.0"})json";

CPIPE_REGISTER_NODE(RegistryTestNode, kRegistryTestManifest)

}  // namespace

TEST_CASE("registry loads descriptors from the cpipe_registry linker section") {
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    const auto* descriptor = registry.find(RegistryTestNode::ID);
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->abi_major == CPIPE_ABI_MAJOR);
    CHECK(descriptor->abi_minor == CPIPE_ABI_MINOR);
    CHECK(std::string_view{descriptor->node_id} == RegistryTestNode::ID);
    CHECK(std::string_view{descriptor->node_version} == RegistryTestNode::VERSION);
    CHECK(std::string_view{descriptor->manifest_json} == kRegistryTestManifest);
    REQUIRE(descriptor->main_entry != nullptr);
}

TEST_CASE("SDK dispatch creates processes and destroys a node instance") {
    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();
    const auto* descriptor = registry.find(RegistryTestNode::ID);
    REQUIRE(descriptor != nullptr);

    auto host = cpipe::runtime::make_default_host();
    void* node_state = nullptr;
    CHECK(descriptor->main_entry(CPIPE_ACTION_CREATE, &host, nullptr, nullptr, nullptr,
                                 &node_state) == CPIPE_OK);
    REQUIRE(node_state != nullptr);

    cpipe_process_ctx process_ctx{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* node_handle = reinterpret_cast<cpipe_node_t*>(node_state);
    CHECK(descriptor->main_entry(CPIPE_ACTION_PROCESS, &host, node_handle, nullptr, &process_ctx,
                                 nullptr) == CPIPE_OK);
    CHECK(descriptor->main_entry("unknown-action", &host, node_handle, nullptr, nullptr, nullptr) ==
          CPIPE_REPLY_DEFAULT);
    CHECK(descriptor->main_entry(CPIPE_ACTION_DESTROY, &host, nullptr, nullptr, node_state,
                                 nullptr) == CPIPE_OK);
}

TEST_CASE("default host exposes unsupported inference suite v1") {
    auto host = cpipe::runtime::make_default_host();
    const auto* suite =
        static_cast<const cpipe_inference_suite_v1*>(host.get_suite(&host, "inference", 1));

    REQUIRE(suite != nullptr);
    REQUIRE(suite->submit_inference != nullptr);
    CHECK(suite->submit_inference(nullptr, "model", nullptr, 0, nullptr, 0) == CPIPE_UNSUPPORTED);
    CHECK(host.get_suite(&host, "inference", 2) == nullptr);
}
