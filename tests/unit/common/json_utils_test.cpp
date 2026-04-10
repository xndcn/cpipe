#include "json_utils.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <cstdio>

// ── parse_string ──────────────────────────────────────────────────────────────

TEST(JsonUtils, ParseString_ValidJson) {
    auto result = cpipe::json::parse_string(R"({"key": 42})");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("key").get<int>(), 42);
}

TEST(JsonUtils, ParseString_EmptyObject) {
    auto result = cpipe::json::parse_string("{}");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(JsonUtils, ParseString_InvalidJson_ReturnsError) {
    auto result = cpipe::json::parse_string("{invalid}");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(JsonUtils, ParseString_EmptyString_ReturnsError) {
    auto result = cpipe::json::parse_string("");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

// ── parse_file ────────────────────────────────────────────────────────────────

TEST(JsonUtils, ParseFile_ValidFile) {
    // Write a temporary JSON file
    const auto path = std::filesystem::temp_directory_path() / "cpipe_test.json";
    {
        std::ofstream f(path);
        f << R"({"name": "cpipe", "version": 1})";
    }
    auto result = cpipe::json::parse_file(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("name").get<std::string>(), "cpipe");
    std::filesystem::remove(path);
}

TEST(JsonUtils, ParseFile_MissingFile_ReturnsError) {
    auto result = cpipe::json::parse_file("/nonexistent/path/file.json");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_IO);
}

TEST(JsonUtils, ParseFile_InvalidJson_ReturnsError) {
    const auto path = std::filesystem::temp_directory_path() / "cpipe_bad.json";
    {
        std::ofstream f(path);
        f << "not json at all";
    }
    auto result = cpipe::json::parse_file(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
    std::filesystem::remove(path);
}

// ── get<T> ────────────────────────────────────────────────────────────────────

TEST(JsonUtils, Get_ExistingKey_ReturnsValue) {
    auto j = nlohmann::json::parse(R"({"count": 7})");
    auto result = cpipe::json::get<int>(j, "count");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7);
}

TEST(JsonUtils, Get_MissingKey_ReturnsError) {
    auto j = nlohmann::json::parse("{}");
    auto result = cpipe::json::get<int>(j, "missing");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(JsonUtils, Get_WrongType_ReturnsError) {
    auto j = nlohmann::json::parse(R"({"key": "not_an_int"})");
    auto result = cpipe::json::get<int>(j, "key");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CPIPE_STATUS_ERROR_INVALID_PARAM);
}

TEST(JsonUtils, Get_StringKey_Works) {
    auto j = nlohmann::json::parse(R"({"name": "test"})");
    auto result = cpipe::json::get<std::string>(j, "name");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "test");
}
