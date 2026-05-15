// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cpipe/runtime/HalideFilterRegistry.hpp>
#include <cpipe/runtime/HostContext.hpp>
#include <cpipe/runtime/Registry.hpp>

extern "C" int cpipe_abi_minor_from_c(void);
void cpipe_link_all_builtin_nodes();
void force_registry_fixture();

TEST_CASE("cpipe_node.h compiles as C99") {
    REQUIRE(cpipe_abi_minor_from_c() == CPIPE_ABI_MINOR);
}

TEST_CASE("Registry walks cpipe_registry section") {
    force_registry_fixture();

    cpipe::runtime::Registry registry;
    registry.load_builtin_nodes();

    const auto* desc = registry.find("com.cpipe.test.registry");
    REQUIRE(desc != nullptr);
    REQUIRE(desc->abi_major == CPIPE_ABI_MAJOR);
    REQUIRE(desc->abi_minor == CPIPE_ABI_MINOR);
    REQUIRE(std::string_view{desc->manifest_json}.find("com.cpipe.test.registry") !=
            std::string_view::npos);
}

TEST_CASE("Halide filter registry walks built-in AOT dispatch entries") {
    cpipe_link_all_builtin_nodes();

    const auto ids = cpipe::runtime::HalideFilterRegistry::instance().halide_filter_ids();
    REQUIRE(std::ranges::find(ids, "passthrough_copy") != ids.end());
    REQUIRE(std::ranges::find(ids, "demosaic_bilinear") != ids.end());
}

TEST_CASE("Inference suite reports unsupported in P0") {
    cpipe::runtime::HostContext host_context;
    auto* host = host_context.host();
    const auto* suite =
        static_cast<const cpipe_inference_suite_v1*>(host->get_suite(host, "inference", 1));

    REQUIRE(suite != nullptr);
    REQUIRE(suite->submit_inference(nullptr, "model", nullptr, 0, nullptr, 0) == CPIPE_UNSUPPORTED);
}
