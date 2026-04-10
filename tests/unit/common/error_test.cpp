#include "error.h"
#include <gtest/gtest.h>

// ── Error construction ────────────────────────────────────────────────────────

TEST(Error, Construct_WithCodeAndMessage) {
    cpipe::Error e{CPIPE_STATUS_ERROR_IO, "file not found"};
    EXPECT_EQ(e.code, CPIPE_STATUS_ERROR_IO);
    EXPECT_EQ(e.message, "file not found");
}

TEST(Error, MakeError_SetsFields) {
    auto e = cpipe::make_error(CPIPE_STATUS_ERROR_INVALID_PARAM, "bad param");
    EXPECT_EQ(e.code, CPIPE_STATUS_ERROR_INVALID_PARAM);
    EXPECT_EQ(e.message, "bad param");
}

// ── status_to_string ──────────────────────────────────────────────────────────

TEST(Error, StatusToString_OK) {
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_OK), "OK");
}

TEST(Error, StatusToString_AllCodes) {
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_ERROR_INVALID_PARAM),      "ERROR_INVALID_PARAM");
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_ERROR_OUT_OF_MEMORY),      "ERROR_OUT_OF_MEMORY");
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_ERROR_PLUGIN_LOAD_FAILED), "ERROR_PLUGIN_LOAD_FAILED");
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_ERROR_IO),                 "ERROR_IO");
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_ERROR_UNSUPPORTED),        "ERROR_UNSUPPORTED");
    EXPECT_EQ(cpipe::status_to_string(CPIPE_STATUS_ERROR_ABI_MISMATCH),       "ERROR_ABI_MISMATCH");
}

// ── expected / unexpected ─────────────────────────────────────────────────────

TEST(Expected, Success_HasValue) {
    cpipe::expected<int, cpipe::Error> result = 42;
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(Expected, Failure_HasError) {
    cpipe::expected<int, cpipe::Error> result =
        cpipe::unexpected<cpipe::Error>(cpipe::make_error(CPIPE_STATUS_ERROR_IO, "fail"));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_IO);
}

TEST(Expected, ValueOr_ReturnsDefaultOnError) {
    cpipe::expected<int, cpipe::Error> result =
        cpipe::unexpected<cpipe::Error>(cpipe::make_error(CPIPE_STATUS_ERROR_IO, "fail"));
    EXPECT_EQ(result.value_or(-1), -1);
}

TEST(Expected, ValueOr_ReturnsValueOnSuccess) {
    cpipe::expected<int, cpipe::Error> result = 99;
    EXPECT_EQ(result.value_or(-1), 99);
}
