// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "cpipe/runtime/Registry.hpp"
#include "cpipe/sdk/cpipe_node.h"

extern "C" int cpipe_c_abi_compile_probe(void);
extern "C" void cpipe_test_registry_anchor(void);
extern "C" auto cpipe_test_registry_node_id() -> const char*;

TEST_CASE("cpipe_node.h compiles as C99") {
    REQUIRE(cpipe_c_abi_compile_probe() == CPIPE_UNSUPPORTED);
}

TEST_CASE("CPIPE_REGISTER_NODE descriptors are discoverable") {
    cpipe_test_registry_anchor();

    cpipe::runtime::Registry registry;
    REQUIRE(registry.load_builtin_nodes() >= 1U);

    const auto* desc = registry.find(cpipe_test_registry_node_id());
    REQUIRE(desc != nullptr);
    CHECK(desc->abi_major == CPIPE_ABI_MAJOR);
    CHECK(desc->abi_minor == CPIPE_ABI_MINOR);
    CHECK(desc->node_version == std::string_view{"1.0.0"});
    REQUIRE(desc->main_entry != nullptr);

    auto host = cpipe::runtime::make_host();
    CHECK(desc->main_entry(CPIPE_ACTION_DESCRIBE, &host, nullptr, nullptr, nullptr, nullptr) ==
          CPIPE_REPLY_DEFAULT);
}

TEST_CASE("inference suite is present but unsupported in T3") {
    auto host = cpipe::runtime::make_host();
    const auto* suite =
        static_cast<const cpipe_inference_suite_v1*>(host.get_suite(&host, "inference", 1));

    REQUIRE(suite != nullptr);
    REQUIRE(suite->submit_inference(nullptr, "model", nullptr, 0, nullptr, 0) == CPIPE_UNSUPPORTED);
    CHECK(host.get_suite(&host, "inference", 2) == nullptr);
}
