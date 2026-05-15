// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/Status.hpp>
#include <cpipe/runtime/Sync.hpp>
#include <cpipe/runtime/VulkanBuffer.hpp>
#include <cpipe/runtime/VulkanCommandBuffer.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;

constexpr std::uint32_t kExpected = 0x12345678U;
constexpr std::array<std::uint32_t, 134> kWriteValueSpirv{
    0x07230203, 0x00010000, 0x0008000a, 0x00000012, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0005000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x00060010, 0x00000004, 0x00000011,
    0x00000001, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00040005, 0x00000007, 0x61746144, 0x00000000, 0x00050006, 0x00000007,
    0x00000000, 0x756c6176, 0x00000065, 0x00040005, 0x00000009, 0x61746164, 0x00000000, 0x00050048,
    0x00000007, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000007, 0x00000003, 0x00040047,
    0x00000009, 0x00000022, 0x00000000, 0x00040047, 0x00000009, 0x00000021, 0x00000000, 0x00040047,
    0x00000011, 0x0000000b, 0x00000019, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00040015, 0x00000006, 0x00000020, 0x00000000, 0x0003001e, 0x00000007, 0x00000006, 0x00040020,
    0x00000008, 0x00000002, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000002, 0x00040015,
    0x0000000a, 0x00000020, 0x00000001, 0x0004002b, 0x0000000a, 0x0000000b, 0x00000000, 0x0004002b,
    0x00000006, 0x0000000c, 0x12345678, 0x00040020, 0x0000000d, 0x00000002, 0x00000006, 0x00040017,
    0x0000000f, 0x00000006, 0x00000003, 0x0004002b, 0x00000006, 0x00000010, 0x00000001, 0x0006002c,
    0x0000000f, 0x00000011, 0x00000010, 0x00000010, 0x00000010, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x0000000d, 0x0000000e, 0x00000009,
    0x0000000b, 0x0003003e, 0x0000000e, 0x0000000c, 0x000100fd, 0x00010038};

BufferLayout storage_layout() {
    BufferLayout layout{};
    layout.kind = BufferKind::Blob;
    layout.format = PixelFormat::BLOB;
    layout.ndim = 1;
    layout.dims[0] = sizeof(std::uint32_t);
    layout.stride[0] = 1;
    return layout;
}

void check_vk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{operation};
    }
}

}  // namespace

TEST_CASE("VulkanCommandBuffer submits through cpipe-owned timeline semaphore") {
    const auto* enabled = std::getenv("CPIPE_VULKAN_AVAILABLE");
    if (enabled == nullptr || std::string_view{enabled} != "ON") {
        SUCCEED("CPIPE_VULKAN_AVAILABLE is not ON; skipping Vulkan command-buffer check");
        return;
    }

    const auto created = cpipe::runtime::VulkanDevicePlane::create();
    REQUIRE(created.status == cpipe::compute::StatusCode::Ok);
    REQUIRE(created.plane != nullptr);

    cpipe::runtime::VulkanBuffer storage{created.plane, storage_layout(),
                                         BufferUsage::Input | BufferUsage::Output |
                                             BufferUsage::CpuRead | BufferUsage::CpuWrite |
                                             BufferUsage::GpuStorage};
    auto* initial = static_cast<std::uint32_t*>(storage.lock_cpu(IBuffer::CpuAccess::Write));
    *initial = 0;
    storage.unlock_cpu();
    storage.flush_cpu_writes();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo set_layout_info{};
    set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout_info.bindingCount = 1;
    set_layout_info.pBindings = &binding;

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    check_vk(vkCreateDescriptorSetLayout(created.plane->device(), &set_layout_info, nullptr,
                                         &set_layout),
             "vkCreateDescriptorSetLayout");

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &set_layout;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    check_vk(vkCreatePipelineLayout(created.plane->device(), &pipeline_layout_info, nullptr,
                                    &pipeline_layout),
             "vkCreatePipelineLayout");

    VkShaderModuleCreateInfo shader_info{};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = kWriteValueSpirv.size() * sizeof(std::uint32_t);
    shader_info.pCode = kWriteValueSpirv.data();

    VkShaderModule shader = VK_NULL_HANDLE;
    check_vk(vkCreateShaderModule(created.plane->device(), &shader_info, nullptr, &shader),
             "vkCreateShaderModule");

    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = shader;
    stage_info.pName = "main";

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage = stage_info;
    pipeline_info.layout = pipeline_layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    check_vk(vkCreateComputePipelines(created.plane->device(), VK_NULL_HANDLE, 1, &pipeline_info,
                                      nullptr, &pipeline),
             "vkCreateComputePipelines");

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    check_vk(vkCreateDescriptorPool(created.plane->device(), &pool_info, nullptr, &descriptor_pool),
             "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = descriptor_pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &set_layout;

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    check_vk(vkAllocateDescriptorSets(created.plane->device(), &set_info, &descriptor_set),
             "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = storage.vk_buffer();
    buffer_info.offset = 0;
    buffer_info.range = sizeof(std::uint32_t);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer_info;
    vkUpdateDescriptorSets(created.plane->device(), 1, &write, 0, nullptr);

    cpipe::runtime::VulkanTimelineSemaphore timeline{*created.plane, 0};
    cpipe::runtime::VulkanCommandBuffer command_buffer{created.plane};

    command_buffer.begin();
    command_buffer.bind_compute_pipeline(pipeline);
    command_buffer.bind_descriptor_set(pipeline_layout, descriptor_set);
    command_buffer.dispatch(1, 1, 1);
    command_buffer.end();
    command_buffer.submit(timeline, 1);

    REQUIRE(created.plane->wait_timeline(timeline, 1, std::chrono::seconds{1}));
    REQUIRE(timeline.current_value() >= 1);

    const auto* output =
        static_cast<const std::uint32_t*>(storage.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(*output == kExpected);
    storage.unlock_cpu();

    vkDestroyDescriptorPool(created.plane->device(), descriptor_pool, nullptr);
    vkDestroyPipeline(created.plane->device(), pipeline, nullptr);
    vkDestroyShaderModule(created.plane->device(), shader, nullptr);
    vkDestroyPipelineLayout(created.plane->device(), pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(created.plane->device(), set_layout, nullptr);
}
