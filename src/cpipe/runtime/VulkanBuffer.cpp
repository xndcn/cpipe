// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cassert>
#include <cpipe/runtime/VulkanBuffer.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace cpipe::runtime {
namespace {

using cpipe::compute::BufferUsage;

void check_vk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{operation};
    }
}

[[nodiscard]] VkBufferUsageFlags vk_usage(BufferUsage usage) {
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (cpipe::compute::has_usage(usage, BufferUsage::GpuStorage)) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    return flags;
}

}  // namespace

VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanDevicePlane> plane,
                           cpipe::compute::BufferLayout layout, BufferUsage usage,
                           std::string color_role)
    : plane_(std::move(plane)),
      layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      metadata_(std::make_shared<cpipe::compute::BufferMetadata>()),
      size_bytes_(layout_.size_bytes()) {
    if (!color_role_.empty()) {
        auto metadata = std::make_shared<cpipe::compute::BufferMetadata>();
        metadata->cs_role = color_role_;
        metadata_ = std::move(metadata);
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size_bytes_;
    buffer_info.usage = vk_usage(usage_);
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo created_allocation{};
    check_vk(vmaCreateBuffer(plane_->allocator(), &buffer_info, &allocation_info, &buffer_,
                             &allocation_, &created_allocation),
             "vmaCreateBuffer");
    mapped_ = created_allocation.pMappedData;
    if (mapped_ == nullptr) {
        check_vk(vmaMapMemory(plane_->allocator(), allocation_, &mapped_), "vmaMapMemory");
    }
}

VulkanBuffer::~VulkanBuffer() {
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(plane_->allocator(), buffer_, allocation_);
    }
}

const cpipe::compute::BufferLayout& VulkanBuffer::layout() const noexcept {
    return layout_;
}

std::uint64_t VulkanBuffer::size_bytes() const noexcept {
    return size_bytes_;
}

std::string_view VulkanBuffer::color_role() const noexcept {
    return color_role_;
}

std::shared_ptr<const cpipe::compute::BufferMetadata> VulkanBuffer::metadata() const noexcept {
    return metadata_;
}

void VulkanBuffer::set_metadata(std::shared_ptr<const cpipe::compute::BufferMetadata> metadata) {
    metadata_ = std::move(metadata);
}

VkBuffer VulkanBuffer::vk_buffer() const noexcept {
    return buffer_;
}

void* VulkanBuffer::lock_cpu(CpuAccess access) {
    assert(!locked_);
    if (access == CpuAccess::Read) {
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuRead));
        check_vk(vmaInvalidateAllocation(plane_->allocator(), allocation_, 0, VK_WHOLE_SIZE),
                 "vmaInvalidateAllocation");
    } else if (access == CpuAccess::Write) {
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuWrite));
    } else {
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuRead));
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuWrite));
        check_vk(vmaInvalidateAllocation(plane_->allocator(), allocation_, 0, VK_WHOLE_SIZE),
                 "vmaInvalidateAllocation");
    }
    locked_ = true;
    return mapped_;
}

void VulkanBuffer::unlock_cpu() {
    assert(locked_);
    locked_ = false;
}

void VulkanBuffer::flush_cpu_writes() {
    check_vk(vmaFlushAllocation(plane_->allocator(), allocation_, 0, VK_WHOLE_SIZE),
             "vmaFlushAllocation");
}

std::shared_ptr<cpipe::compute::IBuffer> VulkanBuffer::sub_view(std::uint32_t x0, std::uint32_t y0,
                                                                std::uint32_t w, std::uint32_t h) {
    (void)x0;
    (void)y0;
    (void)w;
    (void)h;
    std::clog << "warning: VulkanBuffer::sub_view is not implemented in cpipe v1\n";
    return nullptr;
}

}  // namespace cpipe::runtime
