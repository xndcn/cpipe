#include <regex>

#include <gtest/gtest.h>

#include <cpipe/version.h>

TEST(Version, ReturnsNonEmpty) {
    auto v = cpipe::version_string();
    EXPECT_FALSE(v.empty());
}

TEST(Version, MatchesSemverFormat) {
    auto v = cpipe::version_string();
    std::regex semver(R"(\d+\.\d+\.\d+)");
    EXPECT_TRUE(std::regex_match(std::string(v), semver));
}

TEST(Version, MatchesExpected) {
    EXPECT_EQ(cpipe::version_string(), "0.0.1");
}
