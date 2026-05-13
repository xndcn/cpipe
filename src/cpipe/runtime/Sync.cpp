// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Sync.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cpipe::runtime {
namespace {

void check_vk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{operation};
    }
}

[[nodiscard]] std::uint64_t timeout_ns(std::chrono::nanoseconds timeout) {
    if (timeout.count() < 0) {
        return 0;
    }
    const auto count = static_cast<unsigned long long>(timeout.count());
    return count > std::numeric_limits<std::uint64_t>::max()
               ? std::numeric_limits<std::uint64_t>::max()
               : static_cast<std::uint64_t>(count);
}

}  // namespace

VulkanFence::VulkanFence(const VulkanDevicePlane& plane, bool signaled) : plane_(&plane) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (signaled) {
        info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    check_vk(vkCreateFence(plane_->device(), &info, nullptr, &fence_), "vkCreateFence");
}

VulkanFence::VulkanFence(VulkanFence&& other) noexcept
    : plane_(std::exchange(other.plane_, nullptr)),
      fence_(std::exchange(other.fence_, VK_NULL_HANDLE)) {}

VulkanFence& VulkanFence::operator=(VulkanFence&& other) noexcept {
    if (this != &other) {
        destroy();
        plane_ = std::exchange(other.plane_, nullptr);
        fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
    }
    return *this;
}

VulkanFence::~VulkanFence() {
    destroy();
}

VkFence VulkanFence::handle() const noexcept {
    return fence_;
}

bool VulkanFence::wait_host(std::chrono::nanoseconds timeout) const {
    return vkWaitForFences(plane_->device(), 1, &fence_, VK_TRUE, timeout_ns(timeout)) ==
           VK_SUCCESS;
}

bool VulkanFence::is_signaled() const {
    return vkGetFenceStatus(plane_->device(), fence_) == VK_SUCCESS;
}

void VulkanFence::reset() {
    check_vk(vkResetFences(plane_->device(), 1, &fence_), "vkResetFences");
}

void VulkanFence::destroy() noexcept {
    if (plane_ != nullptr && fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(plane_->device(), fence_, nullptr);
    }
    fence_ = VK_NULL_HANDLE;
    plane_ = nullptr;
}

VulkanTimelineSemaphore::VulkanTimelineSemaphore(const VulkanDevicePlane& plane,
                                                 std::uint64_t initial_value)
    : plane_(&plane) {
    VkSemaphoreTypeCreateInfo type_info{};
    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type_info.initialValue = initial_value;

    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type_info;
    check_vk(vkCreateSemaphore(plane_->device(), &info, nullptr, &semaphore_), "vkCreateSemaphore");
}

VulkanTimelineSemaphore::VulkanTimelineSemaphore(VulkanTimelineSemaphore&& other) noexcept
    : plane_(std::exchange(other.plane_, nullptr)),
      semaphore_(std::exchange(other.semaphore_, VK_NULL_HANDLE)) {}

VulkanTimelineSemaphore& VulkanTimelineSemaphore::operator=(
    VulkanTimelineSemaphore&& other) noexcept {
    if (this != &other) {
        destroy();
        plane_ = std::exchange(other.plane_, nullptr);
        semaphore_ = std::exchange(other.semaphore_, VK_NULL_HANDLE);
    }
    return *this;
}

VulkanTimelineSemaphore::~VulkanTimelineSemaphore() {
    destroy();
}

VkSemaphore VulkanTimelineSemaphore::handle() const noexcept {
    return semaphore_;
}

std::uint64_t VulkanTimelineSemaphore::current_value() const {
    std::uint64_t value = 0;
    check_vk(vkGetSemaphoreCounterValue(plane_->device(), semaphore_, &value),
             "vkGetSemaphoreCounterValue");
    return value;
}

bool VulkanTimelineSemaphore::wait_value(std::uint64_t value,
                                         std::chrono::nanoseconds timeout) const {
    VkSemaphoreWaitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &semaphore_;
    info.pValues = &value;
    return vkWaitSemaphores(plane_->device(), &info, timeout_ns(timeout)) == VK_SUCCESS;
}

void VulkanTimelineSemaphore::signal_value_host(std::uint64_t value) {
    VkSemaphoreSignalInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    info.semaphore = semaphore_;
    info.value = value;
    check_vk(vkSignalSemaphore(plane_->device(), &info), "vkSignalSemaphore");
}

void VulkanTimelineSemaphore::destroy() noexcept {
    if (plane_ != nullptr && semaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(plane_->device(), semaphore_, nullptr);
    }
    semaphore_ = VK_NULL_HANDLE;
    plane_ = nullptr;
}

}  // namespace cpipe::runtime
