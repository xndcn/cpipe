// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/Status.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cstdlib>
#include <string_view>

#include "demosaic_bilinear_fixture.hpp"

void cpipe_link_builtin_demosaic_rcd();

TEST_CASE("demosaic.rcd Vulkan AOT variant is available when a Vulkan device is enabled") {
    const auto* enabled = std::getenv("CPIPE_VULKAN_AVAILABLE");
    if (enabled == nullptr || std::string_view{enabled} != "ON") {
        SUCCEED("CPIPE_VULKAN_AVAILABLE is not ON; skipping Vulkan RCD demosaic check");
        return;
    }

    const auto created = cpipe::runtime::VulkanDevicePlane::create();
    REQUIRE(created.status == cpipe::compute::StatusCode::Ok);

    const auto raw = cpipe::tests::synthetic_bayer();
    for (const auto& pattern : cpipe::tests::cfa_patterns()) {
        const auto run = cpipe::tests::run_demosaic_node(
            "com.cpipe.demosaic.rcd", &cpipe_link_builtin_demosaic_rcd, pattern, raw);
        REQUIRE(run.pixels.size() == raw.size() * 4U);
    }
}
