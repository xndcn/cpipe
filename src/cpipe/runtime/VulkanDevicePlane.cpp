// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <cpipe/runtime/VulkanDevicePlane.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cpipe::runtime {
namespace {

using cpipe::compute::StatusCode;

constexpr std::string_view kValidationLayer = "VK_LAYER_KHRONOS_validation";

[[nodiscard]] bool validation_requested_by_build() noexcept {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

[[nodiscard]] std::string vk_result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        default:
            return "VK_ERROR_UNKNOWN";
    }
}

void check_vk(VkResult result, std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error{std::string{operation} + " failed: " + vk_result_name(result)};
    }
}

[[nodiscard]] bool has_validation_layer() {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkLayerProperties> layers(count);
    check_vk(vkEnumerateInstanceLayerProperties(&count, layers.data()),
             "vkEnumerateInstanceLayerProperties");
    return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, kValidationLayer.data()) == 0;
    });
}

[[nodiscard]] std::uint64_t device_local_budget(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < memory_properties.memoryHeapCount; ++i) {
        const auto& heap = memory_properties.memoryHeaps[i];
        if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0U) {
            total += heap.size;
        }
    }
    return total;
}

struct QueueSelection {
    std::uint32_t family_index{VK_QUEUE_FAMILY_IGNORED};
};

[[nodiscard]] QueueSelection find_graphics_compute_queue(VkPhysicalDevice physical_device) {
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

    for (std::uint32_t i = 0; i < family_count; ++i) {
        const auto flags = families[i].queueFlags;
        if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0U && (flags & VK_QUEUE_COMPUTE_BIT) != 0U) {
            return QueueSelection{i};
        }
    }
    return {};
}

[[nodiscard]] bool supports_timeline_semaphore(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;

    vkGetPhysicalDeviceFeatures2(physical_device, &features2);
    return features12.timelineSemaphore == VK_TRUE;
}

[[nodiscard]] bool is_eligible_device(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return false;
    }
    if (!supports_timeline_semaphore(physical_device)) {
        return false;
    }
    return find_graphics_compute_queue(physical_device).family_index != VK_QUEUE_FAMILY_IGNORED;
}

[[nodiscard]] int requested_device_index() {
    const char* env = std::getenv("CPIPE_VULKAN_DEVICE_INDEX");
    if (env == nullptr || *env == '\0') {
        return -2;
    }
    int value = -2;
    const char* end = env + std::strlen(env);
    const auto result = std::from_chars(env, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return -2;
    }
    return value;
}

[[nodiscard]] VulkanDevicePlaneCreateResult unsupported(std::string message) {
    spdlog::error("event=vulkan_device_create status={} reason={}",
                  cpipe::compute::to_string(StatusCode::Unsupported), message);
    return VulkanDevicePlaneCreateResult{StatusCode::Unsupported, nullptr, std::move(message)};
}

}  // namespace

VulkanDevicePlaneCreateResult VulkanDevicePlane::create() {
    const char* icd_env = std::getenv("VK_ICD_FILENAMES");
    if (icd_env != nullptr && *icd_env == '\0') {
        return unsupported("VK_ICD_FILENAMES is empty");
    }

    if (requested_device_index() == -1) {
        return unsupported("CPIPE_VULKAN_DEVICE_INDEX disables Vulkan");
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    try {
        std::uint32_t loader_version = VK_API_VERSION_1_0;
        check_vk(vkEnumerateInstanceVersion(&loader_version), "vkEnumerateInstanceVersion");
        if (loader_version < VK_API_VERSION_1_3) {
            return unsupported("Vulkan loader does not support API 1.3");
        }

        const bool validation_requested = validation_requested_by_build();
        const bool validation_enabled = validation_requested && has_validation_layer();
        if (validation_requested && validation_enabled) {
            spdlog::info("event=vulkan_validation layer={} enabled=true", kValidationLayer);
        } else if (validation_requested) {
            spdlog::warn("event=vulkan_validation layer={} enabled=false reason=not_found",
                         kValidationLayer);
        }

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "cpipe";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        app_info.pEngineName = "cpipe";
        app_info.engineVersion = VK_MAKE_VERSION(0, 2, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

        const char* layers[] = {kValidationLayer.data()};
        VkInstanceCreateInfo instance_info{};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;
        if (validation_enabled) {
            instance_info.enabledLayerCount = 1;
            instance_info.ppEnabledLayerNames = layers;
        }
        check_vk(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance");

        std::uint32_t device_count = 0;
        check_vk(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
                 "vkEnumeratePhysicalDevices");
        if (device_count == 0) {
            vkDestroyInstance(instance, nullptr);
            return unsupported("no Vulkan physical devices found");
        }
        std::vector<VkPhysicalDevice> physical_devices(device_count);
        check_vk(vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data()),
                 "vkEnumeratePhysicalDevices");

        VkPhysicalDevice selected_physical_device = VK_NULL_HANDLE;
        const int requested_index = requested_device_index();
        if (requested_index >= 0) {
            const auto requested = static_cast<std::uint32_t>(requested_index);
            if (requested >= physical_devices.size() || !is_eligible_device(physical_devices[requested])) {
                vkDestroyInstance(instance, nullptr);
                return unsupported("requested Vulkan device is unavailable or ineligible");
            }
            selected_physical_device = physical_devices[requested];
        } else {
            for (VkPhysicalDevice candidate : physical_devices) {
                if (is_eligible_device(candidate)) {
                    selected_physical_device = candidate;
                    break;
                }
            }
        }

        if (selected_physical_device == VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            return unsupported("no Vulkan 1.3 graphics+compute device with timeline semaphore found");
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(selected_physical_device, &properties);
        const auto queue_selection = find_graphics_compute_queue(selected_physical_device);
        const float queue_priority = 1.0F;

        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_selection.family_index;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.timelineSemaphore = VK_TRUE;

        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.pNext = &features12;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;

        check_vk(vkCreateDevice(selected_physical_device, &device_info, nullptr, &device),
                 "vkCreateDevice");

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queue_selection.family_index, 0, &queue);

        VmaAllocatorCreateInfo allocator_info{};
        allocator_info.instance = instance;
        allocator_info.physicalDevice = selected_physical_device;
        allocator_info.device = device;
        allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
        check_vk(vmaCreateAllocator(&allocator_info, &allocator), "vmaCreateAllocator");

        const auto memory_budget = device_local_budget(selected_physical_device);
        auto plane = std::shared_ptr<VulkanDevicePlane>{new VulkanDevicePlane{
            instance, selected_physical_device, device, allocator, queue, queue_selection.family_index,
            properties.apiVersion, memory_budget, validation_requested, validation_enabled}};
        spdlog::info("event=vulkan_device_create status={} device=\"{}\" api={}.{}.{} memory_bytes={}",
                     cpipe::compute::to_string(StatusCode::Ok), properties.deviceName,
                     VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                     VK_VERSION_PATCH(properties.apiVersion), memory_budget);
        return VulkanDevicePlaneCreateResult{StatusCode::Ok, std::move(plane), {}};
    } catch (const std::exception& error) {
        if (allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator);
        }
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
        spdlog::error("event=vulkan_device_create status={} reason={}",
                      cpipe::compute::to_string(StatusCode::Failed), error.what());
        return VulkanDevicePlaneCreateResult{StatusCode::Failed, nullptr, error.what()};
    }
}

VulkanDevicePlane::VulkanDevicePlane(VkInstance instance, VkPhysicalDevice physical_device,
                                     VkDevice device, VmaAllocator allocator, VkQueue queue,
                                     std::uint32_t queue_family_index, std::uint32_t api_version,
                                     std::uint64_t memory_budget_bytes, bool validation_requested,
                                     bool validation_enabled)
    : instance_(instance),
      physical_device_(physical_device),
      device_(device),
      allocator_(allocator),
      queue_(queue),
      queue_family_index_(queue_family_index),
      api_version_(api_version),
      memory_budget_bytes_(memory_budget_bytes),
      validation_requested_(validation_requested),
      validation_enabled_(validation_enabled) {}

VulkanDevicePlane::~VulkanDevicePlane() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    for (const auto& entry : command_pools_) {
        vkDestroyCommandPool(device_, entry.second, nullptr);
    }
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

std::string_view VulkanDevicePlane::backend_name() const noexcept {
    return "vulkan";
}

std::uint64_t VulkanDevicePlane::device_memory_budget_bytes() const noexcept {
    return memory_budget_bytes_;
}

VkInstance VulkanDevicePlane::instance() const noexcept {
    return instance_;
}

VkPhysicalDevice VulkanDevicePlane::physical_device() const noexcept {
    return physical_device_;
}

VkDevice VulkanDevicePlane::device() const noexcept {
    return device_;
}

VmaAllocator VulkanDevicePlane::allocator() const noexcept {
    return allocator_;
}

VkQueue VulkanDevicePlane::graphics_compute_queue() const noexcept {
    return queue_;
}

std::uint32_t VulkanDevicePlane::queue_family_index() const noexcept {
    return queue_family_index_;
}

std::uint32_t VulkanDevicePlane::api_version() const noexcept {
    return api_version_;
}

bool VulkanDevicePlane::validation_requested() const noexcept {
    return validation_requested_;
}

bool VulkanDevicePlane::validation_enabled() const noexcept {
    return validation_enabled_;
}

void VulkanDevicePlane::submit_immediate(const std::function<void(VkCommandBuffer)>& record) const {
    const VkCommandPool pool = command_pool_for_current_thread();

    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    check_vk(vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer),
             "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");
    record(command_buffer);
    check_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    check_vk(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence");

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    {
        std::lock_guard lock{queue_mutex_};
        check_vk(vkQueueSubmit(queue_, 1, &submit_info, fence), "vkQueueSubmit");
    }
    check_vk(vkWaitForFences(device_, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
             "vkWaitForFences");

    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, pool, 1, &command_buffer);
}

VkCommandPool VulkanDevicePlane::command_pool_for_current_thread() const {
    const auto thread_id = std::this_thread::get_id();
    std::lock_guard lock{command_pool_mutex_};
    if (const auto existing = command_pools_.find(thread_id); existing != command_pools_.end()) {
        return existing->second;
    }

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_index_;

    VkCommandPool pool = VK_NULL_HANDLE;
    check_vk(vkCreateCommandPool(device_, &pool_info, nullptr, &pool), "vkCreateCommandPool");
    command_pools_[thread_id] = pool;
    return pool;
}

}  // namespace cpipe::runtime
