#include "fw/execution/any_task.hpp"

#include <gtest/gtest.h>

#include <string>
#include <tuple>

namespace {
fw::task<int> async_value(int x) {
    co_return x * 2;
}
} // namespace

TEST(AnyTask, ReadyValueAwaits) {
    auto result = fw::sync_wait(fw::execution::ready(7));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 7);
}

TEST(AnyTask, TaskIsAValidAnyTask) {
    fw::execution::any_task<int> task = async_value(21);
    auto result = fw::sync_wait(std::move(task));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
}
