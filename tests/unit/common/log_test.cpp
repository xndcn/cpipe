#include "log.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

TEST(Log, Init_DefaultLevel) {
    cpipe::log::init();
    auto logger = cpipe::log::get();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::info);
}

TEST(Log, Init_CustomLevel) {
    cpipe::log::init(spdlog::level::debug);
    auto logger = cpipe::log::get();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::debug);
    // Reset to info so subsequent tests behave normally
    cpipe::log::init(spdlog::level::info);
}

TEST(Log, Get_ReturnsNamedLogger) {
    auto logger = cpipe::log::get();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "cpipe");
}

TEST(Log, Get_ReturnsSameInstance) {
    auto a = cpipe::log::get();
    auto b = cpipe::log::get();
    EXPECT_EQ(a.get(), b.get());
}

TEST(Log, MacroCompiles_AllLevels) {
    // These should compile and not throw; output is discarded at test log level.
    EXPECT_NO_THROW({
        CPIPE_LOG_TRACE("trace {}", 1);
        CPIPE_LOG_DEBUG("debug {}", 2);
        CPIPE_LOG_INFO("info {}", 3);
        CPIPE_LOG_WARN("warn {}", 4);
        CPIPE_LOG_ERROR("error {}", 5);
        CPIPE_LOG_CRITICAL("critical {}", 6);
    });
}

TEST(Log, ConcurrentGet_NoThrow) {
    constexpr int THREADS = 8;
    std::vector<std::thread> threads;
    threads.reserve(THREADS);
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([] {
            auto logger = cpipe::log::get();
            EXPECT_NE(logger, nullptr);
            EXPECT_EQ(logger->name(), "cpipe");
        });
    }
    for (auto& th : threads) th.join();
}
