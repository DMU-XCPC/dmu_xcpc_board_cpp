#include "fw/core/error.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(Error, MakeErrorCode) {
    auto const error = fw::make_error_code(fw::Errc::not_found);
    EXPECT_TRUE(error);
    EXPECT_EQ(std::string{error.category().name()}, "fw");
    EXPECT_EQ(error.message(), "not found");
}

TEST(Error, SuccessCodeIsFalsy) {
    auto const error = fw::make_error_code(fw::Errc::success);
    EXPECT_FALSE(error);
}

TEST(Error, ResultHoldsValueOrError) {
    fw::Result<int> ok = 42;
    ASSERT_TRUE(ok);
    EXPECT_EQ(*ok, 42);

    fw::Result<int> failed = std::unexpected{fw::make_error_code(fw::Errc::internal)};
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(), fw::Errc::internal);
}

TEST(Error, ToStringIncludesCategory) {
    auto const text = fw::to_string(fw::make_error_code(fw::Errc::io_error));
    EXPECT_NE(text.find("fw"), std::string::npos);
    EXPECT_NE(text.find("io error"), std::string::npos);
}
