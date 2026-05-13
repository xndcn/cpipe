// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cpipe/core/BufferLayout.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/IBuffer.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cpipe::runtime {

class VulkanBuffer final : public cpipe::compute::IBuffer {
public:
    VulkanBuffer(std::shared_ptr<VulkanDevicePlane> plane, cpipe::compute::BufferLayout layout,
                 cpipe::compute::BufferUsage usage, std::string color_role = {});
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = delete;
    VulkanBuffer& operator=(VulkanBuffer&&) = delete;
    ~VulkanBuffer() override;

    [[nodiscard]] const cpipe::compute::BufferLayout& layout() const noexcept override;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept override;
    [[nodiscard]] std::string_view color_role() const noexcept override;
    [[nodiscard]] std::shared_ptr<const cpipe::compute::BufferMetadata> metadata()
        const noexcept override;
    void set_metadata(std::shared_ptr<const cpipe::compute::BufferMetadata> metadata) override;
    [[nodiscard]] VkBuffer vk_buffer() const noexcept;

    void* lock_cpu(CpuAccess access) override;
    void unlock_cpu() override;
    void flush_cpu_writes() override;
    std::shared_ptr<cpipe::compute::IBuffer> sub_view(std::uint32_t x0, std::uint32_t y0,
                                                      std::uint32_t w, std::uint32_t h) override;

private:
    std::shared_ptr<VulkanDevicePlane> plane_;
    cpipe::compute::BufferLayout layout_{};
    cpipe::compute::BufferUsage usage_{cpipe::compute::BufferUsage::None};
    std::string color_role_;
    std::shared_ptr<const cpipe::compute::BufferMetadata> metadata_;
    VkBuffer buffer_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    void* mapped_{nullptr};
    std::uint64_t size_bytes_{0};
    bool locked_{false};
};

}  // namespace cpipe::runtime
