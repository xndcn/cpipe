// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "HostSuites.hpp"
#include "RuntimeHandles.hpp"
#include "cpipe/runtime/InferenceContext.hpp"
#include "cpipe/runtime/Registry.hpp"
#include "cpipe/sdk/section.hpp"

extern "C" CPIPE_REGISTRY_SECTION const cpipe_plugin_desc_t cpipe_bad_abi_test_node = {
    999, 0, "com.cpipe.test.badabi", "0.1.0", "{}", nullptr};

TEST_CASE("Registry finds built-in passthrough descriptor and skips bad ABI") {
    const auto descriptors = cpipe::runtime::Registry::load_builtin_nodes();
    const auto has_passthrough =
        std::any_of(descriptors.begin(), descriptors.end(), [](const cpipe_plugin_desc_t* desc) {
            return std::string_view(desc->node_id) == "com.cpipe.builtin.passthrough";
        });
    const auto has_bad_abi =
        std::any_of(descriptors.begin(), descriptors.end(), [](const cpipe_plugin_desc_t* desc) {
            return std::string_view(desc->node_id) == "com.cpipe.test.badabi";
        });
    REQUIRE(has_passthrough);
    REQUIRE_FALSE(has_bad_abi);
}

TEST_CASE("Host inference suite returns unsupported in P0") {
    auto host = cpipe::runtime::make_host();
    const auto* suite =
        static_cast<const cpipe_inference_suite_v1*>(host.get_suite(&host, "inference", 1));
    REQUIRE(suite != nullptr);

    cpipe::runtime::InferenceContext inference;
    cpipe_inference_t handle{&inference};
    REQUIRE(suite->submit_inference(&handle, "model", nullptr, 0, nullptr, 0) == CPIPE_UNSUPPORTED);
}
