#include "fw/execution/task.hpp"

#include <gtest/gtest.h>

#include <tuple>

namespace {

fw::task<int> add_one(int x) {
    int const y = co_await fw::just(x);
    co_return y + 1;
}

} // namespace

TEST(Execution, CoroutineAwaitsSender) {
    auto result = fw::sync_wait(add_one(41));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(Execution, SenderPipeline) {
    auto result = fw::sync_wait(fw::just(21) | fw::then([](int x) { return x * 2; }));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(Execution, TaskIsComposableWithSenders) {
    auto result = fw::sync_wait(add_one(1) | fw::then([](int x) { return x * 10; }));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 20);
}
