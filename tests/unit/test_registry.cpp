// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/Registry.hpp>
#include <cpipe/sdk/registry.hpp>
#include <cpipe/sdk/sdk.hpp>
#include <span>

namespace {

constexpr const char kRegistryManifest[] = R"json({"id":"com.cpipe.test.registry"})json";

class RegisteredTestNode final : public cpipe::sdk::Node {
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

CPIPE_REGISTER_NODE(RegisteredTestNode, kRegistryManifest)

}  // namespace

TEST_CASE("test_registry: walks cpipe_registry section") {
    const auto registry = cpipe::runtime::Registry::load_builtin_nodes();
    const auto* desc = registry.find("com.cpipe.test.registry");

    REQUIRE(desc != nullptr);
    CHECK(desc->abi_major == CPIPE_ABI_MAJOR);
    CHECK(desc->abi_minor == CPIPE_ABI_MINOR);
    CHECK(std::string_view(desc->manifest_json) == kRegistryManifest);
}

TEST_CASE("test_registry: HostContext exposes unsupported inference suite") {
    cpipe::runtime::HostContext context;
    auto* host = context.c_host();
    const auto* suite =
        static_cast<const cpipe_inference_suite_v1*>(host->get_suite(host, "inference", 1));

    REQUIRE(suite != nullptr);
    CHECK(suite->submit_inference(nullptr, "model", nullptr, 0, nullptr, 0) == CPIPE_UNSUPPORTED);
    CHECK(host->get_suite(host, "inference", 2) == nullptr);
}
