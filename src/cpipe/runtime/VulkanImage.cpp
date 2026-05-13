// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <cpipe/runtime/VulkanImage.hpp>

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace cpipe::runtime {
namespace {

using cpipe::compute::BufferUsage;
using cpipe::compute::PixelFormat;

void check_vk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{operation};
    }
}

[[nodiscard]] VkFormat to_vk_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R16_UINT:
            return VK_FORMAT_R16_UINT;
        case PixelFormat::R8G8B8A8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::R16G16B16A16_SFLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::R32_SFLOAT:
            return VK_FORMAT_R32_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

[[nodiscard]] VkImageUsageFlags vk_usage(BufferUsage usage) {
    VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (cpipe::compute::has_usage(usage, BufferUsage::GpuSampled)) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (cpipe::compute::has_usage(usage, BufferUsage::GpuStorage)) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    return flags;
}

struct StagingBuffer {
    StagingBuffer(const VulkanDevicePlane& plane, std::uint64_t size, VkBufferUsageFlags usage)
        : allocator(plane.allocator()) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_info{};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
        allocation_info.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo created_allocation{};
        check_vk(vmaCreateBuffer(allocator, &buffer_info, &allocation_info, &buffer, &allocation,
                                 &created_allocation),
                 "vmaCreateBuffer");
        data = created_allocation.pMappedData;
        if (data == nullptr) {
            check_vk(vmaMapMemory(allocator, allocation, &data), "vmaMapMemory");
        }
    }

    StagingBuffer(const StagingBuffer&) = delete;
    StagingBuffer& operator=(const StagingBuffer&) = delete;

    ~StagingBuffer() {
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }

    VmaAllocator allocator{VK_NULL_HANDLE};
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    void* data{nullptr};
};

void transition_image(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout,
                      VkImageLayout new_layout) {
    if (old_layout == new_layout) {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }

    if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }

    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

}  // namespace

VulkanImage::VulkanImage(std::shared_ptr<VulkanDevicePlane> plane, cpipe::compute::BufferLayout layout,
                         BufferUsage usage, std::string color_role)
    : plane_(std::move(plane)),
      layout_(layout),
      usage_(usage),
      color_role_(std::move(color_role)),
      format_(to_vk_format(layout_.format)),
      size_bytes_(layout_.size_bytes()) {
    if (layout_.kind != cpipe::compute::BufferKind::Image2D || layout_.ndim != 2 ||
        format_ == VK_FORMAT_UNDEFINED) {
        throw std::invalid_argument{"unsupported VulkanImage layout"};
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format_;
    image_info.extent.width = layout_.dims[0];
    image_info.extent.height = layout_.dims[1];
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = vk_usage(usage_);
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    check_vk(vmaCreateImage(plane_->allocator(), &image_info, &allocation_info, &image_, &allocation_,
                            nullptr),
             "vmaCreateImage");
    staging_.resize(static_cast<std::size_t>(size_bytes_));
}

VulkanImage::~VulkanImage() {
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(plane_->allocator(), image_, allocation_);
    }
}

const cpipe::compute::BufferLayout& VulkanImage::layout() const noexcept {
    return layout_;
}

std::uint64_t VulkanImage::size_bytes() const noexcept {
    return size_bytes_;
}

std::string_view VulkanImage::color_role() const noexcept {
    return color_role_;
}

VkImage VulkanImage::vk_image() const noexcept {
    return image_;
}

VkFormat VulkanImage::vk_format() const noexcept {
    return format_;
}

void* VulkanImage::lock_cpu(CpuAccess access) {
    assert(!locked_);
    if (access == CpuAccess::Read) {
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuRead));
        download_to_staging();
    } else if (access == CpuAccess::Write) {
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuWrite));
    } else {
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuRead));
        assert(cpipe::compute::has_usage(usage_, BufferUsage::CpuWrite));
        download_to_staging();
    }
    locked_access_ = access;
    locked_ = true;
    return staging_.data();
}

void VulkanImage::unlock_cpu() {
    assert(locked_);
    if (locked_access_ == CpuAccess::Write || locked_access_ == CpuAccess::ReadWrite) {
        upload_from_staging();
    }
    locked_ = false;
}

void VulkanImage::flush_cpu_writes() {}

std::shared_ptr<cpipe::compute::IBuffer> VulkanImage::sub_view(std::uint32_t x0, std::uint32_t y0,
                                                               std::uint32_t w, std::uint32_t h) {
    (void)x0;
    (void)y0;
    (void)w;
    (void)h;
    std::clog << "warning: VulkanImage::sub_view is not implemented in cpipe v1\n";
    return nullptr;
}

void VulkanImage::upload_from_staging() {
    StagingBuffer staging{*plane_, size_bytes_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    std::memcpy(staging.data, staging_.data(), staging_.size());
    check_vk(vmaFlushAllocation(plane_->allocator(), staging.allocation, 0, VK_WHOLE_SIZE),
             "vmaFlushAllocation");

    const VkImageLayout old_layout = current_layout_;
    plane_->submit_immediate([&](VkCommandBuffer command_buffer) {
        transition_image(command_buffer, image_, old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = layout_.dims[0];
        region.imageExtent.height = layout_.dims[1];
        region.imageExtent.depth = 1;

        vkCmdCopyBufferToImage(command_buffer, staging.buffer, image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transition_image(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL);
    });
    current_layout_ = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanImage::download_to_staging() {
    StagingBuffer staging{*plane_, size_bytes_, VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VkImageLayout old_layout = current_layout_;
    plane_->submit_immediate([&](VkCommandBuffer command_buffer) {
        transition_image(command_buffer, image_, old_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = layout_.dims[0];
        region.imageExtent.height = layout_.dims[1];
        region.imageExtent.depth = 1;

        vkCmdCopyImageToBuffer(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging.buffer, 1, &region);
        transition_image(command_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL);
    });
    current_layout_ = VK_IMAGE_LAYOUT_GENERAL;

    check_vk(vmaInvalidateAllocation(plane_->allocator(), staging.allocation, 0, VK_WHOLE_SIZE),
             "vmaInvalidateAllocation");
    std::memcpy(staging_.data(), staging.data, staging_.size());
}

}  // namespace cpipe::runtime
