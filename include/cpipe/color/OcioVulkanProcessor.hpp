// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/sdk/cpipe_node.h>

#include <cpipe/core/Status.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace cpipe::runtime {
class VulkanDevicePlane;
class VulkanImage;
}  // namespace cpipe::runtime

namespace cpipe::color {

class OcioVulkanProcessor final {
public:
    OcioVulkanProcessor(std::filesystem::path config_path, std::string src_cs, std::string dst_cs);
    OcioVulkanProcessor(const OcioVulkanProcessor&) = delete;
    OcioVulkanProcessor& operator=(const OcioVulkanProcessor&) = delete;
    OcioVulkanProcessor(OcioVulkanProcessor&&) = delete;
    OcioVulkanProcessor& operator=(OcioVulkanProcessor&&) = delete;
    ~OcioVulkanProcessor();

    [[nodiscard]] cpipe_status_t compute_pass(
        const std::shared_ptr<runtime::VulkanDevicePlane>& plane, runtime::VulkanImage& input,
        runtime::VulkanImage& output);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cpipe::color
