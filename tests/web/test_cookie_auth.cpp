#include <gtest/gtest.h>
#include <serverweb/CookieAuthMiddleware.h>

using namespace serverweb::middleware;

TEST(CookieAuthTest, ParsesCookieHeader) {
    auto result = CookieAuth::ExtractCookie("token=abc123; other=xyz", "token");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "abc123");
}

TEST(CookieAuthTest, ReturnsNulloptForMissingCookie) {
    auto result = CookieAuth::ExtractCookie("other=xyz", "token");
    EXPECT_FALSE(result.has_value());
}

TEST(CookieAuthTest, ReturnsNulloptForEmptyCookieHeader) {
    auto result = CookieAuth::ExtractCookie("", "token");
    EXPECT_FALSE(result.has_value());
}

TEST(CookieAuthTest, ParsesCookieWithoutSpaces) {
    auto result = CookieAuth::ExtractCookie("token=abc123;other=xyz", "token");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "abc123");
}

TEST(CookieAuthTest, ParsesLastCookie) {
    auto result = CookieAuth::ExtractCookie("a=1; b=2; token=myval", "token");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "myval");
}
