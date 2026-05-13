// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>

namespace cpipe::runtime {

class VulkanDevicePlane;

class VulkanFence {
public:
    explicit VulkanFence(const VulkanDevicePlane& plane, bool signaled = false);
    VulkanFence(const VulkanFence&) = delete;
    VulkanFence& operator=(const VulkanFence&) = delete;
    VulkanFence(VulkanFence&& other) noexcept;
    VulkanFence& operator=(VulkanFence&& other) noexcept;
    ~VulkanFence();

    [[nodiscard]] VkFence handle() const noexcept;
    [[nodiscard]] bool wait_host(std::chrono::nanoseconds timeout) const;
    [[nodiscard]] bool is_signaled() const;
    void reset();

private:
    void destroy() noexcept;

    const VulkanDevicePlane* plane_{nullptr};
    VkFence fence_{VK_NULL_HANDLE};
};

class VulkanTimelineSemaphore {
public:
    explicit VulkanTimelineSemaphore(const VulkanDevicePlane& plane, std::uint64_t initial_value = 0);
    VulkanTimelineSemaphore(const VulkanTimelineSemaphore&) = delete;
    VulkanTimelineSemaphore& operator=(const VulkanTimelineSemaphore&) = delete;
    VulkanTimelineSemaphore(VulkanTimelineSemaphore&& other) noexcept;
    VulkanTimelineSemaphore& operator=(VulkanTimelineSemaphore&& other) noexcept;
    ~VulkanTimelineSemaphore();

    [[nodiscard]] VkSemaphore handle() const noexcept;
    [[nodiscard]] std::uint64_t current_value() const;
    [[nodiscard]] bool wait_value(std::uint64_t value, std::chrono::nanoseconds timeout) const;
    void signal_value_host(std::uint64_t value);

private:
    void destroy() noexcept;

    const VulkanDevicePlane* plane_{nullptr};
    VkSemaphore semaphore_{VK_NULL_HANDLE};
};

}  // namespace cpipe::runtime
