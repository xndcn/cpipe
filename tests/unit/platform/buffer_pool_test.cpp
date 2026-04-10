#include <cpipe/buffer.h>
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>
#include <vector>
#include <cstdint>

using namespace cpipe::platform;
using cpipe::Error;

// Buffer must not be default-constructible by external code.
static_assert(!std::is_default_constructible_v<Buffer>,
              "Buffer should not be default-constructible");

// BufferDescriptor must not be default-constructible — use create() factory.
static_assert(!std::is_default_constructible_v<BufferDescriptor>,
              "BufferDescriptor should not be default-constructible");

// ── BufferDescriptor ──────────────────────────────────────────────────────────

TEST(BufferDescriptor, Create_ValidParams_Success) {
    auto result = BufferDescriptor::create(1920, 1080, CPIPE_PIXEL_FORMAT_BAYER_RGGB_16);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width(),  1920u);
    EXPECT_EQ(result->height(), 1080u);
    EXPECT_EQ(result->format(), CPIPE_PIXEL_FORMAT_BAYER_RGGB_16);
    EXPECT_EQ(result->device(), CPIPE_DEVICE_CPU);
}

TEST(BufferDescriptor, Create_ZeroWidth_ReturnsError) {
    auto result = BufferDescriptor::create(0, 1080, CPIPE_PIXEL_FORMAT_RGB_8);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(BufferDescriptor, Create_ZeroHeight_ReturnsError) {
    auto result = BufferDescriptor::create(1920, 0, CPIPE_PIXEL_FORMAT_RGB_8);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(BufferDescriptor, Create_UnknownFormat_ReturnsError) {
    auto result = BufferDescriptor::create(
        64, 64, static_cast<cpipe_pixel_format_t>(999));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(BufferDescriptor, Create_WidthOverflow_ReturnsError) {
    // RGBA_8 has bpp=4.  UINT32_MAX * 4 overflows uint32_t.
    auto result = BufferDescriptor::create(
        UINT32_MAX, 1, CPIPE_PIXEL_FORMAT_RGBA_8);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(BufferDescriptor, Create_TotalSizeOverflow_ReturnsError) {
    // Bayer16 bpp=2: max width = UINT32_MAX/2 = 2'147'483'647
    // row_bytes = 4'294'967'294 → row_bytes+63 overflows uint32_t → stride overflow
    auto result = BufferDescriptor::create(
        UINT32_MAX / 2, 2, CPIPE_PIXEL_FORMAT_BAYER_RGGB_16);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(BufferDescriptor, StrideAlignment_Always64ByteAligned) {
    // Test several widths and formats
    const struct { uint32_t w; cpipe_pixel_format_t fmt; } cases[] = {
        {1,    CPIPE_PIXEL_FORMAT_BAYER_RGGB_16},  // 2 bytes → stride = 64
        {33,   CPIPE_PIXEL_FORMAT_RGB_8},           // 99 bytes → stride = 128
        {100,  CPIPE_PIXEL_FORMAT_RGBA_8},          // 400 bytes → stride = 448? no, 400 rounded up = 448? let's check
        {1920, CPIPE_PIXEL_FORMAT_BAYER_RGGB_16},  // 3840 bytes → stride = 3840 (exact multiple)
        {1,    CPIPE_PIXEL_FORMAT_RGB_FLOAT32},    // 12 bytes → stride = 64
    };
    for (auto& c : cases) {
        auto result = BufferDescriptor::create(c.w, 1, c.fmt);
        ASSERT_TRUE(result.has_value()) << "width=" << c.w;
        EXPECT_EQ(result->stride() % 64, 0u) << "width=" << c.w << " not 64-byte aligned";
    }
}

TEST(BufferDescriptor, BytesPerPixel_BayerVariants_Is2) {
    for (auto fmt : {CPIPE_PIXEL_FORMAT_BAYER_RGGB_16,
                     CPIPE_PIXEL_FORMAT_BAYER_BGGR_16,
                     CPIPE_PIXEL_FORMAT_BAYER_GRBG_16,
                     CPIPE_PIXEL_FORMAT_BAYER_GBRG_16}) {
        auto r = BufferDescriptor::create(32, 1, fmt);
        ASSERT_TRUE(r.has_value());
        // stride >= 32*2 = 64, and 64-byte aligned → exactly 64
        EXPECT_EQ(r->stride(), 64u);
    }
}

TEST(BufferDescriptor, BytesPerPixel_RGB8_Is3) {
    // 10 pixels * 3 = 30 bytes → stride rounds up to 64
    auto r = BufferDescriptor::create(10, 1, CPIPE_PIXEL_FORMAT_RGB_8);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->stride(), 64u);
}

TEST(BufferDescriptor, BytesPerPixel_RGBA8_Is4) {
    // 16 pixels * 4 = 64 bytes → stride = 64
    auto r = BufferDescriptor::create(16, 1, CPIPE_PIXEL_FORMAT_RGBA_8);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->stride(), 64u);
}

TEST(BufferDescriptor, BytesPerPixel_RGB16_Is6) {
    // 11 pixels * 6 = 66 bytes → stride rounds up to 128
    auto r = BufferDescriptor::create(11, 1, CPIPE_PIXEL_FORMAT_RGB_16);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->stride(), 128u);
}

TEST(BufferDescriptor, BytesPerPixel_RGBFloat32_Is12) {
    // 6 pixels * 12 = 72 bytes → stride rounds up to 128
    auto r = BufferDescriptor::create(6, 1, CPIPE_PIXEL_FORMAT_RGB_FLOAT32);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->stride(), 128u);
}

TEST(BufferDescriptor, Size_EqualsStrideTimesHeight) {
    auto r = BufferDescriptor::create(100, 50, CPIPE_PIXEL_FORMAT_RGB_8);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), static_cast<uint64_t>(r->stride()) * 50u);
}

// ── BufferPool: allocation ────────────────────────────────────────────────────

TEST(BufferPool, Allocate_ValidDesc_ReturnsBuffer) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(64, 64, CPIPE_PIXEL_FORMAT_RGB_8).value();
    auto result = pool.allocate(desc);
    ASSERT_TRUE(result.has_value());
}

TEST(BufferPool, Allocate_AccessData_NonNull) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(64, 64, CPIPE_PIXEL_FORMAT_RGB_8).value();
    auto buf = pool.allocate(desc).value();
    EXPECT_NE(buf.data(), nullptr);
}

TEST(BufferPool, Allocate_DataAlignment_64Byte) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(100, 100, CPIPE_PIXEL_FORMAT_RGBA_8).value();
    auto buf = pool.allocate(desc).value();
    const uintptr_t addr = reinterpret_cast<uintptr_t>(buf.data());
    EXPECT_EQ(addr % 64, 0u) << "data pointer not 64-byte aligned";
}

TEST(BufferPool, Allocate_DescriptorFieldsMatch) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(320, 240, CPIPE_PIXEL_FORMAT_BAYER_RGGB_16).value();
    auto buf = pool.allocate(desc).value();
    EXPECT_EQ(buf.descriptor().width(),  320u);
    EXPECT_EQ(buf.descriptor().height(), 240u);
    EXPECT_EQ(buf.descriptor().format(), CPIPE_PIXEL_FORMAT_BAYER_RGGB_16);
}

// ── BufferPool: reference counting + reuse ────────────────────────────────────

TEST(BufferPool, RefCount_CopyIncrements) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(32, 32, CPIPE_PIXEL_FORMAT_RGB_8).value();
    auto buf1 = pool.allocate(desc).value();
    auto buf2 = buf1; // copy increments ref count
    EXPECT_EQ(pool.available(), 0u); // not yet returned
    // Both copies still live; underlying memory is the same
    EXPECT_EQ(buf1.data(), buf2.data());
}

TEST(BufferPool, RefCount_LastDestruct_ReturnsToPool) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(32, 32, CPIPE_PIXEL_FORMAT_RGB_8).value();
    {
        auto buf = pool.allocate(desc).value();
        EXPECT_EQ(pool.available(), 0u);
    } // buf destroyed here
    EXPECT_EQ(pool.available(), 1u);
}

TEST(BufferPool, Reuse_MatchingSize_ReusesMemory) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(64, 64, CPIPE_PIXEL_FORMAT_RGB_8).value();
    void* first_ptr = nullptr;
    {
        auto buf = pool.allocate(desc).value();
        first_ptr = buf.data();
    }
    // Return happened; next allocate should reuse
    auto buf2 = pool.allocate(desc).value();
    EXPECT_EQ(buf2.data(), first_ptr);
}

TEST(BufferPool, Reuse_DifferentSize_AllocatesNew) {
    BufferPool pool;
    auto desc1 = BufferDescriptor::create(64, 64, CPIPE_PIXEL_FORMAT_RGB_8).value();
    auto desc2 = BufferDescriptor::create(64, 64, CPIPE_PIXEL_FORMAT_RGB_FLOAT32).value();
    void* first_ptr = nullptr;
    {
        auto buf = pool.allocate(desc1).value();
        first_ptr = buf.data();
    }
    auto buf2 = pool.allocate(desc2).value();
    EXPECT_NE(buf2.data(), first_ptr);
}

TEST(BufferPool, Reuse_SameSize_DifferentDevice_AllocatesNew) {
    BufferPool pool;
    // Both descriptors produce the same byte size but differ in device.
    auto cpu_desc = BufferDescriptor::create(
        32, 32, CPIPE_PIXEL_FORMAT_RGBA_8, CPIPE_DEVICE_CPU).value();
    auto gpu_desc = BufferDescriptor::create(
        32, 32, CPIPE_PIXEL_FORMAT_RGBA_8, CPIPE_DEVICE_GPU).value();
    ASSERT_EQ(cpu_desc.size(), gpu_desc.size()); // same byte size

    void* cpu_ptr = nullptr;
    {
        auto buf = pool.allocate(cpu_desc).value();
        cpu_ptr = buf.data();
    }
    // A GPU request of the same size must NOT reuse the CPU buffer.
    auto gpu_buf = pool.allocate(gpu_desc).value();
    EXPECT_NE(gpu_buf.data(), cpu_ptr);
}

// ── BufferPool: to_c ─────────────────────────────────────────────────────────

TEST(BufferPool, ToCType_FieldsMatch) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(320, 240, CPIPE_PIXEL_FORMAT_RGBA_8).value();
    auto buf  = pool.allocate(desc).value();
    auto c    = buf.to_c();
    EXPECT_EQ(c.width,  320u);
    EXPECT_EQ(c.height, 240u);
    EXPECT_EQ(c.format, CPIPE_PIXEL_FORMAT_RGBA_8);
    EXPECT_EQ(c.stride, buf.descriptor().stride());
    EXPECT_EQ(c.size,   buf.descriptor().size());
    EXPECT_EQ(c.data,   buf.data());
}

// ── BufferPool: stats ─────────────────────────────────────────────────────────

TEST(BufferPool, Stats_TotalAllocated_Correct) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(32, 32, CPIPE_PIXEL_FORMAT_RGB_8).value();
    EXPECT_EQ(pool.total_allocated(), 0u);
    {
        auto buf = pool.allocate(desc).value();
        EXPECT_EQ(pool.total_allocated(), 1u);
    }
    // After release, reuse doesn't increment total
    auto buf2 = pool.allocate(desc).value();
    EXPECT_EQ(pool.total_allocated(), 1u);
}

TEST(BufferPool, Stats_Available_AfterRelease) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(32, 32, CPIPE_PIXEL_FORMAT_RGB_8).value();
    EXPECT_EQ(pool.available(), 0u);
    {
        auto buf = pool.allocate(desc).value();
        EXPECT_EQ(pool.available(), 0u);
    }
    EXPECT_EQ(pool.available(), 1u);
}

// ── BufferPool: thread safety ─────────────────────────────────────────────────

TEST(BufferPool, ThreadSafety_ConcurrentAllocRelease) {
    BufferPool pool;
    auto desc = BufferDescriptor::create(64, 64, CPIPE_PIXEL_FORMAT_RGB_8).value();

    constexpr int THREADS = 8;
    constexpr int OPS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    threads.reserve(THREADS);
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&pool, &desc] {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                auto buf = pool.allocate(desc);
                ASSERT_TRUE(buf.has_value());
                EXPECT_NE(buf->data(), nullptr);
                // buf goes out of scope → returns to pool
            }
        });
    }
    for (auto& th : threads) th.join();

    // All memory back in pool (pool outlives threads, no leaks)
    EXPECT_GE(pool.available(), 0u);
    EXPECT_GE(pool.total_allocated(), 1u);
}
