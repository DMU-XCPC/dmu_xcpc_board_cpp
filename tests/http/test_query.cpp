#include "fw/http/query.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(QueryParams, ParsesAndDecodes) {
    auto const params = fw::http::QueryParams::parse("a=1&b=hello%20world&c=x+y&d");
    ASSERT_TRUE(params.get("a").has_value());
    EXPECT_EQ(*params.get("a"), "1");
    EXPECT_EQ(*params.get("b"), "hello world");
    EXPECT_EQ(*params.get("c"), "x y");
    ASSERT_TRUE(params.get("d").has_value());
    EXPECT_EQ(*params.get("d"), "");
    EXPECT_FALSE(params.get("missing").has_value());
}

TEST(QueryParams, EmptyQuery) {
    auto const params = fw::http::QueryParams::parse("");
    EXPECT_TRUE(params.empty());
}
