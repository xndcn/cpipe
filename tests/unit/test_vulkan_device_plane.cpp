// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

#include <catch2/catch_test_macros.hpp>
#include <cpipe/core/BufferUsage.hpp>
#include <cpipe/core/Status.hpp>
#include <cpipe/runtime/Sync.hpp>
#include <cpipe/runtime/VulkanBuffer.hpp>
#include <cpipe/runtime/VulkanDevicePlane.hpp>
#include <cpipe/runtime/VulkanImage.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

using cpipe::compute::BufferKind;
using cpipe::compute::BufferLayout;
using cpipe::compute::BufferUsage;
using cpipe::compute::IBuffer;
using cpipe::compute::PixelFormat;
using cpipe::compute::StatusCode;
using cpipe::runtime::VulkanBuffer;
using cpipe::runtime::VulkanDevicePlane;
using cpipe::runtime::VulkanFence;
using cpipe::runtime::VulkanImage;
using cpipe::runtime::VulkanTimelineSemaphore;

class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name); existing != nullptr) {
            previous_ = existing;
        }
        setenv(name, value, 1);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    ~ScopedEnv() {
        if (previous_) {
            setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

BufferLayout blob_layout(std::uint32_t bytes) {
    BufferLayout layout{};
    layout.kind = BufferKind::Blob;
    layout.format = PixelFormat::BLOB;
    layout.ndim = 1;
    layout.dims[0] = bytes;
    layout.stride[0] = 1;
    return layout;
}

BufferLayout r16_image_layout(std::uint32_t width, std::uint32_t height) {
    BufferLayout layout{};
    layout.kind = BufferKind::Image2D;
    layout.format = PixelFormat::R16_UINT;
    layout.ndim = 2;
    layout.dims[0] = width;
    layout.dims[1] = height;
    return layout;
}

}  // namespace

TEST_CASE("VulkanDevicePlane creates a usable Vulkan 1.3 device") {
    const auto created = VulkanDevicePlane::create();

    REQUIRE(created.status == StatusCode::Ok);
    REQUIRE(created.plane != nullptr);
    REQUIRE(created.plane->api_version() >= VK_API_VERSION_1_3);
    REQUIRE(created.plane->queue_family_index() != VK_QUEUE_FAMILY_IGNORED);
    REQUIRE(created.plane->device_memory_budget_bytes() > 0);

#ifndef NDEBUG
    REQUIRE(created.plane->validation_requested());
#else
    REQUIRE_FALSE(created.plane->validation_requested());
#endif
}

TEST_CASE("VulkanDevicePlane reports unsupported when no ICD is visible") {
    ScopedEnv no_icd{"VK_ICD_FILENAMES", ""};
    const auto created = VulkanDevicePlane::create();

    REQUIRE(created.status == StatusCode::Unsupported);
    REQUIRE(created.plane == nullptr);
    REQUIRE_FALSE(created.message.empty());
}

TEST_CASE("VulkanBuffer CPU lock round-trips bytes through VMA memory") {
    const auto created = VulkanDevicePlane::create();
    REQUIRE(created.status == StatusCode::Ok);

    VulkanBuffer buffer{created.plane, blob_layout(4096),
                        BufferUsage::Input | BufferUsage::CpuRead | BufferUsage::CpuWrite |
                            BufferUsage::GpuStorage};

    auto* write_ptr = static_cast<std::byte*>(buffer.lock_cpu(IBuffer::CpuAccess::Write));
    REQUIRE(write_ptr != nullptr);
    std::vector<std::byte> expected(buffer.size_bytes());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<std::byte>((i * 17U) & 0xffU);
        write_ptr[i] = expected[i];
    }
    buffer.unlock_cpu();
    buffer.flush_cpu_writes();

    const auto* read_ptr = static_cast<const std::byte*>(buffer.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::memcmp(read_ptr, expected.data(), expected.size()) == 0);
    buffer.unlock_cpu();
}

TEST_CASE("VulkanImage CPU lock uploads and downloads R16 image data") {
    const auto created = VulkanDevicePlane::create();
    REQUIRE(created.status == StatusCode::Ok);

    VulkanImage image{created.plane, r16_image_layout(256, 256),
                      BufferUsage::Intermediate | BufferUsage::CpuRead | BufferUsage::CpuWrite |
                          BufferUsage::GpuSampled};

    auto* write_ptr = static_cast<std::uint16_t*>(image.lock_cpu(IBuffer::CpuAccess::Write));
    REQUIRE(write_ptr != nullptr);
    std::vector<std::uint16_t> expected(256U * 256U);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<std::uint16_t>((i * 31U) & 0xffffU);
        write_ptr[i] = expected[i];
    }
    image.unlock_cpu();
    image.flush_cpu_writes();

    const auto* read_ptr = static_cast<const std::uint16_t*>(image.lock_cpu(IBuffer::CpuAccess::Read));
    REQUIRE(std::memcmp(read_ptr, expected.data(), expected.size() * sizeof(std::uint16_t)) == 0);
    image.unlock_cpu();
}

TEST_CASE("Vulkan sync wrappers expose fence and timeline host waits") {
    const auto created = VulkanDevicePlane::create();
    REQUIRE(created.status == StatusCode::Ok);

    VulkanFence fence{*created.plane, true};
    REQUIRE(fence.is_signaled());
    REQUIRE(fence.wait_host(std::chrono::seconds{1}));

    VulkanTimelineSemaphore timeline{*created.plane, 0};
    REQUIRE(timeline.current_value() == 0);
    timeline.signal_value_host(7);
    REQUIRE(timeline.wait_value(7, std::chrono::seconds{1}));
    REQUIRE(timeline.current_value() >= 7);
}
