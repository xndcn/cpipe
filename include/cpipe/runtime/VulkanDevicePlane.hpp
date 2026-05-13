// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <cpipe/core/Status.hpp>
#include <cpipe/runtime/DevicePlane.hpp>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace cpipe::runtime {

class VulkanDevicePlane;

struct VulkanDevicePlaneCreateResult {
    cpipe::compute::StatusCode status{cpipe::compute::StatusCode::Failed};
    std::shared_ptr<VulkanDevicePlane> plane;
    std::string message;
};

class VulkanDevicePlane final : public IDevicePlane,
                                public std::enable_shared_from_this<VulkanDevicePlane> {
public:
    static VulkanDevicePlaneCreateResult create();

    VulkanDevicePlane(const VulkanDevicePlane&) = delete;
    VulkanDevicePlane& operator=(const VulkanDevicePlane&) = delete;
    VulkanDevicePlane(VulkanDevicePlane&&) = delete;
    VulkanDevicePlane& operator=(VulkanDevicePlane&&) = delete;
    ~VulkanDevicePlane() override;

    [[nodiscard]] std::string_view backend_name() const noexcept override;
    [[nodiscard]] std::uint64_t device_memory_budget_bytes() const noexcept override;

    [[nodiscard]] VkInstance instance() const noexcept;
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;
    [[nodiscard]] VkDevice device() const noexcept;
    [[nodiscard]] VmaAllocator allocator() const noexcept;
    [[nodiscard]] VkQueue graphics_compute_queue() const noexcept;
    [[nodiscard]] std::uint32_t queue_family_index() const noexcept;
    [[nodiscard]] std::uint32_t api_version() const noexcept;
    [[nodiscard]] bool validation_requested() const noexcept;
    [[nodiscard]] bool validation_enabled() const noexcept;

    void submit_immediate(const std::function<void(VkCommandBuffer)>& record) const;

private:
    VulkanDevicePlane(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                      VmaAllocator allocator, VkQueue queue, std::uint32_t queue_family_index,
                      std::uint32_t api_version, std::uint64_t memory_budget_bytes,
                      bool validation_requested, bool validation_enabled);

    [[nodiscard]] VkCommandPool command_pool_for_current_thread() const;

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queue_family_index_{VK_QUEUE_FAMILY_IGNORED};
    std::uint32_t api_version_{0};
    std::uint64_t memory_budget_bytes_{0};
    bool validation_requested_{false};
    bool validation_enabled_{false};

    mutable std::mutex queue_mutex_;
    mutable std::mutex command_pool_mutex_;
    mutable std::unordered_map<std::thread::id, VkCommandPool> command_pools_;
};

}  // namespace cpipe::runtime
