// src/platform/common/buffer_pool.cpp
#include <cpipe/buffer.h>
#include "types.hpp"
#include "error.h"
#include "log.h"

#include <climits>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cpipe::platform {

// ── Internal helpers ──────────────────────────────────────────────────────────

struct DescriptorHash {
    std::size_t operator()(const BufferDescriptor& d) const noexcept {
        // Combine all descriptor fields for a unique hash.
        std::size_t h = std::hash<uint32_t>{}(d.width());
        h ^= std::hash<uint32_t>{}(d.height())  << 1;
        h ^= std::hash<int>{}(static_cast<int>(d.format())) << 2;
        h ^= std::hash<int>{}(static_cast<int>(d.device())) << 3;
        return h;
    }
};

// ── BufferPool::Impl ──────────────────────────────────────────────────────────

struct BufferPool::Impl {
    mutable std::mutex mutex;
    std::unordered_map<BufferDescriptor, std::vector<void*>, DescriptorHash> free_list;
    size_t total{0};

    // Returns a pooled pointer matching the descriptor, or nullptr.
    void* pop(const BufferDescriptor& desc) {
        auto it = free_list.find(desc);
        if (it == free_list.end() || it->second.empty()) return nullptr;
        void* ptr = it->second.back();
        it->second.pop_back();
        return ptr;
    }

    // Return raw memory to the free list, keyed by its original descriptor.
    void push(const BufferDescriptor& desc, void* ptr) {
        free_list[desc].push_back(ptr);
    }

    size_t available_count() const {
        size_t n = 0;
        for (auto& [desc, vec] : free_list) n += vec.size();
        return n;
    }

    ~Impl() {
        for (auto& [desc, ptrs] : free_list) {
            for (void* p : ptrs) {
#ifdef _MSC_VER
                _aligned_free(p);
#else
                std::free(p);
#endif
            }
        }
    }
};

// ── Buffer::BufferData ────────────────────────────────────────────────────────

struct Buffer::BufferData {
    BufferDescriptor descriptor;
    void*            raw_data{nullptr};
    // Type-erased return callback; avoids naming the private BufferPool::Impl.
    std::function<void(void*)> return_fn;

    explicit BufferData(BufferDescriptor d) : descriptor(d) {}

    ~BufferData() {
        if (raw_data && return_fn) {
            return_fn(raw_data);
            raw_data = nullptr;
        }
    }
};

// ── BufferDescriptor ──────────────────────────────────────────────────────────

expected<BufferDescriptor, Error>
BufferDescriptor::create(uint32_t             width,
                         uint32_t             height,
                         cpipe_pixel_format_t format,
                         cpipe_device_type_t  device)
{
    if (width == 0) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM, "BufferDescriptor: width must be > 0"));
    }
    if (height == 0) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM, "BufferDescriptor: height must be > 0"));
    }

    const uint32_t bpp = bytes_per_pixel(from_c(format));
    if (bpp == 0) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM, "BufferDescriptor: unknown pixel format"));
    }

    // Guard: width * bpp must not overflow uint32_t.
    if (width > UINT32_MAX / bpp) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM, "BufferDescriptor: row bytes overflow"));
    }
    const uint32_t row_bytes = width * bpp;

    // Guard: row_bytes + 63 (for alignment rounding) must not overflow uint32_t.
    if (row_bytes > UINT32_MAX - 63u) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM, "BufferDescriptor: stride overflow"));
    }

    BufferDescriptor d;
    d.width_  = width;
    d.height_ = height;
    d.format_ = format;
    d.device_ = device;
    d.stride_ = (row_bytes + 63u) & ~63u;

    // Guard: stride * height must not overflow uint64_t.
    if (static_cast<uint64_t>(d.stride_) > UINT64_MAX / height) {
        return unexpected<Error>(make_error(
            CPIPE_STATUS_ERROR_INVALID_PARAM, "BufferDescriptor: total size overflow"));
    }

    d.size_   = static_cast<uint64_t>(d.stride_) * height;
    return d;
}

// ── Buffer methods ────────────────────────────────────────────────────────────

const BufferDescriptor& Buffer::descriptor() const noexcept {
    return data_->descriptor;
}

void* Buffer::data() noexcept {
    return data_->raw_data;
}

const void* Buffer::data() const noexcept {
    return data_->raw_data;
}

cpipe_buffer_t Buffer::to_c() const noexcept {
    const auto& d = data_->descriptor;
    return cpipe_buffer_t{
        .width  = d.width(),
        .height = d.height(),
        .format = d.format(),
        .stride = d.stride(),
        .device = d.device(),
        .data   = data_->raw_data,
        .size   = d.size(),
    };
}

// ── BufferPool methods ────────────────────────────────────────────────────────

BufferPool::BufferPool() : impl_(std::make_shared<Impl>()) {}

BufferPool::~BufferPool() = default;
// Impl destructor (called when last shared_ptr ref drops) frees pooled memory.

expected<Buffer, Error> BufferPool::allocate(const BufferDescriptor& desc) {
    void* raw = nullptr;
    {
        std::lock_guard lock{impl_->mutex};
        raw = impl_->pop(desc);
    }

    if (!raw) {
        // Allocate new 64-byte-aligned memory.
        const uint64_t size = desc.size();

        // Guard: on 32-bit targets, size can exceed SIZE_MAX.
        if (size > SIZE_MAX) {
            return unexpected<Error>(make_error(
                CPIPE_STATUS_ERROR_OUT_OF_MEMORY,
                "BufferPool: descriptor size exceeds platform address space"));
        }

        // size must be a multiple of alignment for aligned_alloc.
        const size_t alloc_size = (static_cast<size_t>(size) + 63) & ~size_t{63};
#ifdef _MSC_VER
        raw = _aligned_malloc(alloc_size, 64);
#else
        raw = std::aligned_alloc(64, alloc_size);
#endif
        if (!raw) {
            return unexpected<Error>(make_error(
                CPIPE_STATUS_ERROR_OUT_OF_MEMORY,
                "BufferPool: failed to allocate " + std::to_string(size) + " bytes"));
        }
        std::lock_guard lock{impl_->mutex};
        ++impl_->total;
    }

    auto bd = std::make_shared<Buffer::BufferData>(desc);
    bd->raw_data = raw;
    // Capture impl + descriptor so the buffer returns to the correct pool bucket.
    auto impl_ref = impl_;
    auto ret_desc = desc;
    bd->return_fn = [impl_ref, ret_desc](void* ptr) {
        std::lock_guard lock{impl_ref->mutex};
        impl_ref->push(ret_desc, ptr);
    };

    return Buffer{std::move(bd)};
}

size_t BufferPool::available() const noexcept {
    std::lock_guard lock{impl_->mutex};
    return impl_->available_count();
}

size_t BufferPool::total_allocated() const noexcept {
    std::lock_guard lock{impl_->mutex};
    return impl_->total;
}

} // namespace cpipe::platform
