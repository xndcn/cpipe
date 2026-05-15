// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/core/Status.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cstdlib>
#include <string_view>

#include "demosaic_bilinear_fixture.hpp"

TEST_CASE("demosaic.bilinear Vulkan AOT variant matches CPU fixture") {
    const auto* enabled = std::getenv("CPIPE_VULKAN_AVAILABLE");
    if (enabled == nullptr || std::string_view{enabled} != "ON") {
        SUCCEED("CPIPE_VULKAN_AVAILABLE is not ON; skipping Vulkan demosaic check");
        return;
    }

    const auto created = cpipe::runtime::VulkanDevicePlane::create();
    REQUIRE(created.status == cpipe::compute::StatusCode::Ok);
    cpipe::tests::assert_demosaic_node();
}
