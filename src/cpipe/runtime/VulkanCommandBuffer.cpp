// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/Trace.hpp>
#include <cpipe/runtime/VulkanCommandBuffer.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace cpipe::runtime {
namespace {

void check_vk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{operation};
    }
}

}  // namespace

VulkanCommandBuffer::VulkanCommandBuffer(std::shared_ptr<VulkanDevicePlane> plane)
    : plane_(std::move(plane)) {
    if (plane_ == nullptr) {
        throw std::runtime_error{"VulkanCommandBuffer requires a VulkanDevicePlane"};
    }

    pool_ = plane_->command_pool_for_current_thread();

    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = pool_;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    check_vk(vkAllocateCommandBuffers(plane_->device(), &allocate_info, &command_buffer_),
             "vkAllocateCommandBuffers");
}

VulkanCommandBuffer::VulkanCommandBuffer(VulkanCommandBuffer&& other) noexcept
    : plane_(std::move(other.plane_)),
      pool_(std::exchange(other.pool_, VK_NULL_HANDLE)),
      command_buffer_(std::exchange(other.command_buffer_, VK_NULL_HANDLE)) {}

VulkanCommandBuffer& VulkanCommandBuffer::operator=(VulkanCommandBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        plane_ = std::move(other.plane_);
        pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
        command_buffer_ = std::exchange(other.command_buffer_, VK_NULL_HANDLE);
    }
    return *this;
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    destroy();
}

VkCommandBuffer VulkanCommandBuffer::handle() const noexcept {
    return command_buffer_;
}

void VulkanCommandBuffer::begin() {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk(vkBeginCommandBuffer(command_buffer_, &begin_info), "vkBeginCommandBuffer");
}

void VulkanCommandBuffer::end() {
    check_vk(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");
}

void VulkanCommandBuffer::bind_compute_pipeline(VkPipeline pipeline) {
    vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

void VulkanCommandBuffer::bind_descriptor_set(VkPipelineLayout layout, VkDescriptorSet set) {
    vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0,
                            nullptr);
}

void VulkanCommandBuffer::dispatch(std::uint32_t groups_x, std::uint32_t groups_y,
                                   std::uint32_t groups_z) {
    vkCmdDispatch(command_buffer_, groups_x, groups_y, groups_z);
}

void VulkanCommandBuffer::submit(VulkanTimelineSemaphore& signal_timeline,
                                 std::uint64_t signal_value) {
    CPIPE_TRACE_SCOPE("VulkanCommandBuffer::submit");

    VkTimelineSemaphoreSubmitInfo timeline_info{};
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues = &signal_value;

    const VkSemaphore signal = signal_timeline.handle();
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_info;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer_;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &signal;

    {
        std::lock_guard lock{plane_->queue_mutex_};
        check_vk(vkQueueSubmit(plane_->graphics_compute_queue(), 1, &submit_info, VK_NULL_HANDLE),
                 "vkQueueSubmit");
    }
}

void VulkanCommandBuffer::destroy() noexcept {
    if (plane_ != nullptr && pool_ != VK_NULL_HANDLE && command_buffer_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(plane_->device(), pool_, 1, &command_buffer_);
    }
    command_buffer_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    plane_.reset();
}

}  // namespace cpipe::runtime
