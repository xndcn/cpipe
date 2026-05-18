// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cpipe::runtime {

class VulkanImage final : public cpipe::compute::IBuffer {
public:
    VulkanImage(std::shared_ptr<VulkanDevicePlane> plane, cpipe::compute::BufferLayout layout,
                cpipe::compute::BufferUsage usage, std::string color_role = {});
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&&) = delete;
    VulkanImage& operator=(VulkanImage&&) = delete;
    ~VulkanImage() override;

    [[nodiscard]] const cpipe::compute::BufferLayout& layout() const noexcept override;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept override;
    [[nodiscard]] std::string_view color_role() const noexcept override;
    [[nodiscard]] std::shared_ptr<const cpipe::compute::BufferMetadata> metadata()
        const noexcept override;
    void set_metadata(std::shared_ptr<const cpipe::compute::BufferMetadata> metadata) override;
    [[nodiscard]] std::shared_ptr<VulkanDevicePlane> plane() const noexcept;
    [[nodiscard]] VkImage vk_image() const noexcept;
    [[nodiscard]] VkFormat vk_format() const noexcept;
    void transition_to_general();

    void* lock_cpu(CpuAccess access) override;
    void unlock_cpu() override;
    void flush_cpu_writes() override;
    std::shared_ptr<cpipe::compute::IBuffer> sub_view(std::uint32_t x0, std::uint32_t y0,
                                                      std::uint32_t w, std::uint32_t h) override;

private:
    void upload_from_staging();
    void download_to_staging();

    std::shared_ptr<VulkanDevicePlane> plane_;
    cpipe::compute::BufferLayout layout_{};
    cpipe::compute::BufferUsage usage_{cpipe::compute::BufferUsage::None};
    std::string color_role_;
    std::shared_ptr<const cpipe::compute::BufferMetadata> metadata_;
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkFormat format_{VK_FORMAT_UNDEFINED};
    VkImageLayout current_layout_{VK_IMAGE_LAYOUT_UNDEFINED};
    std::uint64_t size_bytes_{0};
    std::vector<std::byte> staging_;
    CpuAccess locked_access_{CpuAccess::Read};
    bool locked_{false};
};

}  // namespace cpipe::runtime
