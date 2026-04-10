// include/cpipe/buffer.h -- Public C++ buffer management API
#pragma once
#include <cpipe/types.h>
#include <cpipe/error.h>
#include <memory>
#include <cstdint>

namespace cpipe::platform {

// ── BufferDescriptor ─────────────────────────────────────────────────────────

class BufferDescriptor {
public:
    /// Factory: validates params and computes 64-byte-aligned stride + size.
    /// Returns Error if width==0 or height==0.
    static expected<BufferDescriptor, Error>
    create(uint32_t             width,
           uint32_t             height,
           cpipe_pixel_format_t format,
           cpipe_device_type_t  device = CPIPE_DEVICE_CPU);

    uint32_t             width()  const noexcept { return width_;  }
    uint32_t             height() const noexcept { return height_; }
    cpipe_pixel_format_t format() const noexcept { return format_; }
    cpipe_device_type_t  device() const noexcept { return device_; }
    uint32_t             stride() const noexcept { return stride_; } ///< bytes/row, 64-byte aligned
    uint64_t             size()   const noexcept { return size_;   } ///< stride * height

    bool operator==(const BufferDescriptor&) const = default;

public:
    BufferDescriptor(const BufferDescriptor&) = default;
    BufferDescriptor& operator=(const BufferDescriptor&) = default;

private:
    BufferDescriptor() = default;

    uint32_t             width_{};
    uint32_t             height_{};
    cpipe_pixel_format_t format_{};
    cpipe_device_type_t  device_{};
    uint32_t             stride_{};
    uint64_t             size_{};
};

// ── Buffer ────────────────────────────────────────────────────────────────────

class BufferPool; // forward declaration

class Buffer {
public:
    const BufferDescriptor& descriptor() const noexcept;
    void*                   data()       noexcept;
    const void*             data()       const noexcept;

    /// Build the C-compatible view of this buffer (does NOT transfer ownership).
    cpipe_buffer_t to_c() const noexcept;

    /// Returns true if this Buffer holds a valid allocation.
    explicit operator bool() const noexcept { return static_cast<bool>(data_); }

private:
    struct BufferData;
    explicit Buffer(std::shared_ptr<BufferData> d) : data_(std::move(d)) {}
    std::shared_ptr<BufferData> data_;
    friend class BufferPool;
};

// ── BufferPool ────────────────────────────────────────────────────────────────

class BufferPool {
public:
    BufferPool();
    ~BufferPool();

    // Non-copyable, moveable
    BufferPool(const BufferPool&)            = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&)                 = default;
    BufferPool& operator=(BufferPool&&)      = default;

    /// Allocate a buffer matching `desc`.  Reuses a free buffer of the same
    /// size if one is available.
    expected<Buffer, Error> allocate(const BufferDescriptor& desc);

    size_t available()       const noexcept; ///< free buffers in pool
    size_t total_allocated() const noexcept; ///< total allocations ever made

private:
    struct Impl;
    std::shared_ptr<Impl> impl_; // shared so allocated Buffers can keep it alive
};

} // namespace cpipe::platform
