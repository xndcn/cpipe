// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <vulkan/vulkan.h>

#include <cpipe/runtime/Sync.hpp>
#include <cstdint>
#include <memory>

namespace cpipe::runtime {

class VulkanDevicePlane;

class VulkanCommandBuffer final {
public:
    explicit VulkanCommandBuffer(std::shared_ptr<VulkanDevicePlane> plane);
    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&& other) noexcept;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&& other) noexcept;
    ~VulkanCommandBuffer();

    [[nodiscard]] VkCommandBuffer handle() const noexcept;

    void begin();
    void end();
    void bind_compute_pipeline(VkPipeline pipeline);
    void bind_descriptor_set(VkPipelineLayout layout, VkDescriptorSet set);
    void dispatch(std::uint32_t groups_x, std::uint32_t groups_y, std::uint32_t groups_z);
    void submit(VulkanTimelineSemaphore& signal_timeline, std::uint64_t signal_value);

private:
    void destroy() noexcept;

    std::shared_ptr<VulkanDevicePlane> plane_;
    VkCommandPool pool_{VK_NULL_HANDLE};
    VkCommandBuffer command_buffer_{VK_NULL_HANDLE};
};

}  // namespace cpipe::runtime
